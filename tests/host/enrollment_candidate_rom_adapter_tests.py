"""Actual ROM worker/adapter tests with synthetic USB, SDK, flash and clocks.

No fixture enumerates, opens, resets or writes physical hardware. Synthetic
images and board-profile receipts are not authenticated device evidence.
"""
from pathlib import Path
import base64
import copy
import csv
import errno
import hashlib
import importlib
import io
import json
import struct
import subprocess
import sys
import tempfile
from types import MethodType, ModuleType, SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import enrollment_candidate_rom_adapter as adapter
import enrollment_candidate_controller as controller
import ble_confirmation_trial as maintained

DISPATCH = {}
exec(compile(adapter.WORKER_LOGIC, '<actual ROM worker dispatch>', 'exec'), DISPATCH)
execute_request = DISPATCH['execute_request']
WorkerError = DISPATCH['WorkerError']

# Verbatim ESPROM.flash_block from the pinned esptool 5.3.1 loader.py,
# lines 1130-1151. Execute this SDK method with a transport double so a lost
# FLASH_DATA acknowledgement discriminates the worker's module-global bound.
SDK_FLASH_BLOCK = '''\
def flash_block(self, data, seq, timeout=DEFAULT_TIMEOUT, encrypted=False):
    """Write block to flash, retry if fail"""

    operation = "encrypted " if encrypted else ""
    for attempts_left in range(WRITE_BLOCK_ATTEMPTS - 1, -1, -1):
        try:
            self.check_command(
                f"write {operation}to target flash after seq {seq}",
                self.ESP_CMDS["FLASH_DATA"],
                struct.pack("<IIII", len(data), seq, 0, 0) + data,
                self.checksum(data),
                timeout=timeout,
            )
            break
        except FatalError:
            if attempts_left:
                self.trace(
                    f"{operation}block write failed, "
                    f"retrying with {attempts_left} attempts left...".capitalize()
                )
            else:
                raise
'''


class FakeFatalError(RuntimeError):
    pass


def table_from_csv(path):
    """Encode source CSV, independently of the worker's expected layout maps."""
    kind_names = {'app': 0, 'data': 1}
    subtypes = {'factory': 0, 'ota': 0, 'nvs': 2, 'ota_0': 16, 'ota_1': 17}
    rows = []
    for row in csv.reader(line for line in path.read_text().splitlines()
                          if line.strip() and not line.lstrip().startswith('#')):
        name, kind, subtype, offset, size, flags = (part.strip() for part in row)
        rows.append(struct.pack('<HBBII16sI', 0x50aa,
            kind_names[kind] if kind in kind_names else int(kind, 0),
            subtypes[subtype] if subtype in subtypes else int(subtype, 0),
            int(offset, 0), int(size, 0), name.encode(), int(flags or '0', 0)))
    raw = b''.join(rows)
    return (raw + b'\xeb\xeb' + b'\xff' * 14 + hashlib.md5(raw).digest()).ljust(4096, b'\xff')


def pending_reset_nvs():
    # Structurally valid live namespace/key, parsed by the maintained helper.
    page = bytearray(b'\xff' * 4096)
    struct.pack_into('<II', page, 0, 0xfffffffe, 1)
    page[8] = 0xfe
    struct.pack_into('<I', page, 28, maintained.nvs_parser.crc(page[4:28]))
    page[32] = 0xfa
    for at, namespace, key in ((64, 0, b'ot_reset_v1'), (96, 1, b'intent_v1')):
        item = bytearray(b'\xff' * 32)
        item[:4] = bytes((namespace, 1, 1, 255))
        item[8:24] = key.ljust(16, b'\0')
        item[24] = 1
        struct.pack_into('<I', item, 4, maintained.nvs_parser.crc(item[:4] + item[8:]))
        page[at:at + 32] = item
    return bytes(page) + b'\xff' * 8192


class FakeClock:
    def __init__(self):
        self.value = 10.0
    def __call__(self):
        return self.value


