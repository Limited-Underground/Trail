#!/usr/bin/env python3
"""Focused adversarial tests for the host-only OT-150 bundle freeze."""

from __future__ import annotations

import ast
import copy
import hashlib
import json
import sys
import tempfile
import unittest
from contextlib import ExitStack
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import ot150_mbedtls_psa_bundle as bundle  # noqa: E402


def sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def json2(total: int, data: int, bss: int) -> bytes:
    value = {
        "version": "1.2",
        "total_size": total,
        "layout": [
            {
                "name": "DIRAM",
                "total": 341_760,
                "used": data + bss,
                "free": 341_760 - data - bss,
                "parts": {".data": {"size": data}, ".bss": {"size": bss}},
            }
        ],
    }
    return (json.dumps(value, indent=2) + "\n").encode("utf-8")


class Fixture:
    def __init__(self, directory: str) -> None:
        self.root = Path(directory).resolve() / "repo"
        self.root.mkdir()
        self.fixed: list[tuple[str, str, int, str, str | None]] = []
        fixed_specs = (
            ("benchmark_plan", "evidence/plan.json", {"schema": "plan"}),
            ("ot149_preparation", "evidence/preparation.json", {"schema": "prep"}),
            ("resource_successor", "evidence/resource.json", {"schema": "resource"}),
            ("frame_parser", "tools/frames.py", b"# frames\n"),
            ("frame_schema", "evidence/frame-schema.json", {"schema": "frame"}),
        )
        for role, relative, value in fixed_specs:
            if isinstance(value, bytes):
                raw = value
                canonical = None
            else:
                raw = (json.dumps(value, indent=2) + "\n").encode("utf-8")
                canonical = sha256(bundle.canonical_bytes(value))
            self._write(self.root / relative, raw)
            self.fixed.append((role, relative, len(raw), sha256(raw), canonical))

        self.runtime = {
            role: self.root / relative
            for role, relative in bundle.RUNTIME_BINDINGS
        }
        for role, path in self.runtime.items():
            self._write(path, f"# {role}\n".encode("ascii"))

        self.restore_payload = b"trail-restore-v0"
        self.restore = self.root / "private" / "trail.bin"
        self._write(self.restore, self.restore_payload)

        self.sdkconfig_payload = b"sdkconfig-v0\n"
        self.candidate = self._side(
            "candidate",
            {
                "application_bin": b"candidate-bin-v0",
                "application_elf": b"candidate-elf-v0",
                "linker_map": b"candidate-map-v0",
                "size_report_json2": json2(200, 20, 30),
            },
        )
        self.control = self._side(
            "control",
            {
                "application_bin": b"control-bin-v0",
                "application_elf": b"control-elf-v0",
                "linker_map": b"control-map-v0",
                "size_report_json2": json2(150, 10, 20),
            },
        )

    @staticmethod
    def _write(path: Path, payload: bytes) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)

    def _side(
        self, side: str, varying: dict[str, bytes]
    ) -> dict[str, dict[str, Path]]:
        common = {
            "bootloader_bin": b"bootloader-v0",
            "partition_table_bin": b"partition-v0",
            "generated_sdkconfig": self.sdkconfig_payload,
        }
        output: dict[str, dict[str, Path]] = {}
        names = {
            "application_bin": "ot150.bin",
            "application_elf": "ot150.elf",
            "linker_map": "ot150.map",
            "bootloader_bin": "bootloader.bin",
            "partition_table_bin": "partition-table.bin",
            "generated_sdkconfig": "sdkconfig",
            "size_report_json2": "size-report.json",
        }
        for run in ("A", "B"):
            artifacts: dict[str, Path] = {}
            for role, payload in {**varying, **common}.items():
                path = self.root / "private-builds" / side / run / names[role]
                self._write(path, payload)
                artifacts[role] = path
            output[run] = artifacts
        return output

    def patches(self) -> tuple[mock._patch, ...]:
        return (
            mock.patch.object(bundle, "ROOT", self.root),
            mock.patch.object(bundle, "FIXED_BINDINGS", tuple(self.fixed)),
            mock.patch.object(bundle, "RESTORE_NAME", self.restore.name),
            mock.patch.object(bundle, "RESTORE_BYTES", len(self.restore_payload)),
            mock.patch.object(bundle, "RESTORE_SHA256", sha256(self.restore_payload)),
            mock.patch.object(
                bundle,
                "GENERATED_SDKCONFIG_SHA256",
                sha256(self.sdkconfig_payload),
            ),
        )


