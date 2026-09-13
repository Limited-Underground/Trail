"""OT-210 image and fresh-original admission through the actual NVS parser.

Fixtures are synthetic three-page NVS bytes, never captured device backups.
namespace_fixture is also used by composed package/handoff tests. No physical
runtime, SDK device module, filesystem capture or process launcher is imported.
"""
import binascii
from dataclasses import FrozenInstanceError
import hashlib
import json
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import security_policy_input_readback as v1
import security_policy_receipt_readback as predecessor
import security_policy_invitation_readback as decoder
import security_policy_input_readback_tests as fixtures
import security_policy_sync_readback_tests as sync_fixtures

IMAGE = decoder.ImageBinding(decoder.SYNC_CONTRACT, "heltec_v4_invitation_eval",
    "4526209643bbfb51ccf95d04a992eed41c877dd72cc0b03f415783a63d9036d2")
PREDECESSOR_IMAGE = predecessor.ImageBinding(predecessor.SYNC_CONTRACT,
    "heltec_v4_security_receipt_sync",
    "d47b09dae0aa1ae878769410a5daaabdac07af2d4c95c9f9d3436a1e03321cee")
RESERVED_NAMES = (b"ot198diag", b"ot208_boot", b"ot208_ia", b"ot208_ib",
                  b"ot187_ta", b"ot187_tb", b"ot187_ra", b"ot187_rb")


def namespace_fixture(name, *, namespace_status=2, key_status=None):
    """Valid NVS namespace with optional live/deleted key; no device material.

    A deleted namespace entry is intentionally structurally unselectable and
    tests retained-history refusal. Status 2 is live; status 0 is deleted.
    """
    rows = [(namespace_status, fixtures.entry(0, 1, name, b"\x01"))]
    if key_status is not None:
        rows.append((key_status, fixtures.entry(1, 8, "marker", b"fixture")))
    return fixtures.fixture(rows)


def diagnostic(stage=8, error=0, record=None, *, input_present=True, extras=()):
    return sync_fixtures.diagnostic(stage, error, record,
        input_present=input_present, extras=extras)


def decode(raw, *, expected=IMAGE, observed=IMAGE):
    return decoder.decode(raw, expected_image=expected, observed_image=observed)


