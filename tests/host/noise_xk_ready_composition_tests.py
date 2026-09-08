"""Real receipt/runtime/runner composition with simulated serial and radio peers only."""
from collections import deque
import importlib.util
from pathlib import Path
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import noise_xk_ready_runner as runner
from noise_xk_buffered_runtime import BufferedReconnectableEndpoint, RunnerReceiptEndpoint, lifecycle

spec = importlib.util.spec_from_file_location("_ready_composition_frozen_tests", ROOT / "tests/host/ot153_noise_xk_radio_runner_tests.py")
assert spec and spec.loader
helpers = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = helpers
spec.loader.exec_module(helpers)


class Clock:
    def __init__(self):
        self.now = 0.0
    def monotonic(self):
        return self.now
    def sleep(self, delay):
        self.now += delay


class Board(helpers.FakeEndpoint):
    def __init__(self, label, events, clock):
        super().__init__(label)
        self.events = events
        self.host_clock = clock
        self.handles = []
        self.pending_boot = deque()
        self.fail_reopen = False
        self.wrong_postboot = False
        # Simulated power-on boot; no host command has been sent.
        super().write_command("restart")
        self.queue.popleft()
        self.commands.clear()

    def factory(self):
        handle = Serial(self, len(self.handles))
        self.handles.append(handle)
        return handle


class Serial:
    def __init__(self, board, generation):
        self.board = board
        self.generation = generation
        self.fragments = deque()
        self.closed = False
        self.invalidated = False
        self.dtr = self.rts = True
        self.port = None

    def open(self):
        assert not self.dtr and not self.rts
        assert self.port == "anonymous-" + self.board.label
        self.board.events.append(("open", self.board.label, self.generation))
        if self.generation and self.board.fail_reopen:
            raise OSError("synthetic backend failure")
        if self.generation:
            self.board.queue.extend(self.board.pending_boot)
            self.board.pending_boot.clear()
            if self.board.wrong_postboot:
                receipt = self.board.queue[0]
                receipt.fields["passed"] = "no"

    def write(self, raw):
        assert not self.closed and not self.invalidated
        command = raw.decode("ascii").strip()
        self.board.events.append(("command", self.board.label, command.split()[0], self.generation))
        self.board.write_command(command)
        if command == "restart":
            acknowledgement = self.board.queue.popleft()
            self.board.pending_boot = deque(self.board.queue)
            self.board.queue = deque((acknowledgement,))
            self.invalidated = True
        return len(raw)

    def flush(self):
        pass

    def readline(self, size):
        assert not self.closed
        self.board.host_clock.now += 0.01
        if self.fragments:
            return self.fragments.popleft()
        if not self.board.queue:
            return b""
        receipt = self.board.queue.popleft()
        self.board.events.append(("receipt", self.board.label, receipt.kind, self.generation))
        raw = ("OT153 " + receipt.kind + " " + " ".join(f"{key}={value}" for key, value in receipt.fields.items()) + "\n").encode("ascii")
        if receipt.kind == "RESTART":
            cut = raw.index(b"accepted=") + len(b"accepted=")
            # Old-handle tail must disappear with its endpoint buffer on reopen.
            self.fragments.extend((b"", raw[cut:] + b"stale-old-handle-tail"))
            return raw[:cut]
        cut = min(13, len(raw) - 1)
        self.fragments.extend((b"", raw[cut:]))
        return raw[:cut]

    def close(self):
        self.closed = True
        self.board.events.append(("close", self.board.label, self.generation))


class Fixture:
    def __init__(self):
        self.clock = Clock()
        self.events = []
        self.a = Board("A", self.events, self.clock)
        self.b = Board("B", self.events, self.clock)
        self.a.peer, self.b.peer = self.b, self.a
        self.tokens_used = 0
        self.endpoints = []

    def token(self):
        self.tokens_used += 1
        return f"{self.tokens_used:016x}"

    def run(self):
        try:
            for board in (self.a, self.b):
                self.endpoints.append(BufferedReconnectableEndpoint("anonymous-" + board.label,
                    board.factory, monotonic=self.clock.monotonic, sleep=self.clock.sleep))
            return runner.run(*self.endpoints, token_factory=self.token)
        finally:
            # This is the coordinator-owned lifetime, not an invented runner close.
            for endpoint in reversed(self.endpoints):
                endpoint.close()


