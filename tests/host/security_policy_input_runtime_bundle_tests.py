"""Synthetic file-only runtime capsule tests; no runtime launch or hardware."""
from pathlib import Path
import base64
import copy
import hashlib
import importlib.util
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("runtime_bundle_tested", ROOT / "tools/security_policy_input_runtime_bundle.py")
bundle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle)


class BundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.base, self.site, self.work = [self.root / x for x in ("base", "site", "work")]
        for p in (self.base / "Lib", self.base / "DLLs", self.site, self.work / ".private", self.work / "tools"):
            p.mkdir(parents=True)
        for name in ("python.exe", "python314.dll", "python3.dll"):
            (self.base / name).write_bytes(b"synthetic " + name.encode())
        (self.base / "Lib" / "stdlib.py").write_bytes(b"value=1\n")
        (self.base / "DLLs" / "native.pyd").write_bytes(b"synthetic native")
        for name in bundle.POLICY:
            (self.work / "tools" / name).write_bytes(b"# synthetic policy\n")
        self.pwsh = self.root / "pwsh.exe"
        self.pwsh.write_bytes(b"synthetic verifier")
        self.lookup = patch.object(bundle.shutil, "which", return_value=str(self.pwsh))
        self.lookup.start()
        self.addCleanup(self.lookup.stop)
        self.dist("esptool", "5.3.1", ["pyserial>=3.3"], {"esptool/__init__.py": b"# tool\n"})
        self.dist("pyserial", "3.5", [], {"serial/__init__.py": b"# serial\n"})
        self.destination = self.work / ".private" / "capsule"

    def dist(self, name, version, requires, files):
        directory = self.site / (name.replace("-", "_") + "-" + version + ".dist-info")
        directory.mkdir(exist_ok=True)
        metadata = ("Name: " + name + "\nVersion: " + version + "\n" +
                    "".join("Requires-Dist: " + r + "\n" for r in requires)).encode()
        all_files = {**files, directory.name + "/METADATA": metadata}
        rows = []
        for filename, raw in all_files.items():
            path = self.site / filename
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(raw)
            digest = base64.urlsafe_b64encode(hashlib.sha256(raw).digest()).decode().rstrip("=")
            rows.append(f"{filename},sha256={digest},{len(raw)}")
        rows.append(directory.name + "/RECORD,,")
        (directory / "RECORD").write_text("\n".join(rows) + "\n", encoding="utf-8")
        return directory

    def build(self):
        return bundle.build(self.base, self.site, self.work, self.destination)

    def test_old_manifest_schema_refused(self):
        manifest=self.build()
        self.assertEqual(manifest['schema'],'OT198-INPUT-RUNTIME-1')
        manifest['schema']='OT189-RUNTIME-1'
        with self.assertRaises(bundle.RuntimeBundleError):bundle.verify(self.destination,manifest)

    def test_stage_required_file_cannot_be_removed_from_manifest(self):
        manifest=self.build()
        name='policy/security_policy_input_execution.py'
        (self.destination/name).unlink();manifest['files'].pop(name)
        with self.assertRaises(bundle.RuntimeBundleError):bundle.verify(self.destination,manifest)

    def test_complete_old_and_new_policy_closure(self):
        manifest=self.build()
        for name in ('security_policy_execution.py','security_policy_input_execution.py',
                     'security_policy_input_readback.py','security_policy_input_observation.py',
                     'security_policy_input_runtime_bundle.py','Invoke-SecurityPolicyInputOperator.ps1'):
            self.assertIn('policy/'+name,manifest['files'])

    def test_complete_capsule_exact_path_and_verifier_pin(self):
        manifest = self.build()
        self.assertTrue(bundle.verify(self.destination, manifest))
        self.assertEqual((self.destination / "python314._pth").read_bytes(), b"Lib\nDLLs\npackages\npolicy\n")
        self.assertEqual(manifest["powershell"]["path"], str(self.pwsh))
        self.assertFalse((self.destination / "manifest.json").exists())

    def test_unrelated_site_cache_and_pth_not_copied(self):
        for name in ("Lib/site-packages/evil.py", "Lib/__pycache__/cached.pyc", "Lib/evil.pth"):
            path = self.base / name
            path.parent.mkdir(exist_ok=True, parents=True)
            path.write_bytes(b"raise RuntimeError()")
        (self.site / "unrelated.py").write_bytes(b"raise RuntimeError()")
        manifest = self.build()
        self.assertFalse(any("evil" in name or "cached" in name or "unrelated" in name for name in manifest["files"]))

    def test_relevant_package_data_and_native_are_copied(self):
        self.dist("esptool", "5.3.1", ["pyserial>=3.3"],
                  {"esptool/__init__.py": b"# tool", "esptool/data.json": b"{}", "esptool/native.pyd": b"native"})
        manifest = self.build()
        self.assertIn("packages/esptool/data.json", manifest["files"])
        self.assertIn("packages/esptool/native.pyd", manifest["files"])

    def test_active_windows_dependency_and_inactive_extras(self):
        self.dist("esptool", "5.3.1", ["pyserial>=3.3", 'colorama; platform_system == "Windows"',
                                      'unknown-package; extra == "dev"', 'another; python_version < "3.11"'],
                  {"esptool/__init__.py": b"# tool"})
        self.dist("colorama", "0.4.6", [], {"colorama/__init__.py": b"# color"})
        manifest = self.build()
        self.assertEqual(manifest["versions"]["colorama"], "0.4.6")

    def test_unknown_active_dependency_refuses_before_creation(self):
        self.dist("esptool", "5.3.1", ["unknown-package"], {"esptool/__init__.py": b"# tool"})
        with self.assertRaises(bundle.RuntimeBundleError): self.build()
        self.assertFalse(self.destination.exists())

    def test_record_hash_tamper_refuses(self):
        (self.site / "serial/__init__.py").write_bytes(b"changed")
        with self.assertRaisesRegex(bundle.RuntimeBundleError, "installed_file_changed"): self.build()

    def test_capsule_tamper_missing_extra_and_pyc_refuse(self):
        manifest = self.build()
        target = self.destination / "Lib/stdlib.py"
        original = target.read_bytes()
        target.write_bytes(b"changed")
        with self.assertRaises(bundle.RuntimeBundleError): bundle.verify(self.destination, manifest)
        target.write_bytes(original)
        for name in ("unlisted.py", "cached.pyc", "evil.pth"):
            extra = self.destination / name
            extra.write_bytes(b"extra")
            with self.assertRaises(bundle.RuntimeBundleError): bundle.verify(self.destination, manifest)
            extra.unlink()
        target.unlink()
        with self.assertRaises(bundle.RuntimeBundleError): bundle.verify(self.destination, manifest)

    def test_manifest_traversal_and_casefold_duplicate_refuse(self):
        manifest = self.build()
        for name in ("../escape", "C:/escape", "Lib\\escape", "./escape", "PYTHON.EXE"):
            altered = copy.deepcopy(manifest)
            altered["files"][name] = altered["files"]["python.exe"]
            with self.assertRaises(bundle.RuntimeBundleError): bundle.verify(self.destination, altered)

    def test_reparse_path_refuses(self):
        with patch.object(Path, "is_symlink", return_value=True):
            with self.assertRaisesRegex(bundle.RuntimeBundleError, "indirect_path"): self.build()

    def test_destination_must_be_fresh_direct_private_child(self):
        self.build()
        with self.assertRaises(bundle.RuntimeBundleError): self.build()
        with self.assertRaises(bundle.RuntimeBundleError):
            bundle.build(self.base, self.site, self.work, self.work / "outside")

    def test_powershell_change_and_path_override_refuse(self):
        manifest = self.build()
        self.pwsh.write_bytes(b"changed")
        with self.assertRaisesRegex(bundle.RuntimeBundleError, "powershell_changed"):
            bundle.verify(self.destination, manifest)
        self.pwsh.write_bytes(b"synthetic verifier")
        (self.destination / "python314._pth").write_bytes(b"Lib\nimport site\n")
        with self.assertRaises(bundle.RuntimeBundleError): bundle.verify(self.destination, manifest)

    def test_esptool_config_exact_and_old_capsule_refused(self):
        manifest = self.build()
        config = self.destination / "esptool.cfg"
        self.assertEqual(config.read_bytes(), b"[esptool]\n")
        config.unlink()
        old = copy.deepcopy(manifest)
        old["files"].pop("esptool.cfg")
        with self.assertRaises(bundle.RuntimeBundleError):
            bundle.verify(self.destination, old)
        config.write_bytes(b"[esptool]\ncustom_reset_sequence=R1\n")
        altered = copy.deepcopy(manifest)
        altered["files"]["esptool.cfg"] = {
            "bytes": config.stat().st_size,
            "sha256": hashlib.sha256(config.read_bytes()).hexdigest()}
        with self.assertRaisesRegex(bundle.RuntimeBundleError, "esptool_config_changed"):
            bundle.verify(self.destination, altered)

    def test_marker_and_version_ranges(self):
        self.assertTrue(bundle.marker('python_full_version >= "3.9" and platform_system == "Windows"'))
        self.assertFalse(bundle.marker('python_full_version == "3.8.*"'))
        self.assertTrue(bundle.compare("4.4.0", ">=", "3.1.6", numeric=True))
        with self.assertRaises(bundle.RuntimeBundleError): bundle.marker('__import__("os")')


if __name__ == "__main__":
    unittest.main()
