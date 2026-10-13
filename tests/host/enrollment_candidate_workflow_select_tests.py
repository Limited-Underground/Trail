"""Literal launcher wiring and actual capture-only composition, synthetic I/O.

No CMD, native view, device, installed SDK or real child process is executed.
"""
import hashlib
import io
import json
from pathlib import Path
import sys
import unittest
from contextlib import redirect_stdout
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import enrollment_candidate_workflow_select as select
import enrollment_candidate_capture_runner as capture_runner
import enrollment_candidate_original_capture as capture
import enrollment_candidate_runtime as runtime
import enrollment_candidate_runner as runner
import enrollment_candidate_owner_workflow as owner
import enrollment_candidate_custody as custody
import enrollment_candidate_capture_runner_tests as existing


class SelectionTests(unittest.TestCase):
    def fixture(self):
        f = existing.Fixture()
        self.addCleanup(f.cleanup)
        helper = existing.CaptureRunnerTests()
        baseline = helper.capture_only_retry(f)
        self.assertTrue(baseline.is_dir())
        f.selector = f.disk.worktree / 'tools' / 'enrollment_candidate_workflow_select.py'
        f.selector.write_bytes(Path(select.__file__).read_bytes())
        f.selection_path = f.disk.worktree / select.BINDING_RELATIVE
        f.selection_path.parent.mkdir()
        f.review_path = f.selection_path.with_name('selection-review.json')
        sidecar = runtime.decode(f.assembly.assembly_path.read_bytes())
        f.selection = dict(schema='OT-CAPTURE-SELECTION-BINDING-1', worktree=str(f.disk.worktree),
            workflow='capture-only', selector=self.ref(f.selector), sources=sidecar['sources'],
            assembly=f.capture_package['assembly'], package=self.ref(f.capture_package_path),
            request=f.capture_package['request'], grant=f.capture_package['grant'])
        actual = runtime.decode((ROOT / '.private/ot0238c-capture-only-20261011/independent-review.json').read_bytes())
        actual['worktree'] = str(f.disk.worktree)
        for relative in select.CORE_FILES:
            target = f.disk.worktree / relative
            if not target.exists():
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((ROOT / relative).read_bytes())
        f.core_review_path = f.selection_path.with_name('closed-core-review.json')
        f.core_review_path.write_bytes(runtime.canonical(actual))
        f.selection['core_review'] = self.ref(f.core_review_path)
        self.save_selection(f)
        f.factory_calls.clear()
        f.log.clear(); f.capture_log.clear()
        for sdk in f.sdks.values(): sdk.trace.clear()
        return f

    def ref(self, path):
        return dict(path=str(path), **runtime.descriptor(path.read_bytes()))

    def save_selection(self, f):
        core = {k: v for k, v in f.selection.items() if k != 'review'}
        review = dict(schema='OT-CAPTURE-SELECTION-REVIEW-1',
            binding_core_sha256=hashlib.sha256(runtime.canonical(core)).hexdigest(),
            outcome='no_unresolved_findings', findings=[], hardware_access=False, live_grants_created=False)
        f.review_path.write_bytes(runtime.canonical(review))
        f.selection['review'] = self.ref(f.review_path)
        f.selection_path.write_bytes(runtime.canonical(f.selection))
        f.selection_sha = runtime.descriptor(f.selection_path.read_bytes())['sha256']

    def launch(self, f, child):
        with patch.object(select, 'ROOT', f.disk.worktree), patch.object(select, '__file__', str(f.selector)):
            return select.launch_capture(f.selection_path, f.selection_sha,
                                         utc=f.utc, monotonic=f.clock, subprocess_run=child)

    def test_default_and_explicit_full_delegate_unchanged_arguments(self):
        for prefix in ([], ['--workflow', 'retained_rekey']):
            with self.subTest(prefix=prefix), patch.object(owner, 'main', return_value=7) as full, \
                    patch.object(select, 'launch_capture', side_effect=AssertionError('capture forbidden')):
                self.assertEqual(select.main(prefix + ['--plan', 'existing-plan', '--plan-sha256', 'a'*64]), 7)
                full.assert_called_once_with(['--plan', 'existing-plan', '--plan-sha256', 'a'*64])

    def test_actual_closed_core_receipt_is_compatible_without_operational_preflight(self):
        path = ROOT / '.private/ot0238c-capture-only-20261011/independent-review.json'
        pin = self.ref(path)
        self.assertEqual(pin['bytes'], 11399)
        self.assertEqual(pin['sha256'], '2052cf6c6e65b536a814f3d7d234c3b3973a3cd321d2fa9b62b1df4bad4e07e0')
        sources = {'tools/' + name: runtime.descriptor((ROOT / 'tools' / name).read_bytes())
                   for name in runtime.SOURCE_NAMES}
        with patch.object(capture_runner, 'preflight', side_effect=AssertionError('preflight forbidden')) as preflight, \
                patch.object(runtime, 'launch', side_effect=AssertionError('launch forbidden')) as launched:
            review = select.admit_core_review(pin, sources)
        self.assertEqual(review['outcome'], 'passed_host_only_no_open_findings')
        preflight.assert_not_called(); launched.assert_not_called()

    def test_familiar_cmd_literal_branch_preserves_exact_full_body_and_is_gated(self):
        cmd = ROOT / '.private/ot0238c-neutral-time-20261006/Start Trail check with more time.cmd'
        raw = cmd.read_bytes()
        prefix, full = raw.split(b':retained_rekey\r\n', 1)
        self.assertEqual(len(full), 1932)
        self.assertEqual(hashlib.sha256(full).hexdigest(),
                         '76569b91156a72c2ea670320b9b3118f3caa422e20b28164cebd92456caaef67')
        text = prefix.decode('ascii')
        self.assertIn('if "%~1"=="" goto retained_rekey', text)
        self.assertIn('if "%~1"=="retained_rekey" goto retained_rekey', text)
        self.assertIn('if "%~1"=="capture-only" goto capture_only', text)
        self.assertIn('if not "%~2"=="" goto selection_invalid', text)
        self.assertIn('goto selection_invalid', text)
        self.assertIn('--binding-sha256\',\'UNPREPARED', text)
        self.assertIn(select.BINDING_RELATIVE.replace('/', '\\'), text)
        self.assertIn(hashlib.sha256(Path(select.__file__).read_bytes()).hexdigest(), text)
        command = next(line for line in text.splitlines() if 'python.exe' in line)
        self.assertNotIn('%', command)
        self.assertIn("'--workflow','capture-only'", command)
        self.assertNotIn('issue_capture', command)
        self.assertNotIn('candidate-package', command)

    def test_unprepared_or_missing_fixed_binding_refuses_without_launch(self):
        with patch.object(runtime, 'launch', side_effect=AssertionError('launch forbidden')) as launched:
            for sha in ('UNPREPARED', None, ''):
                with self.subTest(sha=sha), self.assertRaisesRegex(select.SelectionRefused, '^selection_unprepared$'):
                    select.launch_capture(ROOT / select.BINDING_RELATIVE, sha)
            with redirect_stdout(io.StringIO()):
                code = select.main(['--workflow', 'capture-only', '--binding', str(ROOT / select.BINDING_RELATIVE),
                                    '--binding-sha256', 'UNPREPARED'])
            self.assertEqual(code, 1)
            with patch.object(runtime, 'regular', side_effect=runtime.RuntimeErrorFixed('path_invalid')):
                with self.assertRaisesRegex(runtime.RuntimeErrorFixed, '^path_invalid$'):
                    select.launch_capture(ROOT / select.BINDING_RELATIVE, 'a'*64)
            launched.assert_not_called()

    def test_invalid_arguments_never_delegate_or_launch(self):
        choices = ([], ['--workflow', 'unknown'], ['--workflow', 'capture-only'],
            ['--workflow', 'capture-only', '--binding', 'x', '--binding-sha256', 'a'*64, '--plan', 'y'],
            ['--plan', 'x', '--plan-sha256', 'a'*64, '--binding', 'y'],
            ['--work', 'capture-only', '--binding', 'x', '--binding-sha256', 'a'*64])
        with patch.object(owner, 'main') as full, patch.object(select, 'launch_capture') as launched:
            for args in choices:
                with self.subTest(args=args), redirect_stdout(io.StringIO()):
                    self.assertEqual(select.main(args), 1)
            full.assert_not_called(); launched.assert_not_called()

    def test_selector_never_promotes_failed_held_or_categorized_release(self):
        for outcome, category in (('held', None), ('failed', 'capture_incomplete'),
                                  ('released', 'capture_incomplete'), ('released', None)):
            with self.subTest(outcome=outcome, category=category), redirect_stdout(io.StringIO()), \
                    patch.object(select, 'launch_capture', return_value=dict(outcome=outcome, category=category)):
                code = select.main(['--workflow', 'capture-only', '--binding', str(ROOT / select.BINDING_RELATIVE),
                                    '--binding-sha256', 'a'*64])
                self.assertEqual(code, 0 if outcome == 'released' and category is None else 1)

    def test_actual_selected_runtime_and_capture_release_without_candidate_or_write(self):
        f = self.fixture()
        current = {role: dict(sdk.memory) for role, sdk in f.sdks.items()}
        before = {p.name: p.read_bytes() for p in f.evidence.iterdir() if p.is_file()}
        calls, composed = [], []
        def child(argv, **kwargs):
            calls.append(argv)
            self.assertEqual(argv[4], str(f.assembly.root / 'policy/enrollment_candidate_capture_runner.py'))
            self.assertEqual(argv[argv.index('--mode') + 1], 'capture-only')
            result = f.run_capture(mode='capture-only',
                capture_deadline=float(argv[argv.index('--capture-deadline') + 1]),
                cleanup_deadline=float(argv[argv.index('--cleanup-deadline') + 1]))
            composed.append(result)
            return SimpleNamespace(returncode=0 if result.outcome == 'released' else 1,
                stdout=runtime.canonical(dict(schema='OT-ORIGINAL-CAPTURE-LAUNCH-1', mode='capture-only',
                    outcome=result.outcome, category=None if result.outcome == 'released' else 'capture_incomplete')),
                stderr=b'')
        with patch.object(capture_runner, '_await_handoff', side_effect=AssertionError('handoff forbidden')) as handoff, \
                patch.object(runner, 'run', side_effect=AssertionError('candidate forbidden')) as candidate:
            answer = self.launch(f, child)
        self.assertEqual(answer['outcome'], 'released')
        self.assertIsNone(answer['category'])
        self.assertEqual(len(calls), 1)
        result = composed[0]
        self.assertTrue(result.capture.capture_complete)
        self.assertTrue(result.capture.custody_released)
        self.assertTrue(result.lease_released)
        self.assertIsNone(result.candidate)
        self.assertFalse(f.handoff.exists())
        self.assertFalse((f.evidence / capture.ACTIVE).exists())
        self.assertFalse((f.evidence / custody.ACTIVE).exists())
        self.assertFalse(any(row[0] == 'write' for sdk in f.sdks.values() for row in sdk.trace))
        self.assertFalse(any(call[0] in ('backend', 'operator') for call in f.factory_calls))
        self.assertEqual([row[0] for row in f.capture_log if row[1] == 'capture_reset'], ['A', 'B'])
        for role in custody.ROLES:
            self.assertEqual(current[role], f.sdks[role].memory)
            self.assertTrue(result.capture.roles[role]['handles_closed'])
        self.assertEqual(before, {p.name: p.read_bytes() for p in f.evidence.iterdir() if p.name in before})
        forbidden = ('candidate-grant.json', 'candidate-package.json', 'handoff.json')
        self.assertFalse(any((f.evidence / name).exists() for name in forbidden))
        receipt = runtime.decode(result.receipt_path.read_bytes())
        self.assertEqual(receipt['operation'], 'capture-only')
        self.assertIsNone(receipt['candidate_outcome'])
        self.assertEqual(receipt['original_mode'], 'retained_firmware_current_nvs_capture_only_released')
        handoff.assert_not_called(); candidate.assert_not_called()

    def test_changed_refs_review_modes_and_sources_refuse_before_runtime_factories(self):
        # One synthetic provenance setup; each rejected admission is nonconsuming.
        f = self.fixture()
        saved = dict(f.selection)
        def forbidden(*args, **kwargs): raise AssertionError('child forbidden')
        cases = ('binding_hash', 'review', 'core_schema', 'core_outcome', 'core_source_pin', 'workflow', 'selector', 'source', 'assembly',
                 'package', 'request', 'grant', 'full_capture', 'legacy_request', 'handoff', 'grant_operation')
        for fault in cases:
            with self.subTest(fault=fault):
                f.selection = dict(saved)
                restore = []
                if fault == 'binding_hash':
                    self.save_selection(f); f.selection_sha = '0'*64
                elif fault == 'review':
                    self.save_selection(f)
                    review = runtime.decode(f.review_path.read_bytes()); review['findings'] = ['unresolved']
                    f.review_path.write_bytes(runtime.canonical(review))
                    f.selection['review'] = self.ref(f.review_path)
                    f.selection_path.write_bytes(runtime.canonical(f.selection))
                    f.selection_sha = runtime.descriptor(f.selection_path.read_bytes())['sha256']
                elif fault == 'workflow':
                    f.selection['workflow'] = 'retained_rekey'; self.save_selection(f)
                elif fault.startswith('core_'):
                    path = f.core_review_path
                    restore.append((path, path.read_bytes()))
                    review = runtime.decode(path.read_bytes())
                    if fault == 'core_schema': review['schema'] = 'OT-CAPTURE-SELECTION-REVIEW-1'
                    elif fault == 'core_outcome': review['outcome'] = 'no_unresolved_findings'
                    else: review['files'][0]['sha256'] = '0'*64
                    path.write_bytes(runtime.canonical(review))
                    f.selection['core_review'] = self.ref(path); self.save_selection(f)
                elif fault in ('selector', 'source', 'assembly', 'package', 'request', 'grant'):
                    path = (f.disk.worktree / 'tools/enrollment_candidate_runtime.py' if fault == 'source'
                            else Path(f.selection[fault]['path']))
                    restore.append((path, path.read_bytes())); path.write_bytes(b'changed')
                    self.save_selection(f)
                else:
                    package = dict(f.capture_package)
                    if fault == 'full_capture': package['operation'] = 'capture'
                    elif fault == 'handoff': package['handoff'] = str(f.handoff)
                    else:
                        name = 'request' if fault == 'legacy_request' else 'grant'
                        value = runtime.decode(Path(package[name]['path']).read_bytes())
                        value['schema' if name == 'request' else 'operation'] = (
                            capture.RETAINED_REQUESTS[0] if name == 'request' else 'capture-only')
                        path = f.evidence / ('bad-' + name + '.json'); path.write_bytes(runtime.canonical(value))
                        package[name] = self.ref(path); f.selection[name] = package[name]
                    path = f.evidence / 'bad-package.json'; path.write_bytes(runtime.canonical(package))
                    f.selection['package'] = self.ref(path); self.save_selection(f)
                try:
                    with patch.object(runtime, 'launch', side_effect=AssertionError('launch forbidden')) as launched:
                        with self.assertRaises(Exception): self.launch(f, forbidden)
                        launched.assert_not_called()
                    self.assertEqual(f.factory_calls, [])
                    self.assertFalse((f.evidence / ('enrollment-original-used-' + '5'*32)).exists())
                finally:
                    for path, raw in restore: path.write_bytes(raw)


if __name__ == '__main__':
    unittest.main()
