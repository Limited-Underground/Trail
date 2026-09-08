"""Real solicited endpoint/runner over simulated purging serial handles and peers."""
import importlib.util
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import noise_xk_solicited_runner as runner
from noise_xk_solicited_runtime import SolicitedReconnectableEndpoint

spec = importlib.util.spec_from_file_location("_solicited_composition_fixture",
    ROOT / "tests/host/noise_xk_ready_composition_tests.py")
assert spec and spec.loader
base = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = base
spec.loader.exec_module(base)


class Board(base.Board):
    def __init__(self, *args):
        super().__init__(*args)
        self.generation = 0
        self.bad_generation = None
        self.failure = None

    def factory(self):
        handle = PurgingSerial(self, len(self.handles))
        self.handles.append(handle)
        return handle

    def write_command(self, command):
        if not command.startswith("ready "):
            return super().write_command(command)
        self.commands.append(command)
        challenge = command.split()[1]
        failure = self.failure if self.generation == self.bad_generation else None
        if failure == "missing":
            return
        fields = {"schema": "OTNXREADY1", "challenge": challenge, "accepted": "yes",
                  "stale_selftest": "yes", "radio_ready": "yes", "idle": "yes", "tx": "no"}
        if failure == "challenge":
            fields["challenge"] = ("1" if challenge[0] != "1" else "2") + challenge[1:]
        if failure == "active": fields["idle"] = "no"
        if failure == "selftest": fields["stale_selftest"] = "no"
        if failure == "tx": fields["tx"] = "yes"
        self.receipt("READY", fields)
        profile = base.helpers.MODULE.PROFILE_RECEIPT.copy()
        if failure == "profile": profile["frequency_hz"] = "0"
        self.receipt("PROFILE", profile)
        status = self.status_fields()
        if failure == "counter": status["tx_sent"] = "1"
        self.receipt("STATUS", status)


class PurgingSerial(base.Serial):
    def open(self):
        super().open()
        self.board.generation = self.generation
        self.board.events.append(("purged", self.board.label, len(self.board.queue), self.generation))
        # Model Windows pyserial open clearing queued RX, including the entire
        # initial/post-restart boot contract. Readiness must survive this.
        self.board.queue.clear()
        self.fragments.clear()


class Fixture(base.Fixture):
    def __init__(self):
        self.clock = base.Clock()
        self.events = []
        self.a = Board("A", self.events, self.clock)
        self.b = Board("B", self.events, self.clock)
        self.a.peer, self.b.peer = self.b, self.a
        self.tokens_used = 0
        self.endpoints = []

    def run(self):
        try:
            for board in (self.a, self.b):
                self.endpoints.append(SolicitedReconnectableEndpoint("anonymous-" + board.label, board.factory,
                    monotonic=self.clock.monotonic, sleep=self.clock.sleep))
            return runner.run(*self.endpoints, token_factory=self.token)
        finally:
            for endpoint in reversed(self.endpoints): endpoint.close()


