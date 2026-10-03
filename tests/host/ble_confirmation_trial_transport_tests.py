"""Host-only OT-218 subprocess/ROM seams; never opens a serial device."""
import ast
import base64
import contextlib
import hashlib
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
        if q['operation'] in ('read', 'read_state', 'read_recovery_state', 'read_bound_state'):
            reply['data'] = base64.b64encode(self.memory[q['offset'], q['size']]).decode()
        if q['operation'] in ('write', 'restore_state'):
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

    def test_state_halves_do_not_join_legacy_original_binding(self):
        self.backend.claim(self.binding)
        for index, span in enumerate(sorted(t.STATE_READ_SPANS)):
            raw = bytes([index + 11]) * span[1]
            self.memory[span] = raw
            self.assertEqual(self.backend.read_state_chunk(*span), raw)
            self.assertEqual(json.loads(self.calls[-1][1]['input'])['operation'], 'read_state')
        self.assertEqual(self.backend._captured, {})
        captured = {span: self.backend.read(*span) for span in t.SPANS}
        self.assertTrue(self.backend.bind_originals(captured))
        self.assertEqual(self.backend._originals, self.original)
        self.assertEqual(len(self.backend._originals), 5)
        for span in t.STATE_READ_SPANS:
            with self.assertRaises(t.TransportError): self.backend.read(*span)
            with self.assertRaises(t.TransportError): self.backend.write(span[0], self.memory[span])
        self.backend.reset_original()
        self.assertEqual([json.loads(kw['input'])['operation'] for _, kw in self.calls[-6:]],
                         ['read'] * 5 + ['restart'])

    def test_state_read_requires_claim_and_exact_integer_half(self):
        with self.assertRaises(t.TransportError): self.backend.read_state_chunk(0xf00000, 524288)
        self.assertFalse(self.calls)
        self.backend.claim(self.binding)
        calls, verifications = len(self.calls), len(self.verifications)
        for span in ((0xf00000, 1048576), (0xf40000, 524288), (0xf80000, 524287),
                     (0xf00000, True), (float(0xf00000), 524288), (0x10000, 733184),
                     (True, 524288), (0xf00000, 524288.0)):
            with self.subTest(span=span), self.assertRaises(t.TransportError):
                self.backend.read_state_chunk(*span)
        self.assertEqual(len(self.calls), calls)
        self.assertEqual(len(self.verifications), verifications)

    def test_state_direct_operation_rejects_extra_or_missing_fields_before_child(self):
        self.backend.claim(self.binding)
        calls, verifications = len(self.calls), len(self.verifications)
        for fields in ({}, {'offset': 0xf00000}, {'size': 524288},
                       {'offset': 0xf00000, 'size': 524288, 'data': ''},
                       {'offset': 0xf00000, 'size': 524288, 'private_root': 'PRIVATE'},
                       {'offset': 0xf00000, 'size': 524288, 'route': 'COM8'},
                       {'offset': 0xf00000, 'size': 1048576}):
            with self.subTest(fields=fields), self.assertRaises(t.TransportError):
                self.backend._operation('read_state', **fields)
        self.assertEqual(len(self.calls), calls)
        self.assertEqual(len(self.verifications), verifications)
        self.assertEqual(self.backend.failure_diagnostics[-1],
            dict(operation='read_state', stage='request', category='guard', source='parent'))

    def test_state_read_refused_after_binding_recovery_or_uncertain_mutation(self):
        self.backend.claim(self.binding)
        for attribute, value in (('_mutated', True), ('_recovery_only', True),
                                 ('_originals', dict(self.original))):
            with self.subTest(attribute=attribute):
                previous = getattr(self.backend, attribute)
                setattr(self.backend, attribute, value)
                before = len(self.calls)
                with self.assertRaises(t.TransportError):
                    self.backend.read_state_chunk(0xf00000, 524288)
                with self.assertRaises(t.TransportError):
                    self.backend._operation('read_state', offset=0xf80000, size=524288)
                self.assertEqual(len(self.calls), before)
                setattr(self.backend, attribute, previous)

    def test_state_reply_length_schema_and_base64_fail_closed(self):
        self.backend.claim(self.binding)
        valid = base64.b64encode(b's' * 524288).decode('ascii')
        malformed = ({'ok': True}, {'ok': True, 'data': valid, 'private': 'SECRET'},
                     {'ok': True, 'data': [valid]}, {'ok': True, 'data': ''},
                     {'ok': True, 'data': valid[:-1]},
                     {'ok': True, 'data': '%' + valid[1:]},
                     {'ok': True, 'data': base64.b64encode(b's' * 524287).decode('ascii')})
        for reply in malformed:
            with self.subTest(keys=set(reply)):
                self.backend._run = lambda *args, **kw: SimpleNamespace(returncode=0,
                    stdout=json.dumps(reply).encode(), stderr=b'PRIVATE')
                with self.assertRaisesRegex(t.TransportError, '^ble_trial_transport_refused$'):
                    self.backend.read_state_chunk(0xf00000, 524288)
                self.assertIn(self.backend.failure_diagnostics[-1]['stage'], ('worker_reply', 'read_data'))
                self.assertEqual(self.backend.failure_diagnostics[-1]['source'], 'parent')
        self.backend._run = lambda *args, **kw: SimpleNamespace(returncode=0,
            stdout=b' ' * 1100001, stderr=b'PRIVATE')
        with self.assertRaises(t.TransportError): self.backend.read_state_chunk(0xf00000, 524288)
        self.assertEqual(self.backend.failure_diagnostics[-1]['stage'], 'worker_reply')

    def test_state_failures_keep_normalized_diagnostics_through_restart(self):
        self.backend.claim(self.binding)
        actual = self.backend._run
        for stage in ('read_mac.cli.connect', 'read_flash.cli.command', 'read_file', 'read_length'):
            value = dict(operation='read_state', stage=stage, category='tool')
            self.backend._run = lambda *args, **kw: self.child_failure(value)
            with self.assertRaises(t.TransportError): self.backend.read_state_chunk(0xf00000, 524288)
            self.assertEqual(self.backend.failure_diagnostics[-1], dict(value, source='worker'))
        history = self.backend.failure_diagnostics
        self.backend._run = actual
        self.assertTrue(self.backend._operation('restart')['ok'])
        self.assertEqual(self.backend.failure_diagnostics, history)
        self.assertNotIn('PRIVATE', json.dumps(history))

    def test_state_failure_metadata_cannot_claim_write_or_legacy_read(self):
        self.backend.claim(self.binding)
        for operation, stage in (('read', 'read_flash.cli'), ('read_state', 'write_flash.cli'),
                                 ('read_state', 'write_input'), ('read_state', 'restart.cli')):
            value = dict(operation=operation, stage=stage, category='guard')
            self.backend._run = lambda *args, **kw: self.child_failure(value)
            with self.assertRaises(t.TransportError): self.backend.read_state_chunk(0xf00000, 524288)
            self.assertEqual(self.backend.failure_diagnostics[-1],
                dict(operation='read_state', stage='worker_reply', category='guard', source='parent'))

    def test_state_uncertain_child_launch_keeps_original_only_lifetime(self):
        self.backend.claim(self.binding)
        self.failure = 'exception'
        with self.assertRaises(t.TransportError): self.backend.read_state_chunk(0xf00000, 524288)
        self.assertEqual(self.backend.failure_diagnostics[-1],
            dict(operation='read_state', stage='worker_launch', category='timeout', source='parent'))
        self.assertFalse(self.backend._mutated)
        self.assertIsNone(self.backend._originals)
        self.assertEqual(self.backend._captured, {})

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
        body = [n for n in tree.body if isinstance(n, (ast.ClassDef, ast.FunctionDef))]
        body += [n for n in next(n for n in tree.body if isinstance(n, ast.Try)).body
                 if isinstance(n, ast.FunctionDef)]
        calls = []
        p = SimpleNamespace(device='COM7', vid=0x303a, pid=0x1001, serial_number='010203040506')
        tool = self.inert_tool(lambda argv: calls.append(argv))
        ns = dict(re=re, io=io, contextlib=contextlib, esptool=tool, time=time,
                  stage='runtime', operation=None, first_cli_failure=None,
                  route='COM7', expected='010203040506', comports=lambda: [p] if ports is None else ports)
        exec(compile(ast.Module(body=body, type_ignores=[]), '<worker-functions>', 'exec'), ns)
        return ns, calls

    @staticmethod
    def inert_tool(main):
        aliases = {name: (lambda *args, **kwargs: None) for name in (
            'get_default_connected_device', 'read_mac', 'flash_id', 'read_flash',
            'write_flash', 'run', 'reset_chip')}
        return SimpleNamespace(main=main, **aliases)

    def test_actual_worker_exact_no_stub_argv(self):
        ns, calls = self.worker_seam()
        for operation in (['read-mac'], ['flash-id'], ['read-flash', '0x10000', '733184', 'region.bin'],
                          ['write-flash', '--flash-size', '16MB', '0x10000', 'region.bin']):
            ns['run'](operation)
            self.assertEqual(calls[-1], t.rom_argv('COM7', operation))
        ns['run'](['run'], True)
        self.assertEqual(calls[-1], t.rom_argv('COM7', ['run'], True))

    def test_actual_worker_run_argv_with_assumed_vendor_finally_fixture(self):
        ns, calls = self.worker_seam()
        events = []
        def passive():
            events.append('passive_identity')
        def owned_esptool_main(argv):
            # An assumed vendor lifecycle fixture, NOT actual esptool closure proof.
            # This assertion establishes the worker's accepted argv contract only.
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
        ns['esptool'] = self.inert_tool(owned_esptool_main)
        ns['run'](['run'], True)
        self.assertEqual(events, ['passive_identity', 'open_owned_port',
                                  'rom_run_and_explicit_release', 'close_owned_port'])
        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0], t.rom_argv('COM7', ['run'], True))
        self.assertEqual(calls[0][calls[0].index('--after') + 1], 'hard-reset')

    def test_actual_worker_reenumerates_and_refuses_identity_swap(self):
        wrong = SimpleNamespace(device='COM7', vid=0x303a, pid=0x1001, serial_number='111213141516')
        ns, calls = self.worker_seam([wrong])
        with self.assertRaises(ns['GuardFailure']): ns['run'](['run'], True)
        self.assertFalse(calls)

    def test_actual_worker_duplicate_identity_refused(self):
        ports = [SimpleNamespace(device=r, vid=0x303a, pid=0x1001, serial_number='010203040506') for r in ('COM7','COM8')]
        ns, calls = self.worker_seam(ports)
        with self.assertRaises(ns['GuardFailure']): ns['run'](['run'], True)
        self.assertFalse(calls)

    def worker_dispatch(self, operation='read', *, ports=None, cli=None, request=None,
                        startup=None, tool_hooks=None):
        # Actual request/dispatch statements and actual exception handlers. Runtime
        # capsule verification is deliberately outside this inert host seam.
        ns, _ = self.worker_seam()
        calls, replies = [], []
        class SerialFailure(Exception): pass
        class ToolFailure(Exception): pass
        expected_port = SimpleNamespace(device='COM7', vid=0x303a, pid=0x1001,
                                        serial_number='010203040506')
        def default_cli(argv):
            if 'read-mac' in argv:
                print('MAC: ' + ':'.join(f'{n:02x}' for n in range(1, 7)))
            elif 'flash-id' in argv:
                print('ESP32-S3\nDetected flash size: 16MB')
            elif 'read-flash' in argv:
                Path(argv[-1]).write_bytes(b'r' * int(argv[-2]))
        def invoke_cli(argv):
            calls.append(list(argv))
            if cli is None:
                default_cli(argv)
            else:
                cli(argv, ns, default_cli)
        q = dict(operation=operation, route='COM7', identity='010203040506',
                 private_root=str(self.private), offset=0xd000, size=12288,
                 data=base64.b64encode(b'w' * 12288).decode())
        if operation in ('read_state', 'read_recovery_state', 'read_bound_state', 'restore_state'):
            q.pop('data')
            q.update(manifest={}, offset=0xf00000, size=524288)
            if operation == 'restore_state':
                raw = b'w' * 524288
                q.update(data=base64.b64encode(raw).decode(), original_sha256=hashlib.sha256(raw).hexdigest())
        if request:
            q.update(request)
        tool = self.inert_tool(invoke_cli)
        tool.FatalError = ToolFailure
        if tool_hooks:
            for name, function in tool_hooks.items():
                setattr(tool, name, function)
        ns.update(q=q, stage='runtime', operation=None, first_cli_failure=None, sys=sys, json=json,
                  base64=base64, hashlib=hashlib, pathlib=SimpleNamespace(Path=Path), tempfile=tempfile,
                  serial=SimpleNamespace(SerialException=SerialFailure),
                  esptool=tool,
                  comports=ports if callable(ports) else lambda: [expected_port] if ports is None else ports,
                  print=lambda value: replies.append(json.loads(value)))
        if startup:
            ns['capture_startup'] = startup
        tree = ast.parse(t.WORKER)
        actual_try = next(n for n in tree.body if isinstance(n, ast.Try))
        request_index = next(i for i, n in enumerate(actual_try.body)
            if isinstance(n, ast.Assign) and any(isinstance(x, ast.Name) and x.id == 'stage'
                for x in n.targets) and isinstance(n.value, ast.Constant) and n.value.value == 'request')
        dispatch = ast.Try(body=actual_try.body[request_index:], handlers=actual_try.handlers,
                           orelse=actual_try.orelse, finalbody=actual_try.finalbody)
        code = compile(ast.fix_missing_locations(ast.Module(body=[dispatch], type_ignores=[])),
                       '<actual-worker-request-dispatch>', 'exec')
        exit_code = None
        try:
            exec(code, ns)
        except SystemExit as error:
            exit_code = error.code
        self.assertEqual(len(replies), 1)
        self.assertEqual(exit_code, None if replies[0]['ok'] else 1)
        return replies[0], calls, ns

    def assert_worker_failure(self, reply, operation, stage, category):
        self.assertEqual(reply, {'ok': False, 'failure': {
            'operation': operation, 'stage': stage, 'category': category}})
        self.assertNotIn('PRIVATE', json.dumps(reply))
        self.assertNotIn('COM7', json.dumps(reply))
        self.assertNotIn('010203040506', json.dumps(reply))

    def test_actual_worker_request_guard_precedes_cli(self):
        reply, calls, _ = self.worker_dispatch('invalid')
        self.assert_worker_failure(reply, None, 'request', 'guard')
        self.assertFalse(calls)
        reply, calls, _ = self.worker_dispatch(request={'route': 'PRIVATE invalid route'})
        self.assert_worker_failure(reply, 'read', 'request', 'guard')
        self.assertFalse(calls)

    def test_actual_worker_state_halves_use_read_only_accepted_rom_commands(self):
        for offset in (0xf00000, 0xf80000):
            with self.subTest(offset=offset):
                reply, calls, _ = self.worker_dispatch('read_state', request={'offset': offset})
                self.assertEqual(set(reply), {'ok', 'data'})
                self.assertTrue(reply['ok'])
                self.assertEqual(base64.b64decode(reply['data'], validate=True), b'r' * 524288)
                self.assertLess(len(json.dumps(reply).encode()), 1100000)
                self.assertEqual(calls, [t.rom_argv('COM7', ['read-mac']),
                    t.rom_argv('COM7', ['flash-id']),
                    t.rom_argv('COM7', ['read-flash', hex(offset), '524288', calls[-1][-1]])])
                self.assertFalse(list(self.private.glob('ot218-rom-*')))

    def test_parent_state_capture_roundtrip_uses_actual_inert_worker_dispatch(self):
        self.backend.claim(self.binding)
        commands = []
        def child(argv, **kwargs):
            payload = json.loads(kwargs['input'])
            self.assertEqual(payload['operation'], 'read_state')
            reply, calls, _ = self.worker_dispatch('read_state', request=payload)
            commands.extend(calls)
            return SimpleNamespace(returncode=0, stdout=json.dumps(reply).encode(), stderr=b'')
        self.backend._run = child
        for span in sorted(t.STATE_READ_SPANS):
            self.assertEqual(self.backend.read_state_chunk(*span), b'r' * span[1])
        self.assertEqual(len(commands), 6)
        self.assertFalse(any('write-flash' in command or command[-1] == 'run' for command in commands))
        self.assertEqual(self.backend._captured, {})
        self.assertEqual(self.backend.failure_diagnostics, ())

    def test_actual_worker_state_guard_rejects_wrong_span_and_extra_before_cli(self):
        for fields in ({'offset': 0xf00000, 'size': 1048576},
                       {'offset': 0xf40000}, {'offset': 0x10000, 'size': 733184},
                       {'offset': float(0xf00000)}, {'size': True},
                       {'data': base64.b64encode(b'w' * 524288).decode()},
                       {'write_kind': 'restore'}):
            with self.subTest(fields=set(fields)):
                reply, calls, _ = self.worker_dispatch('read_state', request=fields)
                self.assert_worker_failure(reply, 'read_state', 'request', 'guard')
                self.assertFalse(calls)

    def test_actual_worker_state_spans_are_inaccessible_to_generic_read_and_write(self):
        for operation in ('read', 'write'):
            for offset in (0xf00000, 0xf80000):
                with self.subTest(operation=operation, offset=offset):
                    reply, calls, _ = self.worker_dispatch(operation,
                        request={'offset': offset, 'size': 524288})
                    self.assert_worker_failure(reply, operation, 'request', 'guard')
                    self.assertEqual(calls, [t.rom_argv('COM7', ['read-mac']),
                                             t.rom_argv('COM7', ['flash-id'])])

    def test_actual_worker_state_failure_records_boundary_and_closes_temporary_file(self):
        for raw, stage in ((None, 'read_file'), (b'r' * 524287, 'read_length')):
            with self.subTest(stage=stage):
                def cli(argv, ns, default):
                    if 'read-flash' in argv:
                        if raw is not None:
                            Path(argv[-1]).write_bytes(raw)
                    else:
                        default(argv)
                reply, _, _ = self.worker_dispatch('read_state', cli=cli)
                self.assert_worker_failure(reply, 'read_state', stage,
                    'other' if raw is None else 'guard')
                self.assertFalse(list(self.private.glob('ot218-rom-*')))
        def failed_cli(argv, ns, default):
            if 'read-flash' in argv:
                raise ns['serial'].SerialException('PRIVATE state read failure')
            default(argv)
        reply, calls, _ = self.worker_dispatch('read_state', cli=failed_cli)
        self.assert_worker_failure(reply, 'read_state', 'read_flash.cli', 'serial')
        self.assertEqual(len(calls), 3)
        self.assertFalse(list(self.private.glob('ot218-rom-*')))

    def test_actual_worker_route_before_failure_never_invokes_cli(self):
        reply, calls, _ = self.worker_dispatch(ports=[])
        self.assert_worker_failure(reply, 'read', 'read_mac.route_before', 'guard')
        self.assertFalse(calls)

    def test_actual_worker_cli_failures_are_sanitized_and_stage_specific(self):
        cases = [('read-mac', 'read_mac.cli', 'tool'),
                 ('flash-id', 'flash_id.cli', 'serial'),
                 ('read-flash', 'read_flash.cli', 'timeout')]
        for command, stage, category in cases:
            with self.subTest(command=command):
                def cli(argv, ns, default):
                    if command in argv:
                        error_type = {'tool': ns['esptool'].FatalError,
                                      'serial': ns['serial'].SerialException,
                                      'timeout': TimeoutError}[category]
                        print('PRIVATE raw CLI output COM7 010203040506')
                        raise error_type('PRIVATE vendor failure COM7 010203040506')
                    default(argv)
                reply, _, _ = self.worker_dispatch(cli=cli)
                self.assert_worker_failure(reply, 'read', stage, category)

    def test_actual_worker_observer_preserves_first_failure_before_cleanup_replaces_it(self):
        # Inert alias calls exercise the actual observer and emitting handler.
        # Actual vendor cleanup behavior is covered by the separate CLI fixture.
        for phase, expected_category in (('connect', 'timeout'), ('command', 'other'),
                                          ('teardown', 'other')):
            with self.subTest(phase=phase):
                events = []
                def connect(*args, **kwargs):
                    events.append('connect')
                    if phase == 'connect':
                        raise TimeoutError('PRIVATE primary connect failure')
                def command(*args, **kwargs):
                    events.append('command')
                    if phase == 'command':
                        raise RuntimeError('PRIVATE primary command failure')
                def teardown(*args, **kwargs):
                    events.append('teardown')
                    if phase == 'connect':
                        raise RuntimeError('PRIVATE replacement cleanup failure')
                    if phase == 'command':
                        raise TimeoutError('PRIVATE replacement cleanup timeout')
                    raise RuntimeError('PRIVATE teardown-only failure')
                hooks = dict(get_default_connected_device=connect, read_mac=command,
                             reset_chip=teardown)
                def cli(argv, ns, default):
                    tool = ns['esptool']
                    try:
                        tool.get_default_connected_device('PRIVATE synthetic connection', retry=False)
                        tool.read_mac('PRIVATE synthetic device', flag=True)
                        default(argv)
                    finally:
                        tool.reset_chip('PRIVATE synthetic device', 'no-reset')
                reply, calls, ns = self.worker_dispatch(cli=cli, tool_hooks=hooks)
                stage = 'read_mac.cli.' + phase
                self.assert_worker_failure(reply, 'read', stage, expected_category)
                self.assertEqual(ns['first_cli_failure'], (stage, expected_category))
                self.assertEqual(events, ['connect', 'teardown'] if phase == 'connect' else
                                         ['connect', 'command', 'teardown'])
                self.assertEqual(len(calls), 1)
                for name, original in hooks.items():
                    self.assertIs(getattr(ns['esptool'], name), original)

    def test_actual_worker_observer_delegates_arguments_returns_and_restores_aliases(self):
        token = object()
        events = []
        def hook(name):
            def invoke(*args, **kwargs):
                events.append((name, args, kwargs))
                return token
            return invoke
        names = ('get_default_connected_device', 'read_mac', 'flash_id', 'read_flash',
                 'write_flash', 'run', 'reset_chip')
        hooks = {name: hook(name) for name in names}
        def cli(argv, ns, default):
            tool = ns['esptool']
            command = next(name for name in ('read-mac', 'flash-id', 'read-flash') if name in argv)
            self.assertIs(tool.get_default_connected_device(token, retries=2), token)
            try:
                self.assertIs(getattr(tool, command.replace('-', '_'))(token, offset=0), token)
                default(argv)
            finally:
                self.assertIs(tool.reset_chip(token, 'no-reset'), token)
        reply, calls, ns = self.worker_dispatch(cli=cli, tool_hooks=hooks)
        self.assertTrue(reply['ok'])
        self.assertIsNone(ns['first_cli_failure'])
        self.assertEqual([name for name, args, kwargs in events],
            ['get_default_connected_device', 'read_mac', 'reset_chip',
             'get_default_connected_device', 'flash_id', 'reset_chip',
             'get_default_connected_device', 'read_flash', 'reset_chip'])
        self.assertEqual(events[0][1:], ((token,), {'retries': 2}))
        self.assertEqual(events[1][1:], ((token,), {'offset': 0}))
        self.assertEqual(events[2][1:], ((token, 'no-reset'), {}))
        self.assertEqual([argv for argv in calls], [
            t.rom_argv('COM7', ['read-mac']), t.rom_argv('COM7', ['flash-id']),
            t.rom_argv('COM7', ['read-flash', '0xd000', '12288', calls[-1][-1]])])
        for name, original in hooks.items():
            self.assertIs(getattr(ns['esptool'], name), original)

    def test_actual_worker_output_limit_boundary(self):
        for length, accepted in ((1048576, True), (1048577, False)):
            with self.subTest(length=length):
                def cli(argv, ns, default):
                    if 'read-mac' in argv:
                        text = 'MAC: ' + ':'.join(f'{n:02x}' for n in range(1, 7)) + '\n'
                        sys.stdout.write(text + 'x' * (length - len(text)))
                    else:
                        default(argv)
                reply, calls, _ = self.worker_dispatch(cli=cli)
                if accepted:
                    self.assertEqual(set(reply), {'ok', 'data'})
                    self.assertTrue(reply['ok'])
                    self.assertEqual(base64.b64decode(reply['data']), b'r' * 12288)
                    self.assertEqual(len(calls), 3)
                else:
                    self.assert_worker_failure(reply, 'read', 'read_mac.output_limit', 'guard')
                    self.assertEqual(len(calls), 1)

    def test_actual_worker_route_after_failure_identifies_post_cli_guard(self):
        count = 0
        port = SimpleNamespace(device='COM7', vid=0x303a, pid=0x1001,
                               serial_number='010203040506')
        def ports():
            nonlocal count
            count += 1
            return [port] if count == 1 else []
        reply, calls, _ = self.worker_dispatch(ports=ports)
        self.assert_worker_failure(reply, 'read', 'read_mac.route_after', 'guard')
        self.assertEqual(len(calls), 1)
        self.assertEqual(count, 2)

    def test_actual_worker_identity_and_flash_parsers_keep_distinct_guard_stages(self):
        for command, text, stage in (
                ('read-mac', 'MAC: ' + ':'.join(f'{n:02x}' for n in range(17, 23)), 'read_mac.identity_parse'),
                ('flash-id', 'ESP32-S3\nDetected flash size: 8MB', 'flash_id.flash_parse')):
            with self.subTest(command=command):
                def cli(argv, ns, default):
                    if command in argv:
                        print(text)
                    else:
                        default(argv)
                reply, _, _ = self.worker_dispatch(cli=cli)
                self.assert_worker_failure(reply, 'read', stage, 'guard')

    def test_actual_worker_read_file_and_read_length_boundaries(self):
        for raw, stage, category in ((None, 'read_file', 'other'),
                                     (b'PRIVATE short data', 'read_length', 'guard')):
            with self.subTest(stage=stage):
                def cli(argv, ns, default):
                    if 'read-flash' in argv:
                        if raw is not None:
                            Path(argv[-1]).write_bytes(raw)
                    else:
                        default(argv)
                reply, _, _ = self.worker_dispatch(cli=cli)
                self.assert_worker_failure(reply, 'read', stage, category)
                self.assertFalse(list(self.private.glob('ot218-rom-*')))

    def test_actual_worker_write_input_and_write_cli_boundaries(self):
        reply, calls, _ = self.worker_dispatch('write', request={'data': '%%%PRIVATE'})
        self.assert_worker_failure(reply, 'write', 'write_input', 'other')
        self.assertEqual(len(calls), 2)
        def cli(argv, ns, default):
            if 'write-flash' in argv:
                raise ns['esptool'].FatalError('PRIVATE write failure')
            default(argv)
        reply, calls, _ = self.worker_dispatch('write', cli=cli)
        self.assert_worker_failure(reply, 'write', 'write_flash.cli', 'tool')
        self.assertEqual(len(calls), 3)
        self.assertFalse(list(self.private.glob('ot218-rom-*')))

    def test_actual_worker_restart_and_capture_failure_stages(self):
        def cli(argv, ns, default):
            if argv[-1] == 'run':
                raise ns['serial'].SerialException('PRIVATE restart failure')
            default(argv)
        reply, _, _ = self.worker_dispatch('restart', cli=cli)
        self.assert_worker_failure(reply, 'restart', 'restart.cli', 'serial')
        def startup(*args):
            raise TimeoutError('PRIVATE startup failure')
        reply, calls, _ = self.worker_dispatch('restart_capture', startup=startup)
        self.assert_worker_failure(reply, 'restart_capture', 'startup_capture', 'timeout')
        self.assertEqual(calls[-1], t.rom_argv('COM7', ['run'], True))
        reply, calls, _ = self.worker_dispatch('passive_capture', startup=startup)
        self.assert_worker_failure(reply, 'passive_capture', 'startup_capture', 'timeout')
        self.assertFalse(calls)

    def test_actual_worker_probe_remains_inert_and_success_reply_unchanged(self):
        reply, calls, _ = self.worker_dispatch('probe', ports=lambda: self.fail('probe enumerated ports'))
        self.assertEqual(reply, {'ok': True})
        self.assertFalse(calls)
        for operation in ('verify', 'write', 'restart'):
            with self.subTest(operation=operation):
                reply, _, _ = self.worker_dispatch(operation)
                self.assertEqual(reply, {'ok': True})

    def child_failure(self, value, *, returncode=1, extra=None):
        reply = {'ok': False, 'failure': value}
        if extra:
            reply.update(extra)
        return SimpleNamespace(returncode=returncode, stdout=json.dumps(reply).encode(),
                               stderr=b'PRIVATE stderr COM7 010203040506')

    def test_parent_retains_only_validated_worker_failure(self):
        self.backend.claim(self.binding)
        for operation, stage in (('read', 'read_flash.cli'), ('read', 'read_length'),
                                  ('verify', 'read_mac.route_before')):
            with self.subTest(operation=operation, stage=stage):
                value = dict(operation=operation, stage=stage, category='guard')
                self.backend._run = lambda *args, **kw: self.child_failure(value)
                call = (lambda: self.backend.read(0xd000, 12288)) if operation == 'read' else (
                    lambda: self.backend.reverify(self.binding))
                with self.assertRaisesRegex(t.TransportError, '^ble_trial_transport_refused$') as error:
                    call()
                self.assertIsNone(error.exception.__cause__)
                self.assertEqual(self.backend.failure_diagnostics[-1], dict(value, source='worker'))
        value = dict(operation=None, stage='runtime', category='guard')
        self.backend._run = lambda *args, **kw: self.child_failure(value)
        with self.assertRaises(t.TransportError):
            self.backend.read(0xd000, 12288)
        self.assertEqual(self.backend.failure_diagnostics[-1],
                         dict(operation='read', stage='runtime', category='guard', source='worker'))

    def test_parent_rejects_malformed_spoofed_or_incompatible_worker_metadata(self):
        self.backend.claim(self.binding)
        valid = dict(operation='read', stage='read_flash.cli', category='tool')
        malformed = [None, [], {}, dict(valid, raw_log='PRIVATE'),
                     dict(valid, stage='PRIVATE COM7'), dict(valid, category=True),
                     dict(valid, category='PRIVATE'), dict(valid, operation='write'),
                     dict(valid, operation=None), dict(valid, stage='write_input'),
                     dict(valid, stage='runtime'),
                     dict(valid, stage='write_flash.cli'), dict(valid, stage='restart.cli'),
                     dict(valid, stage='startup_capture')]
        for value in malformed:
            with self.subTest(value=value):
                self.backend._run = lambda *args, **kw: self.child_failure(value)
                with self.assertRaisesRegex(t.TransportError, '^ble_trial_transport_refused$'):
                    self.backend.read(0xd000, 12288)
                self.assertEqual(self.backend.failure_diagnostics[-1],
                    dict(operation='read', stage='worker_reply', category='guard', source='parent'))
        for code, extra in ((0, None), (2, None), (1, {'PRIVATE': 'secret'})):
            with self.subTest(returncode=code, extra=extra):
                self.backend._run = lambda *args, **kw: self.child_failure(valid, returncode=code, extra=extra)
                with self.assertRaises(t.TransportError):
                    self.backend.read(0xd000, 12288)
                self.assertEqual(self.backend.failure_diagnostics[-1]['source'], 'parent')

    def test_failure_validator_refuses_operation_specific_stage_spoofing(self):
        for operation, stage in (('verify', 'read_length'), ('read', 'write_input'),
                                 ('write', 'read_file'), ('restart', 'restart.route_after'),
                                 ('probe', 'read_mac.cli'), ('passive_capture', 'flash_id.cli'),
                                 ('read', 'startup_capture'), ('verify', 'read_flash.cli.command'),
                                 ('read', 'write_flash.cli.teardown'), ('write', 'restart.cli.connect'),
                                 ('passive_capture', 'read_mac.cli.command')):
            with self.subTest(operation=operation, stage=stage):
                with self.assertRaises(t.TransportError):
                    t.validated_worker_failure(dict(operation=operation, stage=stage, category='guard'), operation)

    def test_parent_launch_and_read_decode_failures_remain_redacted(self):
        self.backend.claim(self.binding)
        for error, category in ((subprocess.TimeoutExpired('PRIVATE COM7', 240), 'timeout'),
                                 (OSError('PRIVATE process path'), 'process'),
                                 (RuntimeError('PRIVATE arbitrary exception'), 'other')):
            with self.subTest(category=category):
                def fail(*args, **kw): raise error
                self.backend._run = fail
                with self.assertRaisesRegex(t.TransportError, '^ble_trial_transport_refused$') as caught:
                    self.backend.read(0xd000, 12288)
                self.assertIsNone(caught.exception.__cause__)
                self.assertEqual(self.backend.failure_diagnostics[-1],
                    dict(operation='read', stage='worker_launch', category=category, source='parent'))
        self.backend._run = lambda *args, **kw: SimpleNamespace(returncode=0,
            stdout=b'{"ok":true,"data":"PRIVATE invalid base64%%%"}', stderr=b'PRIVATE')
        with self.assertRaises(t.TransportError):
            self.backend.read(0xd000, 12288)
        self.assertEqual(self.backend.failure_diagnostics[-1],
            dict(operation='read', stage='read_data', category='guard', source='parent'))
        self.assertNotIn('PRIVATE', json.dumps(self.backend.failure_diagnostics))

    def test_standalone_probe_failure_initializes_private_history_without_enumeration(self):
        captured = []
        actual = t.Transport.probe_runtime
        def inspect(instance):
            captured.append(instance)
            return actual(instance)
        def timeout(*args, **kwargs):
            self.assertEqual(json.loads(kwargs['input'])['operation'], 'probe')
            raise subprocess.TimeoutExpired('PRIVATE runtime command', 240)
        with patch.object(t.Transport, 'probe_runtime', inspect):
            with self.assertRaisesRegex(t.TransportError, '^ble_trial_transport_refused$'):
                t.probe_runtime(manifest_path=self.private / 'manifest.json', manifest_sha256='a' * 64,
                    private_root=self.private, subprocess_run=timeout, manifest_verifier=self.verify)
        self.assertEqual(captured[0].failure_diagnostics,
            (dict(operation='probe', stage='worker_launch', category='timeout', source='parent'),))

    def test_primary_failure_survives_successful_original_cleanup(self):
        self.capture()
        original_run = self.backend._run
        value = dict(operation='write', stage='write_flash.cli', category='tool')
        def fail_candidate(argv, **kw):
            if json.loads(kw['input'])['operation'] == 'write':
                return self.child_failure(value)
            return original_run(argv, **kw)
        self.backend._run = fail_candidate
        with self.assertRaises(t.TransportError):
            self.backend.write(0x10000, self.candidate)
        self.assertTrue(self.backend._mutated)
        self.backend._run = original_run
        self.assertTrue(self.backend.reset_original())
        self.assertEqual(self.memory, self.original)
        self.assertEqual(self.backend.failure_diagnostics, (dict(value, source='worker'),))

    def test_failure_history_is_bounded_preserves_primary_and_returns_copies(self):
        self.backend.claim(self.binding)
        expected = []
        for index in range(25):
            value = dict(operation='verify', stage=('read_mac.cli' if index == 0 else 'flash_id.cli'),
                         category=('tool' if index == 0 else ('serial' if index % 2 else 'guard')))
            expected.append(dict(value, source='worker'))
            self.backend._run = lambda *args, **kw: self.child_failure(value)
            with self.assertRaises(t.TransportError):
                self.backend.reverify(self.binding)
            self.assertLessEqual(len(self.backend.failure_diagnostics), 16)
        self.assertEqual(self.backend.failure_diagnostics, tuple([expected[0]] + expected[-15:]))
        exposed = self.backend.failure_diagnostics
        exposed[0]['stage'] = 'PRIVATE caller mutation'
        self.assertEqual(self.backend.failure_diagnostics, tuple([expected[0]] + expected[-15:]))

    def complete_backend(self, recovery=False):
        for index, span in enumerate(sorted(t.STATE_READ_SPANS)):
            self.memory.setdefault(span, bytes([10 + index]) * span[1])
        self.complete_original = {name: self.memory[span] for name, span in t.COMPLETE_NAMED_SPANS.items()}
        self.backend = t.CompleteCustodyTransport(manifest_path=self.private / 'manifest.json',
            manifest_sha256='a' * 64, private_root=self.private, route='COM7',
            expected_identity='010203040506', opaque_binding=self.binding, binding_key=self.key,
            candidate=self.candidate, subprocess_run=self.run_child, manifest_verifier=self.verify,
            recovery_only=recovery)
        return self.backend

    def complete_capture(self):
        self.complete_backend()
        self.backend.claim(self.binding)
        values = {}
        for name, span in t.COMPLETE_NAMED_SPANS.items():
            read = self.backend.read_state_chunk if span in t.STATE_READ_SPANS else self.backend.read
            values[name] = read(*span)
        self.backend.bind_complete_originals(values)
        return values

    def payload_operations(self, calls=None):
        return [json.loads(k['input'])['operation'] for _, k in (self.calls if calls is None else calls)]

    def test_complete_binding_requires_actual_seven_region_capture(self):
        self.complete_backend()
        self.backend.claim(self.binding)
        before = len(self.calls)
        with self.assertRaises(t.TransportError):
            self.backend.bind_complete_originals(self.complete_original)
        with self.assertRaises(t.TransportError): self.backend.bind_originals(self.original)
        with self.assertRaises(t.TransportError): self.backend.bind_recovery_originals(self.original)
        for name, span in t.NAMED_SPANS.items(): self.backend.read(*span)
        with self.assertRaises(t.TransportError):
            self.backend.bind_complete_originals(self.complete_original)
        for span in sorted(t.STATE_READ_SPANS): self.backend.read_state_chunk(*span)
        self.assertTrue(self.backend.bind_complete_originals(self.complete_original))
        self.assertEqual(set(self.backend._captured), t.SPANS)
        self.assertEqual(set(self.backend._state_captured), t.STATE_READ_SPANS)
        self.assertEqual(len(self.calls) - before, 7)

    def test_complete_binding_is_named_exact_and_immutable(self):
        values = self.complete_capture()
        values['state_0'] = b'changed'  # Binding owns a copy of the dictionary.
        self.assertEqual(self.backend._complete_originals, self.complete_original)
        with self.assertRaises(t.TransportError): self.backend.bind_complete_originals(self.complete_original)
        for change in ('missing', 'extra', 'wrong_type', 'wrong_length', 'swap'):
            self.complete_backend()
            self.backend.claim(self.binding)
            values = dict(self.complete_original)
            if change == 'missing': values.pop('state_1')
            elif change == 'extra': values['private'] = b''
            elif change == 'wrong_type': values['state_0'] = bytearray(values['state_0'])
            elif change == 'wrong_length': values['state_0'] = values['state_0'][:-1]
            else: values['state_0'], values['state_1'] = values['state_1'], values['state_0']
            for name, span in t.COMPLETE_NAMED_SPANS.items():
                (self.backend.read_state_chunk if span in t.STATE_READ_SPANS else self.backend.read)(*span)
            with self.assertRaises(t.TransportError): self.backend.bind_complete_originals(values)
            self.assertIsNone(self.backend._originals)

    def test_complete_mutations_require_complete_custody_even_direct_operation(self):
        self.complete_backend()
        self.backend.claim(self.binding)
        before = len(self.calls)
        for call in (lambda: self.backend.write(0x10000, self.candidate),
                     lambda: self.backend.boot_candidate(before_boot=lambda: None),
                     lambda: self.backend.reset_original(),
                     lambda: self.backend.reset_complete_original(before_reset=lambda: None),
                     lambda: self.backend.read_bound_state_chunk(0xf00000, 524288),
                     lambda: self.backend.restore_state_chunk(0xf00000, self.complete_original['state_0']),
                     lambda: self.backend._operation('write', offset=0x10000, size=733184,
                         data=base64.b64encode(self.candidate).decode()),
                     lambda: self.backend._operation('restart'),
                     lambda: self.backend._operation('restart_capture')):
            with self.assertRaises(t.TransportError): call()
        self.assertEqual(len(self.calls), before)

    def test_legacy_transport_cannot_obtain_complete_state_capabilities(self):
        self.capture()
        before = len(self.calls)
        for op in ('read_bound_state', 'read_recovery_state', 'restore_state'):
            with self.assertRaises(t.TransportError):
                self.backend._operation(op, offset=0xf00000, size=524288)
        self.assertEqual(len(self.calls), before)

    def test_complete_bound_state_reads_follow_mutation_and_do_not_recapture(self):
        self.complete_capture()
        self.backend.write(0x10000, self.candidate)
        for name in ('state_0', 'state_1'):
            self.assertEqual(self.backend.read_bound_state_chunk(*t.COMPLETE_NAMED_SPANS[name]),
                             self.complete_original[name])
        with self.assertRaises(t.TransportError): self.backend.read_state_chunk(0xf00000, 524288)
        self.assertEqual(set(self.backend._captured), t.SPANS)
        self.assertEqual(self.payload_operations()[-2:], ['read_bound_state'] * 2)

    def test_complete_state_restore_is_only_exact_original(self):
        self.complete_capture()
        for name in ('state_0', 'state_1'):
            offset, size = t.COMPLETE_NAMED_SPANS[name]
            self.memory[offset, size] = b'changed'.ljust(size, b'!')
            self.assertTrue(self.backend.restore_state_chunk(offset, self.complete_original[name]))
            self.assertEqual(self.memory[offset, size], self.complete_original[name])
        before = len(self.calls)
        for offset, raw in ((0xf00000, b'x' * 524288), (0xf00000, self.complete_original['state_1']),
                            (0xf00000, b'x'), (0xf80000, bytearray(b'x' * 524288)),
                            (0xd000, self.original[0xd000, 12288]), (True, b'x' * 524288)):
            with self.assertRaises(t.TransportError): self.backend.restore_state_chunk(offset, raw)
        self.assertEqual(len(self.calls), before)

    def test_complete_direct_state_restore_rejects_forged_fields_before_child(self):
        self.complete_capture()
        raw = self.complete_original['state_0']
        fields = dict(offset=0xf00000, size=524288, data=base64.b64encode(raw).decode(),
                      original_sha256=hashlib.sha256(raw).hexdigest())
        cases = [dict(fields, original_sha256='0' * 64), dict(fields, private='SECRET'),
                 dict(fields, offset=0xf00001), dict(fields, size=True), dict(fields, data='?'),
                 dict(fields, data=base64.b64encode(b'x' * 524288).decode()),
                 {key: value for key, value in fields.items() if key != 'original_sha256'}]
        before = len(self.calls)
        for invalid in cases:
            with self.assertRaises(t.TransportError): self.backend._operation('restore_state', **invalid)
        self.assertEqual(len(self.calls), before)
        self.assertFalse(self.backend._mutated)

    def test_complete_direct_legacy_write_keeps_exact_custody_guards(self):
        self.complete_capture()
        before = len(self.calls)
        for fields in (dict(offset=0xd000, size=12288, data=base64.b64encode(b'x' * 12288).decode()),
                       dict(offset=0xf00000, size=524288, data=base64.b64encode(self.complete_original['state_0']).decode()),
                       dict(offset=0x10000, size=733184, data=base64.b64encode(self.candidate).decode(), extra=True)):
            with self.assertRaises(t.TransportError): self.backend._operation('write', **fields)
        self.assertEqual(len(self.calls), before)
        self.assertFalse(self.backend._mutated)

    def test_complete_uncertain_state_restore_fences_fresh_capture(self):
        self.complete_capture()
        self.failure = 'exception'
        with self.assertRaises(t.TransportError):
            self.backend.restore_state_chunk(0xf00000, self.complete_original['state_0'])
        self.assertTrue(self.backend._mutated)
        self.assertEqual(self.backend.failure_diagnostics[-1],
            dict(operation='restore_state', stage='worker_launch', category='timeout', source='parent'))
        self.failure = None
        with self.assertRaises(t.TransportError): self.backend.read_state_chunk(0xf00000, 524288)
        self.assertEqual(self.backend.read_bound_state_chunk(0xf00000, 524288), self.complete_original['state_0'])

    def test_complete_recovery_requires_fresh_recovery_only_instance(self):
        self.complete_backend()
        self.backend.claim(self.binding)
        with self.assertRaises(t.TransportError):
            self.backend.bind_complete_recovery_originals(self.complete_original)
        self.complete_backend(recovery=True)
        with self.assertRaises(t.TransportError):
            self.backend.bind_complete_recovery_originals(self.complete_original)
        self.backend.claim(self.binding)
        self.assertTrue(self.backend.bind_complete_recovery_originals(self.complete_original))
        with self.assertRaises(t.TransportError): self.backend.bind_complete_recovery_originals(self.complete_original)
        with self.assertRaises(t.TransportError): self.backend.bind_complete_originals(self.complete_original)
        before = len(self.calls)
        with self.assertRaises(t.TransportError): self.backend.write(0x10000, self.candidate)
        with self.assertRaises(t.TransportError): self.backend.boot_candidate(before_boot=lambda: None)
        self.assertEqual(len(self.calls), before)
        self.assertTrue(self.backend.write(0x10000, self.complete_original['application']))
        self.assertEqual(self.backend.read_bound_state_chunk(0xf80000, 524288), self.complete_original['state_1'])

    def test_untouched_recovery_capture_remains_restore_only_and_prebinding(self):
        self.complete_backend(recovery=True)
        self.backend.claim(self.binding)
        with self.assertRaises(t.TransportError): self.backend.read_state_chunk(0xf00000, 524288)
        values = {}
        for name, span in t.COMPLETE_NAMED_SPANS.items():
            reader = self.backend.read_recovery_state_chunk if span in t.STATE_READ_SPANS else self.backend.read
            values[name] = reader(*span)
        self.assertTrue(self.backend.bind_complete_recovery_originals(values))
        with self.assertRaises(t.TransportError): self.backend.read_recovery_state_chunk(0xf00000, 524288)
        with self.assertRaises(t.TransportError): self.backend.write(0x10000, self.candidate)
        self.assertTrue(self.backend.reset_complete_original(before_reset=lambda: None))

    def test_recovery_state_capture_refuses_normal_mutated_and_wrong_fields(self):
        self.complete_backend()
        self.backend.claim(self.binding)
        before = len(self.calls)
        with self.assertRaises(t.TransportError): self.backend.read_recovery_state_chunk(0xf00000, 524288)
        self.assertEqual(len(self.calls), before)
        for fields in ({'offset': 0xf00000, 'size': 1048576}, {'offset': True, 'size': 524288},
                       {'offset': 0xf00000, 'size': 524288, 'extra': True}, {'offset': 0xf00000}):
            self.complete_backend(recovery=True)
            self.backend.claim(self.binding)
            before = len(self.calls)
            with self.assertRaises(t.TransportError): self.backend._operation('read_recovery_state', **fields)
            self.assertEqual(len(self.calls), before)
        self.backend._mutated = True
        before = len(self.calls)
        with self.assertRaises(t.TransportError): self.backend.read_recovery_state_chunk(0xf00000, 524288)
        self.assertEqual(len(self.calls), before)

    def test_complete_candidate_boot_checks_callback_after_read_before_restart(self):
        self.complete_capture()
        self.backend.write(0x10000, self.candidate)
        events = []
        original_run = self.backend._run
        def child(argv, **kw):
            events.append(json.loads(kw['input'])['operation'])
            return original_run(argv, **kw)
        self.backend._run = child
        self.assertTrue(self.backend.boot_candidate(before_boot=lambda: events.append('durable_intent')))
        self.assertEqual(events, ['read', 'durable_intent', 'restart'])
        self.assertTrue(self.backend._candidate_booted)
        self.assertIsNone(self.backend._restart_kind)

    def test_complete_candidate_callback_failure_or_wrong_bytes_never_boots(self):
        self.complete_capture()
        callback = []
        with self.assertRaises(t.TransportError):
            self.backend.boot_candidate(before_boot=lambda: callback.append('called'))
        self.assertFalse(callback)
        self.backend.write(0x10000, self.candidate)
        before = len(self.calls)
        def fail(): raise OSError('PRIVATE durable intent failure')
        with self.assertRaises(t.TransportError): self.backend.boot_candidate(before_boot=fail)
        self.assertEqual(self.payload_operations(self.calls[before:]), ['read'])
        self.assertFalse(self.backend._candidate_booted)

    def test_complete_effect_callbacks_cannot_reenter_rom_or_mutations(self):
        self.complete_capture()
        self.backend.write(0x10000, self.candidate)
        def callback():
            before = len(self.calls)
            for call in (lambda: self.backend.write(0x10000, self.candidate),
                         lambda: self.backend.read(0xd000, 12288),
                         lambda: self.backend._operation('restart'), lambda: self.backend.hold()):
                with self.assertRaises(t.TransportError): call()
            self.assertEqual(len(self.calls), before)
        self.assertTrue(self.backend.boot_candidate(before_boot=callback))

    def test_complete_final_reset_has_one_seven_region_sweep_then_callback_then_restart(self):
        self.complete_capture()
        events = []
        original_run = self.backend._run
        def child(argv, **kw):
            fields = json.loads(kw['input'])
            events.append((fields['operation'], fields.get('offset')))
            return original_run(argv, **kw)
        self.backend._run = child
        def callback():
            self.assertTrue(self.backend._original_boot_attempted)
            events.append(('durable_original_intent', None))
        self.assertTrue(self.backend.reset_complete_original(before_reset=callback))
        self.assertEqual(events, [('read', span[0]) for span in t.NAMED_SPANS.values()] +
            [('read_bound_state', 0xf00000), ('read_bound_state', 0xf80000),
             ('durable_original_intent', None), ('restart', None)])
        self.assertIsNone(self.backend._restart_kind)

    def test_complete_final_reset_refuses_each_mismatched_region_before_callback(self):
        for name in t.COMPLETE_NAMED_SPANS:
            self.complete_capture()
            span = t.COMPLETE_NAMED_SPANS[name]
            original = self.memory[span]
            self.memory[span] = b'?' * span[1]
            before = len(self.calls)
            callback = []
            with self.assertRaises(t.TransportError):
                self.backend.reset_complete_original(before_reset=lambda: callback.append('called'))
            self.assertFalse(callback)
            self.assertFalse(self.backend._original_boot_attempted)
            self.assertNotIn('restart', self.payload_operations(self.calls[before:]))
            self.memory[span] = original

    def test_complete_original_callback_failure_fences_and_never_restarts(self):
        self.complete_capture()
        before = len(self.calls)
        def fail(): raise OSError('PRIVATE disk failure')
        with self.assertRaises(t.TransportError): self.backend.reset_complete_original(before_reset=fail)
        self.assertEqual(self.payload_operations(self.calls[before:]), ['read'] * 5 + ['read_bound_state'] * 2)
        self.assertTrue(self.backend._original_boot_attempted)
        self.assert_complete_boot_fence()

    def assert_complete_boot_fence(self):
        before = len(self.calls)
        raw = self.complete_original['state_0']
        for call in (lambda: self.backend.write(0xd000, self.complete_original['nvs']),
                     lambda: self.backend.write(0x10000, self.candidate),
                     lambda: self.backend.restore_state_chunk(0xf00000, raw),
                     lambda: self.backend.boot_candidate(before_boot=lambda: None),
                     lambda: self.backend.reset_complete_original(before_reset=lambda: None),
                     lambda: self.backend._operation('write', offset=0xd000, size=12288,
                         data=base64.b64encode(self.complete_original['nvs']).decode()),
                     lambda: self.backend._operation('restore_state', offset=0xf00000, size=524288,
                         data=base64.b64encode(raw).decode(), original_sha256=hashlib.sha256(raw).hexdigest()),
                     lambda: self.backend._operation('restart')):
            with self.assertRaises(t.TransportError): call()
        self.assertEqual(len(self.calls), before)

    def test_complete_uncertain_original_boot_blocks_all_mutation_replays(self):
        self.complete_capture()
        original_run = self.backend._run
        def uncertain(argv, **kw):
            if json.loads(kw['input'])['operation'] == 'restart':
                raise subprocess.TimeoutExpired('PRIVATE original boot', 240)
            return original_run(argv, **kw)
        self.backend._run = uncertain
        with self.assertRaises(t.TransportError): self.backend.reset_complete_original(before_reset=lambda: None)
        self.assertTrue(self.backend._original_boot_attempted)
        self.assertIsNone(self.backend._restart_kind)
        self.assert_complete_boot_fence()

    def test_complete_direct_restart_and_legacy_reset_are_never_authorized(self):
        self.complete_capture()
        before = len(self.calls)
        for call in (lambda: self.backend.reset_original(), lambda: t.Transport.reset_original(self.backend),
                     lambda: self.backend._operation('restart'), lambda: self.backend._operation('restart_capture')):
            with self.assertRaises(t.TransportError): call()
        # The explicitly invoked base reset may read five regions but cannot boot.
        self.assertEqual(self.payload_operations(self.calls[before:]), ['read'] * 5)

    def test_actual_worker_complete_state_operations_use_exact_fixed_halves(self):
        for op in ('read_bound_state', 'read_recovery_state', 'restore_state'):
            for offset in (0xf00000, 0xf80000):
                reply, calls, _ = self.worker_dispatch(op, request={'offset': offset})
                self.assertTrue(reply['ok'])
                self.assertEqual(calls[:2], [t.rom_argv('COM7', ['read-mac']), t.rom_argv('COM7', ['flash-id'])])
                if op == 'restore_state':
                    self.assertEqual(reply, {'ok': True})
                    self.assertEqual(calls[-1], t.rom_argv('COM7', ['write-flash', '--flash-size', '16MB', hex(offset), calls[-1][-1]]))
                else:
                    self.assertEqual(base64.b64decode(reply['data'], validate=True), b'r' * 524288)
                    self.assertEqual(calls[-1], t.rom_argv('COM7', ['read-flash', hex(offset), '524288', calls[-1][-1]]))
                self.assertFalse(list(self.private.glob('ot218-rom-*')))

    def test_actual_worker_complete_state_bad_fields_refuse_before_cli(self):
        for op in ('read_bound_state', 'read_recovery_state', 'restore_state'):
            cases = ({'offset': 0xd000}, {'size': 1048576}, {'size': True}, {'extra': 'PRIVATE'})
            if op == 'restore_state':
                cases += ({'original_sha256': '0' * 64}, {'original_sha256': True}, {'data': '?'},
                          {'data': base64.b64encode(b'r' * 524288).decode()})
            for fields in cases:
                reply, calls, _ = self.worker_dispatch(op, request=fields)
                self.assert_worker_failure(reply, op, 'request', 'guard')
                self.assertFalse(calls)

    def test_complete_state_parent_actual_worker_restore_and_bound_read_roundtrip(self):
        self.complete_capture()
        observed = []
        def child(argv, **kw):
            payload = json.loads(kw['input'])
            def cli(command, ns, default):
                if 'write-flash' in command:
                    self.assertEqual(Path(command[-1]).read_bytes(), self.complete_original['state_0'])
                else: default(command)
            reply, calls, _ = self.worker_dispatch(payload['operation'], request=payload, cli=cli)
            observed.extend(calls)
            return SimpleNamespace(returncode=0, stdout=json.dumps(reply).encode(), stderr=b'')
        self.backend._run = child
        self.assertTrue(self.backend.restore_state_chunk(0xf00000, self.complete_original['state_0']))
        self.assertEqual(self.backend.read_bound_state_chunk(0xf00000, 524288), b'r' * 524288)
        self.assertEqual(len(observed), 6)
        self.assertFalse(list(self.private.glob('ot218-rom-*')))

    def test_complete_state_diagnostics_and_read_reply_limits(self):
        self.complete_capture()
        original_run = self.backend._run
        for operation, stage in (('read_bound_state', 'read_length'), ('restore_state', 'write_flash.cli.teardown')):
            value = dict(operation=operation, stage=stage, category='serial')
            self.backend._run = lambda *args, **kw: self.child_failure(value)
            call = (lambda: self.backend.read_bound_state_chunk(0xf00000, 524288)) if operation == 'read_bound_state' else (
                lambda: self.backend.restore_state_chunk(0xf00000, self.complete_original['state_0']))
            with self.assertRaises(t.TransportError): call()
            self.assertEqual(self.backend.failure_diagnostics[-1], dict(value, source='worker'))
        self.backend._run = original_run
        self.failure = 'short'
        with self.assertRaises(t.TransportError): self.backend.read_bound_state_chunk(0xf00000, 524288)
        self.assertEqual(self.backend.failure_diagnostics[-1]['stage'], 'read_data')
        self.assertNotIn('PRIVATE', json.dumps(self.backend.failure_diagnostics))

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
