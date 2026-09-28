#!/usr/bin/env python3
"""Execute the target's stack-admission guard and audit the corrected builds."""

from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / "firmware/targets/heltec_v4_bench"
HOST = "CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE"
STACK_KEYS = {HOST, "CONFIG_NIMBLE_TASK_STACK_SIZE", "CONFIG_BT_NIMBLE_TASK_STACK_SIZE"}
BEGIN = "# OT0101e host-stack admission:"
END = "# End OT0101e host-stack admission."
AUDIT_ARTIFACTS = os.environ.get("OPENTRAIL_STACK_ARTIFACT_AUDIT") == "1"
BUILDS = {
    "ot0101e-stack-v2-a1": "ON",
    "ot0101e-stack-v2-a2": "ON",
    "ot0101e-stack-v2-ordinary": "OFF",
}


def cmake_command() -> str:
    explicit = os.environ.get("OPENTRAIL_CMAKE")
    if explicit:
        return explicit
    on_path = shutil.which("cmake")
    if on_path:
        return on_path
    # Reuse the installed, pinned ESP-IDF tool on Windows. Do not install a
    # dependency or modify PATH merely to run the maintained admission suite.
    installed = Path.home() / ".espressif/tools/cmake/4.0.3/bin/cmake.exe"
    if installed.is_file():
        return str(installed)
    raise RuntimeError("CMake is required; set OPENTRAIL_CMAKE to the installed executable")


def config(path: Path) -> dict[str, str]:
    result = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("CONFIG_"):
            key, value = line.split("=", 1)
            if key in result:
                raise AssertionError(f"duplicate configuration: {key}")
            result[key] = value
        elif match := re.fullmatch(r"# (CONFIG_[A-Z0-9_]+) is not set", line):
            result[match[1]] = "n"
    return result


def header(path: Path) -> dict[str, str]:
    return dict(re.findall(r"^#define (CONFIG_[A-Z0-9_]+) (.+)$",
                           path.read_text(encoding="utf-8"), re.MULTILINE))


def guard() -> str:
    source = (TARGET / "CMakeLists.txt").read_text(encoding="utf-8")
    if source.count(BEGIN) != 1 or source.count(END) != 1:
        raise AssertionError("target admission block missing or duplicated")
    return source[source.index(BEGIN):source.index(END) + len(END)]


def admit(value: str | None) -> subprocess.CompletedProcess:
    # Execute the actual production block; CMake project setup is deliberately
    # omitted so a stale configuration can be tested without another SDK build.
    with tempfile.TemporaryDirectory(prefix="ot-stack-admission-") as temporary:
        script = Path(temporary) / "admission.cmake"
        script.write_text(guard(), encoding="utf-8", newline="\n")
        command = [cmake_command()]
        if value is not None:
            command.append(f"-D{HOST}={value}")
        return subprocess.run([*command, "-P", str(script)], capture_output=True,
                              text=True, timeout=20, check=False)


class BleStackAdmissionTests(unittest.TestCase):
    def test_defaults_preserve_separate_main_and_evaluation_budgets(self):
        ordinary = config(TARGET / "sdkconfig.defaults")
        evaluation = config(TARGET / "sdkconfig.confirmation-eval.defaults")
        self.assertEqual((ordinary[HOST], ordinary["CONFIG_ESP_MAIN_TASK_STACK_SIZE"]),
                         ("8192", "8192"))
        self.assertEqual((evaluation[HOST], evaluation["CONFIG_ESP_MAIN_TASK_STACK_SIZE"]),
                         ("8192", "24576"))

    @unittest.skipUnless(AUDIT_ARTIFACTS, "private retained-build audit not requested")
    def test_seed_changes_only_the_three_host_stack_names(self):
        original = config(ROOT / "build/ot0101e-conn-a2/sdkconfig")
        corrected = config(TARGET / "sdkconfig")
        self.assertEqual(original.keys(), corrected.keys())
        differences = {key for key in original if original[key] != corrected[key]}
        self.assertEqual(differences, STACK_KEYS)
        for key in STACK_KEYS:
            self.assertEqual((original[key], corrected[key]), ("4096", "8192"))

    def test_actual_guard_rejects_stale_missing_or_invalid_budget(self):
        for value in (None, "", "4096", "8191", "0", "-1", "8192.0", "invalid"):
            with self.subTest(value=value):
                result = admit(value)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("requires at least 8192 stack bytes", result.stderr)

    def test_actual_guard_accepts_corrected_and_larger_budget(self):
        for value in ("8192", "16384"):
            with self.subTest(value=value):
                result = admit(value)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_admission_runs_after_idf_has_loaded_existing_configuration(self):
        source = (TARGET / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertLess(source.index("project(opentrail_heltec_v4_bench)"),
                        source.index(BEGIN))
        self.assertNotIn("OPENTRAIL_CONNECTION_DIAGNOSTICS", guard())
        self.assertNotIn("OPENTRAIL_CONFIRMATION_EVALUATION", guard())

    @unittest.skipUnless(AUDIT_ARTIFACTS, "private retained-build audit not requested")
    def test_retained_generated_4096_configuration_is_rejected(self):
        stale = header(ROOT / "build/ot0101e-conn-a2/config/sdkconfig.h")
        self.assertEqual(stale[HOST], "4096")
        self.assertNotEqual(admit(stale[HOST]).returncode, 0)

    @unittest.skipUnless(AUDIT_ARTIFACTS, "private retained-build audit not requested")
    def test_actual_generated_profiles_keep_all_other_inputs(self):
        original = config(ROOT / "build/ot0101e-conn-a2/sdkconfig")
        original_header = header(ROOT / "build/ot0101e-conn-a2/config/sdkconfig.h")
        expected = dict(original)
        expected.update({key: "8192" for key in STACK_KEYS})
        expected_header = dict(original_header)
        expected_header[HOST] = "8192"
        for name, diagnostics in BUILDS.items():
            with self.subTest(build=name):
                build = ROOT / "build" / name
                self.assertEqual(config(build / "sdkconfig"), expected)
                self.assertEqual(header(build / "config/sdkconfig.h"), expected_header)
                cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
                for key, value in (("OPENTRAIL_CONNECTION_DIAGNOSTICS", diagnostics),
                                   ("OPENTRAIL_DIAGNOSTIC_SENSOR_DISPLAY", "ON"),
                                   ("OPENTRAIL_CONFIRMATION_EVALUATION", "OFF")):
                    self.assertRegex(cache, rf"(?m)^{key}:\w+={value}$")
                self.assertIn("PROJECT_VER:UNINITIALIZED=ot0101e-conn-diag-v1", cache)


if __name__ == "__main__":
    unittest.main(verbosity=2)
