"""No-device fixture tests of exact source closure and separate recovery images."""
from copy import deepcopy
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import noise_xk_source_bundle as bundle


class BundleTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        for path in bundle.SOURCE_PATHS:
            target = self.root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(("source " + path).encode())
        self.sources = bundle.freeze_sources(self.root)
        self.paths = {role: role + ".bin" for role in bundle.IMAGE_ROLES}
        for role, path in self.paths.items():
            (self.root / path).write_bytes(("distinct bytes " + role).encode())
        self.images = bundle.freeze_images(self.root, self.paths)

    def test_complete_manifest_verifies_without_hardware_or_authority(self):
        bundle.verify_sources(self.root, self.sources)
        bundle.verify_images(self.root, self.images, self.paths)
        self.assertEqual(len(self.sources["sources"]), len(bundle.SOURCE_PATHS))
        self.assertNotIn(str(self.root), str(self.sources))
        self.assertNotIn("path", str(self.images))

    def test_every_transitive_source_is_bound(self):
        for descriptor in self.sources["sources"]:
            path = self.root / descriptor["path"]
            original = path.read_bytes()
            path.write_bytes(original.replace(b"source", b"tamper", 1))
            with self.assertRaisesRegex(bundle.BundleError, "source_digest_mismatch"):
                bundle.verify_sources(self.root, self.sources)
            path.write_bytes(original)

    def test_omitted_duplicate_or_extra_source_cannot_verify(self):
        for change in ("omit", "duplicate", "extra"):
            manifest = deepcopy(self.sources)
            if change == "omit":
                manifest["sources"].pop()
            elif change == "duplicate":
                manifest["sources"].append(manifest["sources"][0])
            else:
                manifest["sources"][0]["path"] = "extra.py"
            with self.assertRaisesRegex(bundle.BundleError, "source_closure_mismatch"):
                bundle.verify_sources(self.root, manifest)

    def test_missing_source_fails_even_if_images_match(self):
        (self.root / bundle.SOURCE_PATHS[-1]).unlink()
        bundle.verify_images(self.root, self.images, self.paths)
        with self.assertRaises(bundle.BundleError):
            bundle.verify_sources(self.root, self.sources)

    def test_escape_absolute_and_noncanonical_paths_rejected(self):
        for path in (".", "../outside.py", "/outside.py", "C:/outside.py", "tools/../outside.py", "tools//name.py", "./name.py"):
            manifest = deepcopy(self.sources)
            manifest["sources"][0]["path"] = path
            with self.assertRaisesRegex(bundle.BundleError, "path_invalid"):
                bundle.verify_sources(self.root, manifest)

    def test_link_or_link_parent_rejected(self):
        target = self.root / bundle.SOURCE_PATHS[0]
        payload = target.read_bytes()
        target.unlink()
        real = self.root / "real.py"
        real.write_bytes(payload)
        try:
            target.symlink_to(real)
        except OSError:
            # Windows sandbox may not grant symlink creation; mock just the
            # filesystem query to exercise the same rejection branch.
            from unittest.mock import patch
            target.write_bytes(payload)
            with patch.object(bundle, "_link", side_effect=lambda path: path == target):
                with self.assertRaisesRegex(bundle.BundleError, "link_forbidden"):
                    bundle.verify_sources(self.root, self.sources)
        else:
            with self.assertRaisesRegex(bundle.BundleError, "link_forbidden"):
                bundle.verify_sources(self.root, self.sources)

    def test_linked_parent_directory_is_rejected(self):
        from unittest.mock import patch
        with patch.object(bundle, "_link", side_effect=lambda path: path == self.root / "tools"):
            with self.assertRaisesRegex(bundle.BundleError, "link_forbidden"):
                bundle.verify_sources(self.root, self.sources)

    def test_swapped_role_image_bytes_are_rejected(self):
        a, b = self.root / self.paths["restore_a"], self.root / self.paths["restore_b"]
        first, second = a.read_bytes(), b.read_bytes()
        a.write_bytes(second)
        b.write_bytes(first)
        with self.assertRaisesRegex(bundle.BundleError, "image_digest_mismatch"):
            bundle.verify_images(self.root, self.images, self.paths)

    def test_recovery_requires_both_restores_but_never_reads_benchmark(self):
        (self.root / self.paths["benchmark"]).unlink()
        bundle.verify_sources(self.root, self.sources)
        bundle.verify_images(self.root, self.images, self.paths, recovery=True)
        with self.assertRaises(bundle.BundleError):
            bundle.verify_images(self.root, self.images, self.paths)
        (self.root / self.paths["restore_b"]).unlink()
        with self.assertRaises(bundle.BundleError):
            bundle.verify_images(self.root, self.images, self.paths, recovery=True)

    def test_strict_manifest_shapes_and_numeric_types(self):
        for field, value in (("bytes", True), ("bytes", -1), ("sha256", "INVALID")):
            manifest = deepcopy(self.sources)
            manifest["sources"][0][field] = value
            with self.assertRaisesRegex(bundle.BundleError, "descriptor_invalid"):
                bundle.verify_sources(self.root, manifest)
        manifest = deepcopy(self.images)
        manifest["images"]["extra"] = manifest["images"]["benchmark"]
        with self.assertRaisesRegex(bundle.BundleError, "image_manifest_invalid"):
            bundle.verify_images(self.root, manifest, self.paths)


    def test_recorded_source_snapshot_matches_checkout(self):
        import json
        root = Path(__file__).resolve().parents[2]
        record = json.loads((root / "tests/benchmarks/crypto/OT-163-SUCCESSOR-BINDING-2026-09-08.json").read_text())
        self.assertEqual(record["execution_authority"], False)
        bundle.verify_sources(root, record["sources"])


if __name__ == "__main__":
    unittest.main()
