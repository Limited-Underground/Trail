"""Host-only OT-218 subprocess/ROM seams; never opens a serial device."""
import ast
import base64
import contextlib
import io
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import ble_confirmation_trial_transport as t


class Tests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.private = self.root / '.private'
        self.private.mkdir()
        self.calls, self.verifications = [], []
        self.memory = {span: bytes([i + 1]) * span[1] for i, span in enumerate(sorted(t.SPANS))}
        self.original = dict(self.memory)
        self.candidate = b'c' * 733184
        self.key = b'k' * 32
        self.binding = t.opaque_identity(self.key, '010203040506')
        self.failure = None
        self.backend = t.Transport(manifest_path=self.private / 'manifest.json', manifest_sha256='a' * 64,
            private_root=self.private, route='COM7', expected_identity=bytes(range(1, 7)).hex(':'),
            opaque_binding=self.binding, binding_key=self.key, candidate=self.candidate,
            subprocess_run=self.run_child, manifest_verifier=self.verify)

    def tearDown(self):
        self.temp.cleanup()

    def verify(self, path, sha):
        self.verifications.append((path, sha))
        return {'root': str(self.private / 'capsule'), 'worktree': str(self.root)}

    def run_child(self, argv, **kwargs):
        self.calls.append((argv, kwargs))
        if self.failure == 'exception':
            raise subprocess.TimeoutExpired('PRIVATE COM7', 240)
        q = json.loads(kwargs['input'])
        reply = {'ok': True}
        if q['operation'] in ('restart_capture', 'passive_capture'):
            reply['startup_diagnostics'] = self.startup_reply
        if q['operation'] == 'read':
            reply['data'] = base64.b64encode(self.memory[q['offset'], q['size']]).decode()
        if q['operation'] == 'write':
            self.memory[q['offset'], q['size']] = base64.b64decode(q['data'])
        if self.failure == 'false': reply = {'ok': False, 'private': 'SECRET'}
        if self.failure == 'extra': reply['private'] = 'SECRET'
        if self.failure == 'short': reply['data'] = 'YQ=='
        return SimpleNamespace(returncode=1 if self.failure == 'exit' else 0,
            stdout=json.dumps(reply).encode(), stderr=b'PRIVATE COM7')

    def capture(self):
        self.backend.claim(self.binding)
        values = {s: self.backend.read(*s) for s in t.SPANS}
        self.backend.bind_originals(values)
        return values

    def diagnostic_backend(self, enabled=True, recovery=False, confirmation=False):
        self.startup_reply = {'schema': 'OT225-STARTUP-1', 'capture_started_unix_ms': 1789356233000, 'status': 'window_complete',
            'markers': [{'category': 'runtime_started', 'received_ms': 0}], 'early_output_may_be_missing': True,
            'bytes_read': 40, 'elapsed_ms': 15000}
        self.backend = t.Transport(manifest_path=self.private / 'manifest.json', manifest_sha256='a' * 64,
            private_root=self.private, route='COM7', expected_identity='010203040506',
            opaque_binding=self.binding, binding_key=self.key, candidate=self.candidate,
            subprocess_run=self.run_child, manifest_verifier=self.verify,
            startup_diagnostics=enabled, recovery_only=recovery, confirmation_diagnostics=confirmation)

    def test_post_confirmation_capture_one_use_without_rom_operations(self):
        self.diagnostic_backend(confirmation=True)
        with self.assertRaises(t.TransportError): self.backend.capture_after_confirmation()
        self.capture()
        self.backend.write(0x10000, self.candidate)
        self.backend.boot_candidate()
        before = len(self.calls)
        self.assertEqual(self.backend.capture_after_confirmation(), self.startup_reply)
        self.assertEqual([json.loads(k['input'])['operation'] for _, k in self.calls[before:]], ['passive_capture'])
        with self.assertRaises(t.TransportError): self.backend.capture_after_confirmation()

    def test_post_confirmation_capture_refused_after_rom_reentry_or_wrong_mode(self):
        for enabled, recovery in ((False, False), (True, True)):
            with self.assertRaises(t.TransportError): self.diagnostic_backend(enabled, recovery, True)
        self.diagnostic_backend(confirmation=True)
        self.capture()
        self.backend.write(0x10000, self.candidate)
        self.backend.boot_candidate()
        self.backend.reverify(self.binding)
        with self.assertRaises(t.TransportError): self.backend.capture_after_confirmation()

    def test_actual_worker_passive_branch_exits_before_rom_functions(self):
        tree = ast.parse(t.WORKER)
        body = next(n for n in tree.body if isinstance(n, ast.Try)).body
        branch = next(n for n in body if isinstance(n, ast.If) and 'passive_capture' in ast.unparse(n.test))
        self.assertLess(body.index(branch), next(i for i, n in enumerate(body) if isinstance(n, ast.FunctionDef) and n.name == 'run'))
        captured = []
        class Finished(Exception): pass
        ns = dict(q={'operation': 'passive_capture'}, json=json, serial=object(), comports=object(),
                  expected='010203040506', ProbeFinished=Finished, print=lambda value: captured.append(json.loads(value)),
                  capture_startup=lambda *args: {'passive': True})
        with self.assertRaises(Finished):
            exec(compile(ast.Module(body=[branch], type_ignores=[]), '<actual-passive-branch>', 'exec'), ns)
        self.assertEqual(captured, [{'ok': True, 'startup_diagnostics': {'passive': True}}])

    def test_opt_in_capture_and_original_reset_separate(self):
        self.diagnostic_backend()
        self.capture()
        self.backend.write(0x10000, self.candidate)
        self.assertTrue(self.backend.boot_candidate())
        self.assertEqual(self.backend.startup_diagnostics, self.startup_reply)
        self.assertEqual(json.loads(self.calls[-1][1]['input'])['operation'], 'restart_capture')
        self.backend.write(0x10000, self.original[0x10000, 733184])
        self.assertTrue(self.backend.reset_original())
        self.assertEqual(json.loads(self.calls[-1][1]['input'])['operation'], 'restart')
        self.assertEqual(sum(json.loads(kw['input'])['operation'] == 'restart_capture'
                             for _, kw in self.calls), 1)

    def test_capture_default_off_and_recovery_opt_in_refused(self):
        self.capture()
        self.backend.write(0x10000, self.candidate)
        self.backend.boot_candidate()
        self.assertEqual(json.loads(self.calls[-1][1]['input'])['operation'], 'restart')
        self.assertIsNone(self.backend.startup_diagnostics)
        for enabled, recovery in ((True, True), (1, False), ('yes', False)):
            with self.assertRaises(t.TransportError):
                self.diagnostic_backend(enabled, recovery)

    def test_malformed_subprocess_diagnostics_refused(self):
        self.diagnostic_backend()
        self.capture()
        self.backend.write(0x10000, self.candidate)
        self.startup_reply['raw_log'] = 'PRIVATE SECRET'
        with self.assertRaisesRegex(t.TransportError, '^ble_trial_transport_refused$'):
            self.backend.boot_candidate()
        self.assertIsNone(self.backend.startup_diagnostics)
        self.backend.write(0x10000, self.original[0x10000, 733184])
        self.assertTrue(self.backend.reset_original())
        self.assertEqual(json.loads(self.calls[-1][1]['input'])['operation'], 'restart')

    def test_construction_inert_and_private_repr(self):
        self.assertFalse(self.calls)
        self.assertNotIn('COM7', repr(self.backend))
        self.assertNotIn('010203', repr(self.backend))

    def test_subprocess_exact_boundary_and_stdin_only_identity(self):
        self.backend.claim(self.binding)
        args, kw = self.calls[0]
        self.assertEqual(args, [str(self.private / 'capsule/python.exe'), '-I', '-S', '-B', '-c', t.WORKER])
        self.assertNotIn('COM7', repr(args))
        self.assertNotIn('010203040506', repr(args))
        self.assertEqual(kw['timeout'], 240)
        self.assertFalse(kw['check'])
        self.assertEqual(json.loads(kw['input'])['route'], 'COM7')
        self.assertEqual(kw['env']['ESPTOOL_CFGFILE'], str(self.private / 'capsule/esptool.cfg'))

    def test_binding_and_single_claim(self):
        with self.assertRaises(t.TransportError): self.backend.claim('b' * 64)
        self.assertFalse(self.calls)
        self.backend.claim(self.binding)
        with self.assertRaises(t.TransportError): self.backend.claim(self.binding)

    def test_device_free_runtime_probe(self):
        result = t.probe_runtime(manifest_path=self.private / 'manifest.json', manifest_sha256='a' * 64,
            private_root=self.private, subprocess_run=self.run_child, manifest_verifier=self.verify)
        self.assertEqual(result, {'status': 'pass', 'hardware_access': False, 'serial_enumeration': False})
        payload = json.loads(self.calls[-1][1]['input'])
        self.assertEqual(payload['operation'], 'probe')
        self.assertIsNone(payload['route'])
        self.assertIsNone(payload['identity'])
        tree = ast.parse(t.WORKER)
        body = next(n for n in tree.body if isinstance(n, ast.Try)).body
        probe = next(i for i,n in enumerate(body) if isinstance(n,ast.If) and "'probe'" in ast.unparse(n.test))
        route = next(i for i,n in enumerate(body) if isinstance(n,ast.Assign) and any(isinstance(x,ast.Name) and x.id=='route' for x in n.targets))
        self.assertLess(probe, route)

    def test_all_five_full_actual_spans_capture(self):
        values = self.capture()
        self.assertEqual(values, self.original)
        self.assertEqual(len(values[0x10000, 733184]), 733184)

    def test_reject_old_span_and_protected_writes(self):
        self.capture()
        for span in ((0x10000, 589824), (0, 1), (True, 32768)):
            with self.assertRaises(t.TransportError): self.backend.read(*span)
        for span in ((0, 32768), (0x8000, 4096), (0x9000, 8192)):
            with self.assertRaises(t.TransportError): self.backend.write(span[0], self.original[span])

    def test_writes_require_capture_and_exact_bytes(self):
        self.backend.claim(self.binding)
        with self.assertRaises(t.TransportError): self.backend.write(0x10000, self.candidate)
        values = {s: self.backend.read(*s) for s in t.SPANS}
        values[0x10000, 733184] = b'\xff' * 733184
        with self.assertRaises(t.TransportError): self.backend.bind_originals(values)
        self.backend.bind_originals(self.original)
        with self.assertRaises(t.TransportError): self.backend.write(0xd000, b'\xff' * 12288)
        self.assertTrue(self.backend.bind_originals(self.original))

    def test_recovery_only_cannot_write_or_boot_candidate(self):
        self.backend.claim(self.binding)
        named = {name: self.original[span] for name, span in t.NAMED_SPANS.items()}
        self.backend.bind_recovery_originals(named)
        self.backend.bind_recovery_originals(named)
        with self.assertRaises(t.TransportError): self.backend.write(0x10000, self.candidate)
        with self.assertRaises(t.TransportError): self.backend.boot_candidate()
        self.backend.write(0x10000, named['application'])
        self.backend.reset_original()

    def test_constructed_recovery_only_restores_without_candidate_artifact(self):
        backend = t.Transport(manifest_path=self.private / 'manifest.json', manifest_sha256='a' * 64,
            private_root=self.private, route='COM7', expected_identity='010203040506',
            opaque_binding=self.binding, binding_key=self.key, candidate=b'\xff' * 733184,
            recovery_only=True, subprocess_run=self.run_child, manifest_verifier=self.verify)
        backend.claim(self.binding)
        with self.assertRaises(t.TransportError): backend.boot_candidate()
        backend.bind_recovery_originals(self.original)
        with self.assertRaises(t.TransportError): backend.write(0x10000, b'\xff' * 733184)
        with self.assertRaises(t.TransportError): backend.boot_candidate()
        backend.write(0x10000, self.original[0x10000, 733184])
        self.assertTrue(backend.reset_original())

    def test_original_name_mapping_is_strict(self):
        self.backend.claim(self.binding)
        named = {name: self.original[span] for name, span in t.NAMED_SPANS.items()}
        named['partition'], named['nvs'] = named['nvs'], named['partition']
        with self.assertRaises(t.TransportError): self.backend.bind_recovery_originals(named)

    def test_candidate_restart_requires_full_readback(self):
        self.capture()
        with self.assertRaises(t.TransportError): self.backend.boot_candidate()
        self.backend.write(0x10000, self.candidate)
        self.backend.boot_candidate()
        ops = [json.loads(k['input'])['operation'] for _, k in self.calls]
        self.assertEqual(ops[-2:], ['read', 'restart'])

    def test_original_restart_rechecks_every_span(self):
        self.capture()
        self.backend.write(0x10000, self.candidate)
        with self.assertRaises(t.TransportError): self.backend.reset_original()
        self.backend.write(0x10000, self.original[0x10000, 733184])
        self.backend.reset_original()
        ops = [json.loads(k['input'])['operation'] for _, k in self.calls]
        self.assertEqual(ops[-6:], ['read'] * 5 + ['restart'])

    def test_uncertain_write_fences_and_diagnostics_redaction(self):
        self.capture()
        self.failure = 'exception'
        with self.assertRaises(t.TransportError) as err: self.backend.write(0x10000, self.candidate)
        self.assertEqual(str(err.exception), 'ble_trial_transport_refused')
        self.assertIsNone(err.exception.__cause__)
        self.assertTrue(self.backend._mutated)

    def test_untrusted_child_failures(self):
        self.backend.claim(self.binding)
        for failure in ('exit', 'false', 'extra', 'short'):
            self.failure = failure
            with self.assertRaises(t.TransportError): self.backend.read(0xd000, 12288)

    def test_manifest_rechecked_each_child_hold_never_restarts(self):
        self.backend.claim(self.binding)
        self.backend.hold()
        self.backend.reverify(self.binding)
        self.assertEqual(len(self.verifications), 3)
        self.assertTrue(all(json.loads(k['input'])['operation'] == 'verify' for _, k in self.calls))

    def worker_seam(self, ports=None):
        # Execute the actual nested worker functions, not a second argv model.
        tree = ast.parse(t.WORKER)
        body = [n for n in tree.body if isinstance(n, ast.FunctionDef)]
        body += [n for n in next(n for n in tree.body if isinstance(n, ast.Try)).body
                 if isinstance(n, ast.FunctionDef)]
        calls = []
        p = SimpleNamespace(device='COM7', vid=0x303a, pid=0x1001, serial_number='010203040506')
        tool = SimpleNamespace(main=lambda argv: calls.append(argv))
        ns = dict(re=re, io=io, contextlib=contextlib, esptool=tool, time=time,
                  route='COM7', expected='010203040506', comports=lambda: [p] if ports is None else ports)
        exec(compile(ast.Module(body=body, type_ignores=[]), '<worker-functions>', 'exec'), ns)
        return ns, calls

    def test_actual_worker_exact_no_stub_argv(self):
        ns, calls = self.worker_seam()
        for operation in (['read-mac'], ['flash-id'], ['read-flash', '0x10000', '733184', 'region.bin'],
                          ['write-flash', '--flash-size', '16MB', '0x10000', 'region.bin']):
            ns['run'](operation)
            self.assertEqual(calls[-1], t.rom_argv('COM7', operation))
        ns['run'](['run'], True)
        self.assertEqual(calls[-1], t.rom_argv('COM7', ['run'], True))

    def test_actual_worker_run_retains_accepted_hard_reset_release(self):
        ns, calls = self.worker_seam()
        events = []
        def passive():
            events.append('passive_identity')
        def owned_esptool_main(argv):
            # Pinned esptool owns/closes the port when no external esp is supplied.
            # Exercise that contract without importing its hardware implementation.
            events.append('open_owned_port')
            try:
                calls.append(argv)
                self.assertEqual(argv[-1], 'run')
                self.assertIn('--no-stub', argv)
                self.assertEqual(argv[argv.index('--after') + 1], 'hard-reset')
                self.assertEqual(argv.count('hard-reset'), 1)
                events.append('rom_run_and_explicit_release')
            finally:
                events.append('close_owned_port')
        ns['passive'] = passive
        ns['esptool'] = SimpleNamespace(main=owned_esptool_main)
        ns['run'](['run'], True)
        self.assertEqual(events, ['passive_identity', 'open_owned_port',
                                  'rom_run_and_explicit_release', 'close_owned_port'])
        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0], t.rom_argv('COM7', ['run'], True))
        self.assertEqual(calls[0][calls[0].index('--after') + 1], 'hard-reset')

    def test_actual_worker_reenumerates_and_refuses_identity_swap(self):
        wrong = SimpleNamespace(device='COM7', vid=0x303a, pid=0x1001, serial_number='111213141516')
        ns, calls = self.worker_seam([wrong])
        with self.assertRaises(ValueError): ns['run'](['run'], True)
        self.assertFalse(calls)

    def test_actual_worker_duplicate_identity_refused(self):
        ports = [SimpleNamespace(device=r, vid=0x303a, pid=0x1001, serial_number='010203040506') for r in ('COM7','COM8')]
        ns, calls = self.worker_seam(ports)
        with self.assertRaises(ValueError): ns['run'](['run'], True)
        self.assertFalse(calls)

    def engine_fixture(self, confirmation=False):
        spec = importlib.util.spec_from_file_location('ot218_engine_fixture', Path(__file__).with_name('ble_confirmation_trial_tests.py'))
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        case = module.EngineTests()
        case.setUpClass()
        case.setUp()
        self.addCleanup(case.tearDown)
        self.root = case.root
        self.private = self.root / '.private'
        self.memory = {t.NAMED_SPANS[name]: raw for name, raw in case.original.items()}
        self.startup_reply = {'schema': 'OT225-STARTUP-1', 'capture_started_unix_ms': 1789356233000,
            'status': 'window_complete', 'markers': [], 'early_output_may_be_missing': True,
            'bytes_read': 0, 'elapsed_ms': 15000}
        case.request['device_binding'] = self.binding
        def fresh_transport():
            return t.Transport(manifest_path=self.private / 'manifest.json', manifest_sha256='a' * 64,
                private_root=self.private, route='COM7', expected_identity='010203040506',
                opaque_binding=self.binding, binding_key=self.key,
                candidate=case.candidate.ljust(733184, b'\xff'),
                subprocess_run=self.run_child, manifest_verifier=self.verify,
                startup_diagnostics=confirmation, confirmation_diagnostics=confirmation)
        case.backend = fresh_transport()
        return case, fresh_transport

    def test_real_engine_actual_transport_confirm_restore_integration(self):
        case, _ = self.engine_fixture()
        result = case.execute()
        self.assertTrue(result['restored'])
        self.assertEqual(result['observation'], 'local_confirmed')
        self.assertEqual(self.memory, {t.NAMED_SPANS[name]: raw for name, raw in case.original.items()})
        self.assertEqual(case.observations, 1)
        self.assertFalse(case.held())

    def test_real_operator_observation_post_capture_precedes_restore(self):
        import run_ble_confirmation_trial as operator
        case, _ = self.engine_fixture(confirmation=True)
        lines = operator.Lines(io.StringIO(''.join(json.dumps({'token': 't', 'event': e})+'\n'
            for e in ('ready', 'offer', 'local_confirmed'))))
        case.observe = lambda: operator.trial_observation(lines, case.backend, confirmation_diagnostics=True)
        with patch.object(operator.secrets, 'token_hex', return_value='t'), contextlib.redirect_stdout(io.StringIO()):
            result = case.execute()
        self.assertTrue(result['restored'])
        self.assertEqual(result['observation'], 'local_confirmed')
        ops = [json.loads(k['input'])['operation'] for _, k in self.calls]
        self.assertEqual(ops.count('passive_capture'), 1)
        capture_index = ops.index('passive_capture')
        self.assertEqual(ops[capture_index-1], 'restart_capture')
        self.assertIn('write', ops[capture_index+1:])
        self.assertFalse(case.held())

    def test_post_capture_failure_still_restores_and_never_retries(self):
        case, _ = self.engine_fixture(confirmation=True)
        original = case.backend._run
        def fail_passive(argv, **kwargs):
            if json.loads(kwargs['input'])['operation'] == 'passive_capture':
                raise OSError('capture unavailable')
            return original(argv, **kwargs)
        case.backend._run = fail_passive
        case.observe = case.backend.capture_after_confirmation
        result = case.execute()
        self.assertTrue(result['restored'])
        self.assertEqual(result['observation'], 'trial_failed')
        self.assertEqual(self.memory, {t.NAMED_SPANS[n]: raw for n, raw in case.original.items()})
        self.assertFalse(case.held())

    def test_real_engine_fresh_transport_recovery_integration(self):
        case, fresh = self.engine_fixture()
        original_run = case.backend._run
        writes = 0
        def fail_restore(argv, **kw):
            nonlocal writes
            result = original_run(argv, **kw)
            if json.loads(kw['input'])['operation'] == 'write':
                writes += 1
                if writes == 2: raise OSError('private uncertain restore')
            return result
        case.backend._run = fail_restore
        import ble_confirmation_trial as trial
        with self.assertRaises(trial.TrialError): case.execute()
        self.assertTrue(case.held())
        case.backend = fresh()
        self.assertTrue(case.recover()['restored'])
        self.assertTrue(case.backend._recovery_only)
        self.assertEqual(case.observations, 1)
        self.assertFalse(case.held())


if __name__ == '__main__':
    unittest.main()