class ReadyCompositionTests(unittest.TestCase):
    def test_full_fourteen_frame_result_through_real_fragmented_parser_and_fresh_handles(self):
        f = Fixture()
        result = f.run()
        self.assertTrue(runner.validate_public_result(result))
        self.assertEqual(result["totals"]["fragments"], 14)
        self.assertEqual(result["totals"]["radio_payload_wire_bytes"], 736)
        expected_a, expected_b = helpers.FakeEndpoint("A"), helpers.FakeEndpoint("B")
        expected_a.peer, expected_b.peer = expected_b, expected_a
        tokens = iter(f"{index:016x}" for index in range(1, 30))
        expected = helpers.MODULE.run(expected_a, expected_b, token_factory=lambda: next(tokens))
        self.assertEqual(runner.canonical_bytes(result), helpers.MODULE.canonical_bytes(expected))
        first_restart = next(i for i, event in enumerate(f.events) if event[:1] == ("command",) and event[2] == "restart")
        for board in (f.a, f.b):
            initial_profiles = [i for i, event in enumerate(f.events) if event == ("receipt", board.label, "PROFILE", 0)]
            self.assertEqual(len(initial_profiles), 2)
            self.assertLess(max(initial_profiles), first_restart)
            self.assertEqual(len(board.handles), 2)
            self.assertTrue(all(handle.closed for handle in board.handles))
            self.assertEqual(board.counters["tx_sent"], 7)
            self.assertEqual(board.counters["rx_accepted"], 7)
        for endpoint in f.endpoints:
            self.assertIsNone(endpoint._endpoint)

    def test_parser_receipt_has_exact_runner_type(self):
        receipt = RunnerReceiptEndpoint._receipt_from_line(b"OT153 RESTART accepted=yes wiped=yes tx=no\n")
        self.assertIs(type(receipt), runner.predecessor.frozen.Receipt)

    def test_digit_key_successor_is_narrow_and_preserves_other_frozen_receipts(self):
        ordinary = "OT153 RESTART accepted=yes wiped=yes tx=no\n"
        old = runner.predecessor.frozen.parse_receipt(ordinary)
        new = runner.parse_receipt(ordinary)
        self.assertEqual(new, old)
        for key in ("payload_sha256", "tx_key_sha256", "rx_key_sha256"):
            line = f"OT153 END {key}=" + "a" * 64 + "\n"
            self.assertIsNone(runner.predecessor.frozen.parse_receipt(line))
            self.assertEqual(runner.parse_receipt(line).fields, {key: "a" * 64})
        for line in ("OT153 END unexpected256=value\n", "OT153 END tx_key_sha256=a tx_key_sha256=b\n",
                     "OT153 END payload_sha256=\n", "OT153 END tx_key_sha256=a=b\n"):
            self.assertIsNone(runner.parse_receipt(line))

    def test_coordinator_executes_real_byte_pipeline_and_restores_two_distinct_images(self):
        source = ROOT / "tests/host/noise_xk_role_recovery_coordinator_tests.py"
        spec = importlib.util.spec_from_file_location("_ready_composition_recovery_tests", source)
        assert spec and spec.loader
        recovery = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = recovery
        spec.loader.exec_module(recovery)
        real_run = recovery.c.runner.run
        real_validate = recovery.c.frozen._safe_radio_result
        serial = Fixture()
        with recovery.Fixture() as disk:
            def open_endpoint(identity):
                board = serial.a if disk.backend.roles[identity] == "A" else serial.b
                disk.backend.events.append(("open", board.label))
                endpoint = BufferedReconnectableEndpoint("anonymous-" + board.label, board.factory,
                    monotonic=serial.clock.monotonic, sleep=serial.clock.sleep)
                serial.endpoints.append(endpoint)
                return endpoint
            with mock.patch.object(disk.backend, "open_radio_endpoint", side_effect=open_endpoint), \
                 mock.patch.object(recovery.c.runner, "run", side_effect=lambda a, b: real_run(a, b, token_factory=serial.token)) as called, \
                 mock.patch.object(recovery.c.frozen, "_safe_radio_result", side_effect=real_validate) as validated:
                receipt = disk.execute()
            self.assertEqual(called.call_count, 1)
            self.assertEqual(validated.call_args.args[0]["totals"]["fragments"], 14)
            self.assertTrue(receipt["restoration_complete"])
            self.assertNotEqual(disk.binding.restore_a.sha256, disk.binding.restore_b.sha256)
            self.assertEqual(tuple(disk.backend.current[e] for e in disk.config.private_endpoints), disk.payloads)
            for board, descriptor in ((serial.a, disk.binding.restore_a), (serial.b, disk.binding.restore_b)):
                self.assertIn(("write", board.label, descriptor.sha256), disk.backend.events)
                self.assertIn(("verify", board.label, descriptor.sha256), disk.backend.events)
                self.assertTrue(all(handle.closed for handle in board.handles))
                self.assertEqual(board.counters["tx_sent"], 7)
                self.assertEqual(board.counters["rx_accepted"], 7)

    def test_factory_cannot_reuse_closed_serial_handle_on_reopen(self):
        f = Fixture()
        handle = f.a.factory()
        endpoint = BufferedReconnectableEndpoint("anonymous-A", lambda: handle,
            monotonic=f.clock.monotonic, sleep=f.clock.sleep)
        try:
            with self.assertRaisesRegex(lifecycle.AdapterError, "^endpoint_reopen_failed$"):
                endpoint.reopen()
            self.assertTrue(handle.closed)
            self.assertEqual([event for event in f.events if event[0] == "open"], [("open", "A", 0)])
        finally:
            endpoint.close()

    def test_factory_rejects_nonconsecutive_reuse_of_any_earlier_handle(self):
        f = Fixture()
        old, newer = f.a.factory(), f.a.factory()
        supplied = iter((old, newer, old))
        endpoint = BufferedReconnectableEndpoint("anonymous-A", lambda: next(supplied),
            monotonic=f.clock.monotonic, sleep=f.clock.sleep)
        try:
            endpoint.reopen()
            with self.assertRaisesRegex(lifecycle.AdapterError, "^endpoint_reopen_failed$"):
                endpoint.reopen()
            self.assertEqual([event for event in f.events if event[0] == "open"],
                             [("open", "A", 0), ("open", "A", 1)])
            self.assertTrue(old.closed and newer.closed)
        finally:
            endpoint.close()

    def test_missing_or_wrong_initial_boot_never_restarts_or_generates_tokens(self):
        for role, missing in (("A", True), ("A", False), ("B", True), ("B", False)):
            with self.subTest(role=role, missing=missing):
                f = Fixture()
                board = f.a if role == "A" else f.b
                if missing:
                    board.queue.clear()
                else:
                    board.queue[1].fields["rx"] = "failed"
                with self.assertRaises(runner.RunnerError) as caught:
                    f.run()
                self.assertEqual(caught.exception.stage, "initial_boot_contract_" + role.lower())
                self.assertIsNone(caught.exception.__context__)
                self.assertEqual(f.tokens_used, 0)
                self.assertFalse(any(command == "restart" or command.startswith(("send ", "prepare "))
                                     for node in (f.a, f.b) for command in node.commands))
                self.assertTrue(all(handle.closed for node in (f.a, f.b) for handle in node.handles))

    def test_reopen_and_postboot_failures_keep_stage_and_coordinator_closes_every_handle(self):
        for role, failure in (("A", "reopen"), ("B", "reopen"), ("A", "postboot"), ("B", "postboot")):
            with self.subTest(role=role, failure=failure):
                f = Fixture()
                board = f.a if role == "A" else f.b
                board.fail_reopen = failure == "reopen"
                board.wrong_postboot = failure == "postboot"
                with self.assertRaises(runner.RunnerError) as caught:
                    f.run()
                expected = "restart_reconnect_" if failure == "reopen" else "restart_boot_contract_"
                self.assertEqual(caught.exception.stage, expected + role.lower())
                self.assertIsNone(caught.exception.__context__)
                self.assertEqual(f.tokens_used, 0)
                self.assertTrue(all(handle.closed for node in (f.a, f.b) for handle in node.handles))
                self.assertFalse(any(command.startswith("send ") for node in (f.a, f.b) for command in node.commands))


if __name__ == "__main__":
    unittest.main()
