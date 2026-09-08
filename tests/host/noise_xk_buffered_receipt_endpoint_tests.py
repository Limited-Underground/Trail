"""No hardware: real frozen parser composed with bounded candidate buffering."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from noise_xk_buffered_receipt_endpoint import BufferedReceiptEndpoint, frozen_adapter

ACK = b"OT153 RESTART accepted=yes wiped=yes tx=no\n"

class Serial:
    def __init__(self, chunks, step=0.1):
        self.chunks = list(chunks)
        self.now = 0.0
        self.step = step
        self.writes = []
        self.closed = False
    def readline(self, size):
        self.now += self.step
        return self.chunks.pop(0) if self.chunks else b""
    def write(self, data):
        self.writes.append(data)
    def flush(self):
        pass
    def close(self):
        self.closed = True

def endpoint(chunks, step=0.1):
    serial = Serial(chunks, step)
    return BufferedReceiptEndpoint(serial, monotonic=lambda: serial.now), serial

class BufferedReceiptTests(unittest.TestCase):
    def test_frozen_parser_loses_valid_split_but_successor_accepts_all_splits(self):
        split = ACK.index(b"accepted=") + len(b"accepted=")
        parse = frozen_adapter.SerialRadioEndpoint._receipt_from_line
        self.assertIsNotNone(parse(ACK))
        self.assertIsNone(parse(ACK[:split]))
        self.assertIsNone(parse(ACK[split:]))
        for cut in range(1, len(ACK)):
            with self.subTest(cut=cut):
                ep, _ = endpoint([ACK[:cut], b"", ACK[cut:]])
                result = ep.expect("RESTART", 1000)
                self.assertEqual(result.fields, {"accepted": "yes", "wiped": "yes", "tx": "no"})

    def test_fragmented_outer_color_preserves_exact_fields(self):
        data = b"\x1b[0;32mI (4) bench: " + ACK[:-1] + b"\x1b[0m\r\n"
        ep, _ = endpoint([data[:9], data[9:-3], data[-3:]])
        self.assertEqual(ep.expect("RESTART", 1000).fields["tx"], "no")

    def test_embedded_escape_is_not_normalized_into_valid_field(self):
        ep, _ = endpoint([ACK.replace(b"yes", b"y\x1b[0mes", 1)])
        with self.assertRaisesRegex(frozen_adapter.AdapterError, "receipt_framing_invalid"):
            ep.expect("RESTART", 1000)

    def test_timeout_is_absolute_and_late_complete_record_cannot_pass(self):
        ep, serial = endpoint([ACK], step=1.0)
        with self.assertRaisesRegex(frozen_adapter.AdapterError, "receipt_timeout"):
            ep.expect("RESTART", 1000)
        with self.assertRaisesRegex(frozen_adapter.AdapterError, "endpoint_failed"):
            ep.write_command("restart")
        self.assertEqual(serial.writes, [])

    def test_complete_record_just_before_deadline_passes(self):
        ep, _ = endpoint([ACK[:9], ACK[9:]], step=0.499)
        self.assertEqual(ep.expect("RESTART", 1000).kind, "RESTART")

    def test_partial_or_oversized_record_fails_closed(self):
        for chunks in [[b"x" * 2049], [b"x" * 1024, b"x" * 1025]]:
            ep, _ = endpoint(chunks)
            with self.assertRaisesRegex(frozen_adapter.AdapterError, "receipt_size_invalid"):
                ep.expect("RESTART", 1000)
            self.assertEqual(ep._pending, b"")

    def test_unknown_boot_lines_and_passive_receipts_are_not_confused_with_ack(self):
        ep, _ = endpoint([b"ROM boot\n", b"OT153 STATUS radio_frames=0\n", ACK])
        self.assertEqual(ep.expect("RESTART", 1000).kind, "RESTART")

    def test_unexpected_receipt_and_read_error_fail_closed(self):
        ep, _ = endpoint([b"OT153 RX accepted=no\n"])
        with self.assertRaisesRegex(frozen_adapter.AdapterError, "receipt_sequence_invalid"):
            ep.expect("RESTART", 1000)
        ep, serial = endpoint([])
        def fail(size):
            raise OSError("synthetic private backend detail")
        serial.readline = fail
        with self.assertRaisesRegex(frozen_adapter.AdapterError, "^endpoint_read_failed$") as result:
            ep.expect("RESTART", 1000)
        self.assertIsNone(result.exception.__context__)
        self.assertIsNone(result.exception.__cause__)

    def test_command_contract_is_preserved_and_write_error_poisoned_without_private_context(self):
        ep, serial = endpoint([])
        ep.write_command("restart")
        self.assertEqual(serial.writes, [b"restart\n"])
        def fail(data):
            raise OSError("synthetic private detail")
        serial.write = fail
        with self.assertRaisesRegex(frozen_adapter.AdapterError, "^endpoint_write_failed$") as result:
            ep.write_command("profile")
        self.assertIsNone(result.exception.__context__)
        self.assertIsNone(result.exception.__cause__)
        with self.assertRaisesRegex(frozen_adapter.AdapterError, "endpoint_failed"):
            ep.expect("RESTART", 1000)

    def test_only_bounded_sgr_outer_frames_are_admitted(self):
        for data in [b"\x1b[" + b"1" * 17 + b"m" + ACK,
                     b"\x1b[2J" + ACK, b"\x1b[0m\x1b[0m" + ACK]:
            ep, _ = endpoint([data])
            with self.assertRaisesRegex(frozen_adapter.AdapterError, "receipt_framing_invalid"):
                ep.expect("RESTART", 1000)

    def test_empty_reads_cannot_spin_forever_when_clock_does_not_advance(self):
        ep, _ = endpoint([], step=0.0)
        with self.assertRaisesRegex(frozen_adapter.AdapterError, "receipt_read_budget"):
            ep.expect("RESTART", 1000)

    def test_close_discards_partial_and_fresh_instance_has_no_old_receipt(self):
        ep, serial = endpoint([ACK[:15]])
        with self.assertRaisesRegex(frozen_adapter.AdapterError, "receipt_timeout"):
            ep.expect("RESTART", 200)
        ep.close()
        self.assertTrue(serial.closed)
        self.assertEqual(ep._pending, b"")
        fresh, _ = endpoint([ACK[15:]], step=0.1)
        with self.assertRaisesRegex(frozen_adapter.AdapterError, "receipt_timeout"):
            fresh.expect("RESTART", 300)

if __name__ == "__main__":
    unittest.main()
