"""Diagnostic transparency and privacy over the real solicited byte pipeline."""
import importlib.util
import json
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import noise_xk_retry_diagnostics as diagnostics
import noise_xk_solicited_runner as runner
from noise_xk_solicited_runtime import SolicitedReconnectableEndpoint

spec = importlib.util.spec_from_file_location('_retry_diagnostic_fixture',
    ROOT / 'tests/host/noise_xk_solicited_runner_tests.py')
assert spec and spec.loader
fixture = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = fixture
spec.loader.exec_module(fixture)


def run_fixture(f, observer=None):
    opened = []
    try:
        for board in (f.a, f.b):
            endpoint = SolicitedReconnectableEndpoint('anonymous-' + board.label, board.factory,
                monotonic=f.clock.monotonic, sleep=f.clock.sleep)
            opened.append(observer.wrap(endpoint, board.label) if observer else endpoint)
        return runner.run(*opened, token_factory=f.token)
    finally:
        for endpoint in reversed(opened): endpoint.close()


def corrupt_retry(f, role, kind, field, value):
    board = f.a if role == 'A' else f.b
    original = board.receipt
    seen = []
    def receipt(actual_kind, fields):
        current = board.current or {}
        if (not seen and actual_kind == kind
                and (fields.get('scenario') or current.get('scenario')) == 'retry-m2-withheld'):
            fields = {**fields, field: value}
            seen.append(actual_kind)
        return original(actual_kind, fields)
    board.receipt = receipt
    return seen


class Stub:
    def __init__(self, result=None, error=None):
        self.result, self.error, self.calls = result, error, []
    def invoke(self, *args, **kwargs):
        self.calls.append((args, kwargs))
        if self.error is not None: raise self.error
        return self.result
    write_command = expect = query_ready = reopen = close = invoke


