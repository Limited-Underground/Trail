"""Hostile NVS/image binding tests; optional C++ raw wire-vector agreement gate.

--wire-vectors PATH consumes lines: 16-digit raw u64 hex, then expected 0 or 1.
All assertions use unittest and remain active under Python optimization.
Synthetic NVS exists only in memory and is never a device backup.
"""
import binascii
from dataclasses import FrozenInstanceError
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import security_policy_input_readback as v1
import security_policy_sync_readback as decoder
import security_policy_input_readback_tests as fixtures

VECTORS = None
SYNC_IMAGE = decoder.ImageBinding(decoder.SYNC_CONTRACT, "heltec_v4_security_sync_diag", "a" * 64)
V1_IMAGE = decoder.ImageBinding(decoder.V1_CONTRACT, "heltec_v4_security_input_diag", "b" * 64)


def input_record(discarded=0, first_bytes=0, flags=0, terminal=0, first_reason=0,
                 delay=0, magic=0xA20):
    """Independent fixture packer intentionally permits semantically invalid fields."""
    word = (magic | discarded << 12 | first_bytes << 21 | flags << 28 |
            terminal << 35 | first_reason << 40 | delay << 45)
    prefix = word.to_bytes(6, "little")
    return prefix + binascii.crc_hqx(prefix, 0xffff).to_bytes(2, "little")


def diagnostic(stage=4, error=0, record=None, *, input_present=True, extras=()):
    rows = [fixtures.entry(0, 1, "ot198diag", b"\x01"),
            fixtures.entry(1, 8, "stage", fixtures.stage_record(stage, error))]
    if input_present:
        rows.append(fixtures.entry(1, 8, "input", input_record() if record is None else record))
    return fixtures.fixture([*rows, *extras])


def decode(raw, expected=SYNC_IMAGE, observed=SYNC_IMAGE):
    return decoder.decode(raw, expected_image=expected, observed_image=observed)


