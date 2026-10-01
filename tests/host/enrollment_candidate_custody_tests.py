"""Six-span custody fault regressions using bounded fake flash; no device IO."""
import hashlib
from collections.abc import MutableMapping
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import enrollment_candidate_custody as custody
from enrollment_candidate_controller import TrialResult


EXPECTED_SPANS = {
    'bootloader': (0, 32768), 'partition': (0x8000, 4096),
    'otadata': (0x9000, 8192), 'nvs': (0xd000, 12288),
    'application': (0x10000, 733184), 'ota0_prefix': (0x500000, 16384),
}
EXPECTED_ORDER = ('nvs', 'ota0_prefix', 'partition', 'otadata',
                  'application', 'bootloader')


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


class Clock:
    def __init__(self):
        self.value = 10.0

    def __call__(self):
        return self.value


class Flash(MutableMapping):
    def __init__(self, originals):
        self.backing = bytearray(b'\xa5') * 16777216
        for name, raw in originals.items():
            self[name] = raw

    def __getitem__(self, name):
        offset, size = EXPECTED_SPANS[name]
        return bytes(self.backing[offset:offset + size])

    def __setitem__(self, name, raw):
        offset, size = EXPECTED_SPANS[name]
        if type(raw) is not bytes or len(raw) != size:
            raise AssertionError('fake flash write escaped its exact span')
        self.backing[offset:offset + size] = raw

    def __delitem__(self, name):
        raise AssertionError('fake flash regions cannot disappear')

    def __iter__(self):
        return iter(EXPECTED_SPANS)

    def __len__(self):
        return len(EXPECTED_SPANS)


class Device:
    def __init__(self, role, images):
        self.role = role
        self.binding = ('a' if role == 'A' else 'b') * 64
        role_byte = 32 if role == 'A' else 64
        self.original = {name: bytes([role_byte + index + 1]) * span[1]
                         for index, (name, span) in enumerate(EXPECTED_SPANS.items())}
        self.flash = Flash(self.original)
        self.images = dict(images)
        self.candidate_layout_sha256 = digest(images['partition'])
        self.sentinel_specs = {0xc000: b'default-NVS gap', 0xc3000: b'application tail',
                               0x4fffff: b'P', 0x504000: b'OTA0 untouched suffix',
                               0x900000: b'OTA1 unchanged', 0xff0000: b'ot_state unchanged'}
        for offset, raw in self.sentinel_specs.items():
            self.flash.backing[offset:offset + len(raw)] = raw
        self.original_sentinels = self.sentinels
        self.mode = 'original'
        self.route = 'selected-' + role

    @property
    def sentinels(self):
        return {offset: bytes(self.flash.backing[offset:offset + len(raw)])
                for offset, raw in self.sentinel_specs.items()}


