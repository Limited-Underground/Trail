"""Actual OT-218 custody engine with deterministic physical-I/O doubles."""
from pathlib import Path
import sys
import tempfile
import unittest
import struct
import hashlib
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import ble_confirmation_trial as trial

PRODUCTION_CANDIDATE = dict(trial.CANDIDATE)
PRODUCTION_PARTITION_SHA = trial.PARTITION_SHA


class Backend:
    def __init__(self, captured):
        self.memory = dict(captured)
        self.operations = []
        self.originals = None
        self.fail_write = None
        self.fail_read = None
        self.fail_reset = False
        self.change_identity = False
    def claim(self, binding):
        self.operations.append('claim')
        return True
    def reverify(self, binding):
        return not self.change_identity
    def read(self, offset, size):
        name = next(k for k, v in trial.SPANS.items() if v == (offset, size))
        if self.fail_read == name:
            raise OSError('private error must not escape')
        return self.memory[name]
    def bind_originals(self, captured):
        if self.originals is not None:
            assert self.originals == captured
        self.originals = dict(captured)
    bind_recovery_originals = bind_originals
    def write(self, offset, raw):
        name = next(k for k, v in trial.SPANS.items() if v[0] == offset)
        self.operations.append('write_' + name)
        self.memory[name] = raw  # Deliberately ambiguous failure after mutation.
        if self.fail_write == len([x for x in self.operations if x.startswith('write_')]):
            raise OSError('ambiguous private write')
    def boot_candidate(self):
        self.operations.append('candidate_boot')
        return True
    def reset_original(self):
        self.operations.append('original_reset')
        return not self.fail_reset and self.memory == self.originals
    def hold(self):
        self.operations.append('hold')


def namespace_image(name, key=None):
    page = bytearray(b'\xff' * 4096)
    struct.pack_into('<II', page, 0, 0xfffffffe, 1)
    page[8] = 0xfe
    struct.pack_into('<I', page, 28, trial.nvs_parser.crc(page[4:28]))
    page[32] = 0xfe
    item = bytearray(b'\xff' * 32)
    item[:4] = bytes([0, 1, 1, 255])
    item[8:24] = name + b'\0' * (16 - len(name))
    item[24] = 1
    struct.pack_into('<I', item, 4, trial.nvs_parser.crc(item[:4] + item[8:]))
    page[64:96] = item
    if key is not None:
        page[32] = 0xfa
        entry = bytearray(b'\xff' * 32)
        entry[:4] = bytes([1, 1, 1, 255])
        entry[8:24] = key + b'\0' * (16 - len(key))
        entry[24] = 1
        struct.pack_into('<I', entry, 4, trial.nvs_parser.crc(entry[:4] + entry[8:]))
        page[96:128] = entry
    return bytes(page) + b'\xff' * 8192


class EngineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Synthetic bytes exercise custody, not candidate firmware acceptance.
        cls.candidate = (b'OT218-HOST-FIXTURE' * 45672)[:730736]
        rows = ((1, 0, 0x9000, 0x2000, b'otadata'),
                (1, 2, 0xd000, 0x3000, b'nvs'),
                (0, 0, 0x10000, 0x4f0000, b'factory'),
                (0, 0x10, 0x500000, 0x500000, b'ota_0'),
                (0, 0x11, 0xa00000, 0x500000, b'ota_1'),
                (0x40, 0, 0xf00000, 0x100000, b'ot_state'))
        entries = b''.join(struct.pack('<HBBII16sI', 0x50aa, kind, subtype,
                            offset, size, name, 0) for kind, subtype, offset, size, name in rows)
        checksum = b'\xeb\xeb' + b'\xff' * 14 + hashlib.md5(entries).digest()
        cls.partition = (entries + checksum).ljust(4096, b'\xff')
    def setUp(self):
        self.bindings = mock.patch.multiple(trial,
            CANDIDATE=trial.descriptor(self.candidate), PARTITION_SHA=trial.sha(self.partition))
        self.bindings.start()
        self.addCleanup(self.bindings.stop)
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / '.private').mkdir()
        self.original = {'bootloader': b'B' * 32768, 'partition': self.partition,
                         'ota': b'\xff' * 8192,
                         'application': b'A' * 589824 + b'T' * (733184 - 589824),
                         'nvs': b'\xff' * 12288}
        self.request = {'schema': 'OT218-BLE-REQUEST-1', 'runtime_sha256': '1' * 64,
                        'device_binding': '2' * 64, 'candidate': dict(trial.CANDIDATE),
                        'protected': {k: trial.descriptor(self.original[k]) for k in ('bootloader', 'partition', 'ota')},
                        'original_prefix': trial.descriptor(self.original['application'][:589824]),
                        'boot_compatibility': 'inferred-ot208-image-header-compatibility', 'case': 'confirm-and-restore'}
        self.backend = Backend(self.original)
        self.observations = 0
    def tearDown(self):
        try:
            self.temp.cleanup()
        finally:
            self.bindings.stop()
    def grant(self, operation='execute', number=1):
        obj = {'schema': 'OT218-BLE-GRANT-1', 'attempt': f'{number:032x}',
               'request_sha256': trial.sha(trial.canonical(self.request)),
               'runtime_sha256': self.request['runtime_sha256'], 'operation': operation,
               'origin_attempt': f'{1:032x}' if operation == 'recover' else None,
               'attempt_count': 1, 'actions': trial.ACTIONS if operation == 'execute' else trial.RECOVERY_ACTIONS,
               'issued_utc': 100, 'expires_utc': 200}
        raw = trial.canonical(obj)
        return raw, trial.sha(raw)
    def observe(self):
        self.observations += 1
        return 'local_confirmed'
    def execute(self, **kwargs):
        return trial.execute(self.root, self.request, *self.grant(), self.candidate,
                             self.backend, self.observe, utc=kwargs.get('utc', lambda: 150))
    def recover(self, number=2):
        return trial.recover(self.root, self.request, *self.grant('recover', number), self.backend, utc=lambda: 150)
    def held(self):
        return (self.root / '.private' / trial.ACTIVE).exists()
    def test_confirm_and_exact_extended_restoration(self):
        result = self.execute()
        self.assertEqual(result['observation'], 'local_confirmed')
        self.assertEqual(self.backend.memory, self.original)
        self.assertEqual(self.observations, 1)
        self.assertFalse(self.held())
        captured = (self.root / '.private' / ('ble-confirmation-' + f'{1:032x}' + '-application.bin')).read_bytes()
        self.assertEqual(captured[-143360:], b'T' * 143360)
    def test_production_candidate_pin_rejects_synthetic_fixture(self):
        self.assertEqual(PRODUCTION_CANDIDATE, {'bytes': 730736,
            'sha256': '28dadebed9c08ed4a52bfe3d38266144da70116943f522a9d7421bb6f915110c'})
        self.assertEqual(PRODUCTION_PARTITION_SHA,
            'b7bbaf702afd377973aa2371f288bcea50548865d10e2cdada4d5e7f98a91601')
        with mock.patch.multiple(trial, CANDIDATE=PRODUCTION_CANDIDATE,
                                 PARTITION_SHA=PRODUCTION_PARTITION_SHA):
            with self.assertRaises(trial.TrialError):
                self.execute()
        self.assertEqual(self.backend.operations, [])

    def test_candidate_byte_drift_rejected_before_claim(self):
        self.candidate = bytes([self.candidate[0] ^ 1]) + self.candidate[1:]
        with self.assertRaises(trial.TrialError):
            self.execute()
        self.assertEqual(self.backend.operations, [])

    def test_consumed_grant_cannot_repeat(self):
        self.execute()
        with self.assertRaises(trial.TrialError):
            self.execute()
        self.assertEqual(self.observations, 1)
    def test_expired_grant_no_claim(self):
        with self.assertRaises(trial.TrialError):
            self.execute(utc=lambda: 200)
        self.assertEqual(self.backend.operations, [])
    def test_ambiguous_candidate_write_restores_without_observer(self):
        self.backend.fail_write = 1
        result = self.execute()
        self.assertTrue(result['restored'])
        self.assertEqual(self.observations, 0)
        self.assertEqual(self.backend.memory, self.original)
    def test_restore_failure_keeps_custody_then_fresh_recovery(self):
        self.backend.fail_write = 2
        with self.assertRaises(trial.TrialError):
            self.execute()
        self.assertTrue(self.held())
        self.backend.fail_write = None
        self.assertTrue(self.recover()['restored'])
        self.assertEqual(self.observations, 1)
        self.assertFalse(self.held())
    def test_reset_failure_keeps_custody(self):
        self.backend.fail_reset = True
        with self.assertRaises(trial.TrialError):
            self.execute()
        self.assertTrue(self.held())
    def test_capture_failure_can_recover_without_candidate(self):
        self.backend.fail_read = 'application'
        with self.assertRaises(trial.TrialError):
            self.execute()
        self.assertTrue(self.held())
        self.backend.fail_read = None
        self.assertTrue(self.recover()['restored'])
        self.assertNotIn('candidate_boot', self.backend.operations)
        self.assertFalse(any(x.startswith('write_') for x in self.backend.operations))
    def test_changed_identity_holds_without_write(self):
        self.backend.change_identity = True
        with self.assertRaises(trial.TrialError):
            self.execute()
        self.assertTrue(self.held())
        self.assertFalse(any(x.startswith('write_') for x in self.backend.operations))
    def test_original_prefix_mismatch_no_candidate(self):
        self.backend.memory['application'] = b'X' + self.original['application'][1:]
        with self.assertRaises(trial.TrialError):
            self.execute()
        self.assertNotIn('write_application', self.backend.operations)
    def test_all_fresh_namespaces_and_reset_are_refused(self):
        for name in trial.NAMESPACES:
            with self.subTest(name=name):
                captured = dict(self.original, nvs=namespace_image(name))
                with self.assertRaises(trial.TrialError):
                    trial.inspect_baseline(self.request, captured)
    def test_old_namespace_is_not_blanket_rejected(self):
        self.assertTrue(trial.inspect_baseline(self.request, dict(self.original, nvs=namespace_image(b'ot198diag'))))
    def test_pending_reset_never_writes_candidate(self):
        self.backend.memory['nvs'] = namespace_image(b'ot_reset_v1', b'intent_v1')
        with self.assertRaises(trial.TrialError):
            self.execute()
        self.assertNotIn('write_application', self.backend.operations)
    def test_empty_reset_namespace_is_safe(self):
        self.assertTrue(trial.inspect_baseline(self.request, dict(self.original, nvs=namespace_image(b'ot_reset_v1'))))
    def test_pending_reset_also_blocks_prewrite_recovery_reset(self):
        self.backend.memory['nvs'] = namespace_image(b'ot_reset_v1', b'record_v1')
        with self.assertRaises(trial.TrialError):
            self.execute()
        with self.assertRaises(trial.TrialError):
            self.recover()
        self.assertTrue(self.held())
        self.assertNotIn('original_reset', self.backend.operations)
    def test_tampered_capture_refuses_recovery(self):
        self.backend.fail_write = 2
        with self.assertRaises(trial.TrialError):
            self.execute()
        capture = self.root / '.private' / ('ble-confirmation-' + f'{1:032x}' + '-application.bin')
        capture.write_bytes(b'X' * 733184)
        with self.assertRaises(trial.TrialError):
            self.recover()
        self.assertTrue(self.held())
    def test_pending_snapshot_preserved_on_recovery(self):
        self.backend.fail_write = 2
        with self.assertRaises(trial.TrialError):
            self.execute()
        pending = self.root / '.private' / ('ble-confirmation-' + f'{1:032x}' + '.pending')
        pending.write_bytes(b'interrupted snapshot')
        self.backend.fail_write = None
        self.assertTrue(self.recover()['restored'])
        self.assertEqual((self.root / '.private' / ('ble-confirmation-' + f'{2:032x}' + '.interrupted')).read_bytes(), b'interrupted snapshot')
    def test_unknown_observation_restores_and_is_failure(self):
        self.observe = lambda: 'Submitted'
        self.assertEqual(self.execute()['observation'], 'trial_failed')
    def test_keyboard_interrupt_observation_restores(self):
        def interrupted():
            raise KeyboardInterrupt()
        self.observe = interrupted
        result = self.execute()
        self.assertTrue(result['restored'])
        self.assertEqual(result['observation'], 'trial_failed')
        self.assertEqual(self.backend.memory, self.original)
        self.assertFalse(self.held())


if __name__ == '__main__':
    unittest.main()
