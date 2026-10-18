"""Read-only tests for the frozen diagnostic evidence after worktree relocation."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import enrollment_candidate_settings_successor as gate
import enrollment_candidate_settings_successor_tests as existing


PRIVATE = ROOT / '.private'
LINKAGE_REF = dict(path=str(PRIVATE / 'ot0238c-inspection-inventory-correction-20261011'
    / 'inspection/diagnostic-linkage.json'), **gate.HISTORICAL_LINKAGE_PIN)


class HistoricalRelocationTests(unittest.TestCase):
    def test_wrong_linkage_path_or_bytes_refuses_before_relocation(self):
        with tempfile.TemporaryDirectory(dir=PRIVATE) as temporary:
            private = Path(temporary) / '.private'
            location = private / 'ot0238c-inspection-inventory-correction-20261011' / 'inspection'
            location.mkdir(parents=True)
            (location / 'diagnostic-linkage.json').write_bytes(b'wrong pinned linkage')
            ref = dict(path=str(location / 'diagnostic-linkage.json'), **gate.HISTORICAL_LINKAGE_PIN)
            with patch.object(gate, '_HistoricalClosure', side_effect=AssertionError('untrusted mapping')):
                with self.assertRaises(gate.SettingsSuccessorError):
                    gate.settled_diagnostic(dict(ref, path=str(location / 'elsewhere.json')), private)
                with self.assertRaises(gate.SettingsSuccessorError):
                    gate.settled_diagnostic(ref, private)

    def test_current_root_stays_literal_and_old_root_needs_fixed_linkage(self):
        current = gate._HistoricalClosure(PRIVATE, False)
        present = str(PRIVATE / 'example' / 'leaf.json')
        self.assertEqual(current.physical_path(present), Path(present))
        with self.assertRaises(gate.SettingsSuccessorError):
            current.physical_path(gate.HISTORICAL_PRIVATE_ROOT + r'\example\leaf.json')
        self.assertEqual(gate.LINKAGE, gate.HISTORICAL_LINKAGE_PIN)

    def test_actual_fixed_closure_reads_all_pinned_leaves_from_e_only(self):
        seen = {}
        original_read = gate.read

        def e_only(ref, private):
            path = Path(ref['path'])
            self.assertTrue(str(path).startswith(str(PRIVATE) + '\\'))
            self.assertFalse(str(path).startswith(gate.HISTORICAL_PRIVATE_ROOT + '\\'))
            seen[path] = (ref['bytes'], ref['sha256'])
            return original_read(ref, private)

        with patch.object(gate, 'read', side_effect=e_only):
            linkage, receipt, package = gate.settled_diagnostic(LINKAGE_REF, PRIVATE)
        self.assertEqual(linkage, json.loads(Path(LINKAGE_REF['path']).read_bytes()))
        self.assertEqual(receipt['package']['path'], gate.HISTORICAL_PRIVATE_ROOT +
            r'\ot0238c-inspection-inventory-correction-20261011\diagnostic-package.json')
        self.assertEqual(package['evidence_root'], gate.HISTORICAL_PRIVATE_ROOT +
            r'\ot0238c-inspection-inventory-correction-20261011\inspection')
        seen.pop(Path(LINKAGE_REF['path']))
        self.assertEqual(len(seen), 65)
        self.assertEqual(sum(size for size, _ in seen.values()), 1838754)

    def test_missing_e_leaf_has_no_historical_drive_fallback_and_corruption_refuses(self):
        with tempfile.TemporaryDirectory(dir=PRIVATE) as temporary:
            private = Path(temporary) / '.private'
            private.mkdir()
            logical = gate.HISTORICAL_PRIVATE_ROOT + r'\isolated\leaf.json'
            raw = b'accepted historical bytes'
            ref = dict(path=logical, bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())
            historical = gate._HistoricalClosure(private, True)
            real_is_file = Path.is_file
            checked = []
            def original_exists(path):
                checked.append(str(path))
                return str(path) == logical or real_is_file(path)
            with patch.object(Path, 'is_file', autospec=True, side_effect=original_exists):
                with self.assertRaises(gate.SettingsSuccessorError):
                    historical.read(ref)
            self.assertNotIn(logical, checked)
            physical = private / 'isolated' / 'leaf.json'
            physical.parent.mkdir()
            physical.write_bytes(raw)
            self.assertEqual(historical.read(ref), raw)
            physical.write_bytes(b'corrupted historical bytes')
            with self.assertRaises(gate.SettingsSuccessorError):
                historical.read(ref)

    def test_mapped_leaf_symlink_and_ancestor_junction_refuse(self):
        with tempfile.TemporaryDirectory(dir=PRIVATE) as temporary:
            private = Path(temporary) / '.private'
            parent = private / 'isolated'; parent.mkdir(parents=True)
            leaf = parent / 'leaf.json'; leaf.write_bytes(b'leaf')
            ref = dict(path=gate.HISTORICAL_PRIVATE_ROOT + r'\isolated\leaf.json',
                bytes=4, sha256=hashlib.sha256(b'leaf').hexdigest())
            real_symlink = Path.is_symlink
            real_junction = Path.is_junction
            with patch.object(Path, 'is_symlink', autospec=True,
                    side_effect=lambda path: path == leaf or real_symlink(path)):
                with self.assertRaises(gate.SettingsSuccessorError):
                    gate._HistoricalClosure(private, True).read(ref)
            with patch.object(Path, 'is_junction', autospec=True,
                    side_effect=lambda path: path == parent or real_junction(path)):
                with self.assertRaises(gate.SettingsSuccessorError):
                    gate._HistoricalClosure(private, True).read(ref)

    def test_wrong_prefix_traversal_aliases_and_conflicting_pins_refuse(self):
        historical = gate._HistoricalClosure(PRIVATE, True)
        old = gate.HISTORICAL_PRIVATE_ROOT + r'\sample\leaf.json'
        bad = (gate.HISTORICAL_PRIVATE_ROOT + r'-other\sample\leaf.json',
            gate.HISTORICAL_PRIVATE_ROOT + r'\sample\..\leaf.json',
            gate.HISTORICAL_PRIVATE_ROOT + r'\sample\.\leaf.json',
            gate.HISTORICAL_PRIVATE_ROOT + r'\sample\\leaf.json',
            gate.HISTORICAL_PRIVATE_ROOT + r'\sample/leaf.json',
            gate.HISTORICAL_PRIVATE_ROOT + r'\sample\leaf.json.',
            r'C:\lu\OpenTrail\.private\other\.private\sample\leaf.json')
        for value in bad:
            with self.subTest(value=value), self.assertRaises(gate.SettingsSuccessorError):
                gate._HistoricalClosure(PRIVATE, True).physical_path(value)
        self.assertEqual(historical.physical_path(old), PRIVATE / 'sample' / 'leaf.json')
        for alias in (str(PRIVATE / 'sample' / 'leaf.json'),
                      gate.HISTORICAL_PRIVATE_ROOT + r'\SAMPLE\LEAF.JSON'):
            with self.subTest(alias=alias), self.assertRaises(gate.SettingsSuccessorError):
                historical.physical_path(alias)
        # Model an NTFS short-name alias without depending on 8.3 names being
        # enabled on this test volume.
        resolved = Path.resolve
        canonical = PRIVATE / 'sample' / 'leaf.json'
        def alias_resolve(path, *args, **kwargs):
            if path.name in ('leaf.json', 'LEAF~1.JSO'):
                return canonical
            return resolved(path, *args, **kwargs)
        with patch.object(Path, 'resolve', autospec=True, side_effect=alias_resolve):
            closure = gate._HistoricalClosure(PRIVATE, True)
            closure.physical_path(old)
            with self.assertRaises(gate.SettingsSuccessorError):
                closure.physical_path(gate.HISTORICAL_PRIVATE_ROOT + r'\sample\LEAF~1.JSO')
        with tempfile.TemporaryDirectory(dir=PRIVATE) as temporary:
            private = Path(temporary) / '.private'
            private.mkdir()
            leaf = private / 'leaf'; leaf.write_bytes(b'a')
            closure = gate._HistoricalClosure(private, False)
            ref = dict(path=str(leaf), bytes=1, sha256=hashlib.sha256(b'a').hexdigest())
            self.assertEqual(closure.read(ref), b'a')
            with self.assertRaises(gate.SettingsSuccessorError):
                closure.read(dict(ref, sha256=hashlib.sha256(b'b').hexdigest()))

    def test_preserved_directory_membership_and_forbidden_markers_refuse(self):
        original_tempdir = tempfile.TemporaryDirectory
        with patch.object(existing.tempfile, 'TemporaryDirectory',
                side_effect=lambda: original_tempdir(dir=PRIVATE)):
            fixture = existing.Fixture()
        self.addCleanup(fixture.close)
        gate.settled_diagnostic(fixture.linkage, fixture.private)
        extra = fixture.legacy / 'extra'; extra.write_bytes(b'extra')
        with self.assertRaises(gate.SettingsSuccessorError):
            gate.settled_diagnostic(fixture.linkage, fixture.private)
        extra.unlink()
        missing = next(fixture.legacy.iterdir())
        raw = missing.read_bytes(); missing.unlink()
        with self.assertRaises(gate.SettingsSuccessorError):
            gate.settled_diagnostic(fixture.linkage, fixture.private)
        missing.write_bytes(raw)
        for name in ('blocked.pending', 'blocked.interrupted', 'blocked.transfer',
                     'enrollment-candidate-active.json',
                     'enrollment-candidate-' + 'a' * 32 + '.json'):
            marker = fixture.legacy / name
            marker.write_bytes(b'')
            with self.subTest(name=name), self.assertRaises(gate.SettingsSuccessorError):
                gate.settled_diagnostic(fixture.linkage, fixture.private)
            marker.unlink()


if __name__ == '__main__':
    unittest.main()