class Backend:
    """Ownership, exact spans, admission and time remain explicit in the double."""
    def __init__(self, device, log, clock, *, recovery=False):
        self.device, self.log, self.clock = device, log, clock
        self.role = device.role
        self.owned = False
        self.phase = 'restore' if recovery else 'capture'
        self.read_since_write = set()
        self.fault = None
        self.fault_used = False
        self.close_uncertain = False
        self.guard_fault = None
        self.advance = None
        self.attempts = []
        self.expected_route = device.route
        self.claims = 0

    def _attempt(self, action, name, deadline):
        self.attempts.append((self.role, action, name, deadline, self.clock()))

    def _check(self, deadline):
        if not self.clock() < deadline:
            raise RuntimeError('fake operation exceeded deadline')
        if not self.owned or self.device.mode != 'rom':
            raise RuntimeError('fake ROM owner absent')
        if self.device.route != self.expected_route:
            raise RuntimeError('fake selected route changed')

    def _record(self, action, name=None, deadline=None):
        self.log.append((self.role, action, name, deadline))
        if self.advance is not None:
            self.clock.value = self.advance
            self.advance = None

    def _name(self, offset, size):
        matches = [name for name, span in EXPECTED_SPANS.items()
                   if span == (offset, size)]
        if len(matches) != 1:
            raise AssertionError('IO escaped the six exact spans')
        return matches[0]

    def _fault(self, action, name):
        if self.fault_used or self.fault is None:
            return None
        phase, operation, span, kind = self.fault
        if (phase, operation, span) == (self.phase, action, name):
            self.fault_used = True
            return kind
        return None

    def _admission(self):
        values = dict(device_binding=self.device.binding,
                      model='heltec_v4_esp32s3', flash_bytes=16777216,
                      security='verified-write-permitted',
                      layout_sha256=digest(self.device.flash['partition']),
                      boot_selection='verified-factory',
                      rom_held=self.owned and self.device.mode == 'rom')
        if (self.phase == 'restore' and values['layout_sha256'] not in
                (digest(self.device.original['partition']), self.device.candidate_layout_sha256)):
            values['boot_selection'] = 'verified-rom-restore-only'
        if self.guard_fault is not None:
            values.update(self.guard_fault)
        return custody.Admission(**values)

    def claim(self, binding, deadline):
        self._attempt('claim', None, deadline)
        if (binding != self.device.binding or not self.clock() < deadline
                or self.device.route != self.expected_route):
            raise RuntimeError('fake selected device unavailable')
        self.claims += 1
        if self.claims > 1:
            self.phase = 'restore'
        self.device.mode = 'rom'
        self.owned = True
        self._record('claim', deadline=deadline)
        return self._admission()

    def guard(self, binding, deadline):
        self._attempt('guard', None, deadline)
        self._check(deadline)
        if binding != self.device.binding:
            raise RuntimeError('fake selected identity changed')
        self._record('guard', deadline=deadline)
        return self._admission()

    def read(self, offset, size, deadline):
        self._attempt('read', self._name(offset, size), deadline)
        self._check(deadline)
        name = self._name(offset, size)
        self._record('read', name, deadline)
        kind = self._fault('read', name)
        if kind == 'disconnect':
            self.owned = False
            raise RuntimeError('PRIVATE read handle lost')
        raw = self.device.flash[name]
        if kind == 'short':
            return raw[:-1]
        if kind == 'corrupt':
            return bytes([raw[0] ^ 1]) + raw[1:]
        self.read_since_write.add(name)
        return raw

    def write(self, offset, raw, deadline):
        self._attempt('write', self._name(offset, len(raw)), deadline)
        self._check(deadline)
        name = self._name(offset, len(raw))
        restoring = raw == self.device.original[name]
        self._record('restore' if restoring else 'write', name, deadline)
        self.read_since_write.clear()
        kind = self._fault('write', name)
        if kind == 'false_success':
            return True
        if kind in ('partial', 'disconnect'):
            cut = len(raw) // 2
            self.device.flash[name] = raw[:cut] + self.device.flash[name][cut:]
            if kind == 'disconnect':
                self.owned = False
            raise RuntimeError('PRIVATE partial write')
        self.device.flash[name] = raw
        return True

    def boot_candidate(self, deadline):
        self._attempt('boot_candidate', None, deadline)
        self._check(deadline)
        self._record('boot_candidate', deadline=deadline)
        if self.device.flash['application'] != self.device.images['application'].ljust(733184, b'\xff'):
            raise AssertionError('candidate boot without exact application')
        if self.device.flash['partition'] != self.device.images['partition']:
            raise AssertionError('candidate boot without exact partition table')
        if self.device.flash['ota0_prefix'] != b'\xff' * 16384:
            raise AssertionError('unknown OTA0 was not provisioned before candidate boot')
        self.device.mode = 'candidate'
        self.owned = False
        return True

    def hold_rom(self, deadline):
        self._attempt('hold_rom', None, deadline)
        if not self.clock() < deadline:
            raise RuntimeError('fake hold deadline expired')
        self.device.mode = 'rom'
        self.owned = True
        self.phase = 'restore'
        self._record('hold_rom', deadline=deadline)
        return True

    def reset_original(self, deadline):
        self._attempt('reset_original', None, deadline)
        self._check(deadline)
        self._record('reset_original', deadline=deadline)
        if self.device.flash != self.device.original:
            raise AssertionError('original boot while changed bytes or reset intent remain')
        if self.read_since_write != set(EXPECTED_SPANS):
            raise AssertionError('original boot before independent six-span sweep')
        if self.device.sentinels != self.device.original_sentinels:
            raise AssertionError('outside original changed')
        self.device.mode = 'original'
        self.owned = False
        return True

    def close(self):
        self._record('close')
        if self.close_uncertain:
            return False
        self.owned = False
        return True

    def assert_idle(self):
        return not self.owned and not self.close_uncertain


