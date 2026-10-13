"""Focused successor template source admission and held-attempt continuity."""
import ast
import io
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import enrollment_candidate_interactive_launch as visible
import enrollment_candidate_runtime as runtime
import enrollment_candidate_visible_session as session
from enrollment_candidate_interactive_launch_tests import Fixture

TEMPLATE = ROOT / '.private/ot0238c-visible-launch-20261005/prestage/session.template.py'


class SessionTests(unittest.TestCase):
    def fixture(self):
        f = Fixture(); self.addCleanup(f.cleanup); f.ready()
        ready_ref = visible.reference(f.readiness / 'readiness.json')
        authorization_ref = visible.save_once(f.output / 'owner-authorization.json',
            dict(schema='OT0238C-V13-OWNER-AUTHORIZATION-1', approved=True, source='fresh_human_reply',
                 visible_binding_sha256='c' * 64, session_nonce='a' * 32, visible_readiness=ready_ref))
        request_ref = visible.save_once(f.output / 'request.json', {'synthetic': True})
        issued = int(f.clock.utc())
        grant = dict(attempt='d' * 32, issued_utc=issued, capture_expires_utc=issued + 2100,
                     cleanup_expires_utc=issued + 3600, request_sha256='e' * 64)
        grant_ref = visible.save_once(f.output / 'capture-grant.json', grant)
        package = dict(schema='OT-ORIGINAL-CAPTURE-RUNNER-1', operation='capture', evidence_root=str(f.output),
                       assembly=f.bound['assembly'], grant=grant_ref, request=request_ref)
        package_ref = visible.save_once(f.output / 'capture-package.json', package)
        intent = dict(schema='OT0238C-LAUNCH-INTENT-1', utc='synthetic', package=package_ref, one_attempt=True,
            visible_readiness=ready_ref, visible_binding_sha256='c' * 64, capture_grant=grant_ref,
            started_utc=f.clock.utc(), started_monotonic=f.clock.mono(), owner_authorization=authorization_ref)
        visible.save_once(f.output / 'launch-intent.json', intent)
        visible.save_once(f.output / 'enrollment-original-active.json', {'attempt': grant['attempt'], 'request_sha256': grant['request_sha256']})
        visible.save_once(f.output / ('enrollment-original-' + grant['attempt'] + '.json'),
                          {'attempt': grant['attempt'], 'phase': 'held', 'capture_complete': True})
        f.grant = grant
        return f

    def continuity(self, f):
        # Actual grant schema is covered by maintained capture tests. This
        # fixture never admits or issues real authority; inject exact records.
        with patch.object(visible, 'ROOT', f.root.parent):
            return visible.require_continuity('synthetic', 'c' * 64, admission=f.scope,
                monotonic=f.clock.mono, utc=f.clock.utc, binding_admitter=f.admit,
                grant_validator=lambda request, raw, sha, mode, utc: runtime.decode(raw))

    def test_continuity_outlives900_but_never_renews_original2100(self):
        f = self.fixture(); f.clock.value += 1500
        with self.assertRaises(visible.AdmissionError): f.require()
        self.assertEqual(f.bound, self.continuity(f))
        f.clock.value += 600
        with self.assertRaises(visible.AdmissionError): self.continuity(f)

    def test_changed_grant_new_attempt_absent_held_marker_desktop_and_clock_refuse(self):
        for change in ('grant', 'marker', 'desktop', 'clock', 'authorization'):
            f = self.fixture(); f.clock.value += 1500
            if change == 'grant': (f.output / 'capture-grant.json').write_bytes(b'changed')
            if change == 'marker': (f.output / 'enrollment-original-active.json').unlink()
            if change == 'desktop': f.scope = lambda: dict(__import__('enrollment_candidate_interactive_launch_tests').SCOPE, session_id=2)
            if change == 'clock': f.clock.value = 1
            if change == 'authorization': (f.output / 'owner-authorization.json').write_bytes(b'changed')
            with self.assertRaises(Exception): self.continuity(f)

    def test_saved_status_inspection_does_not_require_fresh_ui_or_held_marker(self):
        called = []
        class Module:
            def status(self): called.append('saved_status'); return {'saved_only': True}
        with patch.object(visible, 'require_status', return_value={'session': {}}):
            result = session.dispatch('binding', 'c' * 64, 'status', session_loader=lambda bound: Module())
        self.assertEqual({'saved_only': True}, result)
        self.assertEqual(['saved_status'], called)

    def test_swapped_session_nonce_wrong_intent_binding_or_extended_ceiling_refuse(self):
        for field in ('nonce', 'binding', 'extension'):
            f = self.fixture(); f.clock.value += 1500
            if field == 'nonce': f.bound['session_nonce'] = 'b' * 32
            elif field == 'binding':
                p = f.output / 'launch-intent.json'; value = runtime.decode(p.read_bytes())
                value['visible_binding_sha256'] = 'b' * 64
                p.write_bytes(runtime.canonical(value))
            else:
                f.grant['capture_expires_utc'] += 1
                p = f.output / 'capture-grant.json'; p.write_bytes(runtime.canonical(f.grant))
                intent_path = f.output / 'launch-intent.json'; intent = runtime.decode(intent_path.read_bytes())
                intent['capture_grant'] = visible.reference(p)
                package_path = f.output / 'capture-package.json'; package = runtime.decode(package_path.read_bytes())
                package['grant'] = intent['capture_grant']; package_path.write_bytes(runtime.canonical(package))
                intent['package'] = visible.reference(package_path); intent_path.write_bytes(runtime.canonical(intent))
            with self.assertRaises(Exception): self.continuity(f)

    def test_template_readiness_precedes_authorization_inputs_and_every_device_step(self):
        text = TEMPLATE.read_text(encoding='utf8'); tree = ast.parse(text)
        funcs = {n.name: n for n in tree.body if isinstance(n, ast.FunctionDef)}
        calls = [n for n in funcs['require_owner_authorization'].body if isinstance(n, ast.Expr) and isinstance(n.value, ast.Call)]
        self.assertEqual('require_visible_readiness', calls[0].value.func.id)
        for name in ('baseline', 'route', 'issue_capture', 'launch', 'prepare_handoff', 'publish_handoff'):
            self.assertEqual('require_owner_authorization', funcs[name].body[0].value.func.id)
        self.assertIn('visible.launch(VISIBLE_BINDING, VISIBLE_BINDING_SHA256', text)
        self.assertNotIn('result = runtime.launch(', text)
        self.assertIn('VISIBLE_CONTINUATION or', text)
        self.assertIn("'visible_readiness':ref(", text)
        self.assertIn('Use the reviewed enrollment_candidate_visible_session.py', text)

    def test_cleanup_direct_dispatch_all_actions_refuses_before_native_or_session_loader(self):
        from enrollment_candidate_interactive_launch_tests import RecoveryBindingFixture
        for action in session.ACTIONS:
            f=RecoveryBindingFixture();self.addCleanup(f.close)
            kwargs={}
            if action=='attest':kwargs['owner_ref']={'synthetic':'owner'}
            if action=='publish_handoff':kwargs['handoff_review_ref']={'synthetic':'review'}
            with self.subTest(action=action),patch.object(visible,'native_admission',
                    side_effect=AssertionError('native UI')),self.assertRaisesRegex(
                    visible.AdmissionError,'^recovery_baseline_unresolved$'):
                session.dispatch(f.bound['path'],f.bound['sha256'],action,
                    session_loader=lambda *args:(_ for _ in ()).throw(AssertionError('session import')),**kwargs)
            self.assertEqual(list(f.output.iterdir()),[]);self.assertEqual(list(f.ready.iterdir()),[])

    def test_cleanup_cli_preserves_fixed_hold_category_without_authority(self):
        from enrollment_candidate_interactive_launch_tests import RecoveryBindingFixture
        f=RecoveryBindingFixture();self.addCleanup(f.close);stream=io.StringIO()
        with patch.object(sys,'stdout',stream):
            result=session.main(['--binding',f.bound['path'],'--binding-sha256',f.bound['sha256'],'--action','route'])
        self.assertEqual(result,1)
        row=runtime.decode(stream.getvalue().encode())
        self.assertEqual(row['category'],'recovery_baseline_unresolved')
        self.assertEqual(list(f.output.iterdir()),[]);self.assertEqual(list(f.ready.iterdir()),[])


if __name__ == '__main__': unittest.main()
