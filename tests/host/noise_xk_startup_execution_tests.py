"""Bounded startup-noise policy in the real concrete execution composition."""
import contextlib
import importlib.util
import json
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import noise_xk_startup_execution as startup

spec = importlib.util.spec_from_file_location('_startup_execution_fixture',
    ROOT / 'tests/host/noise_xk_diagnostic_execution_tests.py')
assert spec and spec.loader
base = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = base
spec.loader.exec_module(base)


class Fixture:
    def __enter__(self):
        self.stack = contextlib.ExitStack()
        proxy = SimpleNamespace(DiagnosticExecutionSession=startup.StartupExecutionSession,
            SOURCE_PATHS=startup.SOURCE_PATHS, freeze_sources=startup.freeze_sources)
        self.stack.enter_context(mock.patch.object(base, 'diagnostic', proxy))
        self.x = self.stack.enter_context(base.Fixture(2))
        old = self.x.f.backend
        module = self.x.session.backend_module
        bindings = tuple(module.RoleBinding(b.role, b.private_route, b.private_identity, b.restore) for b in old._bindings)
        self.x.f.backend = module.BoundBackend(bindings,
            inventory=old._inventory, transport=old._transport, rom_reader=old._rom_reader)
        for route in self.x.f.routes: self.x.f.backend.admit_rom(route)
        self.x.f.events.clear()
        original_open = self.x.f.backend.open_radio_endpoint
        def open_endpoint(route):
            endpoint = original_open(route)
            clock = self.x.f.boards[route].host_clock
            endpoint._monotonic, endpoint._sleep = clock.monotonic, clock.sleep
            endpoint._endpoint._monotonic = clock.monotonic
            return endpoint
        self.x.f.backend.open_radio_endpoint = open_endpoint
        return self.x
    def __exit__(self, *args): return self.stack.__exit__(*args)


def wire_ready(challenge, **changes):
    fields = {'schema': 'OTNXREADY1', 'challenge': challenge, 'accepted': 'yes',
        'stale_selftest': 'yes', 'radio_ready': 'yes', 'idle': 'yes', 'tx': 'no'}
    fields.update(changes)
    return ('OT153 READY ' + ' '.join(k + '=' + v for k, v in fields.items()) + '\n').encode()


def inject_before_ready(raw_factory, *, repeat=False):
    original = base.base.Serial.readline
    def read(handle, size):
        if handle.board.label == 'B' and handle.generation == 1 and (
                repeat or not getattr(handle, '_startup_injected', False)):
            handle._startup_injected = True
            handle.board.host_clock.now += 0.5 if repeat else 0.01
            challenge = next(c.split()[1] for c in reversed(handle.board.commands) if c.startswith('ready '))
            return raw_factory(challenge)
        return original(handle, size)
    return mock.patch.object(base.base.Serial, 'readline', read)


class Tests(unittest.TestCase):
    def test_malformed_recognized_startup_and_stale_ready_allow_exact_fresh_contract(self):
        def noise(challenge):
            stale = format(int(challenge[0], 16) ^ 1, 'x') + challenge[1:]
            return (b'OT153 BOOT schema=\nOT153 STATUS broken\n'
                    + wire_ready(stale) + b'OT153 PROFILE broken=\n')
        with Fixture() as x, inject_before_ready(noise):
            result = x.execute()
            self.assertTrue(result['radio_result_validated'])
            self.assertTrue(result['restoration_complete'])
            self.assertEqual(result['radio_result']['totals']['fragments'], 14)
            self.assertEqual(result['radio_result']['totals']['radio_payload_wire_bytes'], 736)
            self.assertEqual(tuple(x.f.current[r] for r in x.f.routes), x.f.payloads)
            self.assertFalse(x.f.backend._leases)

    def test_bad_fresh_ready_stale_unhealthy_unknown_active_and_reject_fail_and_restore(self):
        cases = [lambda challenge: wire_ready(challenge, idle='no'),
                 lambda challenge: wire_ready(challenge, accepted='no'),
                 lambda challenge: wire_ready(challenge)[:-1] + b' bad=\n',
                 lambda challenge: wire_ready(format(int(challenge[0], 16) ^ 1, 'x') + challenge[1:], tx='yes'),
                 lambda challenge: b'OT153 UNKNOWN malformed=\n',
                 lambda challenge: b'OT153 RX accepted=yes\n',
                 lambda challenge: b'OT153 REJECT command=ready reason=not_idle\n']
        for raw in cases:
            with Fixture() as x, inject_before_ready(raw):
                with self.assertRaises(base.prior.CoordinatorError): x.execute()
                receipt = json.loads(x.c.frozen.EXECUTION_RECEIPT_PATH.read_text())
                self.assertFalse(receipt['radio_result_validated'])
                self.assertEqual(receipt['failure']['stage'], 'restart_boot_contract_b')
                self.assertTrue(receipt['restoration_complete'])
                self.assertEqual(tuple(x.f.current[r] for r in x.f.routes), x.f.payloads)

    def test_post_fresh_profile_malformed_is_not_replaced_by_startup_acceptance(self):
        with Fixture() as x:
            board = x.f.boards[x.f.routes[1]]
            original = board.receipt
            def receipt(kind, fields):
                if kind == 'PROFILE' and len(board.handles) > 1:
                    fields = {'broken': ''}
                return original(kind, fields)
            board.receipt = receipt
            with self.assertRaises(base.prior.CoordinatorError): x.execute()
            result = json.loads(x.c.frozen.EXECUTION_RECEIPT_PATH.read_text())
            self.assertEqual(result['failure']['stage'], 'restart_boot_contract_b')
            self.assertTrue(result['restoration_complete'])
            self.assertFalse(result['radio_result_validated'])

    def test_startup_noise_remains_bounded_by_absolute_deadline(self):
        with Fixture() as x, inject_before_ready(lambda challenge: b'OT153 BOOT broken=\n', repeat=True):
            with self.assertRaises(base.prior.CoordinatorError): x.execute()
            result = json.loads(x.c.frozen.EXECUTION_RECEIPT_PATH.read_text())
            self.assertTrue(result['restoration_complete'])
            failed = [r for r in x.session.snapshot() if r['operation'] == 'query_ready' and r['phase'] == 'failed']
            self.assertEqual(failed[-1]['error'], 'readiness_timeout')
            board = x.f.boards[x.f.routes[1]]
            self.assertLessEqual(sum(c.startswith('ready ') for c in board.commands), 21)

    def test_extended_closure_includes_policy_and_tamper_prevents_io(self):
        self.assertEqual(len(startup.SOURCE_PATHS), 27)
        with Fixture() as x:
            (x.f.root / 'tools/noise_xk_startup_execution.py').write_bytes(b'tampered')
            with self.assertRaises(Exception): x.execute()
            self.assertEqual(x.f.events, [])
            self.assertFalse(x.c.frozen.JOURNAL_PATH.exists())


if __name__ == '__main__': unittest.main()
