"""Actual coordinator/backend/byte transport composition; no hardware or RF."""
from collections import deque
import contextlib
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import noise_xk_solicited_coordinator as c
import noise_xk_solicited_backend as backend

spec = importlib.util.spec_from_file_location("_solicited_execution_serial", ROOT / "tests/host/noise_xk_ready_composition_tests.py")
serial = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = serial
spec.loader.exec_module(serial)


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


class Board(serial.Board):
    def write_command(self, command):
        if command.startswith("ready "):
            self.commands.append(command)
            self.receipt("READY", {"schema": "OTNXREADY1", "challenge": command.split()[1],
                "accepted": "yes", "stale_selftest": "yes", "radio_ready": "yes", "idle": "yes", "tx": "no"})
            self.receipt("PROFILE", serial.helpers.MODULE.PROFILE_RECEIPT.copy())
            self.receipt("STATUS", self.status_fields())
        else:
            super().write_command(command)


class Serial(serial.Serial):
    def __init__(self, fixture):
        self.fixture = fixture
        self.is_open = False
        self.dtr = self.rts = True
        self.port = None

    def open(self):
        route, dtr, rts = self.port, self.dtr, self.rts
        board = self.fixture.boards[route]
        super().__init__(board, len(board.handles))
        self.port, self.dtr, self.rts = route, dtr, rts
        board.handles.append(self)
        super().open()
        self.is_open = True
        # Model a driver discarding ALL startup records on every final open.
        self.fixture.events.append(("purged", board.label, len(board.queue)))
        board.queue.clear()
        self.fragments.clear()

    def readline(self, size):
        if self.fixture.fail_ready == self.board.label:
            raise OSError("synthetic read failure")
        return super().readline(size)

    def close(self):
        super().close()
        self.is_open = False


class Fixture:
    def __enter__(self):
        self.stack = contextlib.ExitStack()
        self.root = Path(self.stack.enter_context(tempfile.TemporaryDirectory())).resolve()
        private = self.root / ".private"
        private.mkdir()
        for name, value in {"ROOT": self.root, "PRIVATE_ROOT": private,
            "JOURNAL_PATH": private / c.JOURNAL_NAME,
            "EXECUTION_RECEIPT_PATH": private / c.EXECUTION_NAME,
            "RECOVERY_RECEIPT_PATH": private / c.RECOVERY_NAME}.items():
            self.stack.enter_context(mock.patch.object(c.frozen, name, value))
        self.events = []
        clock = serial.Clock()
        self.routes = ("anonymous-A", "anonymous-B")
        self.boards = {route: Board(role, self.events, clock) for route, role in zip(self.routes, ("A", "B"))}
        self.boards[self.routes[0]].peer = self.boards[self.routes[1]]
        self.boards[self.routes[1]].peer = self.boards[self.routes[0]]
        self.payloads = (b"first original application", b"distinct second original application")
        self.paths = tuple(self.root / (role + ".bin") for role in ("A", "B"))
        for path, raw in zip(self.paths, self.payloads):
            path.write_bytes(raw)
        self.benchmark = self.root / "benchmark.bin"
        self.benchmark.write_bytes(b"benchmark")
        descriptors = tuple(c.RestoreDescriptor(p.name, len(raw), sha(raw)) for p, raw in zip(self.paths, self.payloads))
        self.binding = c.ExecutionBinding("benchmark.bin", 9, sha(b"benchmark"), *descriptors,
            c.APPLICATION_OFFSET, c.BAUD, c.RUNNER_PATH.name, sha(c.RUNNER_PATH.read_bytes()), c.runner.SCHEMA)
        self.config = c.RunConfig(self.routes, self.binding, self.benchmark, self.paths)
        self.authority = SimpleNamespace(validate=lambda binding, recovery:
            c.AuthorityGrant("a" * 64, 1, False, True, c.binding_digest(self.binding)))
        self.current = dict(zip(self.routes, self.payloads))
        self.fail_restore = None
        self.fail_ready = None
        records = [SimpleNamespace(device=route, serial_number=f"02000000000{i}", vid=0x303a, pid=0x1001)
                   for i, route in enumerate(self.routes, 1)]
        bindings = tuple(backend.RoleBinding(role, record.device, record.serial_number, descriptor)
            for role, record, descriptor in zip(("A", "B"), records, descriptors))
        transport = SimpleNamespace(write_application=self.write, verify_application=self.verify,
            hard_reset=self.reset, _serial=SimpleNamespace(Serial=lambda **kw: Serial(self)))
        self.backend = backend.BoundBackend(bindings, inventory=lambda: records, transport=transport,
            rom_reader=lambda route: "MAC: " + next(p.serial_number for p in records if p.device == route))
        for route in self.routes:
            self.backend.admit_rom(route)
        self.events.clear()
        return self

    def __exit__(self, *args):
        return self.stack.__exit__(*args)

    def write(self, route, offset, image):
        role = self.boards[route].label
        self.events.append(("write", role, image.sha256))
        if role == self.fail_restore and image.payload != b"benchmark":
            raise OSError("synthetic restore failure")
        self.current[route] = image.payload

    def verify(self, route, offset, image):
        self.events.append(("verify", self.boards[route].label, image.sha256))
        if self.current[route] != image.payload:
            raise OSError("synthetic byte mismatch")

    def reset(self, route):
        self.events.append(("reset", self.boards[route].label))

    def execute(self):
        return c.execute(self.config, self.backend, self.authority)