class FakeSDK:
    """Transport double only; all acceptance/dispatch lives in production."""
    def __init__(self, originals, clock):
        self.memory, self.clock = dict(originals), clock
        self.trace, self.handles = [], []
        self.loader = ModuleType('synthetic_transport_pinned_loader')
        self.loader.__dict__.update(WRITE_BLOCK_ATTEMPTS=3, DEFAULT_TIMEOUT=3,
                                   FatalError=FakeFatalError, struct=struct)
        exec(compile(SDK_FLASH_BLOCK, '<pinned esptool5.3.1 flash_block>', 'exec'), self.loader.__dict__)
        sdk = self
        class ResetStrategy:
            def __init__(self, port): self.port = port
            def __call__(self):
                raise AssertionError('SDK retrying reset wrapper was not replaced')
            def reset(self):
                sdk.trace.append(('entry-reset',))
                if sdk.entry_reset_fault: raise OSError(sdk.entry_reset_fault, 'synthetic reset fault')
        class HardReset(ResetStrategy):
            def __init__(self, port, uses_usb, flow_control):
                super().__init__(port)
                assert uses_usb is False and flow_control is False
            def reset(self):
                sdk.trace.append(('reset', 'hard-reset'))
                if sdk.reset_fault: raise OSError(sdk.reset_fault, 'synthetic reset fault')
        self.reset = SimpleNamespace(ResetStrategy=ResetStrategy, HardReset=HardReset)
        self.entry_reset_fault, self.reset_fault, self.force_clear_fault = None, None, None
        self.force_mask, self.jtag, self.otg, self.flow = 0, True, False, False
        self.mac = '123456789abc'
        self.ports = [SimpleNamespace(vid=0x303a, pid=0x1001,
                                     serial_number=self.mac, device='COM19')]
        self.rom_fields = {}
        self.security = {'flags': 0, 'chip_id': 9, 'flash_crypt_cnt': 0}
        self.secure_boot, self.encrypted, self.download_disabled = 0, False, 0
        self.flash_size, self.flash_kind = '16MB', 0
        self.write_fault, self.close_fault, self.read_fault = None, False, None
        self.route_mutation = None
        self.calls = 0

    def comports(self):
        self.calls += 1
        if self.route_mutation and self.calls == self.route_mutation[0]:
            self.ports[0].device = self.route_mutation[1]
        return list(self.ports)

    def rom(self, port, baud, tracing):
        self.trace.append(('connect-construct', port, baud, tracing))
        sdk = self
        class ROM:
            CHIP_NAME = 'ESP32-S3'
            IS_STUB = False
            sync_stub_detected = False
            secure_download_mode = False
            RTC_CNTL_OPTION1_REG = 0x60008000
            RTC_CNTL_FORCE_DOWNLOAD_BOOT_MASK = 4
            def __init__(self):
                self._port = SimpleNamespace(is_open=True, close=self.close)
                self.flash_block = MethodType(sdk.loader.flash_block, self)
                self.ESP_CMDS = {'FLASH_DATA': 3}
                self.__dict__.update(sdk.rom_fields)
            def close(self):
                sdk.trace.append(('close',))
                if not sdk.close_fault:
                    self._port.is_open = False
            def connect(self, mode, attempts):
                sdk.trace.append(('connect', mode, attempts))
                sdk.reset.ResetStrategy(self._port)()
            def get_security_info(self, cache):
                sdk.trace.append(('security', cache))
                return dict(sdk.security)
            def get_secure_boot_enabled(self): return sdk.secure_boot
            def get_flash_encryption_enabled(self): return sdk.encrypted
            def get_encrypted_download_disabled(self): return sdk.download_disabled
            def read_mac(self, source):
                sdk.trace.append(('mac', source))
                return tuple(bytes.fromhex(sdk.mac))
            def flash_type(self): return sdk.flash_kind
            def uses_usb_jtag_serial(self): return sdk.jtag
            def uses_usb_otg(self): return sdk.otg
            def uses_hardware_flow_control(self): return sdk.flow
            def write_reg(self, address, value, mask):
                sdk.trace.append(('force-clear', address, value, mask))
                if sdk.force_clear_fault: raise OSError('synthetic register failure')
            def read_reg(self, address): return sdk.force_mask
            def checksum(self, raw): return 0
            def trace(self, message): sdk.trace.append(('sdk-retry-trace',))
            def check_command(self, message, command, payload, checksum, timeout):
                sdk.trace.append(('FLASH_DATA', command))
                raise FakeFatalError('synthetic lost block acknowledgement')
        esp = ROM()
        self.handles.append(esp)
        return esp

    def attach_flash(self, esp):
        self.trace.append(('attach',))
    def detect_flash_size(self, esp): return self.flash_size
    def read_flash(self, esp, offset, size, **options):
        self.trace.append(('read', offset, size, options))
        name = next((n for n, span in adapter.SPANS.items() if span == (offset, size)), None)
        if name is None:
            raise AssertionError('outside exact approved spans')
        if self.read_fault == name:
            return self.memory[name][:-1]
        return self.memory[name]
    def write_flash(self, esp, pairs, **options):
        self.trace.append(('write', pairs[0][0], len(pairs[0][1]), options,
                           esp.WRITE_FLASH_ATTEMPTS, self.loader.WRITE_BLOCK_ATTEMPTS))
        assert len(pairs) == 1
        offset, raw = pairs[0]
        name = next(n for n, span in adapter.SPANS.items() if span == (offset, len(raw)))
        if self.write_fault == 'lost-block-ack':
            esp.flash_block(raw[:1024], 0)
        if self.write_fault == 'partial':
            self.memory[name] = raw[:10] + self.memory[name][10:]
            raise OSError('synthetic private SDK failure')
        if self.write_fault != 'false-success':
            self.memory[name] = raw
    def reset_chip(self, esp, mode):
        self.trace.append(('reset', mode))


