"""Compile unchanged hash-admitted ESP-IDF stdio functions with a fake ROM.

This reproduces an upper-layer reporting boundary, not physical byte loss.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import noise_xk_console_source_probe as probe

FIXTURE = ROOT / "tests/host/fixtures/noise_xk_console_source"
PREFIX = r'''
#include <cstdint>
#include <cstddef>
#include <cerrno>
#include <string>
#include <vector>
#include <stdexcept>
#include <iostream>
namespace measured {
using ssize_t = int32_t; // ESP32-S3 syscall ABI width, independent of host size_t.
struct _reent { int error = 0; };
#define __errno_r(r) ((r)->error)
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define CONFIG_ESP_CONSOLE_NONE 0
#define CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG 0
#define ESP_ROM_CONSOLE_OUTPUT_SECONDARY 0
std::string attempted, delivered;
std::vector<int> status;
int esp_rom_output_tx_one_char(uint8_t c) {
  size_t i=attempted.size(); attempted.push_back(char(c));
  int result=i<status.size()?status[i]:0;
  if(result==0)delivered.push_back(char(c));
  return result; // Header contract: 0 success, 1 failure. No physical ROM is called.
}
'''
SUFFIX = r'''
void check(int fd,const std::string& input,std::vector<int> schedule,ssize_t expected,
           const std::string& calls,const std::string& accepted,int error=0) {
  attempted.clear();delivered.clear();status=schedule;_reent r;
  ssize_t result=_write_r_console(&r,fd,input.data(),input.size());
  if(result!=expected || attempted!=calls || delivered!=accepted || r.error!=error)
    throw std::runtime_error("console probe mismatch");
}
}
int main(int argc,char** argv){
 using measured::check;
 if(argc!=2)return 2;
 const std::string test=argv[1];
 if(test=="success") {
   check(1,"ABC",{},3,"ABC","ABC");check(2,"ABC",{},3,"ABC","ABC");
 } else if(test=="all_rom_failed") {
   check(1,"ABC",{1,1,1},3,"ABC","");check(2,"ABC",{1,1,1},3,"ABC","");
 } else if(test=="selective_rom_failed")check(1,"ABC",{0,1,0},3,"ABC","AC");
 else if(test=="newline_conversion")check(2,"A\n",{},2,"A\r\n","A\r\n");
 else if(test=="newline_rom_failed")check(1,"A\n",{0,1,1},2,"A\r\n","A");
 else if(test=="zero_length") {
   check(1,"",{},0,"","");check(2,"",{},0,"","");
 } else if(test=="invalid_fd") {
   check(8,"ABC",{},-1,"","",EBADF);check(8,"",{},-1,"","",EBADF);
 } else return 2;
 std::cout<<test<<" PASS\n";
}
'''


class ConsoleSourceProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        (ROOT / "build").mkdir(exist_ok=True)

    def copied_fixture(self, directory):
        target = Path(directory) / "fixture"
        shutil.copytree(FIXTURE, target)
        return target

    def test_exact_fixture_and_unchanged_body_admission(self):
        raw, evidence = probe.admit(FIXTURE)
        bodies = probe.admitted_bodies(FIXTURE)
        for (name, signature), body in zip(probe.SIGNATURES.items(), bodies):
            self.assertIn(body, raw[name])
            self.assertTrue(body.startswith(signature.encode()))
            self.assertIn(b"\r\n", body)
        self.assertEqual(evidence["upstream"], "ESP-IDF v6.0.2")

    def test_actual_compiled_reporting_boundary(self):
        # Admission occurs before temporary source creation or compiler invocation.
        bodies = probe.admitted_bodies(FIXTURE)
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "native g++ is required")
        with tempfile.TemporaryDirectory(prefix="nxk-console-", dir=ROOT / "build") as directory:
            source, exe = Path(directory) / "actual.cpp", Path(directory) / "actual.exe"
            source.write_bytes(PREFIX.encode() + b"\n".join(bodies) + SUFFIX.encode())
            command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-DNDEBUG", str(source), "-o", str(exe)]
            built = subprocess.run(command, capture_output=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stderr.decode(errors="replace"))
            for case in ("success", "all_rom_failed", "selective_rom_failed", "newline_conversion",
                         "newline_rom_failed", "zero_length", "invalid_fd"):
                with self.subTest(case=case):
                    ran = subprocess.run([str(exe), case], capture_output=True, timeout=5)
                    self.assertEqual(ran.returncode, 0, ran.stderr.decode(errors="replace"))
                    self.assertEqual(ran.stdout.decode().strip(), case + " PASS")

    def test_changed_source_rejected_before_extraction(self):
        for name in probe.SIGNATURES:
            with self.subTest(name=name), tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
                fixture = self.copied_fixture(directory)
                with (fixture / name).open("ab") as stream:
                    stream.write(b"\n")
                with self.assertRaisesRegex(probe.ProbeError, "fixture_digest_mismatch"):
                    probe.admitted_bodies(fixture)

    def test_normalized_source_rejected(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
            fixture = self.copied_fixture(directory)
            path = fixture / "stdio_simple.c"
            path.write_bytes(path.read_bytes().replace(b"\r\n", b"\n"))
            with self.assertRaisesRegex(probe.ProbeError, "fixture_digest_mismatch"):
                probe.admitted_bodies(fixture)

    def test_changed_provenance_cannot_readmit_sources(self):
        import hashlib
        import json
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
            fixture = self.copied_fixture(directory)
            source = fixture / "stdio_simple.c"
            source.write_bytes(source.read_bytes() + b"\n")
            path = fixture / "provenance.json"
            value = json.loads(path.read_bytes())
            value["sources"][source.name].update(bytes=source.stat().st_size,
                                              sha256=hashlib.sha256(source.read_bytes()).hexdigest())
            path.write_text(json.dumps(value))
            with self.assertRaisesRegex(probe.ProbeError, "fixture_digest_mismatch"):
                probe.admitted_bodies(fixture)

    def test_changed_map_config_or_license_rejected(self):
        for name in ("provenance.json", "LICENSE"):
            with self.subTest(name=name), tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
                fixture = self.copied_fixture(directory)
                with (fixture / name).open("ab") as stream:
                    stream.write(b" ")
                with self.assertRaisesRegex(probe.ProbeError, "fixture_digest_mismatch"):
                    probe.admitted_bodies(fixture)

    def test_missing_or_duplicate_anchor_rejected(self):
        for raw in (b"void other() {}", b"void f() {} void f() {}"):
            with self.subTest(raw=raw), self.assertRaisesRegex(probe.ProbeError, "function_anchor_ambiguous"):
                probe.extract_function(raw, "void f()")

    def test_missing_or_unterminated_body_rejected(self):
        for raw, code in ((b"void f();", "function_body_missing"), (b"void f() {", "function_body_unterminated")):
            with self.subTest(raw=raw), self.assertRaisesRegex(probe.ProbeError, code):
                probe.extract_function(raw, "void f()")

    def test_discarded_symbols_not_admitted_as_live(self):
        _, evidence = probe.admit(FIXTURE)
        evidence["link_map"]["live_write"]["first_line"] = 900
        with self.assertRaisesRegex(probe.ProbeError, "map_section_mismatch"):
            probe.validate_provenance(evidence)

    def test_wrong_console_config_rejected(self):
        _, evidence = probe.admit(FIXTURE)
        selection = evidence["sdkconfig"]["selections"][-1]
        selection["lines"] = [line.replace("PORT_NUM=4", "PORT_NUM=0") for line in selection["lines"]]
        with self.assertRaisesRegex(probe.ProbeError, "console_config_mismatch"):
            probe.validate_provenance(evidence)

    def test_wrong_live_object_rejected(self):
        _, evidence = probe.admit(FIXTURE)
        evidence["link_map"]["live_write"]["lines"] = [" .text._write_r_console", "0x4201c460 other.c.obj"]
        with self.assertRaisesRegex(probe.ProbeError, "linked_symbol_mismatch"):
            probe.validate_provenance(evidence)

    def test_unbound_full_artifacts_rejected(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
            path = Path(directory) / "wrong"
            path.write_bytes(b"not the accepted build")
            with self.assertRaisesRegex(probe.ProbeError, "build_artifact_digest_mismatch"):
                probe.verify_full_artifacts(FIXTURE, path, path)


if __name__ == "__main__":
    unittest.main()
