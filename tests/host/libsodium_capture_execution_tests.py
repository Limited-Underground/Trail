"""Composed eight-operation capture, custody, restoration and independent audit."""
from pathlib import Path
from copy import deepcopy
import importlib.util
import json
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import libsodium_capture_execution as m

spec = importlib.util.spec_from_file_location('libsodium_execution_fixtures', ROOT / 'tests/host/ot121_local_primitive_frame_tests.py')
fixtures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)


class Endpoint:
    def __init__(self, raw):
        self.raw, self.closed = raw, False

    def read(self, size):
        if not self.raw:
            raise RuntimeError('mock stream ended')
        chunk, self.raw = self.raw[:size], self.raw[size:]
        return chunk

    def write(self, raw):
        raise AssertionError('autorun capture must never write controls')

    def close(self):
        self.closed = True


class Tests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.addCleanup(patch.stopall)
        self.root = Path(self.tmp.name)
        self.paths = [self.root / name for name in ('candidate.bin', 'original-a.bin', 'original-b.bin')]
        self.raw = [b'candidate', b'original-A', b'original-B-distinct']
        for path, raw in zip(self.paths, self.raw):
            path.write_bytes(raw)
        patch.object(m, 'BENCHMARK', m.descriptor(self.paths[0])).start()
        patch.object(m, 'ORIGINALS', tuple(m.descriptor(p) for p in self.paths[1:])).start()
        self.package = m.freeze_package(*self.paths)
        self.session = m.Session(self.package)
        self.c = self.session.coordinator
        self.c.ROOT = self.root
        self.c.PRIVATE_ROOT = self.root / '.private'
        self.c.PRIVATE_ROOT.mkdir()
        for attr, suffix in (('JOURNAL_PATH', 'journal'), ('EXECUTION_RECEIPT_PATH', 'execution'), ('RECOVERY_RECEIPT_PATH', 'recovery')):
            setattr(self.c, attr, self.c.PRIVATE_ROOT / ('libsodium-capture-3-' + suffix + '.json'))
        self.endpoints = (object(), object())
        self.config = self.session.config(self.endpoints)
        self.capture = fixtures._encode(fixtures._valid_records())
        case = self

        class Backend:
            def __init__(self):
                self.current = dict(zip(case.endpoints, case.raw[1:]))
                self.events, self.handles = [], []
                self.fail_write = self.fail_restore = False
                self.identity = True

            def verify_role(self, endpoint, role, descriptor):
                i = ('A', 'B').index(role)
                return self.identity and endpoint is case.endpoints[i] and descriptor.sha256 == m.digest(case.raw[i + 1])

            def write_application(self, endpoint, offset, image):
                self.events.append(('write', case.endpoints.index(endpoint), image.payload))
                self.current[endpoint] = image.payload
                if (self.fail_write and image.payload == case.raw[0]) or (self.fail_restore and image.payload != case.raw[0]):
                    raise RuntimeError('mock partial write')

            def verify_application(self, endpoint, offset, image):
                if self.current[endpoint] != image.payload:
                    raise RuntimeError('mock readback mismatch')

            def hard_reset(self, endpoint):
                self.events.append(('reset', case.endpoints.index(endpoint)))

            reset = hard_reset

            def baseline_digest(self, endpoint):
                return ('a' if endpoint is case.endpoints[0] else 'b') * 64

            def interval_ready(self, endpoint):
                self.events.append(('interval', case.endpoints.index(endpoint)))
                return True

            def close_interval(self, endpoint):
                self.events.append(('close_interval', case.endpoints.index(endpoint)))

            def resume_release(self, endpoint):
                return False

            def release_untouched(self, endpoint):
                self.events.append(('release_untouched', case.endpoints.index(endpoint)))

            def is_present(self, endpoint):
                return True

            def open(self, endpoint):
                handle = Endpoint(case.capture)
                self.handles.append(handle)
                return handle

        class Authority:
            def validate(self, binding, *, recovery):
                if binding != case.session.binding:
                    raise RuntimeError('wrong binding')
                return case.c.AuthorityGrant('a' * 64, 1, False, False)

        self.backend, self.authority = Backend(), Authority()

    def run_success(self):
        return self.session.execute(self.config, self.backend, self.authority)

    def assert_restored(self):
        self.assertEqual(list(self.backend.current.values()), self.raw[1:])
        self.assertTrue(all(h.closed for h in self.backend.handles))

    def capture_path(self, role='A'):
        return self.c.PRIVATE_ROOT / ('libsodium-capture-3-' + role + '-validated.frames')

    def test_success_distinct_restores_and_independent_audit(self):
        result = self.run_success()
        self.assertTrue(result['restoration_complete'])
        self.assert_restored()
        self.assertEqual([e[1] for e in self.backend.events if e[0] == 'write'], [0, 0, 1, 1])
        for role, custody in result['validated_capture_custody'].items():
            raw = self.capture_path(role).read_bytes()
            self.assertEqual(len(raw.splitlines()), 1621)
            self.assertEqual(custody, dict(bytes=len(raw), sha256=m.digest(raw), frame_count=1621))
        self.assertEqual(m.audit_receipt(self.package, result, self.c.PRIVATE_ROOT, expected_authority_sha='a' * 64)['result'], 'passed')

    def test_partial_first_write_restores(self):
        self.backend.fail_write = True
        with self.assertRaises(self.c.CoordinatorError):
            self.run_success()
        self.assert_restored()

    def test_partial_second_write_after_first_restored(self):
        original = self.backend.write_application
        def write(endpoint, offset, image):
            original(endpoint, offset, image)
            if endpoint is self.endpoints[1] and image.payload == self.raw[0]:
                raise RuntimeError('second partial write')
        with patch.object(self.backend, 'write_application', side_effect=write):
            with self.assertRaises(self.c.CoordinatorError):
                self.run_success()
        self.assert_restored()
        self.assertEqual([e[1] for e in self.backend.events if e[0] == 'write'], [0, 0, 1, 1])

    def test_missing_authority_rejected_before_mutation(self):
        with self.assertRaises(self.c.CoordinatorError):
            self.session.execute(self.config, self.backend, None)
        self.assertEqual(self.backend.events, [])

    def test_radio_grant_rejected_before_mutation(self):
        with patch.object(self.authority, 'validate', return_value=self.c.AuthorityGrant('a' * 64, 1, False, True)):
            with self.assertRaises(self.c.CoordinatorError):
                self.run_success()
        self.assertEqual(self.backend.events, [])

    def test_consumed_journal_rejected(self):
        self.c.JOURNAL_PATH.write_bytes(b'consumed')
        with self.assertRaises(self.c.CoordinatorError):
            self.run_success()
        self.assertEqual(self.backend.events, [])

    def test_malformed_capture_restores(self):
        self.capture = b'OTCBXRF2 {"malformed":true}\n'
        with self.assertRaises(self.c.CoordinatorError):
            self.run_success()
        self.assert_restored()
        self.assertFalse(self.capture_path().exists())

    def test_truncated_capture_restores(self):
        self.capture = self.capture[:len(self.capture) // 2]
        with self.assertRaises(self.c.CoordinatorError):
            self.run_success()
        self.assert_restored()

    def test_framing_rejection_context_survives_restoration_receipt(self):
        self.capture = b''.join(self.capture.splitlines(keepends=True)[:1417]) + b'\r\n'
        with self.assertRaises(self.c.CoordinatorError):
            self.run_success()
        self.assert_restored()
        receipt = json.loads(self.c.EXECUTION_RECEIPT_PATH.read_bytes())
        diagnostic = receipt['failure']['capture_diagnostics']
        self.assertEqual(diagnostic['rejection_class'], 'blank')
        self.assertEqual(diagnostic['rejected_line_bytes'], 2)
        self.assertEqual(diagnostic['frame_lines_buffered'], 1417)
        self.assertEqual(diagnostic['last_framed_kind'], 'operation_summary')
        self.assertEqual(diagnostic['last_framed_operation'], 'chacha20poly1305_decrypt')
        self.assertEqual(diagnostic['last_framed_phase'], 'warm')
        self.assertFalse(self.capture_path().exists())

    def test_persistence_failure_still_restores_and_resets(self):
        original, count = self.c._persist, [0]
        def persist(journal):
            count[0] += 1
            return False if count[0] > 2 else original(journal)
        with patch.object(self.c, '_persist', side_effect=persist):
            with self.assertRaises(self.c.CoordinatorError):
                self.run_success()
        self.assert_restored()
        self.assertIn(('reset', 0), self.backend.events)

    def test_existing_custody_is_not_overwritten(self):
        self.capture_path().write_bytes(b'owner data')
        with self.assertRaises(self.c.CoordinatorError):
            self.run_success()
        self.assert_restored()
        self.assertEqual(self.capture_path().read_bytes(), b'owner data')

    def test_capture_fsync_failure_restores(self):
        import os
        original = os.fsync
        def sync(fd):
            if self.capture_path().exists():
                raise OSError('mock custody durability failure')
            return original(fd)
        with patch.object(os, 'fsync', side_effect=sync):
            with self.assertRaises(self.c.CoordinatorError):
                self.run_success()
        self.assert_restored()

    def test_capture_readback_failure_restores(self):
        original = Path.read_bytes
        def read(path):
            raw = original(path)
            return raw + b'changed' if path == self.capture_path() else raw
        with patch.object(Path, 'read_bytes', read):
            with self.assertRaises(self.c.CoordinatorError):
                self.run_success()
        self.assert_restored()

    def test_missing_candidate_recovery_restores_original(self):
        self.backend.fail_write = self.backend.fail_restore = True
        with self.assertRaises(self.c.CoordinatorError):
            self.run_success()
        self.paths[0].unlink()
        self.backend.fail_write = self.backend.fail_restore = False
        result = self.session.recover(self.config, self.backend, self.authority)
        self.assertTrue(result['restoration_complete'])
        self.assert_restored()

    def test_changed_original_rejected_before_mutation(self):
        self.paths[1].write_bytes(b'changed')
        with self.assertRaises(ValueError):
            self.run_success()
        self.assertEqual(self.backend.events, [])

    def test_rom_preflight_has_no_reset_and_closes_before_original_release(self):
        self.run_success()
        events = self.backend.events
        first_write = next(i for i, e in enumerate(events) if e[0] == 'write')
        self.assertFalse(any(e[0] == 'reset' for e in events[:first_write]))
        for role in (0, 1):
            close = events.index(('close_interval', role))
            self.assertEqual(events[close + 1], ('reset', role))
        self.assertLess(events.index(('close_interval', 0)),
                        next(i for i, e in enumerate(events) if e[0] == 'write' and e[1] == 1))
        journal = json.loads(self.c.JOURNAL_PATH.read_bytes())
        self.assertEqual(journal['nvs_baselines'], {'A': 'a' * 64, 'B': 'b' * 64})
        self.assertEqual(journal['schema'], 'OTLSCJ3')

    def test_interval_rejection_releases_both_untouched_without_write(self):
        with patch.object(self.backend, 'interval_ready', return_value=False):
            with self.assertRaises(self.c.CoordinatorError):
                self.run_success()
        self.assertFalse(any(e[0] == 'write' for e in self.backend.events))
        self.assertIn(('release_untouched', 0), self.backend.events)
        self.assertIn(('release_untouched', 1), self.backend.events)

    def test_independent_close_failure_blocks_original_reset_and_b_write(self):
        with patch.object(self.backend, 'close_interval', side_effect=RuntimeError('readback failed')):
            with self.assertRaises(self.c.CoordinatorError):
                self.run_success()
        self.assertEqual(sum(e == ('reset', 0) for e in self.backend.events), 1)
        self.assertFalse(any(e[0] == 'write' and e[1] == 1 for e in self.backend.events))

    def test_recovery_refuses_changed_baseline_without_app_write(self):
        self.backend.fail_write = self.backend.fail_restore = True
        with self.assertRaises(self.c.CoordinatorError):
            self.run_success()
        self.backend.events.clear()
        with patch.object(self.backend, 'baseline_digest', return_value='c' * 64):
            with self.assertRaises(self.c.CoordinatorError):
                self.session.recover(self.config, self.backend, self.authority)
        self.assertFalse(any(e[0] == 'write' for e in self.backend.events))
        self.assertEqual(self.backend.events, [])

    def test_failed_untouched_release_cannot_publish_restored_receipt(self):
        self.capture = b'invalid\n'
        with patch.object(self.backend, 'release_untouched', side_effect=RuntimeError('release failed')):
            with self.assertRaisesRegex(ValueError, 'untouched_release_failed'):
                self.run_success()
        self.assertFalse(self.c.EXECUTION_RECEIPT_PATH.exists())

    def test_pending_release_recovery_does_not_rewrite_application(self):
        self.backend.fail_write = self.backend.fail_restore = True
        with self.assertRaises(self.c.CoordinatorError):
            self.run_success()
        self.backend.events.clear()
        with patch.object(self.backend, 'resume_release', return_value=True):
            result = self.session.recover(self.config, self.backend, self.authority)
        self.assertTrue(result['restoration_complete'])
        self.assertFalse(any(e[0] == 'write' for e in self.backend.events))

    def test_changed_source_and_scope_rejected(self):
        bad = deepcopy(self.package)
        bad['scope']['radio_allowed'] = True
        with self.assertRaises(ValueError):
            m.verify_package(bad)
        original = Path.read_bytes
        target = ROOT / 'tools/ot121_local_primitive_frames.py'
        def read(path):
            raw = original(path)
            return raw + b'\n' if path == target else raw
        with patch.object(Path, 'read_bytes', read):
            with self.assertRaises(ValueError):
                m.Session(self.package)

    def test_audit_rejects_missing_capture(self):
        result = self.run_success()
        self.capture_path('B').unlink()
        with self.assertRaises((ValueError, OSError)):
            m.audit_receipt(self.package, result, self.c.PRIVATE_ROOT, expected_authority_sha='a' * 64)

    def test_audit_rejects_tampered_capture(self):
        result = self.run_success()
        self.capture_path().write_bytes(b'changed')
        with self.assertRaises(ValueError):
            m.audit_receipt(self.package, result, self.c.PRIVATE_ROOT, expected_authority_sha='a' * 64)

    def test_audit_rejects_forged_derived_result_even_with_matching_result_hash(self):
        result = self.run_success()
        result['nodes'][0]['result']['operations_completed'] = 7
        raw = json.dumps(result['nodes'][0]['result'], sort_keys=True, separators=(',', ':'), ensure_ascii=True).encode('ascii')
        result['nodes'][0]['result_sha256'] = m.digest(raw)
        with self.assertRaises(ValueError):
            m.audit_receipt(self.package, result, self.c.PRIVATE_ROOT, expected_authority_sha='a' * 64)

    def test_audit_rejects_changed_metadata_and_unapproved_authority(self):
        result = self.run_success()
        changes = ({'version': True}, {'version': 1}, {'node_count': True},
                   {'node_count': 1}, {'unexpected': False},
                   {'authority_raw_sha256': 'b' * 64})
        for change in changes:
            with self.subTest(change=change):
                altered = deepcopy(result)
                altered.update(change)
                with self.assertRaises(ValueError):
                    m.audit_receipt(self.package, altered, self.c.PRIVATE_ROOT,
                                    expected_authority_sha='a' * 64)
        with self.assertRaises(ValueError):
            m.audit_receipt(self.package, result, self.c.PRIVATE_ROOT,
                            expected_authority_sha='b' * 64)
        with self.assertRaises(ValueError):
            m.audit_receipt(self.package, result, self.c.PRIVATE_ROOT,
                            expected_authority_sha='invalid')

    def test_audit_rejects_changed_privacy_or_selection_claim(self):
        result = self.run_success()
        for field, key in (('privacy', 'device_identifiers_recorded'),
                           ('claims', 'candidate_selected')):
            with self.subTest(field=field):
                altered = deepcopy(result)
                altered[field][key] = True
                with self.assertRaises(ValueError):
                    m.audit_receipt(self.package, altered, self.c.PRIVATE_ROOT,
                                    expected_authority_sha='a' * 64)


if __name__ == '__main__':
    unittest.main()