class Fixture:
    def __init__(self, case='first', nvs=None):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.private = self.root / '.private'
        self.private.mkdir()
        self.clock = FakeClock()
        self.originals = {'bootloader': b'B' * 32768,
            'partition': table_from_csv(ROOT / 'firmware/targets/heltec_v4_bench/partitions.csv'),
            'otadata': b'\xff' * 8192, 'nvs': b'\xff' * 12288 if nvs is None else nvs,
            'application': b'O' * 733184, 'ota0_prefix': b'R' * 16384}
        self.images = {'application': b'HOST-ONLY-CANDIDATE' * 127,
            'partition': table_from_csv(ROOT / 'firmware/targets/heltec_v4_enrollment_candidate_eval/partitions.csv')}
        self.key = b'K' * 32
        self.binding = adapter.opaque_identity(self.key, '123456789abc')
        self.request = {'schema': 'OT-CANDIDATE-REQUEST-1', 'runtime_sha256': '1' * 64,
            'case': case, 'group': 7,
            'images': {k: adapter.descriptor(v) for k, v in self.images.items()},
            'roles': {role: {'device_binding': binding,
                'originals': {k: adapter.descriptor(v) for k, v in self.originals.items()}}
                for role, binding in [('A', self.binding), ('B', 'b' * 64)]}}
        self.manifest = {'worktree': str(self.root), 'root': str(self.private / 'fake-capsule'), 'files': {}}
        self.sdk = FakeSDK(self.originals, self.clock)
        self.runner_calls, self.verifier_calls = [], []
        self.answer_fault, self.runner_fault = None, None
        self.runtime = adapter.Runtime(self.private / 'manifest.json', '1' * 64, self.private,
            subprocess_run=self.runner, manifest_verifier=self.verifier, monotonic=self.clock)
        self.lease = adapter.HardwareLease(self.private, monotonic=self.clock)
        self.profile = adapter.DeviceProfile('heltec_v4_esp32s3', self.binding, 16777216, 'a' * 64)
        self.backend = self.make_backend()

    def make_backend(self, **overrides):
        args = dict(request=self.request, role='A', images=self.images,
            binding_key=self.key, expected_identity='123456789abc', device_profile=self.profile)
        args.update(overrides)
        return adapter.ROMBackend(self.runtime, self.lease, **args)
    def verifier(self, path, pin):
        self.verifier_calls.append((path, pin))
        return copy.deepcopy(self.manifest)
    def runner(self, argv, **kwargs):
        self.runner_calls.append((argv, kwargs))
        if self.runner_fault == 'timeout':
            raise subprocess.TimeoutExpired(argv, kwargs['timeout'])
        q = json.loads(kwargs['input'])
        try:
            answer = execute_request(q, self.sdk.rom, self.sdk, self.sdk.comports,
                                     self.clock, loader=self.sdk.loader, reset=self.sdk.reset)
            result = SimpleNamespace(returncode=0, stdout=json.dumps(answer).encode(), stderr=b'')
        except BaseException:
            result = SimpleNamespace(returncode=1, stdout=b'{"ok":false,"closed":false}', stderr=b'')
        if self.answer_fault:
            result = self.answer_fault(result)
        if self.runner_fault == 'late': self.clock.value = q['deadline']
        if self.runner_fault == 'rollback': self.clock.value -= 1
        return result
    def q(self, operation, **kwargs):
        return {'schema': 'OT-CANDIDATE-ROM-WORKER-1', 'deadline': 100.0,
            'identity': '123456789abc', 'operation': operation, 'mode': 'normal',
            'originals': self.request['roles']['A']['originals'],
            'candidate': {k: adapter.descriptor(v) for k, v in self.backend._candidate.items()}, **kwargs}
    def worker(self, operation, **kwargs):
        return execute_request(self.q(operation, **kwargs), self.sdk.rom,
                               self.sdk, self.sdk.comports, self.clock,
                               loader=self.sdk.loader, reset=self.sdk.reset)
    def capture(self):
        for _ in range(2):
            for offset, size in adapter.SPANS.values():
                self.backend.read(offset, size, 100)
    def install(self):
        self.backend.claim(self.binding, 100)
        self.capture()
        for name in ('application', 'partition', 'ota0_prefix'):
            self.backend.write(adapter.SPANS[name][0], self.backend._candidate[name], 100)
    def release_app(self):
        token = self.lease.begin_passive(self.binding, 1, 100)
        handle = SimpleNamespace(is_open=False, port='COM19')
        self.lease.attach_passive(token, handle, 'COM19')
        assert self.lease.release_passive(token, handle)
    def cleanup(self):
        # Test-only OS lock cleanup for deliberately unresolved synthetic owners.
        if self.lease._file is not None:
            self.lease._file.close()
            self.lease._file = None
        self.temp.cleanup()


