"""Real diagnostic execution, source gates and independent restoration; no hardware."""
import contextlib
import dataclasses
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import noise_xk_diagnostic_execution as diagnostic
import noise_xk_solicited_bundle as bundle
import noise_xk_solicited_coordinator as prior

spec = importlib.util.spec_from_file_location('_diagnostic_execution_fixture',
    ROOT / 'tests/host/noise_xk_solicited_execution_tests.py')
assert spec and spec.loader
base = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = base
spec.loader.exec_module(base)


def sha(raw): return hashlib.sha256(raw).hexdigest()


class Fixture:
    def __init__(self, ordinal=1): self.ordinal = ordinal
    def __enter__(self):
        self.stack = contextlib.ExitStack()
        self.session = diagnostic.DiagnosticExecutionSession(self.ordinal, capacity=512)
        self.c = self.session.coordinator
        self.stack.enter_context(mock.patch.object(base, 'c', self.c))
        self.f = self.stack.enter_context(base.Fixture())
        original_open = self.f.backend.open_radio_endpoint
        def open_endpoint(route):
            endpoint = original_open(route)
            clock = self.f.boards[route].host_clock
            endpoint._monotonic, endpoint._sleep = clock.monotonic, clock.sleep
            endpoint._endpoint._monotonic = clock.monotonic
            return endpoint
        self.f.backend.open_radio_endpoint = open_endpoint
        for relative in diagnostic.SOURCE_PATHS:
            path = self.f.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes((ROOT / relative).read_bytes())
        self.paths = {'benchmark': bundle.BENCHMARK_NAME, 'restore_a': 'A.bin', 'restore_b': 'B.bin'}
        self.f.benchmark = self.f.root / self.paths['benchmark']
        self.f.benchmark.write_bytes(b'benchmark')
        self.f.binding = dataclasses.replace(self.f.binding, benchmark_name=self.paths['benchmark'])
        self.f.config = dataclasses.replace(self.f.config, binding=self.f.binding, benchmark_path=self.f.benchmark)
        images = bundle.freeze_images(self.f.root, self.paths)
        artifact = {k: images['images']['benchmark'][k] for k in ('bytes', 'sha256')}
        path = diagnostic.SOURCE_PATHS[0]
        raw_source = (self.f.root / path).read_bytes()
        record = {'schema': 'OT163-SOLICITED-READINESS-BUILD-1',
            'source_inputs': {path: {'bytes': len(raw_source), 'sha256': sha(raw_source)}},
            'artifacts': {bundle.BENCHMARK_NAME: {'a': artifact, 'b': artifact, 'byte_identical': True}}}
        raw = json.dumps(record).encode()
        (self.f.root / bundle.BUILD_RECORD_PATH).write_bytes(raw)
        # CI has no hardware binary. Pin this synthetic build record while
        # exercising the actual provenance/source/image validation functions.
        self.stack.enter_context(mock.patch.object(bundle, 'BUILD_RECORD_SHA256', sha(raw)))
        self.snapshot = {'sources': diagnostic.freeze_sources(self.f.root), 'images': images}
        return self
    def __exit__(self, *args): return self.stack.__exit__(*args)
    def execute(self):
        return self.session.execute(self.f.root, self.snapshot, self.paths,
            self.f.config, self.f.backend, self.f.authority)
    def recover(self):
        return self.session.recover(self.f.root, self.snapshot, self.paths,
            self.f.config, self.f.backend, self.f.authority)