class Tests(unittest.TestCase):
    def refuse(self, raw, **kwargs):
        with self.assertRaisesRegex(decoder.ReadbackError, "^sync_readback_refused$"):
            decode(raw, **kwargs)

    def test_exact_image_binding_required_before_parse(self):
        for expected, observed in ((SYNC_IMAGE, V1_IMAGE), (V1_IMAGE, SYNC_IMAGE),
                                   (SYNC_IMAGE, decoder.ImageBinding(decoder.SYNC_CONTRACT,
                                    SYNC_IMAGE.target, "c" * 64)), (None, SYNC_IMAGE),
                                   (SYNC_IMAGE, dict(contract=decoder.SYNC_CONTRACT))):
            with self.subTest(expected=expected, observed=observed):
                with patch.object(v1, "_parse", side_effect=AssertionError("must not parse")):
                    self.refuse(b"", expected=expected, observed=observed)
        with self.assertRaises(TypeError):
            decoder.decode(diagnostic())

    def test_wrong_contract_target_or_hash_refuses(self):
        for contract, target, sha in (("auto", SYNC_IMAGE.target, "a" * 64),
                                     (decoder.SYNC_CONTRACT, V1_IMAGE.target, "a" * 64),
                                     (decoder.SYNC_CONTRACT, SYNC_IMAGE.target, "A" * 64),
                                     (decoder.SYNC_CONTRACT, SYNC_IMAGE.target, "a" * 63),
                                     (decoder.SYNC_CONTRACT, SYNC_IMAGE.target, True)):
            with self.assertRaises(decoder.ReadbackError):
                decoder.ImageBinding(contract, target, sha)
        with self.assertRaises(FrozenInstanceError):
            SYNC_IMAGE.sha256 = "c" * 64

    def test_old_decoder_and_single_key_contract_unchanged(self):
        for stage, errors in v1.ALLOWED.items():
            for error in errors:
                raw = fixtures.diagnostic(stage, error)
                self.assertEqual(v1.decode(raw), decode(raw, V1_IMAGE, V1_IMAGE))
                if stage >= 4:
                    self.refuse(raw)
        with self.assertRaises(v1.ReadbackError):
            v1.decode(diagnostic())
        self.refuse(diagnostic(), expected=V1_IMAGE, observed=V1_IMAGE)

    def test_stage_only_is_incomplete_under_explicit_sync_binding(self):
        for stage, error in ((1, 0), (2, 0), (2, 1), (3, 0)):
            result = decode(diagnostic(stage, error, input_present=False))
            self.assertIsNone(result["input"])
            self.assertEqual("not_recorded", result["input_status"])
            self.assertEqual("OT200-SYNC-INPUT-READBACK-1", result["schema"])

    def test_input_before_stage_result_is_partial_progress(self):
        for terminal in decoder.TERMINAL_REASONS:
            result = decode(diagnostic(3, record=input_record(discarded=193 if terminal == 19 else 0,
                                                              flags=1 if terminal == 19 else 0,
                                                              terminal=terminal)))
            self.assertEqual("waiting", result["stage"])
            self.assertEqual("committed_before_stage_result", result["input_status"])
        for stage in (1, 2):
            self.refuse(diagnostic(stage))

    def test_stage_result_requires_record_and_exact_terminal_mapping(self):
        for terminal in range(32):
            for error in v1.ALLOWED[4]:
                raw = diagnostic(4, error, input_record(discarded=193 if terminal == 19 else 0,
                                                        flags=1 if terminal == 19 else 0,
                                                        terminal=terminal))
                if terminal in decoder.TERMINAL_REASONS and error == (16 if terminal == 19 else terminal):
                    result = decode(raw)
                    self.assertEqual(decoder.REASONS[terminal], result["input"]["terminal_reason"])
                else:
                    self.refuse(raw)
        for stage in range(4, 9):
            self.refuse(diagnostic(stage, input_present=False))

    def test_later_stages_require_successful_input_but_preserve_stage_error(self):
        for stage in range(5, 9):
            for error in v1.ALLOWED[stage]:
                self.assertEqual(v1.ERRORS[error], decode(diagnostic(stage, error))["error"])
                for terminal in decoder.TERMINAL_REASONS - {0}:
                    self.refuse(diagnostic(stage, error, input_record(discarded=193 if terminal == 19 else 0,
                                                                      flags=1 if terminal == 19 else 0,
                                                                      terminal=terminal)))

    def test_all_terminal_and_first_reason_patterns_through_real_nvs(self):
        for terminal in range(32):
            for first in range(32):
                size = {0: 0, 12: 95, 13: 31, 15: 63}.get(first, 31)
                discarded = 193 if terminal == 19 else 192 if first else 0
                flags = (1 if discarded else 0) | (0x10 if first else 0) | (2 if first in (13, 15) else 0)
                if terminal in (0, 3) and discarded:
                    flags |= 64
                record = input_record(discarded, size, flags, terminal, first, 7)
                raw = diagnostic(3, record=record)
                if terminal in {0, 3, *range(9, 20)} and first in {0, 12, 13, 15}:
                    value = decode(raw)["input"]
                    self.assertEqual(discarded, value["discarded_bytes"])
                    self.assertEqual(decoder.REASONS[terminal], value["terminal_reason"])
                    self.assertEqual(decoder.REASONS[first], value["first_frame_reason"])
                else:
                    self.refuse(raw)

    def test_first_reason_count_and_flag_combinations(self):
        for first in range(32):
            for size in (0, 1, 30, 31, 32, 62, 63, 64, 94, 95, 96, 127):
                for flags in range(128):
                    allowed = ((first == 0 and size == 0) or (first == 12 and size == 95) or
                               (first == 13 and 31 <= size <= 95 and size != 63) or
                               (first == 15 and size == 63)) and bool(flags & 16) == (first != 0) \
                        and bool(flags & 1) and not flags & 64 and (not flags & 8 or bool(flags & 32)) \
                        and (first not in (13, 15) or bool(flags & 2))
                    record = input_record(192, size, flags, 9, first)
                    if allowed:
                        self.assertEqual(size, decoder._decode_input_record(record)["first_rejected_frame_bytes"])
                    else:
                        with self.assertRaises(decoder.ReadbackError):
                            decoder._decode_input_record(record)

    def test_exact_discard_bounds_and_all_delay_buckets(self):
        for discarded in (0, 1, 192, 193, 254, 255, 256, 287, 288):
            for delay in range(8):
                terminal = 19 if discarded > 192 else 9
                record = input_record(discarded=discarded, flags=1 if discarded else 0, terminal=terminal, delay=delay)
                result = decode(diagnostic(error=16 if terminal == 19 else terminal, record=record))["input"]
                self.assertEqual(discarded, result["discarded_bytes"])
                self.assertEqual(decoder.DELAY_BUCKETS[delay], result["first_read_delay_bucket"])
        for discarded in (289, 511):
            self.refuse(diagnostic(error=16, record=input_record(discarded=discarded, flags=1, terminal=19)))

    def test_crc_valid_impossible_observations_refuse(self):
        impossible = (
            input_record(discarded=288),  # Cannot proceed to send after sync exhaustion.
            input_record(flags=64),  # Cannot accept after discard when none was discarded.
            input_record(flags=2, terminal=9),  # No discarded bytes can have LF classification.
            input_record(discarded=1, terminal=9),  # Missing discarded-present flag.
            input_record(discarded=0, flags=1, terminal=9),
            input_record(discarded=192, flags=1, terminal=19),
            input_record(discarded=193, flags=1, terminal=9),
            input_record(discarded=94, first_bytes=95, flags=17, terminal=9, first_reason=12),
            input_record(discarded=1, flags=9, terminal=9),  # SLIP C0 must also be non-ASCII.
            input_record(discarded=31, first_bytes=31, flags=17, terminal=9, first_reason=13),
            input_record(discarded=63, first_bytes=63, flags=17, terminal=9, first_reason=15),
            input_record(discarded=1, flags=65, terminal=9),
            input_record(discarded=1, flags=1, terminal=0),
            input_record(discarded=1, flags=1, terminal=3),
        )
        for record in impossible:
            self.refuse(diagnostic(3, record=record))
        self.refuse(diagnostic(8, record=impossible[0]))
        for terminal in (0, 3, 17, 18):
            self.assertEqual(decoder.REASONS[terminal],
                             decode(diagnostic(3, record=input_record(discarded=1, flags=65,
                                                                      terminal=terminal)))["input"]["terminal_reason"])
        for terminal in (9, 10, 11, 12, 13, 14, 15, 16, 19):
            self.refuse(diagnostic(3, record=input_record(discarded=1, flags=65, terminal=terminal)))

    def test_regressed_first_sample_bucket_is_explicitly_indeterminate(self):
        # First feed before parser construction records clock regression, one
        # discarded byte, and the shared no-sample/long/invalid-sample bucket.
        result = decode(diagnostic(4, 11, input_record(discarded=1, flags=1, terminal=11, delay=7)))["input"]
        self.assertEqual("clock_regression", result["terminal_reason"])
        self.assertEqual("no_read_or_at_least_60s_or_regressed_sample", result["first_read_delay_bucket"])

    def test_crc_magic_and_reserved_version_gaps(self):
        record = input_record()
        for bit in range(64):
            bad = (int.from_bytes(record, "little") ^ (1 << bit)).to_bytes(8, "little")
            self.refuse(diagnostic(record=bad))
        for magic in (0, 0x980, 0xA00, 0xA1F, 0xA21, 0xFFF):
            self.refuse(diagnostic(record=input_record(magic=magic)))
        for byte in (0, 1, 2, 3):
            stage = bytearray(fixtures.stage_record(3)); stage[byte] ^= 1
            stage[6:] = binascii.crc_hqx(stage[:6], 0xffff).to_bytes(2, "little")
            self.refuse(fixtures.fixture([fixtures.entry(0, 1, "ot198diag", b"\x01"),
                                          fixtures.entry(1, 8, "stage", bytes(stage))]))

    def test_duplicate_unexpected_missing_and_wrong_type_keys(self):
        for row in (fixtures.entry(1, 8, "input", input_record()),
                    fixtures.entry(1, 8, "stage", fixtures.stage_record(4)),
                    fixtures.entry(1, 8, "frame", input_record()),
                    fixtures.entry(1, 8, "secret", b"fixture"),
                    fixtures.entry(0, 1, "ot198diag", b"\x02")):
            self.refuse(diagnostic(extras=(row,)))
        for kind, key in ((4, "input"), (8, "frame")):
            self.refuse(diagnostic(input_present=False, extras=(fixtures.entry(1, kind, key, input_record()),)))
        self.refuse(fixtures.fixture([fixtures.entry(0, 1, "ot198diag", b"\x01"),
                                     fixtures.entry(1, 8, "input", input_record())]))

    def test_real_parser_reuse_and_freshness(self):
        self.assertIs(v1.assert_fresh, decoder.assert_fresh)
        self.assertTrue(decoder.assert_fresh(fixtures.fixture()))
        with self.assertRaises(v1.ReadbackError):
            decoder.assert_fresh(diagnostic())
        deleted = fixtures.fixture([(0, fixtures.entry(0, 1, "ot198diag", b"\x01"))])
        with self.assertRaises(v1.ReadbackError):
            decoder.assert_fresh(deleted)
        with patch.object(v1, "_parse", wraps=v1._parse) as parser:
            decode(diagnostic())
            parser.assert_called_once()
        for raw in (b"", b"\xff" * 12287, bytearray(diagnostic()), "private"):
            self.refuse(raw)

    def test_fixed_projection_has_no_completion_receipt_identity_or_raw_bytes(self):
        value = decode(diagnostic(8))
        self.assertEqual({"schema", "image", "stage", "error", "input", "input_status"}, set(value))
        self.assertEqual({"discarded_bytes", "first_rejected_frame_bytes", "flags", "terminal_reason",
                          "first_frame_reason", "first_read_delay_bucket"}, set(value["input"]))

    def test_canonical_contract_agreement(self):
        contract = json.loads((ROOT / "firmware/components/security_diagnostics/sync_record_v1.json").read_text())
        self.assertEqual(decoder.SYNC_CONTRACT, contract["schema"])
        self.assertEqual(("ot198diag", "input", "u64", 8, "little"),
                         tuple(contract[key] for key in ("namespace", "key", "nvs_type", "bytes", "byte_order")))
        self.assertEqual({str(bit): label for bit, label in enumerate(decoder.FLAG_NAMES)}, contract["flags"])
        self.assertEqual({"kind": "crc16-ccitt", "initial": 65535, "polynomial": 4129,
                          "offset": 6, "covered_bytes": 6}, contract["crc"])
        self.assertEqual({"shift": 0, "bits": 12, "value": decoder.INPUT_MAGIC}, contract["fields"]["magic"])
        self.assertEqual(sorted(decoder.TERMINAL_REASONS), contract["fields"]["terminal_reason"]["allowed"])
        self.assertEqual(sorted(decoder.FIRST_FRAME_REASONS), contract["fields"]["first_frame_reason"]["allowed"])
        for name, shift, bits in (("discarded_bytes", 12, 9), ("first_rejected_frame_bytes", 21, 7),
                                  ("flags", 28, 7), ("terminal_reason", 35, 5),
                                  ("first_frame_reason", 40, 5), ("first_read_delay_bucket", 45, 3)):
            self.assertEqual((shift, bits), (contract["fields"][name]["shift"], contract["fields"][name]["bits"]))
        self.assertEqual(288, contract["fields"]["discarded_bytes"]["maximum"])
        self.assertEqual(95, contract["fields"]["first_rejected_frame_bytes"]["maximum"])

    def test_cpp_wire_vector_agreement(self):
        if VECTORS is None:
            self.skipTest("C++ wire-vector gate requires explicit --wire-vectors")
        rows = Path(VECTORS).read_text(encoding="ascii").splitlines()
        self.assertGreaterEqual(len(rows), 1024)
        for row in rows:
            raw, expected = row.split()
            self.assertEqual(16, len(raw))
            self.assertIn(expected, ("0", "1"))
            data = int(raw, 16).to_bytes(8, "little")
            try:
                decoder._decode_input_record(data)
                accepted = True
            except decoder.ReadbackError:
                accepted = False
            self.assertEqual(expected == "1", accepted, row)
            if accepted:
                self.assertEqual(decoder._decode_input_record(data), decode(diagnostic(3, record=data))["input"])
            else:
                self.refuse(diagnostic(3, record=data))


if __name__ == "__main__":
    if "--wire-vectors" in sys.argv:
        index = sys.argv.index("--wire-vectors")
        VECTORS = sys.argv[index + 1]
        del sys.argv[index:index + 2]
    unittest.main(verbosity=2)