class ROMAdapterTests(unittest.TestCase):
    def fixture(self, **kwargs):
        f = Fixture(**kwargs)
        self.addCleanup(f.cleanup)
        return f

    def test_import_and_constructors_are_inert_and_profiles_are_separate(self):
        with mock.patch('subprocess.run', side_effect=AssertionError('unexpected child')):
            importlib.reload(adapter)
            f = self.fixture()
        self.assertEqual(f.runner_calls + f.verifier_calls + f.sdk.trace, [])
        self.assertIsNone(f.lease._file)
        for profile in (adapter.DeviceProfile('ESP32-S3', f.binding, 16777216, 'a' * 64),
                        adapter.DeviceProfile('heltec_v4_esp32s3', 'c' * 64, 16777216, 'a' * 64),
                        adapter.DeviceProfile('heltec_v4_esp32s3', f.binding, 8388608, 'a' * 64),
                        adapter.DeviceProfile('heltec_v4_esp32s3', f.binding, 16777216, 'bad')):
            with self.subTest(profile=profile), self.assertRaises(adapter.AdapterError):
                f.make_backend(device_profile=profile)
        with self.assertRaises(adapter.AdapterError):
            f.make_backend(expected_identity='223456789abc')
        self.assertEqual(f.runner_calls, [])

    def test_actual_worker_route_is_passive_and_swaps_are_refused(self):
        f = self.fixture()
        self.assertEqual(f.worker('route')['route'], 'COM19')
        self.assertEqual(f.sdk.trace, [])
        for bad in ('serial', 'vid', 'pid', 'duplicate', 'port'):
            g = self.fixture()
            if bad == 'serial': g.sdk.ports[0].serial_number = '223456789abc'
            if bad == 'vid': g.sdk.ports[0].vid = 0
            if bad == 'pid': g.sdk.ports[0].pid = 0
            if bad == 'duplicate': g.sdk.ports.append(copy.copy(g.sdk.ports[0]))
            if bad == 'port': g.sdk.ports[0].device = 'COM0'
            with self.subTest(bad=bad), self.assertRaises(WorkerError): g.worker('guard')
            self.assertEqual(g.sdk.trace, [])
        f.sdk.mac = '223456789abc'
        with self.assertRaises(WorkerError): f.worker('guard')
        self.assertFalse(f.sdk.handles[-1]._port.is_open)
        self.assertFalse(any(e[0] in ('write', 'reset') for e in f.sdk.trace))

    def test_chip_flash_stub_security_and_download_restrictions_are_fresh(self):
        mutations = [('CHIP_NAME', 'ESP32'), ('IS_STUB', True),
            ('sync_stub_detected', True), ('secure_download_mode', True),
            ('flash_size', '8MB'), ('flash_kind', 1), ('secure_boot', 1),
            ('encrypted', True), ('download_disabled', 1)]
        mutations += [('flags', 1 << bit) for bit in (0, 2, 8, 9, 10)]
        mutations += [('chip_id', 8), ('flash_crypt_cnt', 1), ('flags', 1 << 11)]
        for name, value in mutations:
            f = self.fixture()
            if name in ('CHIP_NAME', 'IS_STUB', 'sync_stub_detected', 'secure_download_mode'):
                f.sdk.rom_fields[name] = value
            elif name in f.sdk.security: f.sdk.security[name] = value
            else: setattr(f.sdk, name, value)
            with self.subTest(name=name, value=value), self.assertRaises(WorkerError): f.worker('guard')
            self.assertEqual(sum(e[0] == 'close' for e in f.sdk.trace), 1)
            self.assertFalse(any(e[0] in ('write', 'reset') for e in f.sdk.trace))
        good = self.fixture()
        good.worker('guard'); good.worker('guard')
        self.assertEqual([e for e in good.sdk.trace if e[0] == 'security'], [('security', False)] * 2)
        self.assertEqual([e for e in good.sdk.trace if e[0] == 'connect'], [('connect', 'default-reset', 1)] * 2)

    def test_layout_boot_selector_and_damage_only_allow_bounded_restore(self):
        for change in ('unknown-table', 'bad-flags', 'selected-ota', 'md5'):
            f = self.fixture()
            if change == 'selected-ota': f.sdk.memory['otadata'] = b'X' * 8192
            else:
                raw = bytearray(f.originals['partition'])
                raw[28 if change == 'bad-flags' else 200 if change == 'md5' else 0] ^= 1
                f.sdk.memory['partition'] = bytes(raw)
                if change in ('bad-flags', 'md5'):
                    f.request['roles']['A']['originals']['partition'] = adapter.descriptor(bytes(raw))
            with self.subTest(change=change), self.assertRaises(WorkerError): f.worker('guard')
            restored = f.worker('guard', mode='restore-only') if change in ('unknown-table', 'selected-ota') else None
            if restored:
                self.assertEqual(restored['admission']['boot_selection'], 'verified-rom-restore-only')
                with self.assertRaises(WorkerError): f.worker('reset_original', mode='restore-only')
            self.assertFalse(any(e[0] == 'reset' for e in f.sdk.trace))

    def test_exact_six_spans_and_protected_bootloader_write_refusal(self):
        f = self.fixture()
        for name, (offset, size) in adapter.SPANS.items():
            got = f.worker('read', span=name)
            self.assertEqual(base64.b64decode(got['data']), f.originals[name])
            self.assertIn(('read', offset, size, {'output': None, 'flash_size': 'keep', 'no_progress': True}), f.sdk.trace)
        for bad in ('bootloader', 'ota_1', 'ot_state', 'outside'):
            with self.subTest(span=bad), self.assertRaises(WorkerError):
                f.worker('write', mode='restore-only', span=bad, write_kind='restore',
                         data=base64.b64encode(f.originals['bootloader']).decode())
        self.assertFalse(any(e[0] == 'write' for e in f.sdk.trace))

    def test_real_runtime_dispatch_argv_environment_and_reply_validation(self):
        f = self.fixture()
        with mock.patch.dict('os.environ', {'PYTHONPATH': 'unsafe', 'ESPTOOL_PORT': 'unsafe'}):
            formatted_mac = ':'.join(f.sdk.mac[i:i + 2] for i in range(0, len(f.sdk.mac), 2)).upper()
            self.assertEqual(f.runtime.route(formatted_mac, 100), 'COM19')
        argv, kwargs = f.runner_calls[-1]
        self.assertEqual(argv[1:5], ['-I', '-S', '-B', '-c'])
        self.assertEqual(argv[5], adapter.WORKER)
        self.assertEqual(kwargs['timeout'], 90)
        self.assertNotIn('PYTHONPATH', kwargs['env']); self.assertNotIn('ESPTOOL_PORT', kwargs['env'])
        self.assertEqual(kwargs['env']['ESPTOOL_CFGFILE'], str(Path(f.manifest['root']) / 'esptool.cfg'))
        self.assertTrue(f.runtime.assert_idle())
        corruptions = [lambda r: SimpleNamespace(returncode=0, stdout=b'{"ok":true,"closed":false,"route":"COM19"}', stderr=b''),
            lambda r: SimpleNamespace(returncode=0, stdout=b'{"ok":true,"closed":true,"route":"COM19","extra":0}', stderr=b''),
            lambda r: SimpleNamespace(returncode=0, stdout=b'private not JSON', stderr=b''),
            lambda r: SimpleNamespace(returncode=0, stdout=b'X' * 1100001, stderr=b''),
            lambda r: SimpleNamespace(returncode=0, stdout=r.stdout, stderr=b'X' * 1048577)]
        for fault in corruptions:
            f.answer_fault = fault
            with self.subTest(fault=fault), self.assertRaises(adapter.AdapterError): f.runtime.route(f.sdk.mac, 100)
            self.assertTrue(f.runtime.assert_idle())
        with self.assertRaisesRegex(adapter.AdapterError, 'isolated_caller_required'):
            f.runtime.serial_api(100)

    def test_runtime_deadlines_rollback_and_timeout_do_not_promote_results(self):
        for fault in ('late', 'rollback', 'timeout'):
            f = self.fixture(); f.runner_fault = fault
            with self.subTest(fault=fault), self.assertRaises((adapter.AdapterError, controller.ControllerError)):
                f.runtime.route(f.sdk.mac, 100)
            self.assertTrue(f.runtime.assert_idle())
            self.assertEqual(len(f.runner_calls), 1)
        f = self.fixture(); f.clock.value = 100
        with self.assertRaises(adapter.AdapterError): f.runtime.route(f.sdk.mac, 100)
        self.assertEqual(f.runner_calls, [])
        f = self.fixture(); f.manifest['worktree'] = str(f.private)
        with self.assertRaises(adapter.AdapterError): f.runtime.route(f.sdk.mac, 100)
        self.assertEqual(f.runner_calls, [])

    def test_worker_fresh_routes_close_and_deadlines_fail_closed(self):
        for fault in ('route', 'short-read', 'close', 'expired', 'rollback'):
            f = self.fixture()
            if fault == 'route': f.sdk.route_mutation = (5, 'COM20')
            if fault == 'short-read': f.sdk.read_fault = 'partition'
            if fault == 'close': f.sdk.close_fault = True
            if fault == 'expired': f.clock.value = 100
            clock = f.clock
            if fault == 'rollback':
                values = iter([10, 10, 9]); clock = lambda: next(values, 9)
            with self.subTest(fault=fault), self.assertRaises(WorkerError):
                execute_request(f.q('guard'), f.sdk.rom, f.sdk, f.sdk.comports, clock,
                                loader=f.sdk.loader, reset=f.sdk.reset)
            self.assertFalse(any(e[0] in ('write', 'reset') for e in f.sdk.trace))

    def test_capture_barrier_safe_write_flags_readback_and_full_restore(self):
        f = self.fixture(); f.backend.claim(f.binding, 100)
        with self.assertRaises(adapter.AdapterError):
            f.backend.write(adapter.SPANS['application'][0], f.backend._candidate['application'], 100)
        f.capture()
        for name in ('application', 'partition', 'ota0_prefix'):
            f.backend.write(adapter.SPANS[name][0], f.backend._candidate[name], 100)
        self.assertTrue(f.backend.boot_candidate(100))
        with self.assertRaises(adapter.AdapterError): f.backend.boot_candidate(100)
        f.backend.claim(f.binding, 200)
        self.assertTrue(f.backend._restore_only)
        with self.assertRaises(adapter.AdapterError): f.backend.boot_candidate(200)
        for name in ('application', 'ota0_prefix', 'nvs', 'otadata', 'partition'):
            f.backend.write(adapter.SPANS[name][0], f.originals[name], 200)
        for name, (offset, size) in adapter.SPANS.items():
            self.assertEqual(f.backend.read(offset, size, 200), f.originals[name])
        self.assertTrue(f.backend.reset_original(200)); self.assertTrue(f.lease.close())
        writes = [e for e in f.sdk.trace if e[0] == 'write']
        self.assertEqual(len(writes), 8)
        self.assertNotIn(0, [e[1] for e in writes])
        expected = dict(flash_size='keep', flash_mode='keep', flash_freq='keep',
            no_compress=True, no_progress=True, force=False, erase_all=False,
            encrypt=False, ignore_flash_enc_efuse=False)
        self.assertTrue(all(e[3] == expected and e[4:] == (1, 1) for e in writes))
        self.assertEqual([e for e in f.sdk.trace if e[0] == 'reset'], [('reset', 'hard-reset')] * 2)

    def test_uncertain_or_false_success_write_never_retries_and_reclaims_restore_only(self):
        for fault in ('partial', 'false-success', 'lost-block-ack'):
            f = self.fixture(); f.backend.claim(f.binding, 100); f.capture()
            f.sdk.write_fault = fault
            with self.subTest(fault=fault), self.assertRaises(adapter.AdapterError):
                f.backend.write(adapter.SPANS['application'][0], f.backend._candidate['application'], 100)
            self.assertTrue(f.backend._mutated)
            self.assertEqual(sum(e[0] == 'write' for e in f.sdk.trace), 1)
            if fault == 'lost-block-ack':
                self.assertEqual([e for e in f.sdk.trace if e[0] == 'FLASH_DATA'], [('FLASH_DATA', 3)])
                self.assertFalse(any(e[0] == 'sdk-retry-trace' for e in f.sdk.trace))
            self.assertEqual(sum(e[0] == 'reset' for e in f.sdk.trace), 0)
            f.sdk.write_fault = None
            f.backend.claim(f.binding, 200)
            with self.assertRaises(adapter.AdapterError):
                f.backend.write(adapter.SPANS['ota0_prefix'][0], f.backend._candidate['ota0_prefix'], 200)
            f.backend.write(adapter.SPANS['application'][0], f.originals['application'], 200)
            self.assertEqual(sum(e[0] == 'write' for e in f.sdk.trace), 2)

    def test_default_nvs_marker_blocks_candidate_and_restored_original_reset(self):
        unsafe = pending_reset_nvs()
        with self.assertRaises(maintained.TrialError): maintained.safe_reset_marker(unsafe)
        f = self.fixture(nvs=unsafe)
        with self.assertRaisesRegex(adapter.AdapterError, 'pending_reset_storage'):
            f.backend.claim(f.binding, 100)
        self.assertFalse(any(e[0] in ('write', 'reset') for e in f.sdk.trace))
        with self.assertRaisesRegex(adapter.AdapterError, 'pending_reset_storage'):
            f.backend.boot_candidate(100)
        g = self.fixture(nvs=unsafe)
        g.backend = g.make_backend(recovery_only=True)
        g.backend.claim(g.binding, 100)
        with self.assertRaisesRegex(adapter.AdapterError, 'pending_reset_storage'):
            g.backend.reset_original(100)
        self.assertFalse(any(e[0] == 'reset' for e in f.sdk.trace + g.sdk.trace))

    def test_initial_boot_requires_blank_prefix_but_warm_restart_preserves_prepared_bytes(self):
        for case in ('retained_rekey', 'recovery_after_A_commit', 'recovery_after_B_commit'):
            f = self.fixture(case=case); f.install()
            f.sdk.memory['ota0_prefix'] = b'P' * 16384
            with self.subTest(case=case), self.assertRaises(WorkerError): f.worker('boot_candidate')
            f.sdk.memory['ota0_prefix'] = f.backend._candidate['ota0_prefix']
            f.backend.boot_candidate(100); f.release_app()
            retained = b'PREPARED-STATE'.ljust(16384, b'\x7f')
            f.sdk.memory['ota0_prefix'] = retained
            writes = sum(e[0] == 'write' for e in f.sdk.trace)
            self.assertTrue(f.backend.restart_candidate(100))
            self.assertEqual(f.sdk.memory['ota0_prefix'], retained)
            self.assertEqual(sum(e[0] == 'write' for e in f.sdk.trace), writes)
            self.assertEqual(sum(e[0] == 'reset' for e in f.sdk.trace), 2)
            before = len(f.runner_calls)
            with self.assertRaisesRegex(adapter.AdapterError, 'restart_not_admitted'):
                f.backend.restart_candidate(100)
            self.assertEqual(len(f.runner_calls), before)

    def test_warm_restart_case_ownership_deadline_identity_and_protected_pins(self):
        for fault in ('case', 'active', 'uncertain-close', 'extended', 'expired', 'identity', 'app', 'nvs', 'bootloader', 'otadata', 'table'):
            f = self.fixture(case='first' if fault == 'case' else 'retained_rekey')
            f.install(); f.backend.boot_candidate(100)
            if fault in ('active', 'uncertain-close'):
                token = f.lease.begin_passive(f.binding, 1, 100)
                handle = SimpleNamespace(is_open=False, port='COM19')
                f.lease.attach_passive(token, handle, 'COM19'); handle.is_open = True
                if fault == 'uncertain-close':
                    self.assertFalse(f.lease.release_passive(token, handle))
                    handle.is_open = False
                    self.assertFalse(f.lease.release_passive(token, handle))
            else: f.release_app()
            if fault == 'expired': f.clock.value = 100
            if fault == 'identity': f.sdk.mac = '223456789abc'
            region = {'app': 'application', 'nvs': 'nvs', 'bootloader': 'bootloader',
                      'otadata': 'otadata', 'table': 'partition'}.get(fault)
            if region: f.sdk.memory[region] = b'X' * len(f.sdk.memory[region])
            before_resets = sum(e[0] == 'reset' for e in f.sdk.trace)
            with self.subTest(fault=fault), self.assertRaises((adapter.AdapterError, controller.ControllerError)):
                f.backend.restart_candidate(101 if fault == 'extended' else 100)
            self.assertEqual(sum(e[0] == 'reset' for e in f.sdk.trace), before_resets)

    def test_shared_os_lease_command_exclusion_and_passive_handoff(self):
        f = self.fixture(); f.backend.claim(f.binding, 100)
        second = adapter.HardwareLease(f.private, monotonic=f.clock)
        with self.assertRaisesRegex(adapter.AdapterError, 'hardware_lease_busy'): second.acquire(100)
        f.lease._command.acquire()
        try:
            with self.assertRaisesRegex(adapter.AdapterError, 'hardware_lease_busy'): f.backend.guard(f.binding, 100)
        finally: f.lease._command.release()
        f.lease.mark_boot(f.binding)
        token = f.lease.begin_passive(f.binding, 1, 100)
        handle = SimpleNamespace(is_open=False, port='COM19')
        f.lease.attach_passive(token, handle, 'COM19'); handle.is_open = True
        with self.assertRaisesRegex(adapter.AdapterError, 'passive_owner_active'):
            f.lease.enter_rom(f.binding, 100)
        self.assertFalse(f.lease.release_passive(token, handle))
        handle.is_open = False
        self.assertFalse(f.lease.release_passive(token, handle))
        self.assertFalse(f.lease.assert_idle()); self.assertFalse(f.lease.close())
        with self.assertRaisesRegex(adapter.AdapterError, 'passive_owner_active'):
            f.lease.enter_rom(f.binding, 100)

    def test_runtime_pin_drift_and_repeat_claim_cannot_renew_execution(self):
        f = self.fixture(); f.runtime.manifest_sha256 = '2' * 64
        with self.assertRaisesRegex(adapter.AdapterError, 'runtime_binding_invalid'):
            f.make_backend()
        self.assertEqual(f.runner_calls, [])
        f.runtime.manifest_sha256 = '1' * 64
        f.backend.claim(f.binding, 100)
        before = len(f.runner_calls)
        f.runtime.manifest_sha256 = '2' * 64
        with self.assertRaisesRegex(adapter.AdapterError, 'runtime_binding_invalid'):
            f.backend.guard(f.binding, 100)
        self.assertEqual(len(f.runner_calls), before)
        f.runtime.manifest_sha256 = '1' * 64
        f.backend.claim(f.binding, 200)
        self.assertTrue(f.backend._restore_only)
        with self.assertRaises(adapter.AdapterError): f.backend.boot_candidate(200)
        with self.assertRaisesRegex(adapter.AdapterError, 'deadline_extended'):
            f.backend.guard(f.binding, 201)

    def test_actual_strict_reset_refuses_reopen_retry_and_force_clear_failure(self):
        for error in (errno.EIO, errno.EINVAL, errno.ENOTTY):
            f = self.fixture(); f.sdk.entry_reset_fault = error
            with self.subTest(entry_error=error), self.assertRaises(OSError): f.worker('guard')
            self.assertEqual(sum(e[0] == 'entry-reset' for e in f.sdk.trace), 1)
            self.assertEqual(sum(e[0] == 'close' for e in f.sdk.trace), 1)
        called = []
        closed = SimpleNamespace(port=SimpleNamespace(is_open=False), reset=lambda: called.append('reset'))
        with self.assertRaises(WorkerError): DISPATCH['strict_reset'](closed, lambda: called.append('check'))
        self.assertEqual(called, ['check'])
        for fault in ('reset-eio', 'reset-einval', 'reset-enotty', 'clear-write', 'clear-mask', 'jtag', 'otg', 'flow'):
            f = self.fixture(); f.install()
            if fault.startswith('reset-'):
                f.sdk.reset_fault = {'reset-eio': errno.EIO, 'reset-einval': errno.EINVAL,
                                     'reset-enotty': errno.ENOTTY}[fault]
            if fault == 'clear-write': f.sdk.force_clear_fault = True
            if fault == 'clear-mask': f.sdk.force_mask = 4
            if fault == 'jtag': f.sdk.jtag = False
            if fault == 'otg': f.sdk.otg = True
            if fault == 'flow': f.sdk.flow = True
            with self.subTest(fault=fault), self.assertRaises(adapter.AdapterError): f.backend.boot_candidate(100)
            self.assertEqual(sum(e[0] == 'reset' for e in f.sdk.trace), int(fault.startswith('reset-')))
            before = len(f.runner_calls)
            with self.assertRaises(adapter.AdapterError): f.backend.boot_candidate(100)
            self.assertEqual(len(f.runner_calls), before)


if __name__ == '__main__':
    unittest.main(verbosity=2)
