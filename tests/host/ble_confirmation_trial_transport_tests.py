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
from types import SimpleNamespace
import unittest

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
        ns = dict(re=re, io=io, contextlib=contextlib, esptool=tool,
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

    def engine_fixture(self):
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
        case.request['device_binding'] = self.binding
        def fresh_transport():
            return t.Transport(manifest_path=self.private / 'manifest.json', manifest_sha256='a' * 64,
                private_root=self.private, route='COM7', expected_identity='010203040506',
                opaque_binding=self.binding, binding_key=self.key,
                candidate=case.candidate.ljust(733184, b'\xff'),
                subprocess_run=self.run_child, manifest_verifier=self.verify)
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
