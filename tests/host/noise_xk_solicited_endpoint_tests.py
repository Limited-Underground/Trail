"""Host-only readiness transactions, fragmentation, retries and FIFO boundaries."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import noise_xk_solicited_endpoint as m
import noise_xk_ready_runner as runner


def challenge(number):
    return format(number, "032x")


def ready(number):
    return ("OT153 READY schema=OTNXREADY1 challenge=" + challenge(number)
            + " accepted=yes stale_selftest=yes radio_ready=yes idle=yes tx=no\n").encode()


PROFILE = b"OT153 PROFILE profile=test\n"
STATUS = b"OT153 STATUS ready=yes\n"
ACK = b"OT153 RESTART accepted=yes wiped=yes tx=no\n"


class Serial:
    def __init__(self, chunks, steps=None):
        self.chunks = list(chunks)
        self.steps = list(steps or [])
        self.now = 0.0
        self.writes = []
    def readline(self, size):
        self.now += self.steps.pop(0) if self.steps else 0.01
        return self.chunks.pop(0) if self.chunks else b""
    def write(self, data): self.writes.append(data)
    def flush(self): pass
    def close(self): pass


def endpoint(chunks, steps=None):
    serial = Serial(chunks, steps)
    ep = m.SolicitedReceiptEndpoint(serial, monotonic=lambda: serial.now)
    return ep, serial


def tokens():
    counter = iter(range(1, 100))
    return lambda: challenge(next(counter))


class Tests(unittest.TestCase):
    def test_exact_ready_grammar_extends_only_previous_commands(self):
        valid = m.SolicitedReceiptEndpoint._command_valid
        self.assertTrue(valid("ready " + challenge(1)))
        self.assertTrue(valid("restart"))
        for value in ("ready", "ready " + "F" * 32, "ready " + "0" * 31,
                      "ready " + "0" * 33, " ready " + challenge(1),
                      "ready  " + challenge(1), "ready " + challenge(1) + "\n", None):
            self.assertFalse(valid(value))

    def test_latest_real_receipt_and_trailing_profile_status_are_preserved(self):
        ep, serial = endpoint([ready(1) + PROFILE + STATUS, ACK])
        matched, receipt = ep.query_ready(challenge_factory=tokens())
        self.assertEqual(matched, challenge(1))
        self.assertIs(type(receipt), type(runner.parse_receipt(ready(1).decode())))
        self.assertEqual(ep.expect("PROFILE", 1000).fields, {"profile": "test"})
        self.assertEqual(ep.expect("STATUS", 1000).fields, {"ready": "yes"})
        self.assertEqual(serial.writes, [("ready " + challenge(1) + "\n").encode()])
        ep.write_command("restart")
        self.assertEqual(ep.expect("RESTART", 1000).kind, "RESTART")

    def test_delayed_previous_reply_is_drained_before_latest_without_more_writes(self):
        ep, serial = endpoint([b"", ready(1), PROFILE, STATUS, ready(2), PROFILE, STATUS], [0.6])
        matched, _ = ep.query_ready(challenge_factory=tokens())
        self.assertEqual(matched, challenge(2))
        self.assertEqual(len(serial.writes), 2)
        ep.expect("PROFILE", 1000); ep.expect("STATUS", 1000)
        self.assertEqual(len(serial.writes), 2)

    def test_partial_previous_ready_survives_retry_without_poisoning(self):
        cut = ready(1).index(b"accepted")
        ep, serial = endpoint([ready(1)[:cut], ready(1)[cut:], PROFILE, STATUS, ready(2), PROFILE, STATUS], [0.6])
        self.assertEqual(ep.query_ready(challenge_factory=tokens())[0], challenge(2))
        self.assertFalse(ep._failed)
        self.assertEqual(len(serial.writes), 2)

    def test_all_ready_fragment_boundaries_and_outer_color(self):
        raw = b"\x1b[0;32mI (3) bench: " + ready(1)[:-1] + b"\x1b[0m\r\n"
        for cut in range(1, len(raw)):
            ep, _ = endpoint([raw[:cut], b"", raw[cut:], PROFILE, STATUS])
            self.assertEqual(ep.query_ready(challenge_factory=tokens())[0], challenge(1))

    def test_startup_passive_records_do_not_replace_correlated_readiness(self):
        ep, _ = endpoint([b"ROM boot\n", b"OT153 BOOT firmware=test\n", PROFILE, STATUS,
                          b"OT153 STALE_SELFTEST accepted=yes\n", b"OT153 COMMANDS ready=yes\n", ready(1)])
        self.assertEqual(ep.query_ready(challenge_factory=tokens())[0], challenge(1))

    def test_unknown_challenge_rejection_bad_field_set_and_malformed_fail_closed(self):
        for raw in (ready(99), ready(1).replace(b"accepted=yes", b"accepted=no"),
                    ready(1).replace(b" tx=no", b" tx=no extra=yes"),
                    ready(1).replace(b" tx=no", b""),
                    ready(1).replace(b"idle=yes", b"idle=yes idle=yes"),
                    ready(1).replace(b"OTNXREADY1", b"OTNXREADY2"),
                    ready(1).replace(b"tx=no", b"tx=yes")):
            ep, _ = endpoint([raw])
            with self.assertRaisesRegex(Exception, "^readiness_receipt_invalid$"):
                ep.query_ready(challenge_factory=tokens())
            self.assertTrue(ep._failed)

    def test_old_reply_must_be_accepted_and_complete_before_next_ready(self):
        for replies in ([ready(1), STATUS, ready(2)],
                        [ready(1), PROFILE, ready(2)],
                        [ready(1).replace(b"idle=yes", b"idle=no"), PROFILE, STATUS, ready(2)]):
            ep, _ = endpoint([b"", *replies], [0.6])
            with self.assertRaises(Exception): ep.query_ready(challenge_factory=tokens())

    def test_active_events_fail_even_while_booting(self):
        for kind in ("PREPARE", "TX", "RX", "RESTART", "STAGE_ACCEPT", "TIMEOUT"):
            ep, _ = endpoint([("OT153 " + kind + " accepted=yes\n").encode()])
            with self.assertRaisesRegex(Exception, "^readiness_sequence_invalid$"):
                ep.query_ready(challenge_factory=tokens())

    def test_absolute_deadline_and_retry_count_are_bounded(self):
        ep, serial = endpoint([], [0.5] * 21)
        with self.assertRaisesRegex(Exception, "^readiness_timeout$"):
            ep.query_ready(challenge_factory=tokens())
        self.assertEqual(len(serial.writes), 20)
        self.assertEqual(len(set(serial.writes)), 20)
        self.assertLessEqual(serial.now, 10.0)
        with self.assertRaises(Exception): ep.write_command("restart")
        ep, _ = endpoint([ready(1)], [1.0])
        with self.assertRaisesRegex(Exception, "^readiness_timeout$"):
            ep.query_ready(challenge_factory=tokens(), timeout_ms=1000)

    def test_bad_or_reused_factory_challenge_is_terminal(self):
        for factory in (lambda: "bad", lambda: None, lambda: challenge(1)):
            ep, serial = endpoint([b""], [0.6])
            with self.assertRaisesRegex(Exception, "^readiness_challenge_invalid$"):
                ep.query_ready(challenge_factory=factory)
            self.assertLessEqual(len(serial.writes), 1)

    def test_read_byte_record_budgets_and_embedded_escape(self):
        for raw in (b"x" * 2049, ready(1).replace(b"idle=yes", b"idle=y\x1b[0mes")):
            ep, _ = endpoint([raw])
            with self.assertRaises(Exception): ep.query_ready(challenge_factory=tokens())
        ep, _ = endpoint([b"", b"", b""])
        with patch.object(m, "_MAX_READS", 2):
            with self.assertRaisesRegex(Exception, "^readiness_read_budget$"):
                ep.query_ready(challenge_factory=tokens())
        ep, _ = endpoint([b"boot\nboot\nboot\n"])
        with patch.object(m, "_MAX_READS", 2):
            with self.assertRaisesRegex(Exception, "^readiness_record_budget$"):
                ep.query_ready(challenge_factory=tokens())

    def test_private_read_failure_is_sanitized_and_query_cannot_repeat(self):
        ep, serial = endpoint([])
        def fail(size): raise OSError("synthetic private route")
        serial.readline = fail
        with self.assertRaisesRegex(Exception, "^endpoint_read_failed$") as raised:
            ep.query_ready(challenge_factory=tokens())
        self.assertIsNone(raised.exception.__context__)
        ep, _ = endpoint([ready(1)])
        ep.query_ready(challenge_factory=tokens())
        with self.assertRaisesRegex(Exception, "^readiness_endpoint_invalid$"):
            ep.query_ready(challenge_factory=tokens())


if __name__ == "__main__":
    unittest.main()
