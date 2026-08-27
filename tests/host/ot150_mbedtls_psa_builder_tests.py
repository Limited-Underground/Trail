#!/usr/bin/env python3
"""Focused static and inertness checks for the OT-150 matched builder."""

from __future__ import annotations

import shutil
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BUILDER = ROOT / "tools" / "Build-Ot150MbedtlsPsaBundle.ps1"


class Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = BUILDER.read_text(encoding="utf-8")

    def test_builder_is_inert_without_explicit_execute(self) -> None:
        shell = shutil.which("powershell") or shutil.which("pwsh")
        self.assertIsNotNone(shell)
        completed = subprocess.run(
            [
                shell,
                "-NoProfile",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                str(BUILDER),
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn(
            "OT-150 builder is inert unless -Execute is supplied",
            completed.stdout + completed.stderr,
        )

    def test_fixed_semantic_project_version_replaces_git_derived_version(self) -> None:
        self.assertIn(
            "$requiredProjectVersion = 'ot150-mbedtls-psa-v0'", self.source
        )
        self.assertIn('"PROJECT_VER=$requiredProjectVersion"', self.source)
        self.assertIn(
            "$description.project_version -ne $requiredProjectVersion", self.source
        )
        self.assertIn("$binaryText.Contains($requiredProjectVersion)", self.source)
        self.assertNotIn("eb41a25-dirty", self.source)
        self.assertNotIn("8f92bfd-dirty", self.source)

    def test_four_fresh_runs_are_sequential_and_private(self) -> None:
        ordered = [
            "Invoke-BuildRun 'candidate' 'A'",
            "Invoke-BuildRun 'candidate' 'B'",
            "Invoke-BuildRun 'control' 'A'",
            "Invoke-BuildRun 'control' 'B'",
        ]
        positions = [self.source.index(value) for value in ordered]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("OutputRoot must be initially absent", self.source)
        self.assertIn("initial_build_directory_absent = $true", self.source)
        self.assertIn("four distinct private raw build logs", self.source)
        self.assertNotIn("Start-Job", self.source)
        self.assertNotIn("ForEach-Object -Parallel", self.source)

    def test_pinned_toolchain_and_offline_build_policy_are_exact(self) -> None:
        required = [
            "7101770dc6db2667b3c477cc31365dd1acd6db4e",
            "--no-ccache",
            "$env:IDF_COMPONENT_MANAGER = '0'",
            "$env:PYTHONNOUSERSITE = '1'",
            "Pinned ESP-IDF worktree is not clean",
            "Python 3\\.14\\.6",
            "cmake version 4\\.0\\.3",
            "^1\\.12\\.1$",
            "15\\.2\\.0",
            "ba38639de2a4d1f4fa48657e94cd3f250a3744ea8595e81fc74e9da0431a15c9",
        ]
        for marker in required:
            self.assertIn(marker, self.source)

    def test_shared_ot149_identities_remain_exact_gates(self) -> None:
        required = [
            "106890",
            "00fd8a75e1df36e7cb4d4aa2275492297e1300383e15cbbf2d4a6284dd99d85e",
            "15216",
            "604af9d70953d917734f45b4c1cb764a23c17c8e3e5b28e11e1f3f6a02ef1c38",
            "3072",
            "84569aa2badf3f7294042129b19d0b480784a93a550ada3253b57bc92a0671ab",
            "443",
            "973ce7d2d3559a792d62eacd859db1b52b7569080cb85c3f2fedeed4db6cc621",
        ]
        for marker in required:
            self.assertIn(marker, self.source)
        self.assertNotIn(
            "bb246459ac67175e234eecd46d02b32a146f2e333d2dab78d6510cbac9991197",
            self.source,
        )
        self.assertNotIn(
            "90f17bfdebc73cfc6f36520b7287dd206eaf1445b41dc1cd59fdeb429524dd8b",
            self.source,
        )

    def test_json2_and_otmrar1_outputs_are_exact_and_validated(self) -> None:
        required = [
            "OT-150-OT005-MBEDTLS-PSA-CANDIDATE-SIZE-REPORT-V1.json",
            "OT-150-OT005-MBEDTLS-PSA-CONTROL-SIZE-REPORT-V1.json",
            "OT-150-OT005-MATCHED-RESOURCE-RESULT-V1.json",
            "-m esp_idf_size --format json2 --output-file",
            "schema = 'OTMRAR1'",
            "status = 'matched_resource_result_admitted'",
            "$resourceValidator evaluate $contractPath $privateResultPath",
        ]
        for marker in required:
            self.assertIn(marker, self.source)
        validation = self.source.index(
            "$resourceValidator evaluate $contractPath $privateResultPath"
        )
        publication = self.source.index(
            "[System.IO.File]::Copy($privateResultPath, $resultPath, $false)"
        )
        self.assertLess(validation, publication)
        self.assertIn("ConvertTo-LfFinalBytes $json", self.source)
        self.assertNotIn("Normalize-JsonTextFile $sizeReport", self.source)

    def test_public_output_checkout_policies_are_exact(self) -> None:
        paths = [
            "tests/benchmarks/crypto/OT-150-OT005-MBEDTLS-PSA-CANDIDATE-SIZE-REPORT-V1.json",
            "tests/benchmarks/crypto/OT-150-OT005-MBEDTLS-PSA-CONTROL-SIZE-REPORT-V1.json",
            "tests/benchmarks/crypto/OT-150-OT005-MATCHED-RESOURCE-RESULT-V1.json",
        ]
        completed = subprocess.run(
            ["git", "check-attr", "-z", "text", "eol", "--", *paths],
            cwd=ROOT,
            capture_output=True,
            check=True,
        )
        fields = completed.stdout.decode("utf-8").split("\0")
        values = {
            (fields[index], fields[index + 1]): fields[index + 2]
            for index in range(0, len(fields) - 1, 3)
        }
        for path in paths[:2]:
            self.assertEqual(values[(path, "text")], "set")
            self.assertEqual(values[(path, "eol")], "crlf")
        self.assertEqual(values[(paths[2], "text")], "set")
        self.assertEqual(values[(paths[2], "eol")], "lf")

    def test_builder_has_no_hardware_or_authority_path(self) -> None:
        forbidden = [
            "write_flash",
            "erase_flash",
            "esptool.py",
            "serial.tools",
            "COM[",
            "one_attempt_authority_created = $true",
            "hardware_or_device_accessed = $true",
        ]
        for marker in forbidden:
            self.assertNotIn(marker, self.source)
        self.assertIn("hardware_accessed = $false", self.source)
        self.assertIn("execution_authority_created = $false", self.source)


if __name__ == "__main__":
    unittest.main(verbosity=2)