class Tests(unittest.TestCase):
    def test_purged_initial_and_reopened_boot_still_produce_identical_fourteen_frames(self):
        f = Fixture()
        actual = f.run()
        self.assertTrue(runner.validate_public_result(actual))
        self.assertEqual(actual["totals"]["fragments"], 14)
        self.assertEqual(actual["totals"]["radio_payload_wire_bytes"], 736)
        a, b = base.helpers.FakeEndpoint("A"), base.helpers.FakeEndpoint("B")
        a.peer, b.peer = b, a
        tokens = iter(format(n, "016x") for n in range(1, 30))
        expected = base.helpers.MODULE.run(a, b, token_factory=lambda: next(tokens))
        self.assertEqual(runner.canonical_bytes(actual), base.helpers.MODULE.canonical_bytes(expected))
        for board in (f.a, f.b):
            self.assertEqual(len(board.handles), 2)
            self.assertTrue(all(handle.closed for handle in board.handles))
            self.assertEqual(board.counters["tx_sent"], 7)
            self.assertEqual(board.counters["rx_accepted"], 7)
            self.assertEqual(len([e for e in f.events if e[0] == "purged" and e[1] == board.label and e[2] > 0]), 2)
            self.assertEqual([e[3] for e in f.events if e[:3] == ("receipt", board.label, "READY")], [0, 1])
            self.assertFalse(any(e[0] == "receipt" and e[1] == board.label and e[2] in
                                 ("BOOT", "STALE_SELFTEST", "COMMANDS") for e in f.events))

    def test_both_initial_ready_checks_precede_restart_and_postopen_checks_precede_tokens(self):
        f = Fixture(); f.run()
        commands = [(i, event) for i, event in enumerate(f.events) if event[0] == "command"]
        first_restart = next(i for i, e in commands if e[2] == "restart")
        first_prepare = next(i for i, e in commands if e[2] == "prepare")
        for role in ("A", "B"):
            initial = next(i for i, e in enumerate(f.events) if e == ("receipt", role, "READY", 0))
            reopened = next(i for i, e in enumerate(f.events) if e == ("receipt", role, "READY", 1))
            self.assertLess(initial, first_restart)
            self.assertLess(reopened, first_prepare)
            self.assertEqual(len([e for _, e in commands if e[1:3] == (role, "ready")]), 2)

    def test_bad_initial_readiness_fails_before_restart_or_radio_token(self):
        for role in ("A", "B"):
            for failure in ("missing", "challenge", "active", "selftest", "tx"):
                with self.subTest(role=role, failure=failure):
                    f = Fixture(); board = f.a if role == "A" else f.b
                    board.bad_generation, board.failure = 0, failure
                    with self.assertRaises(runner.RunnerError) as error: f.run()
                    self.assertEqual(error.exception.stage, "initial_boot_contract_" + role.lower())
                    self.assertEqual(f.tokens_used, 0)
                    self.assertFalse(any(e[0] == "command" and e[2] != "ready" for e in f.events))
                    self.assertTrue(all(h.closed for b in (f.a, f.b) for h in b.handles))

    def test_bad_postreopen_readiness_fails_before_radio_token(self):
        for role in ("A", "B"):
            f = Fixture(); board = f.a if role == "A" else f.b
            board.bad_generation, board.failure = 1, "active"
            with self.assertRaises(runner.RunnerError) as error: f.run()
            self.assertEqual(error.exception.stage, "restart_boot_contract_" + role.lower())
            self.assertEqual(f.tokens_used, 0)
            self.assertFalse(any(e[0] == "command" and e[2] in ("prepare", "send", "arm-tx") for e in f.events))
            self.assertTrue(all(h.closed for b in (f.a, f.b) for h in b.handles))

    def test_real_profile_and_status_validators_reject_bad_readiness_snapshot(self):
        for generation in (0, 1):
            for failure in ("profile", "counter"):
                f = Fixture(); f.a.bad_generation, f.a.failure = generation, failure
                with self.assertRaises(runner.RunnerError) as error: f.run()
                self.assertEqual(error.exception.stage,
                    "initial_boot_contract_a" if generation == 0 else "restart_boot_contract_a")
                self.assertEqual(f.tokens_used, 0)

    def test_closed_runtime_rejects_readiness_without_serial_write(self):
        f = Fixture(); f.run()
        before = list(f.events)
        for endpoint in f.endpoints:
            with self.assertRaisesRegex(Exception, "^endpoint_closed$"):
                endpoint.query_ready()
        self.assertEqual(f.events, before)

    def test_nonconsecutive_handle_reuse_rejected_after_fresh_readiness(self):
        f = Fixture()
        old, newer = f.a.factory(), f.a.factory()
        supplied = iter((old, newer, old))
        endpoint = SolicitedReconnectableEndpoint("anonymous-A", lambda: next(supplied),
            monotonic=f.clock.monotonic, sleep=f.clock.sleep)
        try:
            first, _ = endpoint.query_ready()
            endpoint.reopen()
            second, _ = endpoint.query_ready()
            self.assertNotEqual(first, second)
            with self.assertRaisesRegex(Exception, "^endpoint_reopen_failed$"):
                endpoint.reopen()
            self.assertEqual([e for e in f.events if e[0] == "open"],
                             [("open", "A", 0), ("open", "A", 1)])
        finally:
            endpoint.close()
        self.assertTrue(old.closed and newer.closed)


if __name__ == "__main__":
    unittest.main()