class Tests(unittest.TestCase):
    def test_timeout_arrives_only_after_asynchronous_2196ms_expiry(self):
        f = fixture.Fixture()
        emitted = []
        waits = []
        for board in (f.a, f.b):
            original_rx_start = board.emit_rx_start
            original_timeout = board.emit_timeout
            original_factory = board.factory
            pending = {'start': None, 'due': None, 'scheduled': False}

            def rx_start(message, start, deadline, *, board=board, pending=pending,
                         original=original_rx_start):
                original(message, start, deadline)
                if message == 'm2' and board.current['scenario'] == 'retry-m2-withheld':
                    self.assertEqual(deadline, 2196)
                    pending.update(start=f.clock.now, due=f.clock.now + 2.196, scheduled=False)

            def schedule(*, board=board, pending=pending):
                self.assertIsNotNone(pending['due'])
                self.assertLess(f.clock.now, pending['due'])
                self.assertFalse(any(r.kind == 'TIMEOUT' for r in board.queue))
                pending['scheduled'] = True

            def factory(*, board=board, pending=pending, make=original_factory,
                        emit=original_timeout):
                handle = make()
                original_read = handle.readline
                def read(size):
                    if pending['scheduled']:
                        self.assertFalse(any(r.kind == 'TIMEOUT' for r in board.queue))
                        if not board.queue and not handle.fragments:
                            before = f.clock.now
                            f.clock.now = min(pending['due'], f.clock.now + 0.05)
                            waits.append((board.label, before, f.clock.now))
                            if f.clock.now >= pending['due']:
                                emit()
                                # The asynchronous fake fires exactly at expiry,
                                # not at the baseline fake's immediate call or
                                # its extra ten-millisecond scheduling allowance.
                                receipt = board.queue[-1]
                                self.assertEqual(receipt.kind, 'TIMEOUT')
                                receipt.fields['timeout_us'] = str(int(receipt.fields['start_us']) + 2196000)
                                receipt.fields['measured_us'] = '2196000'
                                emitted.append((board.label, pending['start'], f.clock.now))
                                pending['scheduled'] = False
                            else:
                                return b''
                    return original_read(size)
                handle.readline = read
                return handle

            board.emit_rx_start = rx_start
            board.emit_timeout = schedule
            board.factory = factory

        observer = diagnostics.RetryDiagnostics(512)
        actual = run_fixture(f, observer)
        expected = run_fixture(fixture.Fixture())
        self.assertEqual(runner.canonical_bytes(actual), runner.canonical_bytes(expected))
        self.assertEqual([role for role, _, _ in emitted], ['A', 'B'])
        for _, start, end in emitted: self.assertAlmostEqual(end - start, 2.196, places=9)
        self.assertGreater(len(waits), 20)
        timeout_rows = [r for r in observer.snapshot() if r.get('receipt_kind') == 'TIMEOUT']
        self.assertEqual(len(timeout_rows), 2)
        self.assertTrue(all(r['fields']['measured_us'] == 2196000 for r in timeout_rows))
        self.assertEqual(actual['totals']['fragments'], 14)
        self.assertEqual(actual['totals']['radio_payload_wire_bytes'], 736)

    def test_normal_composition_result_is_byte_identical_fourteen_frames(self):
        original = run_fixture(fixture.Fixture())
        observer = diagnostics.RetryDiagnostics(512)
        f = fixture.Fixture()
        actual = run_fixture(f, observer)
        self.assertEqual(runner.canonical_bytes(actual), runner.canonical_bytes(original))
        self.assertEqual(actual['totals']['fragments'], 14)
        self.assertEqual(actual['totals']['radio_payload_wire_bytes'], 736)
        self.assertTrue(runner.validate_public_result(actual))
        rows = observer.snapshot()
        self.assertTrue(any(r.get('receipt_kind') == 'WITHHELD' for r in rows))
        self.assertTrue(any(r.get('receipt_kind') == 'TIMEOUT' for r in rows))
        self.assertTrue(all(r['phase'] != 'failed' for r in rows))
        encoded = json.dumps(rows)
        for node in (f.a, f.b):
            for command in node.commands:
                for token in command.split()[1:3]:
                    if len(token) in (16, 32): self.assertNotIn(token, encoded)
        for cycle in actual['cycles']:
            for scenario in ('baseline', 'bounded_retry'):
                for frame in cycle[scenario]['frames']:
                    self.assertNotIn(frame['payload_sha256'], encoded)

    def test_every_retry_failure_retains_exact_error_and_distinct_last_substep(self):
        cases = [('A', 'PREPARED', 'accepted', 'no'), ('B', 'PREPARED', 'accepted', 'no'),
                 ('B', 'RX_START', 'deadline_ms', '1'), ('A', 'TX_ARM', 'uses', '2'),
                 ('A', 'TX_START', 'wire', '999'), ('A', 'TX_DONE', 'result', '-1'),
                 ('A', 'RX_START', 'deadline_ms', '1'), ('B', 'RX', 'accepted', 'no'),
                 ('B', 'STAGE_ACCEPT', 'stage', '0'), ('B', 'TX_ARM', 'uses', '2'),
                 ('B', 'WITHHELD', 'transmitted', 'yes'),
                 ('A', 'TIMEOUT', 'forced', 'no'), ('B', 'ABORT', 'wiped', 'no')]
        boundaries = set()
        for role, kind, field, value in cases:
            with self.subTest(role=role, kind=kind):
                plain = fixture.Fixture(); first = corrupt_retry(plain, role, kind, field, value)
                with self.assertRaises(runner.RunnerError) as original: run_fixture(plain)
                observed = fixture.Fixture(); second = corrupt_retry(observed, role, kind, field, value)
                observer = diagnostics.RetryDiagnostics()
                with self.assertRaises(runner.RunnerError) as wrapped: run_fixture(observed, observer)
                self.assertEqual(first, [kind]); self.assertEqual(second, [kind])
                self.assertIs(type(wrapped.exception), type(original.exception))
                self.assertEqual(wrapped.exception.args, original.exception.args)
                self.assertEqual(wrapped.exception.stage, 'cycle1_retry_timeout')
                last = [r for r in observer.snapshot() if r['operation'] == 'expect'][-1]
                self.assertEqual((last['role'], last['kind'], last['phase']), (role, kind, 'succeeded'))
                self.assertEqual(last['receipt_kind'], kind)
                self.assertEqual(last['fields'][field], int(value) if field in ('wire', 'result', 'deadline_ms', 'stage', 'uses') else value)
                boundaries.add((last['role'], last['kind']))
        self.assertEqual(len(boundaries), len(cases))

    def test_all_method_returns_arguments_and_exception_objects_preserved(self):
        calls = [('write_command', ('status',), {}), ('expect', ('TIMEOUT', 42), {}),
                 ('query_ready', (), {'timeout_ms': 99}), ('reopen', (), {}), ('close', (), {})]
        for method, args, kwargs in calls:
            for error in (None, RuntimeError('private-detail'), KeyboardInterrupt('private-interrupt')):
                observer = diagnostics.RetryDiagnostics()
                result = (format(7, '032x'), SimpleNamespace(kind='READY', fields={'accepted': 'yes'}))
                stub = Stub(result, error); wrapped = observer.wrap(stub, 'A')
                if error is None:
                    self.assertIs(getattr(wrapped, method)(*args, **kwargs), result)
                else:
                    with self.assertRaises(type(error)) as caught: getattr(wrapped, method)(*args, **kwargs)
                    self.assertIs(caught.exception, error)
                    self.assertEqual(observer.snapshot()[-1]['error'], 'operation_failed')
                self.assertEqual(stub.calls, [(args, kwargs)])

    def test_untrusted_fields_raw_strings_identifiers_and_digests_are_not_exported(self):
        secret = 'synthetic-private-value'
        digest = 'a' * 64
        receipt = SimpleNamespace(kind=secret, fields={
            'raw': secret, 'address': secret, 'device_id': secret, 'session_hash': digest,
            'attempt_hash': digest, 'payload_sha256': digest, 'tx_key_sha256': digest,
            'rx_key_sha256': digest, 'challenge': secret, 'accepted': secret,
            'scenario': secret, 'role': secret, 'message': secret, 'result': secret,
            'wire': secret, 'deadline_us': secret, 'tx_sent': secret})
        observer = diagnostics.RetryDiagnostics()
        wrapped = observer.wrap(Stub(receipt), 'B')
        self.assertIs(wrapped.expect(secret, 1000), receipt)
        wrapped.write_command(secret + ' ' + digest)
        snapshot = observer.snapshot(); encoded = json.dumps(snapshot)
        self.assertNotIn(secret, encoded); self.assertNotIn(digest, encoded)
        self.assertEqual(snapshot[1]['kind'], 'OTHER')
        self.assertEqual(snapshot[1]['fields'], {})
        self.assertEqual(snapshot[-1]['command'], 'other')

    def test_numeric_enum_flag_projection_is_strict_and_bounded(self):
        fields = {'accepted': 'no', 'message': 'm2', 'role': 'R', 'scenario': 'retry-m2-withheld',
                  'wire': '48', 'start_us': str(2**64 - 1), 'done_us': str(2**64),
                  'measured_us': '-1', 'deadline_us': '9' * 1000, 'tx_sent': 1,
                  'rx_restart': '-32768', 'last_radio': '32768', 'result': '+0'}
        observer = diagnostics.RetryDiagnostics()
        observer.wrap(Stub(SimpleNamespace(kind='TIMEOUT', fields=fields)), 'A').expect('TIMEOUT', 1000)
        self.assertEqual(observer.snapshot()[-1]['fields'], {
            'accepted': 'no', 'message': 'm2', 'role': 'R', 'scenario': 'retry-m2-withheld',
            'wire': 48, 'start_us': 2**64 - 1, 'rx_restart': -32768})

    def test_ring_capacity_deep_copy_and_invalid_roles(self):
        observer = diagnostics.RetryDiagnostics(8)
        wrapped = observer.wrap(Stub(SimpleNamespace(kind='STATUS', fields={'tx_sent': '0'})), 'A')
        for _ in range(50): wrapped.expect('STATUS', 1)
        self.assertEqual(len(observer.snapshot()), 8)
        snapshot = observer.snapshot(); snapshot[-1]['fields']['tx_sent'] = 123
        self.assertEqual(observer.snapshot()[-1]['fields']['tx_sent'], 0)
        for capacity in (0, 7, 513, True, 8.0):
            with self.assertRaises(ValueError): diagnostics.RetryDiagnostics(capacity)
        for role in ('C', 'private', None):
            with self.assertRaises(ValueError): observer.wrap(Stub(), role)

    def test_instrumentation_cannot_mask_result_when_receipt_access_throws(self):
        class HostileReceipt:
            @property
            def kind(self): raise RuntimeError('private-property')
        result = HostileReceipt()
        observer = diagnostics.RetryDiagnostics()
        self.assertIs(observer.wrap(Stub(result), 'A').expect('TIMEOUT', 1000), result)
        self.assertNotIn('private-property', json.dumps(observer.snapshot()))


if __name__ == '__main__': unittest.main()