class Controller:
    def __init__(self, devices, log, clock):
        self.devices, self.log, self.clock = devices, log, clock
        self.idle = True
        self.close_uncertain = False
        self.failure = None
        self.advance = None

    def run(self, case, group, deadline):
        self.idle = False
        self.log.append(('controller', 'run', case, deadline))
        for device in self.devices.values():
            if device.mode != 'candidate':
                raise AssertionError('controller ran before both candidate boots')
            device.flash['ota0_prefix'] = b'candidate membership'.ljust(16384, b'\x99')
            device.flash['nvs'] = b'durable reset intent'.ljust(12288, b'\x88')
            device.flash['otadata'] = b'candidate boot metadata'.ljust(8192, b'\x77')
        if self.advance is not None:
            self.clock.value = self.advance
        failure = self.failure
        if self.close_uncertain and failure is None:
            failure = ('cleanup', 'handles_not_closed')
        return TrialResult(case, 'failed' if failure else 'passed', 'traffic',
                           failure, 8, 1, not self.close_uncertain, 4)

    def close(self):
        self.log.append(('controller', 'close', None, None))
        if self.close_uncertain:
            return False
        self.idle = True
        return True

    def assert_idle(self):
        return self.idle


class CustodyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.clock = Clock()
        self.log = []
        self.images = {'application': b'pinned synthetic application',
                       'partition': b'C' * 4096}
        self.devices = {role: Device(role, self.images) for role in ('A', 'B')}
        self.backends = {role: Backend(device, self.log, self.clock)
                         for role, device in self.devices.items()}
        self.controller = Controller(self.devices, self.log, self.clock)
        self.request = {
            'schema': 'OT-CANDIDATE-REQUEST-1', 'runtime_sha256': 'c' * 64,
            'case': 'first', 'group': 7,
            'images': {name: custody.descriptor(raw) for name, raw in self.images.items()},
            'roles': {role: {'device_binding': device.binding,
                             'originals': {name: custody.descriptor(raw)
                                           for name, raw in device.original.items()}}
                      for role, device in self.devices.items()},
        }

    def grant(self, operation='execute', attempt='1' * 32, origin=None,
              *, execute_expires=200, restore_expires=300):
        value = {'schema': 'OT-CANDIDATE-GRANT-1', 'attempt': attempt,
                 'request_sha256': digest(custody.canonical(self.request)),
                 'runtime_sha256': self.request['runtime_sha256'],
                 'operation': operation, 'origin_attempt': origin,
                 'attempt_count': 1, 'actions': custody.actions_for(self.request, operation),
                 'issued_utc': 100, 'execute_expires_utc': execute_expires,
                 'restore_expires_utc': restore_expires}
        raw = custody.canonical(value)
        return raw, digest(raw)

    def execute(self):
        raw, pin = self.grant()
        return custody.execute(self.root, self.request, raw, pin, self.images,
                               self.backends, self.controller, utc=lambda: 150,
                               monotonic=self.clock)

    def recover(self, *, passive_idle=lambda: True):
        backends = {role: Backend(device, self.log, self.clock, recovery=True)
                    for role, device in self.devices.items()}
        raw, pin = self.grant('recover', '2' * 32, '1' * 32)
        return custody.recover(self.root, self.request, raw, pin, backends,
                               passive_idle, utc=lambda: 150, monotonic=self.clock)

    def events(self, action, role=None):
        return [event for event in self.log
                if event[1] == action and (role is None or event[0] == role)]

    def assert_restored(self, result):
        self.assertTrue(result.custody_released)
        self.assertFalse(result.owner_confirmed)
        for role in ('A', 'B'):
            self.assertEqual(self.devices[role].flash, self.devices[role].original)
            self.assertEqual(self.devices[role].sentinels, self.devices[role].original_sentinels)
            self.assertTrue(result.roles[role]['restore_verified'])
            self.assertTrue(result.roles[role]['original_boot_allowed'])
            self.assertTrue(result.roles[role]['handles_closed'])

    def test_exact_six_span_contract_and_nonblank_originals(self):
        self.assertEqual(custody.SPANS, EXPECTED_SPANS)
        self.assertEqual(custody.RESTORE_ORDER, EXPECTED_ORDER)
        self.assertNotEqual(self.devices['A'].original['ota0_prefix'], b'\xff' * 16384)
        self.assertNotEqual(self.devices['A'].original['ota0_prefix'],
                            self.devices['B'].original['ota0_prefix'])

    def test_capture_barrier_sequential_install_and_verified_restore_order(self):
        result = self.execute()
        self.assert_restored(result)
        self.assertIsNone(result.first_failure)
        first_write = next(index for index, event in enumerate(self.log) if event[1] == 'write')
        for role in ('A', 'B'):
            captured = [event[2] for event in self.log[:first_write]
                        if event[0] == role and event[1] == 'read']
            self.assertTrue(all(captured.count(name) >= 2 for name in EXPECTED_SPANS))
        a_boot = self.log.index(self.events('boot_candidate', 'A')[0])
        b_first_write = self.log.index(self.events('write', 'B')[0])
        self.assertLess(a_boot, b_first_write)
        passive_close = self.log.index(self.events('close', 'controller')[-1])
        a_final_close = self.log.index(self.events('close', 'A')[-1])
        b_restore_begin = self.log.index(self.events('hold_rom', 'B')[0])
        self.assertLess(a_final_close, b_restore_begin)
        for role in ('A', 'B'):
            restored_names = [event[2] for event in self.events('restore', role)]
            # Candidate bootloader is unchanged: its order slot verifies rather than writes.
            self.assertEqual(restored_names, list(EXPECTED_ORDER[:-1]))
            self.assertLess(passive_close, self.log.index(self.events('hold_rom', role)[0]))
            last_write = self.log.index(self.events('restore', role)[-1])
            reset = self.log.index(self.events('reset_original', role)[0])
            swept = [event[2] for event in self.log[last_write + 1:reset]
                     if event[0] == role and event[1] == 'read']
            self.assertEqual(swept[-6:], list(EXPECTED_SPANS))
            restore_events = self.events('restore', role)
            for index, event in enumerate(restore_events):
                start = self.log.index(event)
                end = (self.log.index(restore_events[index + 1])
                       if index + 1 < len(restore_events) else reset)
                self.assertTrue(any(row[0] == role and row[1] == 'read' and row[2] == event[2]
                                    for row in self.log[start + 1:end]))

    def test_capture_failure_on_each_node_span_prevents_candidate_mutation(self):
        for role in ('A', 'B'):
            for name in EXPECTED_SPANS:
                with self.subTest(role=role, span=name):
                    self.setUp()
                    self.backends[role].fault = ('capture', 'read', name, 'short')
                    result = self.execute()
                    self.assertIsNotNone(result.first_failure)
                    self.assertFalse(self.events('write'))
                    self.assertFalse(self.events('boot_candidate'))
                    self.assertFalse(self.events('run'))

    def test_candidate_partial_and_false_success_do_not_boot_failed_node(self):
        for role in ('A', 'B'):
            for name in ('application', 'partition', 'ota0_prefix'):
                for kind in ('partial', 'false_success'):
                    with self.subTest(role=role, span=name, fault=kind):
                        self.setUp()
                        self.backends[role].fault = ('capture', 'write', name, kind)
                        result = self.execute()
                        self.assert_restored(result)
                        self.assertIsNotNone(result.first_failure)
                        self.assertFalse(self.events('boot_candidate', role))
                        self.assertFalse(self.events('run'))

    def test_uncertain_passive_close_forbids_all_later_rom_operations(self):
        self.controller.close_uncertain = True
        result = self.execute()
        self.assertFalse(result.custody_released)
        after = self.log.index(self.events('close', 'controller')[-1])
        self.assertFalse(any(event[1] in ('hold_rom', 'read', 'restore', 'reset_original')
                             for event in self.log[after + 1:]))
        self.log.clear()
        with self.assertRaises(custody.Error):
            self.recover(passive_idle=lambda: False)
        self.assertFalse(self.events('claim'))

    def test_restore_partial_false_success_and_handle_loss_keep_custody(self):
        for name in EXPECTED_ORDER[:-1]:
            for kind in ('partial', 'false_success', 'disconnect'):
                with self.subTest(span=name, fault=kind):
                    self.setUp()
                    self.backends['A'].fault = ('restore', 'write', name, kind)
                    result = self.execute()
                    self.assertFalse(result.custody_released)
                    self.assertIsNotNone(result.first_failure)
                    self.assertFalse(self.events('reset_original', 'A'))
                    self.assertTrue(self.events('reset_original', 'B'))

    def test_handle_loss_at_each_restore_read_never_boots_unverified_original(self):
        for name in EXPECTED_ORDER:
            with self.subTest(span=name):
                self.setUp()
                self.backends['A'].fault = ('restore', 'read', name, 'disconnect')
                result = self.execute()
                self.assertFalse(result.custody_released)
                self.assertFalse(self.events('reset_original', 'A'))
                self.assertTrue(self.events('reset_original', 'B'))

    def test_fresh_recovery_restores_only_unresolved_node_without_candidate(self):
        self.backends['A'].fault = ('restore', 'write', 'application', 'partial')
        first = self.execute()
        self.assertFalse(first.custody_released)
        self.assertTrue(self.events('reset_original', 'B'))
        self.images.clear()
        self.log.clear()
        recovered = self.recover()
        self.assert_restored(recovered)
        self.assertFalse(any(event[0] == 'B' and event[1] in ('claim', 'hold_rom', 'restore', 'reset_original')
                             for event in self.log))
        self.assertFalse(self.events('write'))
        self.assertFalse(self.events('boot_candidate'))
        self.assertFalse(self.events('run'))
        self.assertEqual(recovered.first_failure, first.first_failure)

    def test_failed_trial_first_failure_survives_restore_failure_and_recovery(self):
        self.controller.failure = ('status_B', 'target_refused')
        self.backends['A'].fault = ('restore', 'write', 'nvs', 'partial')
        result = self.execute()
        self.assertEqual(result.first_failure, self.controller.failure)
        self.assertFalse(result.custody_released)
        recovered = self.recover()
        self.assert_restored(recovered)
        self.assertEqual(recovered.first_failure, self.controller.failure)

    def test_typed_identity_layout_security_and_rom_guards_refuse_before_write(self):
        for values in ({'device_binding': 'e' * 64}, {'model': 'other-board'},
                       {'flash_bytes': 8388608}, {'security': 'unknown'},
                       {'layout_sha256': 'f' * 64}, {'boot_selection': 'unknown'},
                       {'rom_held': False}):
            with self.subTest(admission=values):
                self.setUp()
                self.backends['A'].guard_fault = values
                result = self.execute()
                self.assertIsNotNone(result.first_failure)
                self.assertFalse(self.events('write'))
                self.assertFalse(self.events('boot_candidate'))

    def test_expired_case_restores_with_original_fixed_restore_deadline(self):
        self.controller.advance = 61.0
        result = self.execute()
        self.assert_restored(result)
        self.assertIsNotNone(result.first_failure)
        restoration = [event[3] for event in self.log
                       if event[1] in ('hold_rom', 'restore', 'reset_original')]
        self.assertTrue(restoration)
        self.assertEqual(set(restoration), {160.0})

    def test_late_restore_success_cannot_renew_deadline_or_boot_original(self):
        self.controller.advance = 159.0
        self.backends['A'].advance = None
        original_write = self.backends['A'].write

        def late_write(offset, raw, deadline):
            result = original_write(offset, raw, deadline)
            if raw == self.devices['A'].original['nvs']:
                self.clock.value = 161.0
            return result

        self.backends['A'].write = late_write
        result = self.execute()
        self.assertFalse(result.custody_released)
        self.assertFalse(self.events('reset_original'))
        self.assertEqual({event[3] for event in self.events('restore')}, {160.0})
        for backend in self.backends.values():
            self.assertTrue(all(attempt[4] < attempt[3] for attempt in backend.attempts))

    def test_consumed_execute_grant_never_reenters_hardware(self):
        self.assert_restored(self.execute())
        self.log.clear()
        with self.assertRaises(custody.Error):
            self.execute()
        self.assertEqual(self.log, [])

    def test_changed_saved_capture_blocks_first_candidate_write(self):
        read = self.backends['B'].read
        seen = 0

        def tamper_after_second_read(offset, size, deadline):
            nonlocal seen
            raw = read(offset, size, deadline)
            if (offset, size) == EXPECTED_SPANS['ota0_prefix']:
                seen += 1
                if seen == 2:
                    original = custody.Journal(self.root, '1' * 32).original('A', 'ota0_prefix')
                    original.write_bytes(b'bad saved original')
            return raw

        self.backends['B'].read = tamper_after_second_read
        result = self.execute()
        self.assertFalse(result.custody_released)
        self.assertIsNotNone(result.first_failure)
        self.assertFalse(self.events('write'))
        self.assertFalse(self.events('boot_candidate'))

    def test_live_route_drift_blocks_stale_owner_and_fresh_recovery_rebinds(self):
        run = self.controller.run

        def swapped_route(case, group, deadline):
            result = run(case, group, deadline)
            self.devices['A'].route = 'new-selected-A'
            return result

        self.controller.run = swapped_route
        result = self.execute()
        self.assertFalse(result.custody_released)
        self.assertFalse(self.events('restore', 'A'))
        self.assertFalse(self.events('reset_original', 'A'))
        self.assertTrue(self.events('reset_original', 'B'))
        self.log.clear()
        recovered = self.recover()
        self.assert_restored(recovered)
        self.assertTrue(self.events('claim', 'A'))
        self.assertFalse(self.events('claim', 'B'))

    def test_guard_drift_after_valid_capture_prevents_next_mutation(self):
        read = self.backends['B'].read
        seen = 0

        def drift_after_capture(offset, size, deadline):
            nonlocal seen
            raw = read(offset, size, deadline)
            if (offset, size) == EXPECTED_SPANS['ota0_prefix']:
                seen += 1
                if seen == 2:
                    self.backends['A'].guard_fault = {'device_binding': 'e' * 64}
            return raw

        self.backends['B'].read = drift_after_capture
        result = self.execute()
        self.assertFalse(result.custody_released)
        self.assertIsNotNone(result.first_failure)
        self.assertFalse(self.events('write'))
        self.assertFalse(self.events('boot_candidate'))

    def test_journal_barrier_failure_prevents_candidate_write_or_provision(self):
        for name in ('application', 'partition', 'ota0_prefix'):
            with self.subTest(span=name):
                self.setUp()
                save = custody.Journal.save
                failed = False

                def barrier(journal, state):
                    nonlocal failed
                    if (not failed and state['roles']['A']['events']
                            and state['roles']['A']['events'][-1] == 'candidate_' + name + '_intent'):
                        failed = True
                        raise custody.Error('record_failed')
                    return save(journal, state)

                with patch.object(custody.Journal, 'save', barrier):
                    result = self.execute()
                self.assertTrue(failed)
                self.assertIsNotNone(result.first_failure)
                self.assertFalse(any(event[0] == 'A' and event[1] == 'write' and event[2] == name
                                     for event in self.log))
                self.assertFalse(self.events('boot_candidate', 'A'))

    def test_journal_barrier_before_original_reset_holds_custody(self):
        save = custody.Journal.save
        failed = False

        def barrier(journal, state):
            nonlocal failed
            if (not failed and state['roles']['A']['events']
                    and state['roles']['A']['events'][-1] == 'original_reset_intent'):
                failed = True
                raise custody.Error('record_failed')
            return save(journal, state)

        with patch.object(custody.Journal, 'save', barrier):
            result = self.execute()
        self.assertFalse(result.custody_released)
        self.assertFalse(self.events('reset_original', 'A'))
        self.assertTrue(self.events('reset_original', 'B'))
        self.assertTrue((self.root / custody.ACTIVE).exists())

    def test_cold_recovery_refuses_tampered_original_before_device_io(self):
        self.backends['A'].fault = ('restore', 'write', 'nvs', 'partial')
        self.assertFalse(self.execute().custody_released)
        custody.Journal(self.root, '1' * 32).original('A', 'ota0_prefix').write_bytes(b'tampered')
        self.log.clear()
        with self.assertRaises(custody.Error):
            self.recover()
        self.assertEqual(self.log, [])
        self.assertTrue((self.root / custody.ACTIVE).exists())

    def test_cold_recovery_refuses_malformed_and_forged_completion_journals(self):
        for fault in ('truncated', 'wrong_role', 'forged_completion',
                      'cleared_boot_flag', 'cleared_restore_flag'):
            with self.subTest(journal=fault):
                self.setUp()
                self.backends['A'].fault = ('restore', 'write', 'nvs', 'partial')
                self.assertFalse(self.execute().custody_released)
                journal = custody.Journal(self.root, '1' * 32)
                state = custody.decode(journal.file.read_bytes())
                if fault == 'truncated':
                    journal.file.write_bytes(b'{')
                elif fault == 'wrong_role':
                    state['roles']['C'] = state['roles'].pop('A')
                    journal.file.write_bytes(custody.canonical(state))
                elif fault == 'forged_completion':
                    state['roles']['A'].update(restore_verified=True,
                                              original_boot_allowed=True,
                                              handles_closed=True)
                    journal.file.write_bytes(custody.canonical(state))
                else:
                    flag = ('original_boot_allowed' if fault == 'cleared_boot_flag'
                            else 'restore_verified')
                    state['roles']['B'][flag] = False
                    self.devices['B'].flash['nvs'] = b'new product NVS'.ljust(12288, b'\x55')
                    journal.file.write_bytes(custody.canonical(state))
                self.log.clear()
                with self.assertRaises(custody.Error):
                    self.recover()
                self.assertEqual(self.log, [])
                self.assertTrue((self.root / custody.ACTIVE).exists())

    def test_raw_true_owner_confirmation_never_credits_usual_screen_acceptance(self):
        observations = []

        def raw_confirmation(deadline):
            observations.append(deadline)
            self.assertTrue(self.controller.assert_idle())
            self.assertTrue(all(device.mode == 'original' for device in self.devices.values()))
            return True

        self.controller.confirm_original = raw_confirmation
        result = self.execute()
        self.assertTrue(result.custody_released)
        self.assertFalse(result.owner_confirmed)
        self.assertEqual(result.first_failure, ('owner_confirmation', 'owner_confirmation_missing'))
        self.assertEqual(observations, [160.0])

    def test_post_reset_journal_failure_never_rewrites_running_original_nvs(self):
        save = custody.Journal.save

        def barrier(journal, state):
            if 'original_boot_verified' in state['roles']['A']['events']:
                raise custody.Error('record_failed')
            return save(journal, state)

        with patch.object(custody.Journal, 'save', barrier):
            result = self.execute()
        self.assertFalse(result.custody_released)
        self.assertTrue(self.events('reset_original', 'A'))
        self.devices['A'].flash['nvs'] = b'normal firmware new NVS'.ljust(12288, b'\x22')
        self.log.clear()
        recovered = self.recover()
        self.assertFalse(recovered.custody_released)
        self.assertFalse(self.events('restore', 'A'))
        self.assertFalse(self.events('reset_original', 'A'))
        self.assertTrue((self.root / custody.ACTIVE).exists())

    def test_restore_guard_detects_identity_drift_before_first_restore_write(self):
        hold = self.backends['A'].hold_rom

        def swapped_identity(deadline):
            result = hold(deadline)
            self.devices['A'].binding = 'e' * 64
            return result

        self.backends['A'].hold_rom = swapped_identity
        result = self.execute()
        self.assertFalse(result.custody_released)
        self.assertFalse(self.events('restore', 'A'))
        self.assertFalse(self.events('reset_original', 'A'))
        self.assertTrue(self.events('reset_original', 'B'))

    def test_verified_original_boot_with_failed_close_is_reconciled_without_rom_io(self):
        reset = self.backends['A'].reset_original

        def reset_then_uncertain_close(deadline):
            result = reset(deadline)
            self.backends['A'].close_uncertain = True
            return result

        self.backends['A'].reset_original = reset_then_uncertain_close
        result = self.execute()
        self.assertFalse(result.custody_released)
        self.assertTrue(result.roles['A']['original_boot_allowed'])
        new_nvs = b'new product data'.ljust(12288, b'\x44')
        self.devices['A'].flash['nvs'] = new_nvs
        self.log.clear()
        recovered = self.recover()
        self.assertTrue(recovered.custody_released)
        self.assertEqual(self.devices['A'].flash['nvs'], new_nvs)
        self.assertFalse(any(event[0] == 'A' and event[1] in
                             ('claim', 'guard', 'read', 'restore', 'hold_rom', 'reset_original')
                             for event in self.log))

    def test_callback_mapping_mutation_cannot_replace_admitted_pair_or_images(self):
        claim = self.backends['A'].claim
        original_request = custody.decode(custody.canonical(self.request))
        original_b = self.backends['B']
        replacement = Backend(Device('B', self.images), self.log, self.clock)

        def mutated_caller(binding, deadline):
            self.request['roles']['B']['device_binding'] = 'e' * 64
            self.images['application'] = b'changed after admission'
            self.backends['B'] = replacement
            return claim(binding, deadline)

        self.backends['A'].claim = mutated_caller
        result = self.execute()
        self.assert_restored(result)
        self.assertTrue(original_b.claims)
        self.assertFalse(replacement.claims)
        state = custody.decode(custody.Journal(self.root, '1' * 32).file.read_bytes())
        self.assertEqual(state['request'], original_request)

    def test_os_lease_rejects_cross_process_recovery_and_releases_after_close(self):
        child = subprocess.Popen(
            [sys.executable, '-I', '-S', '-B', str(Path(__file__).resolve()),
             '--custody-lease-child', str(self.root)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
        ready, release = self.root / 'lease-child-ready', self.root / 'lease-child-release'
        try:
            deadline = time.monotonic() + 10
            while not ready.exists() and child.poll() is None and time.monotonic() < deadline:
                time.sleep(.02)
            self.assertTrue(ready.exists(), 'fake child never acquired custody lease')
            with self.assertRaisesRegex(custody.Error, '^controller_busy$'):
                self.recover()
            self.assertEqual(self.log, [])
            release.write_bytes(b'release fake child')
            stdout, stderr = child.communicate(timeout=15)
            self.assertEqual(child.returncode, 0, stdout + stderr)
            self.assertEqual(stdout.strip(), 'fake child custody released')
            self.assertFalse((self.root / custody.ACTIVE).exists())
            raw, pin = self.grant(attempt='3' * 32)
            result = custody.execute(self.root, self.request, raw, pin, self.images,
                                     self.backends, self.controller, utc=lambda: 150,
                                     monotonic=self.clock)
            self.assert_restored(result)
        finally:
            release.write_bytes(b'release fake child')
            if child.poll() is None:
                try:
                    child.communicate(timeout=15)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.communicate(timeout=5)


def lease_child(root):
    fixture = CustodyTests('test_exact_six_span_contract_and_nonblank_originals')
    fixture.setUp()
    fixture.root = Path(root)
    run = fixture.controller.run

    def paused_case(case, group, deadline):
        (fixture.root / 'lease-child-ready').write_bytes(b'fake child owns lease')
        limit = time.monotonic() + 15
        while not (fixture.root / 'lease-child-release').exists():
            if time.monotonic() >= limit:
                raise RuntimeError('fake child release deadline')
            time.sleep(.02)
        return run(case, group, deadline)

    fixture.controller.run = paused_case
    try:
        result = fixture.execute()
        if not result.custody_released:
            raise RuntimeError('fake child custody did not release')
        print('fake child custody released')
    finally:
        fixture.doCleanups()


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--custody-lease-child':
        lease_child(sys.argv[2])
    else:
        unittest.main()