class Tests(unittest.TestCase):
    def test_backend_decorator_uses_exact_endpoint_identity_and_forwards_unchanged(self):
        endpoint_a, endpoint_b = object(), object()
        opened = object()
        backend = mock.Mock()
        backend.open_radio_endpoint.return_value = opened
        observer = mock.Mock()
        wrapped = object()
        observer.wrap.return_value = wrapped
        decorated = diagnostic._ObservedBackend(
            backend, (endpoint_a, endpoint_b), observer)

        descriptor = object()
        image = object()
        decorated.verify_role(endpoint_a, 'A', descriptor)
        decorated.write_application(endpoint_b, 0x10000, image)
        decorated.verify_application(endpoint_a, 0x10000, image)
        decorated.hard_reset(endpoint_b)
        self.assertIs(decorated.open_radio_endpoint(endpoint_b), wrapped)

        backend.verify_role.assert_called_once_with(endpoint_a, 'A', descriptor)
        backend.write_application.assert_called_once_with(endpoint_b, 0x10000, image)
        backend.verify_application.assert_called_once_with(endpoint_a, 0x10000, image)
        backend.hard_reset.assert_called_once_with(endpoint_b)
        backend.open_radio_endpoint.assert_called_once_with(endpoint_b)
        observer.wrap.assert_called_once_with(opened, 'B')

        equal_but_distinct = mock.MagicMock()
        equal_but_distinct.__eq__.return_value = True
        with self.assertRaisesRegex(RuntimeError, '^diagnostic_endpoint_mismatch$'):
            decorated.open_radio_endpoint(equal_but_distinct)
        backend.open_radio_endpoint.assert_called_once_with(endpoint_b)

    def test_real_full_result_and_both_distinct_restorations(self):
        with Fixture() as x:
            result = x.execute()
            self.assertTrue(result['radio_result_validated'])
            self.assertTrue(result['restoration_complete'])
            self.assertEqual(result['radio_result']['totals']['fragments'], 14)
            self.assertEqual(result['radio_result']['totals']['radio_payload_wire_bytes'], 736)
            self.assertTrue(x.c.runner.validate_public_result(result['radio_result']))
            self.assertEqual(tuple(x.f.current[r] for r in x.f.routes), x.f.payloads)
            for role, descriptor in (('A', x.f.binding.restore_a), ('B', x.f.binding.restore_b)):
                self.assertIn(('write', role, descriptor.sha256), x.f.events)
                self.assertIn(('verify', role, descriptor.sha256), x.f.events)
            self.assertFalse(x.f.backend._leases)
            self.assertTrue(all(not h.is_open for b in x.f.boards.values() for h in b.handles))
            serialized = json.dumps(x.session.snapshot())
            for board in x.f.boards.values():
                for command in board.commands:
                    for token in command.split()[1:3]:
                        if len(token) in (16, 32): self.assertNotIn(token, serialized)
            for cycle in result['radio_result']['cycles']:
                for scenario in ('baseline', 'bounded_retry'):
                    for frame in cycle[scenario]['frames']:
                        self.assertNotIn(frame['payload_sha256'], serialized)

    def test_retry_rejection_keeps_error_receipt_safe_snapshot_and_restores(self):
        with Fixture() as x:
            board = x.f.boards[x.f.routes[1]]
            original = board.receipt
            def receipt(kind, fields):
                if kind == 'WITHHELD': fields = {**fields, 'transmitted': 'yes'}
                return original(kind, fields)
            board.receipt = receipt
            with self.assertRaises(prior.CoordinatorError) as error: x.execute()
            self.assertIs(type(error.exception), prior.CoordinatorError)
            receipt = json.loads(x.c.frozen.EXECUTION_RECEIPT_PATH.read_text())
            self.assertEqual(receipt['result'], 'noise_xk_radio_execution_aborted')
            self.assertTrue(receipt['restoration_complete'])
            self.assertFalse(receipt['radio_result_validated'])
            self.assertEqual(receipt['failure']['stage'], 'cycle1_retry_timeout')
            last = [r for r in x.session.snapshot() if r['operation'] == 'expect'][-1]
            self.assertEqual((last['role'], last['receipt_kind']), ('B', 'WITHHELD'))
            self.assertEqual(last['fields']['transmitted'], 'yes')
            self.assertEqual(tuple(x.f.current[r] for r in x.f.routes), x.f.payloads)
            self.assertNotIn('diagnostics', receipt)

    def test_failed_reopen_closes_real_handles_before_restoration(self):
        with Fixture() as x:
            x.f.boards[x.f.routes[0]].fail_reopen = True
            with self.assertRaises(prior.CoordinatorError): x.execute()
            receipt = json.loads(x.c.frozen.EXECUTION_RECEIPT_PATH.read_text())
            self.assertEqual(receipt['failure']['stage'], 'restart_reconnect_a')
            self.assertTrue(receipt['restoration_complete'])
            self.assertFalse(x.f.backend._leases)
            self.assertEqual(tuple(x.f.current[r] for r in x.f.routes), x.f.payloads)

    def test_unconfirmed_close_retains_global_lease_and_blocks_rom_restore(self):
        with Fixture() as x:
            original_close = base.Serial.close
            def close(handle):
                if handle.board.label == 'A' and handle.generation == 0:
                    raise OSError('synthetic private close failure')
                return original_close(handle)
            with mock.patch.object(base.Serial, 'close', close):
                with self.assertRaises(prior.CoordinatorError): x.execute()
            receipt = json.loads(x.c.frozen.EXECUTION_RECEIPT_PATH.read_text())
            self.assertFalse(receipt['restoration_complete'])
            self.assertIn('A', x.f.backend._leases)
            for role, descriptor in (('A', x.f.binding.restore_a), ('B', x.f.binding.restore_b)):
                self.assertNotIn(('write', role, descriptor.sha256), x.f.events)
            last_open = max(i for i, e in enumerate(x.f.events) if e[0] == 'open')
            self.assertFalse(any(e[0] in ('write', 'verify', 'reset') for e in x.f.events[last_open + 1:]))
            self.assertNotIn('synthetic private close failure', json.dumps(x.session.snapshot()))

    def test_source_tamper_rejected_before_backend_or_consumption(self):
        with Fixture() as x:
            (x.f.root / 'tools/noise_xk_retry_diagnostics.py').write_bytes(b'tampered')
            with self.assertRaises(Exception): x.execute()
            self.assertEqual(x.f.events, [])
            self.assertFalse(x.c.frozen.JOURNAL_PATH.exists())
            self.assertEqual(x.session.snapshot(), [])

    def test_namespace_isolated_and_recovery_requires_no_benchmark_or_radio(self):
        prior_paths = (prior.frozen.JOURNAL_PATH, prior.frozen.EXECUTION_RECEIPT_PATH,
                       prior.frozen.RECOVERY_RECEIPT_PATH)
        with Fixture(2) as x:
            self.assertNotEqual(x.c.frozen.JOURNAL_PATH, prior.frozen.JOURNAL_PATH)
            self.assertIn('diagnostic-2', x.c.frozen.JOURNAL_PATH.name)
            x.f.fail_ready, x.f.fail_restore = 'A', 'A'
            with self.assertRaises(prior.CoordinatorError): x.execute()
            self.assertEqual(x.f.current[x.f.routes[1]], x.f.payloads[1])
            x.f.benchmark.unlink()
            x.f.fail_restore = None
            before = len([e for e in x.f.events if e[0] == 'open'])
            recovered = x.recover()
            self.assertTrue(recovered['restoration_complete'])
            self.assertEqual(tuple(x.f.current[r] for r in x.f.routes), x.f.payloads)
            self.assertEqual(len([e for e in x.f.events if e[0] == 'open']), before)
        self.assertEqual(prior_paths, (prior.frozen.JOURNAL_PATH, prior.frozen.EXECUTION_RECEIPT_PATH,
                                     prior.frozen.RECOVERY_RECEIPT_PATH))

    def test_fresh_session_cannot_reuse_consumed_namespace(self):
        with Fixture() as x:
            x.execute()
            journal = x.c.frozen.JOURNAL_PATH.read_bytes()
            events = list(x.f.events)
            fresh = diagnostic.DiagnosticExecutionSession(1)
            other = diagnostic.DiagnosticExecutionSession(2)
            self.assertNotEqual(fresh.coordinator.JOURNAL_NAME, other.coordinator.JOURNAL_NAME)
            with contextlib.ExitStack() as stack:
                for name in ('ROOT', 'PRIVATE_ROOT', 'JOURNAL_PATH', 'EXECUTION_RECEIPT_PATH', 'RECOVERY_RECEIPT_PATH'):
                    stack.enter_context(mock.patch.object(fresh.coordinator.frozen, name, getattr(x.c.frozen, name)))
                with self.assertRaises(prior.CoordinatorError):
                    fresh.execute(x.f.root, x.snapshot, x.paths, x.f.config, x.f.backend, x.f.authority)
            self.assertEqual(x.f.events, events)
            self.assertEqual(x.c.frozen.JOURNAL_PATH.read_bytes(), journal)
            self.assertEqual(fresh.snapshot(), [])


if __name__ == '__main__': unittest.main()