def blob_fixture(payload):
    """Real variable-length NVS entry with independent data/item/page CRCs."""
    padded = payload + b"\xff" * ((-len(payload)) % 32)
    metadata = struct.pack("<HHI", len(payload), 65535,
                           zlib.crc32(payload, 0xffffffff) & 0xffffffff)
    rows = [fixtures.entry(0, 1, "unrelated", b"\x01"),
            fixtures.entry(1, 0x41, "contents", metadata, span=1 + len(padded) // 32)]
    rows.extend(padded[index:index + 32] for index in range(0, len(padded), 32))
    return fixtures.fixture(rows)


class Tests(unittest.TestCase):
    def refuse(self, raw, *, fresh=False, **kwargs):
        with self.assertRaisesRegex(decoder.ReadbackError, "^sync_readback_refused$"):
            if fresh:
                decoder.assert_fresh(raw)
            else:
                decode(raw, **kwargs)

    def test_all_eight_physical_names_refuse_even_empty_or_deleted(self):
        self.assertEqual(frozenset(RESERVED_NAMES), decoder.FRESH_NAMESPACES)
        for name in RESERVED_NAMES:
            for ns_status, key_status in ((2, None), (2, 2), (2, 0), (0, None), (0, 0)):
                with self.subTest(name=name, namespace_status=ns_status, key_status=key_status):
                    self.refuse(namespace_fixture(name, namespace_status=ns_status,
                                                  key_status=key_status), fresh=True)

    def test_names_are_exact_no_payload_or_logical_alias_search(self):
        for name in (b"ot_state", b"ot_counter", b"ot208", b"ot208_boot_x",
                     b"xot208_boot", b"OT208_BOOT", b"ot187_tc", b"ot195diag"):
            self.assertTrue(decoder.assert_fresh(namespace_fixture(name, key_status=2)))
        raw = blob_fixture(b"|".join(RESERVED_NAMES))
        self.assertTrue(decoder.assert_fresh(raw))
        corrupted = bytearray(raw)
        corrupted[128] ^= 1
        self.refuse(bytes(corrupted), fresh=True)

    def test_fully_erased_or_purged_has_no_recoverable_history_claim(self):
        self.assertTrue(decoder.assert_fresh(fixtures.fixture()))
        self.assertTrue(decoder.assert_fresh(fixtures.fixture([(0, b"\x00" * 32)])))
        self.refuse(fixtures.fixture())
        # Residual bytes in an allegedly empty entry cannot be reclassified fresh.
        self.refuse(fixtures.fixture([(3, fixtures.entry(0, 1, "ot208_boot", b"\x01"))]), fresh=True)

    def test_recognizable_history_across_supported_pages_refuses(self):
        ordinary = namespace_fixture("ordinary")[:4096]
        for name in RESERVED_NAMES:
            retained = fixtures.fixture([fixtures.entry(0, 1, name, b"\x02")],
                                        sequence=1, state=0xfffffffc)[:4096]
            self.refuse(ordinary + retained + b"\xff" * 4096, fresh=True)

    def test_structural_failures_cannot_be_bypassed_by_absent_reserved_names(self):
        for raw in (b"", b"\xff" * 12287, b"\xff" * 12289,
                    bytearray(fixtures.fixture()), "private", None,
                    fixtures.fixture([fixtures.entry(1, 8, "orphan", b"fixture")]),
                    fixtures.fixture([(0, fixtures.entry(0, 1, "ordinary", b"\x01"))]),
                    fixtures.fixture([fixtures.entry(0, 1, "ordinary", b"\x01"),
                                      fixtures.entry(0, 1, "other", b"\x01")]),
                    fixtures.fixture([], state=0xfffffff8)):
            self.refuse(raw, fresh=True)
        raw = bytearray(namespace_fixture("ordinary"))
        for offset in (4, 28, 68, 72):
            bad = raw.copy()
            bad[offset] ^= 1
            self.refuse(bytes(bad), fresh=True)

    def test_fresh_admission_reuses_real_structural_parser_once(self):
        raw = namespace_fixture("ordinary", key_status=2)
        with patch.object(v1, "_parse", wraps=v1._parse) as parser:
            self.assertTrue(decoder.assert_fresh(raw))
            parser.assert_called_once_with(raw, True)
        # New refusal is tighter; the unchanged predecessor remains unchanged.
        for name in RESERVED_NAMES[1:]:
            self.assertTrue(predecessor.assert_fresh(namespace_fixture(name)))
            self.refuse(namespace_fixture(name), fresh=True)

    def test_binding_refuses_wrong_target_type_or_hash_before_nvs_parse(self):
        wrong_sha = decoder.ImageBinding(decoder.SYNC_CONTRACT, IMAGE.target, "b" * 64)
        for expected, observed in ((IMAGE, wrong_sha), (None, IMAGE),
                                   (IMAGE, {"contract": IMAGE.contract}),
                                   (PREDECESSOR_IMAGE, IMAGE), (IMAGE, PREDECESSOR_IMAGE)):
            with self.subTest(expected=type(expected).__name__, observed=type(observed).__name__):
                with patch.object(v1, "_parse", side_effect=AssertionError("must not parse")):
                    self.refuse(b"", expected=expected, observed=observed)
        with self.assertRaises(TypeError):
            decoder.decode(diagnostic())
        with self.assertRaises(predecessor.ReadbackError):
            predecessor.decode(diagnostic(), expected_image=IMAGE, observed_image=IMAGE)

    def test_image_contract_constructor_is_explicit_and_immutable(self):
        for contract, target, sha in (("auto", IMAGE.target, IMAGE.sha256),
                (decoder.SYNC_CONTRACT, PREDECESSOR_IMAGE.target, IMAGE.sha256),
                (decoder.SYNC_CONTRACT, IMAGE.target, "A" * 64),
                (decoder.SYNC_CONTRACT, IMAGE.target, "a" * 63),
                (decoder.SYNC_CONTRACT, IMAGE.target, True)):
            with self.assertRaises(decoder.ReadbackError):
                decoder.ImageBinding(contract, target, sha)
        with self.assertRaises(FrozenInstanceError):
            IMAGE.target = "other"

    def test_existing_record_pairs_have_identical_fixed_projection(self):
        for stage, errors in v1.ALLOWED.items():
            for error in errors:
                record = sync_fixtures.input_record(terminal=error) if stage == 4 else None
                raw = diagnostic(stage, error, record, input_present=stage >= 3)
                expected = predecessor.decode(raw, expected_image=PREDECESSOR_IMAGE,
                                               observed_image=PREDECESSOR_IMAGE)
                self.assertEqual(expected, decode(raw))

    def test_captured_invitation_names_are_not_original_freshness_admission(self):
        rows = []
        for index, name in enumerate(RESERVED_NAMES[1:], 2):
            rows.append(fixtures.entry(0, 1, name, bytes([index])))
            rows.append(fixtures.entry(index, 8, "marker", b"fixture"))
        captured = diagnostic(extras=rows)
        result = decode(captured)
        self.assertEqual("send_return", result["stage"])
        self.assertEqual("none", result["error"])
        self.refuse(captured, fresh=True)
        self.assertEqual({"schema", "image", "stage", "error", "input", "input_status"}, set(result))
        for name in RESERVED_NAMES:
            self.assertNotIn(name.decode(), json.dumps(result))

    def test_partial_and_failed_evaluation_do_not_become_completion_claims(self):
        value = decode(diagnostic(3))
        self.assertEqual("committed_before_stage_result", value["input_status"])
        self.assertEqual("waiting", value["stage"])
        for stage in (1, 2, 3):
            self.assertEqual("not_recorded", decode(diagnostic(stage, input_present=False))["input_status"])
        for error in (4, 5, 6, 7):
            self.assertEqual(v1.ERRORS[error], decode(diagnostic(6, error))["error"])
        self.assertNotIn("complete", json.dumps(value))

    def test_codec_cross_fields_and_stage_order_remain_strict(self):
        for raw in (diagnostic(1), diagnostic(2), diagnostic(4, input_present=False),
                    diagnostic(8, input_present=False),
                    diagnostic(4, 9, sync_fixtures.input_record(terminal=10)),
                    diagnostic(8, record=sync_fixtures.input_record(terminal=9)),
                    diagnostic(8, record=sync_fixtures.input_record(discarded=288)),
                    diagnostic(3, record=sync_fixtures.input_record(flags=64)),
                    diagnostic(extras=(fixtures.entry(1, 8, "unexpected", b"fixture"),))):
            self.refuse(raw)
        for offset in range(8):
            record = bytearray(sync_fixtures.input_record())
            record[offset] ^= 1
            self.refuse(diagnostic(record=bytes(record)))
        exhausted = sync_fixtures.input_record(discarded=193, flags=1, terminal=19)
        self.assertEqual("sync_limit", decode(diagnostic(4, 16, exhausted))["input"]["terminal_reason"])

    def test_pinned_sdk_generated_fixture_uses_the_same_nvs_admission(self):
        folder = ROOT / "tests/host/fixtures/security_policy_stage"
        raw = (folder / "sdk-stage.bin").read_bytes()
        provenance = json.loads((folder / "provenance.json").read_text(encoding="utf-8"))
        self.assertEqual(provenance["fixture_sha256"], hashlib.sha256(raw).hexdigest())
        self.assertTrue(decoder.assert_fresh(raw))
        self.refuse(raw)


if __name__ == "__main__":
    unittest.main(verbosity=2)