class BundleTests(unittest.TestCase):
    def _fixture(self, directory: str) -> tuple[Fixture, ExitStack]:
        fixture = Fixture(directory)
        stack = ExitStack()
        self.addCleanup(stack.close)
        for patcher in fixture.patches():
            stack.enter_context(patcher)
        return fixture, stack

    def _build(self, fixture: Fixture) -> dict:
        return bundle.build_preparation(
            fixture.candidate,
            fixture.control,
            fixture.restore,
            fixture.runtime,
        )

    def test_01_current_canonical_parent_bindings_are_exact(self) -> None:
        bindings = bundle.fixed_bindings()
        self.assertEqual(
            bindings["benchmark_plan"]["raw_sha256"],
            "0280570b74b2b505b5a92e7834b24136caa0956ff1c536d7638bff0a8b105f2a",
        )
        self.assertEqual(
            bindings["ot149_preparation"]["raw_sha256"],
            "49d1cfba5fca01afeb1928be922eec287873b85b1d517c81c249e148331b8867",
        )
        self.assertEqual(
            bindings["resource_successor"]["canonical_sha256"],
            "1c44d3d6f0c0d7c38ce83c51f1b79c75130455f2bbf99fed7edb4ffa1b9efaf2",
        )
        self.assertEqual(bindings["frame_parser"]["bytes"], 12_472)
        self.assertEqual(bindings["frame_schema"]["bytes"], 7_512)

    def test_02_builds_deterministic_canonical_host_only_preparation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot150-bundle-") as directory:
            fixture, _ = self._fixture(directory)
            value = self._build(fixture)
            again = self._build(fixture)
            self.assertEqual(value, again)
            self.assertEqual(value["schema"], "OT150MERBP0")
            self.assertEqual(value["build_policy"]["project_version"], bundle.PROJECT_VER)
            self.assertEqual(value["candidate"]["operations"], list(bundle.OPERATIONS))
            self.assertEqual(
                value["future_public_outputs"]["matched_resource_result"],
                {
                    "path": bundle.RESOURCE_RESULT_RELATIVE,
                    "schema": "OTMRAR1",
                    "admitted": False,
                },
            )
            self.assertEqual(
                value["future_public_outputs"]["json2_reports"]["candidate"]["path"],
                bundle.CANDIDATE_REPORT_RELATIVE,
            )
            verdict = bundle.validate_preparation(
                value,
                fixture.candidate,
                fixture.control,
                fixture.restore,
                fixture.runtime,
            )
            self.assertTrue(verdict["executable_resource_bundle_prepared"])
            self.assertFalse(verdict["execution_authorized"])
            self.assertEqual(
                bundle.decode_canonical(bundle.canonical_document(value), "fixture"),
                value,
            )

    def test_03_a_b_paths_and_bytes_must_be_independent_and_equal(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot150-bundle-") as directory:
            fixture, _ = self._fixture(directory)
            reused = copy.deepcopy(fixture.candidate)
            reused["B"]["application_bin"] = reused["A"]["application_bin"]
            with self.assertRaisesRegex(bundle.ContractError, "A/B paths"):
                bundle.build_preparation(
                    reused, fixture.control, fixture.restore, fixture.runtime
                )
            fixture.candidate["B"]["application_bin"].write_bytes(b"tampered")
            with self.assertRaisesRegex(bundle.ContractError, "A/B artifacts differ"):
                self._build(fixture)

    def test_04_exact_artifact_roles_and_json2_reports_are_required(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot150-bundle-") as directory:
            fixture, _ = self._fixture(directory)
            missing = copy.deepcopy(fixture.control)
            del missing["A"]["linker_map"]
            with self.assertRaisesRegex(bundle.ContractError, "artifact role"):
                bundle.build_preparation(
                    fixture.candidate, missing, fixture.restore, fixture.runtime
                )
            for run in ("A", "B"):
                fixture.control[run]["size_report_json2"].write_bytes(
                    b'{"version":"1.1","total_size":1,"layout":[]}\n'
                )
            with self.assertRaisesRegex(bundle.ContractError, "version mismatch"):
                self._build(fixture)

        with tempfile.TemporaryDirectory(prefix="ot150-bundle-") as directory:
            fixture, _ = self._fixture(directory)
            report = json.loads(
                fixture.control["A"]["size_report_json2"].read_text(encoding="utf-8")
            )
            report["layout"].append(copy.deepcopy(report["layout"][0]))
            raw = (json.dumps(report, indent=2) + "\n").encode("utf-8")
            for run in ("A", "B"):
                fixture.control[run]["size_report_json2"].write_bytes(raw)
            with self.assertRaisesRegex(bundle.ContractError, "unique DIRAM"):
                self._build(fixture)

    def test_05_candidate_control_match_except_linkage(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot150-bundle-") as directory:
            fixture, _ = self._fixture(directory)
            for run in ("A", "B"):
                fixture.control[run]["bootloader_bin"].write_bytes(b"other boot")
            with self.assertRaisesRegex(bundle.ContractError, "common artifacts differ"):
                self._build(fixture)

        with tempfile.TemporaryDirectory(prefix="ot150-bundle-") as directory:
            fixture, _ = self._fixture(directory)
            payload = fixture.candidate["A"]["application_bin"].read_bytes()
            for run in ("A", "B"):
                fixture.control[run]["application_bin"].write_bytes(payload)
            with self.assertRaisesRegex(bundle.ContractError, "linkage distinction"):
                self._build(fixture)

    def test_06_exact_ot147_restore_and_candidate_only_future_write(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot150-bundle-") as directory:
            fixture, _ = self._fixture(directory)
            value = self._build(fixture)
            future = value["images"]["future_benchmark_write"]
            self.assertEqual(future["application_offset"], 0x10000)
            self.assertTrue(future["future_writable_after_separate_authority_only"])
            self.assertFalse(future["control_application_writable"])
            self.assertFalse(future["other_artifacts_writable"])
            fixture.restore.write_bytes(b"wrong")
            with self.assertRaisesRegex(bundle.ContractError, "restoration image"):
                self._build(fixture)

    def test_07_public_preparation_contains_no_private_paths(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot150-private-root-") as directory:
            fixture, _ = self._fixture(directory)
            value = self._build(fixture)
            raw = bundle.canonical_document(value).decode("ascii")
            self.assertNotIn(str(fixture.root), raw)
            self.assertNotIn("private-builds", raw)
            self.assertNotIn("\\", raw)
            self.assertTrue(value["privacy"]["build_artifacts_record_name_only"])
            self.assertTrue(
                all(not item["path"].startswith("/") for item in value["runtime"].values())
            )

    def test_08_runtime_bindings_are_distinct_repo_relative_files(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot150-bundle-") as directory:
            fixture, _ = self._fixture(directory)
            reused = dict(fixture.runtime)
            reused["protocol_transport"] = fixture.runtime["coordinator"]
            with self.assertRaisesRegex(bundle.ContractError, "identity reused"):
                bundle.build_preparation(
                    fixture.candidate, fixture.control, fixture.restore, reused
                )
            outside = Path(directory).resolve() / "outside.py"
            outside.write_bytes(b"outside\n")
            outside_runtime = dict(fixture.runtime)
            outside_runtime["coordinator"] = outside
            with self.assertRaisesRegex(bundle.ContractError, "repository relative"):
                bundle.build_preparation(
                    fixture.candidate,
                    fixture.control,
                    fixture.restore,
                    outside_runtime,
                )
            missing = dict(fixture.runtime)
            del missing["bundle_validator"]
            with self.assertRaisesRegex(bundle.ContractError, "binding set mismatch"):
                bundle.build_preparation(
                    fixture.candidate, fixture.control, fixture.restore, missing
                )
            extra = dict(fixture.runtime)
            extra["untrusted_validator"] = fixture.root / "tools/untrusted.py"
            fixture._write(extra["untrusted_validator"], b"# untrusted\n")
            with self.assertRaisesRegex(bundle.ContractError, "binding set mismatch"):
                bundle.build_preparation(
                    fixture.candidate, fixture.control, fixture.restore, extra
                )
            substitute = dict(fixture.runtime)
            substitute["bundle_validator"] = fixture.root / "tools/substitute.py"
            fixture._write(substitute["bundle_validator"], b"# substitute\n")
            with self.assertRaisesRegex(bundle.ContractError, "binding path mismatch"):
                bundle.build_preparation(
                    fixture.candidate, fixture.control, fixture.restore, substitute
                )

    def test_09_authority_admission_and_completion_claims_stay_false(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot150-bundle-") as directory:
            fixture, _ = self._fixture(directory)
            value = self._build(fixture)
            self.assertTrue(value["claims"]["executable_resource_bundle_prepared"])
            self.assertTrue(all(item is False for item in value["authority"].values()))
            for key, item in value["claims"].items():
                if key != "executable_resource_bundle_prepared":
                    self.assertFalse(item, key)
            tampered = copy.deepcopy(value)
            tampered["claims"]["matched_resource_result_admitted"] = True
            with self.assertRaisesRegex(bundle.ContractError, "boundary mismatch"):
                bundle.validate_preparation(
                    tampered,
                    fixture.candidate,
                    fixture.control,
                    fixture.restore,
                    fixture.runtime,
                )

    def test_10_canonical_and_source_surface_fail_closed(self) -> None:
        with self.assertRaisesRegex(bundle.ContractError, "duplicate key"):
            bundle.decode_canonical(b'{"schema":"x","schema":"y"}\n', "fixture")
        with self.assertRaisesRegex(bundle.ContractError, "not canonical"):
            bundle.decode_canonical(b'{"schema": "x"}\n', "fixture")
        source_path = ROOT / "tools/ot150_mbedtls_psa_bundle.py"
        tree = ast.parse(source_path.read_text(encoding="utf-8"))
        imports = {
            alias.name.split(".")[0]
            for node in ast.walk(tree)
            if isinstance(node, ast.Import)
            for alias in node.names
        }
        imports.update(
            node.module.split(".")[0]
            for node in ast.walk(tree)
            if isinstance(node, ast.ImportFrom) and node.module
        )
        self.assertTrue({"serial", "subprocess", "esptool"}.isdisjoint(imports))
        self.assertFalse(hasattr(bundle, "build_authority"))
        self.assertFalse(hasattr(bundle, "write_new"))
        self.assertFalse(hasattr(bundle, "main"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
