"""Pair-custody fault tests; no device IO."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import enrolled_confirmation_trial as pair


class Backend:
    def __init__(self, role, log):
        self.role, self.log = role, log
        self.memory = {span: bytes([i + 1]) * span[1] for i, span in enumerate(pair.SPANS.values())}
        self.original = dict(self.memory)
        self.fail_write = self.fail_restore = False
        self.bound = None
    def claim(self, binding):
        self.log.append((self.role, 'claim')); return True
    def reverify(self, binding):
        self.log.append((self.role, 'verify')); return True
    def read(self, offset, size):
        self.log.append((self.role, 'read', offset)); return self.memory[(offset, size)]
    def bind_originals(self, raw):
        self.bound = raw; return True
    def bind_recovery_originals(self, raw):
        self.bound = raw; return True
    def write(self, offset, raw):
        restoring = raw == self.original[(offset, len(raw))]
        self.log.append((self.role, 'restore' if restoring else 'write', offset))
        self.memory[(offset, len(raw))] = raw
        if (restoring and self.fail_restore) or (not restoring and self.fail_write):
            raise RuntimeError('injected')
        return True
    def boot_candidate(self):
        self.log.append((self.role, 'boot')); return True
    def reset_original(self):
        assert self.memory == self.original
        self.log.append((self.role, 'reset')); return True


class Bridge:
    def __init__(self, log):
        self.log = log; self.fail = False; self.closes = True; self.idle = True
    def __call__(self):
        self.idle = False; self.log.append(('bridge', 'run'))
        if self.fail: raise RuntimeError('injected')
        return 'status_exchanged'
    def close(self):
        self.log.append(('bridge', 'close')); self.idle = self.closes; return self.closes
    def assert_idle(self): return self.idle


class PairTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name); (self.root / '.private').mkdir()
        self.log = []; self.backends = {r: Backend(r, self.log) for r in pair.ROLES}
        self.candidates = {'A': b'candidate-a', 'B': b'candidate-b'}
        protected = {n: pair.base.descriptor(self.backends['A'].original[s]) for n, s in pair.SPANS.items() if n in ('bootloader', 'partition', 'ota')}
        self.addCleanup(patch.stopall)
        patch.object(pair.base, 'PARTITION_SHA', protected['partition']['sha256']).start()
        patch.object(pair.base, 'OTA_SHA', protected['ota']['sha256']).start()
        patch.object(pair.base, 'inspect_baseline', return_value=True).start()
        patch.object(pair.base, 'safe_reset_marker', return_value=True).start()
        self.nvs_parser = patch.object(pair.base.nvs_parser, '_parse', return_value=({}, {})).start()
        self.request = {'schema': 'OT-ENROLLED-REQUEST-1', 'runtime_sha256': 'c'*64, 'bridge_sha256': 'd'*64, 'case': 'enrolled-status-and-restore', 'roles': {r: {'device_binding': r.lower()*64, 'candidate': pair.base.descriptor(self.candidates[r]), 'protected': protected, 'original_prefix': pair.base.descriptor(self.backends[r].original[pair.SPANS['application']][:589824])} for r in pair.ROLES}}
        self.bridge = Bridge(self.log)
    def grant(self, operation='execute', attempt='1'*32, origin=None):
        g = {'schema': 'OT-ENROLLED-GRANT-1', 'attempt': attempt, 'request_sha256': pair.sha(pair.canonical(self.request)), 'runtime_sha256': self.request['runtime_sha256'], 'operation': operation, 'origin_attempt': origin, 'attempt_count': 1, 'actions': pair.ACTIONS if operation == 'execute' else pair.RECOVERY_ACTIONS, 'issued_utc': 100, 'expires_utc': 200}
        raw = pair.canonical(g); return raw, pair.sha(raw)
    def execute(self):
        raw, pin = self.grant()
        return pair.execute(self.root, self.request, raw, pin, self.candidates, self.backends, self.bridge, utc=lambda: 150)
    def test_success_both_captured_before_first_write_and_close_before_restore(self):
        result = self.execute(); self.assertEqual(result['observation'], 'status_exchanged')
        first_write = next(i for i, e in enumerate(self.log) if e[1] == 'write')
        for role in pair.ROLES:
            for offset, _ in pair.SPANS.values(): self.assertIn((role, 'read', offset), self.log[:first_write])
        self.assertLess(self.log.index(('A', 'boot')), self.log.index(('B', 'write', 65536)))
        self.assertLess(self.log.index(('B', 'boot')), self.log.index(('bridge', 'run')))
        self.assertLess(self.log.index(('bridge', 'close')), self.log.index(('A', 'restore', 65536)))
        self.assertFalse((self.root/'.private'/pair.ACTIVE).exists())
    def test_second_write_failure_restores_both(self):
        self.backends['B'].fail_write = True
        self.assertEqual(self.execute()['observation'], 'trial_failed')
        self.assertNotIn(('bridge', 'run'), self.log)
        for role in pair.ROLES: self.assertIn((role, 'reset'), self.log)
    def test_first_write_failure_releases_second_original_from_rom(self):
        self.backends['A'].fail_write = True; self.execute()
        self.assertNotIn(('B', 'write', 65536), self.log)
        self.assertIn(('B', 'reset'), self.log)
    def test_bridge_exception_closes_and_restores(self):
        self.bridge.fail = True; self.assertEqual(self.execute()['observation'], 'trial_failed')
        self.assertIn(('B', 'reset'), self.log)
    def test_uncertain_passive_close_blocks_all_rom_and_recovery_requires_idle(self):
        self.bridge.closes = False
        with self.assertRaises(pair.Error): self.execute()
        end = self.log.index(('bridge', 'close'))
        self.assertEqual(self.log[end+1:], [])
        raw, pin = self.grant('recover', '2'*32, '1'*32)
        with self.assertRaises(pair.Error): pair.recover(self.root, self.request, raw, pin, self.backends, lambda: False, utc=lambda: 150)
        self.assertTrue((self.root/'.private'/pair.ACTIVE).exists())
    def test_failed_A_restoration_does_not_skip_B_and_fresh_recovery_no_candidate(self):
        self.backends['A'].fail_restore = True
        with self.assertRaises(pair.Error): self.execute()
        self.assertIn(('B', 'reset'), self.log)
        self.backends['A'].fail_restore = False; self.log.clear()
        raw, pin = self.grant('recover', '2'*32, '1'*32)
        self.assertTrue(pair.recover(self.root, self.request, raw, pin, self.backends, lambda: True, utc=lambda: 150)['restored'])
        self.assertFalse(any(e[1] in ('write', 'boot', 'run') for e in self.log))
        self.assertNotIn(('B', 'reset'), self.log)
    def test_original_tamper_holds_custody_but_restores_other_role(self):
        self.backends['A'].fail_restore = True
        with self.assertRaises(pair.Error): self.execute()
        original = self.root/'.private'/('enrolled-confirmation-'+'1'*32+'-A-application.bin')
        original.write_bytes(b'bad'); self.backends['A'].fail_restore = False
        raw, pin = self.grant('recover', '2'*32, '1'*32)
        with self.assertRaises(pair.Error): pair.recover(self.root, self.request, raw, pin, self.backends, lambda: True, utc=lambda: 150)
        self.assertTrue((self.root/'.private'/pair.ACTIVE).exists())
    def test_consumed_execute_grant_cannot_repeat(self):
        self.execute(); self.log.clear()
        with self.assertRaises(Exception): self.execute()
        self.assertEqual(self.log, [])
    def test_duplicate_identity_and_oversize_refused_without_claim(self):
        self.request['roles']['B']['device_binding'] = self.request['roles']['A']['device_binding']
        with self.assertRaises(pair.Error): self.execute()
        self.request['roles']['B']['device_binding'] = 'b'*64
        self.request['roles']['A']['candidate']['bytes'] = 733185
        with self.assertRaises(pair.Error): self.execute()
        self.assertEqual(self.log, [])
    def test_actual_transport_original_binding_and_restoration_composition(self):
        import base64
        import ble_confirmation_trial_transport as transport
        for index, role in enumerate(pair.ROLES):
            memory = self.backends[role].memory
            key = b'k' * 32; identity = '00112233445' + str(index)
            binding = transport.opaque_identity(key, identity)
            self.request['roles'][role]['device_binding'] = binding
            backend = transport.Transport(manifest_path=self.root/'manifest.json', manifest_sha256='c'*64,
                private_root=self.root/'.private', route='COM' + str(index + 1),
                expected_identity=identity, opaque_binding=binding, binding_key=key,
                candidate=self.candidates[role].ljust(733184, b'\xff'))
            def operation(op, memory=memory, role=role, **kw):
                self.log.append((role, op))
                if op == 'read': return {'ok': True, 'data': base64.b64encode(memory[(kw['offset'], kw['size'])]).decode()}
                if op == 'write': memory[(kw['offset'], kw['size'])] = base64.b64decode(kw['data'])
                return {'ok': True}
            backend._operation = operation
            self.backends[role] = backend
        self.assertTrue(self.execute()['restored'])

    def test_expiry_after_captures_restores_originals_without_candidate_write(self):
        ticks = iter([150, 201]); raw, pin = self.grant()
        result = pair.execute(self.root, self.request, raw, pin, self.candidates, self.backends,
                              self.bridge, utc=lambda: next(ticks))
        self.assertEqual(result['observation'], 'trial_failed')
        self.assertFalse(any(e[1] == 'write' for e in self.log))
        for role in pair.ROLES: self.assertIn((role, 'reset'), self.log)

    def test_prewrite_capture_failure_never_writes_candidate(self):
        original_read = self.backends['B'].read
        failed = [False]
        def read(offset, size):
            if not failed[0]:
                failed[0] = True
                raise RuntimeError('read failure')
            return original_read(offset, size)
        self.backends['B'].read = read
        self.assertEqual(self.execute()['observation'], 'trial_failed')
        self.assertFalse(any(e[1] == 'write' for e in self.log))
        for role in pair.ROLES: self.assertIn((role, 'reset'), self.log)

    def test_any_pair_namespace_preexistence_refuses_before_candidate_write(self):
        for namespace in pair.PAIR_NAMESPACES:
            with self.subTest(namespace=namespace):
                self.nvs_parser.return_value = ({1: namespace}, {})
                with self.assertRaises(pair.Error): pair.inspect_baseline(self.request['roles']['A'], {'nvs': b''})
        self.nvs_parser.return_value = ({1: b'ot230_role'}, {})
        self.assertEqual(self.execute()['observation'], 'trial_failed')
        self.assertFalse(any(e[1] == 'write' for e in self.log))
        self.assertIn(('A', 'reset'), self.log)

    def test_changed_candidate_refuses_before_device_claim(self):
        self.candidates['A'] += b'changed'
        with self.assertRaises(pair.Error): self.execute()
        self.assertEqual(self.log, [])

    def test_caller_mapping_mutation_cannot_replace_admitted_pair(self):
        original_backends = dict(self.backends)
        original_candidates = dict(self.candidates)
        original_request = pair.decode(pair.canonical(self.request))
        def claim(binding):
            self.assertEqual(binding, original_request['roles']['A']['device_binding'])
            self.request['roles']['B']['device_binding'] = 'e' * 64
            self.request['roles']['B']['candidate'] = pair.base.descriptor(b'changed')
            self.candidates['B'] = b'changed'
            self.backends['B'] = Backend('replacement', self.log)
            return True
        original_backends['A'].claim = claim
        write = original_backends['B'].write
        candidate_writes = []
        def checked_write(offset, raw):
            if raw != original_backends['B'].original[(offset, len(raw))]:
                candidate_writes.append(raw)
            return write(offset, raw)
        original_backends['B'].write = checked_write
        self.assertTrue(self.execute()['restored'])
        self.assertEqual(candidate_writes, [original_candidates['B'].ljust(733184, b'\xff')])
        self.assertIn(('B', 'boot'), self.log)
        self.assertIn(('B', 'reset'), self.log)
        self.assertFalse(any(e[0] == 'replacement' for e in self.log))
        state = pair.decode((self.root/'.private'/('enrolled-confirmation-'+'1'*32+'.json')).read_bytes())
        self.assertEqual(state['request'], original_request)
        self.assertEqual(original_backends['B'].memory, original_backends['B'].original)

    def test_recovery_snapshots_before_idle_callback(self):
        self.backends['A'].fail_restore = True
        with self.assertRaises(pair.Error): self.execute()
        admitted = dict(self.backends); admitted['A'].fail_restore = False
        raw, pin = self.grant('recover', '2'*32, '1'*32)
        def idle():
            self.request['roles']['A']['device_binding'] = 'e' * 64
            self.backends['A'] = Backend('replacement', self.log)
            return True
        self.assertTrue(pair.recover(self.root, self.request, raw, pin, self.backends, idle, utc=lambda: 150)['restored'])
        self.assertFalse(any(e[0] == 'replacement' for e in self.log))

    def test_expired_authority_never_claims(self):
        raw, pin = self.grant()
        with self.assertRaises(pair.Error): pair.execute(self.root, self.request, raw, pin, self.candidates, self.backends, self.bridge, utc=lambda: 201)
        self.assertEqual(self.log, [])

    def test_radio_and_usb_grants_are_not_interchangeable(self):
        for case in ('enrolled-status-and-restore', 'enrolled-status-radio-and-restore'):
            self.request['case'] = case
            raw, _ = self.grant(); value = pair.decode(raw)
            value['actions'] = pair.actions_for(self.request, 'execute')
            good = pair.canonical(value)
            pair.validate_grant(self.request, good, pair.sha(good), 'execute', lambda: 150)
            value['actions'] = pair.actions_for({'case': 'enrolled-status-radio-and-restore' if case == 'enrolled-status-and-restore' else 'enrolled-status-and-restore'}, 'execute')
            wrong = pair.canonical(value)
            with self.assertRaises(pair.Error):
                pair.validate_grant(self.request, wrong, pair.sha(wrong), 'execute', lambda: 150)
        self.assertEqual(pair.actions_for(self.request, 'recover'), pair.RECOVERY_ACTIONS)

    def test_radio_request_uses_same_restoration_scope(self):
        self.request['case'] = 'enrolled-status-radio-and-restore'
        raw,_ = self.grant(); value = pair.decode(raw)
        value['actions'] = pair.actions_for(self.request, 'execute'); raw = pair.canonical(value)
        result = pair.execute(self.root, self.request, raw, pair.sha(raw), self.candidates,
                              self.backends, self.bridge, utc=lambda: 150)
        self.assertTrue(result['restored'])
        for role in pair.ROLES: self.assertIn((role, 'reset'), self.log)


class DerivationTests(unittest.TestCase):
    def test_original_restore_engine_is_mechanically_preserved(self):
        import ast
        import inspect
        import pair_confirmation_trial as previous
        def functions(module):
            return {node.name: node for node in ast.parse(inspect.getsource(module)).body
                    if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))}
        before, after = functions(previous), functions(pair)
        for name in ('path', 'originals', 'capture', 'restore_role', 'restore_all'):
            self.assertEqual(ast.dump(before[name]), ast.dump(after[name]), name)
        # Execute/recovery differ only in their new journal/one-use filename
        # protocol labels, not in mutation, protected readback, or reset ordering.
        class Labels(ast.NodeTransformer):
            def visit_Constant(self, node):
                if isinstance(node.value, str):
                    node.value=node.value.replace('OT-ENROLLED-', 'OT-PAIR-').replace('enrolled-confirmation', 'pair-confirmation')
                return node
        for name in ('execute', 'recover'):
            self.assertEqual(ast.dump(before[name]), ast.dump(Labels().visit(after[name])), name)

    def test_enrolled_namespace_and_case_are_mandatory(self):
        self.assertIn(b'ot240_eval', pair.PAIR_NAMESPACES)
        self.assertIn('status_exchanged', pair.OUTCOMES)
        self.assertNotIn('local_confirmed', pair.OUTCOMES)
        with self.assertRaises(pair.Error):
            pair.actions_for({'case':'radio-confirm-and-restore'}, 'execute')
        self.assertNotEqual(pair.actions_for({'case':'enrolled-status-and-restore'}, 'execute'),
                            pair.actions_for({'case':'enrolled-status-radio-and-restore'}, 'execute'))


if __name__ == '__main__': unittest.main()
