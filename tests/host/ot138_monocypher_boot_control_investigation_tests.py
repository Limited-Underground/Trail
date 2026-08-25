#!/usr/bin/env python3
"""Host-only tests for the bounded OT-138 boot/control investigation."""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "ot138_monocypher_boot_control_investigation.py"


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


inspection = load("ot138_boot_control_investigation", MODULE_PATH)


class Ot138BootControlInvestigationTests(unittest.TestCase):
    def test_01_exact_public_ot137_counter_shape_is_reproduced(self) -> None:
        fabricated = inspection.fabricated_ot137_shape()
        result = inspection.reproduce((fabricated[:512], fabricated[512:]))
        self.assertEqual(result.classification, inspection.CLASSIFICATION)
        self.assertEqual(result.failure_code, "preamble_invalid")
        self.assertEqual(result.diagnostics, inspection.EXPECTED_DIAGNOSTICS)
        self.assertEqual(440 + 584, result.diagnostics["bytes_observed"])

    def test_02_classification_is_invariant_to_nonempty_fragmentation(self) -> None:
        fabricated = inspection.fabricated_ot137_shape()
        partitions = [
            (fabricated[:512], fabricated[512:]),
            (fabricated[:256], fabricated[256:512], fabricated[512:]),
            (fabricated[:31], fabricated[31:512], fabricated[512:]),
            tuple(bytes([value]) for value in fabricated),
        ]
        results = [inspection.reproduce(chunks) for chunks in partitions]
        self.assertEqual({result.classification for result in results}, {inspection.CLASSIFICATION})
        self.assertTrue(all(result.failure_code == "preamble_invalid" for result in results))
        self.assertTrue(all(result.diagnostics["start_write_attempts"] == 1 for result in results))
        self.assertTrue(all(result.diagnostics["empty_reads"] == 0 for result in results))

    def test_03_fabricated_fixture_contains_only_declared_shape(self) -> None:
        fabricated = inspection.fabricated_ot137_shape()
        self.assertEqual(len(fabricated), 1024)
        self.assertEqual(fabricated.count(b"\n"), 11)
        self.assertEqual(set(fabricated), {ord("X"), ord("P"), ord("\n")})
        self.assertEqual(fabricated[:440].count(b"\n"), 11)
        self.assertNotIn(b"\n", fabricated[440:])

    def test_04_public_config_and_evidence_anchors_classify_conflict(self) -> None:
        result = inspection.inspect()
        self.assertEqual(result.generated_sdkconfig_sha256, inspection.GENERATED_SDKCONFIG_SHA256)
        self.assertTrue(result.usb_console_and_info_conflict)
        self.assertTrue(result.suppression_occurs_after_boot)
        self.assertEqual(result.physical_content, "unconfirmed_not_retained")

    def test_05_successor_direction_is_quiet_and_console_isolated(self) -> None:
        result = inspection.inspect()
        self.assertEqual(result.successor_direction, "console_isolated_quiet_target")
        self.assertEqual(result.successor_settings, (
            "CONFIG_ESP_CONSOLE_NONE=y",
            "CONFIG_ESP_CONSOLE_SECONDARY_NONE=y",
            "CONFIG_BOOTLOADER_LOG_LEVEL_NONE=y",
            "CONFIG_LOG_DEFAULT_LEVEL_NONE=y",
            "CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=y",
            "CONFIG_LOG_DYNAMIC_LEVEL_CONTROL=y",
        ))
        self.assertFalse(result.successor_built)

    def test_06_frozen_protocol_and_privacy_boundaries_remain_explicit(self) -> None:
        result = inspection.inspect()
        self.assertEqual(result.max_preamble_bytes, 512)
        self.assertEqual(result.exact_ready, "OTCBXCTL1 READY\n")
        self.assertTrue(result.frame_before_ready_rejected)
        self.assertFalse(result.raw_capture_retained)
        self.assertTrue(result.exact_public_shape_reproduced)
        self.assertTrue(result.nonempty_fragmentation_invariant)

    def test_07_invalid_fabricated_chunks_fail_closed(self) -> None:
        fabricated = inspection.fabricated_ot137_shape()
        for chunks in [
            (),
            (fabricated[:-1],),
            (fabricated[:513], fabricated[513:]),
            (b"", fabricated),
        ]:
            with self.assertRaises(inspection.InspectionError):
                inspection.reproduce(chunks)

    def test_08_no_execution_or_private_surface_exists(self) -> None:
        source = MODULE_PATH.read_text(encoding="utf-8")
        for forbidden in [
            "argparse",
            "subprocess",
            "import serial",
            "esptool",
            "write_application",
            "hard_reset",
            "list_ports",
            ".private",
            "COM7",
            "if __name__ ==",
        ]:
            self.assertNotIn(forbidden, source)
        rendered = repr(inspection.inspect())
        self.assertNotIn("D:\\", rendered)
        self.assertNotIn("COM", rendered)
        self.assertNotIn("device", rendered.casefold())
    def test_09_errors_are_sanitized_and_geometry_is_explicit(self) -> None:
        fabricated = inspection.fabricated_ot137_shape()
        with self.assertRaises(inspection.InspectionError) as caught:
            inspection.reproduce((fabricated[:513], fabricated[513:]))
        error = caught.exception
        self.assertIsNone(error.__cause__)
        self.assertIsNone(error.__context__)
        self.assertEqual(str(error), "fabricated_chunks_invalid")
        result = inspection.reproduce((fabricated[:512], fabricated[512:]))
        self.assertEqual(result.diagnostics["read_calls"], 2)
        self.assertEqual(result.diagnostics["start_write_attempts"], 1)
        self.assertEqual(result.diagnostics["preamble_lines_ignored"], 11)


if __name__ == "__main__":
    unittest.main(verbosity=2)