class ExecutionTests(unittest.TestCase):
    def test_purged_boot_full_byte_pipeline_and_distinct_restores(self):
        with Fixture() as f:
            result = f.execute()
            self.assertTrue(result["radio_result_validated"])
            self.assertTrue(result["restoration_complete"])
            self.assertEqual(result["radio_result"]["totals"]["fragments"], 14)
            self.assertEqual(result["radio_result"]["totals"]["radio_payload_wire_bytes"], 736)
            self.assertEqual(tuple(f.current[r] for r in f.routes), f.payloads)
            self.assertNotEqual(f.binding.restore_a.sha256, f.binding.restore_b.sha256)
            for board in f.boards.values():
                self.assertEqual(len(board.handles), 2)
                self.assertTrue(all(not handle.is_open for handle in board.handles))
                self.assertEqual(sum(command.startswith("ready ") for command in board.commands), 2)
                self.assertEqual(board.counters["tx_sent"], 7)
            self.assertEqual(len([e for e in f.events if e[0] == "purged"]), 4)
            self.assertTrue(all(e[2] > 0 for e in f.events if e[0] == "purged"))
            self.assertFalse(f.backend._leases)

    def test_readiness_failure_closes_handles_and_restores_both_independently(self):
        for role in ("A", "B"):
            with self.subTest(role=role), Fixture() as f:
                f.fail_ready = role
                with self.assertRaises(c.CoordinatorError):
                    f.execute()
                result = json.loads(c.frozen.EXECUTION_RECEIPT_PATH.read_text())
                self.assertTrue(result["restoration_complete"])
                self.assertFalse(result["radio_result_validated"])
                self.assertEqual(tuple(f.current[r] for r in f.routes), f.payloads)
                self.assertFalse(f.backend._leases)
                self.assertTrue(all(not h.is_open for b in f.boards.values() for h in b.handles))
                self.assertFalse(any(command.startswith("send ") for b in f.boards.values() for command in b.commands))

    def test_restore_failure_does_not_block_other_role_and_recovery_needs_no_benchmark(self):
        with Fixture() as f:
            f.fail_ready = "A"
            f.fail_restore = "A"
            with self.assertRaises(c.CoordinatorError):
                f.execute()
            self.assertEqual(f.current[f.routes[1]], f.payloads[1])
            self.assertNotEqual(f.current[f.routes[0]], f.payloads[0])
            f.benchmark.unlink()
            f.fail_restore = None
            before = len([e for e in f.events if e[0] == "open"])
            receipt = c.recover(f.config, f.backend, f.authority)
            self.assertTrue(receipt["restoration_complete"])
            self.assertEqual(tuple(f.current[r] for r in f.routes), f.payloads)
            self.assertEqual(len([e for e in f.events if e[0] == "open"]), before)


if __name__ == "__main__":
    unittest.main()
