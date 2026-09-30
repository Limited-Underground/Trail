"""Host-only Windows job lifetime checks; no serial/USB/ADB imports or calls."""
import ctypes
import json
import os
from pathlib import Path
import queue
import secrets
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
TEMP_ROOT = ROOT / '.private' if (ROOT / '.private').is_dir() else None
sys.path.insert(0, str(ROOT / 'tools'))
import connection_capture_custody as custody


class Child:
    pid = 123
    def poll(self):
        return None


class FakeNative:
    def __init__(self):
        self.events = []
        self.count = 0
        self.handle = None
        self.before_create = lambda: None

    def session(self, pid):
        return 7

    def create(self, name):
        self.before_create()
        self.events.append('create')
        self.handle = 42
        return self.handle

    def configure(self, handle):
        self.events.append('configure')

    def assign(self, handle, child):
        self.events.append('assign')

    def open(self, name):
        self.events.append('open')
        return self.handle

    def active(self, handle):
        self.events.append('active')
        return self.count

    def close(self, handle):
        self.events.append('close')


class GuardTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='capture-custody-', dir=TEMP_ROOT)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / '.private').mkdir()
        self.attempt = secrets.token_hex(16)
        self.path = custody._lease_path(self.root, self.attempt)
        self.native = FakeNative()
        self.patch = mock.patch.object(custody, '_native', return_value=self.native)
        self.patch.start()
        self.addCleanup(self.patch.stop)

    def test_durable_lease_precedes_job_creation_and_assignment(self):
        self.native.before_create = lambda: self.assertEqual(custody._read_lease(self.path)['attempt'], self.attempt)
        job = custody.create_and_assign(self.root, self.attempt, Child())
        self.assertEqual(self.native.events, ['create', 'configure', 'assign'])
        lease = custody._read_lease(self.path)
        self.assertEqual(lease['session_id'], 7)
        self.assertEqual(lease['job_name'], custody._name(self.attempt))
        job.close()
        job.close()
        self.assertEqual(self.native.events.count('close'), 1)

    def test_existing_lease_cannot_be_reused_or_overwritten(self):
        job = custody.create_and_assign(self.root, self.attempt, Child())
        raw = self.path.read_bytes()
        with self.assertRaises(custody.CustodyError):
            custody.create_and_assign(self.root, self.attempt, Child())
        self.assertEqual(self.path.read_bytes(), raw)
        self.assertEqual(self.native.events.count('create'), 1)
        job.close()

    def test_assignment_failure_preserves_lease_and_closes_job(self):
        def fail(*args):
            raise OSError('private error must not escape')
        self.native.assign = fail
        with self.assertRaisesRegex(custody.CustodyError, '^connection_capture_custody_refused$'):
            custody.create_and_assign(self.root, self.attempt, Child())
        self.assertTrue(self.path.is_file())
        self.assertEqual(self.native.events, ['create', 'configure', 'close'])

    def test_missing_lease_still_queries_name_and_live_job_denies(self):
        self.assertTrue(custody.require_released(self.root, self.attempt))
        self.assertEqual(self.native.events, ['open'])
        self.native.handle, self.native.count = 42, 1
        with self.assertRaises(custody.CustodyError):
            custody.require_released(self.root, self.attempt)
        self.assertEqual(self.native.events[-3:], ['open', 'active', 'close'])

    def test_same_session_zero_active_job_allows_release(self):
        job = custody.create_and_assign(self.root, self.attempt, Child())
        self.assertTrue(custody.require_released(self.root, self.attempt))
        job.close()

    def test_session_change_denies_before_job_lookup(self):
        job = custody.create_and_assign(self.root, self.attempt, Child())
        self.native.events.clear()
        self.native.session = lambda pid: 8
        with self.assertRaises(custody.CustodyError):
            custody.require_released(self.root, self.attempt)
        self.assertNotIn('open', self.native.events)
        job.close()

    def test_malformed_oversize_null_and_duplicate_lease_deny(self):
        for raw in (b'null', b'{}', b'x' * 1025, b'{"schema":1,"schema":2}', b'NaN'):
            with self.subTest(raw=raw[:30]):
                self.path.write_bytes(raw)
                with self.assertRaises(custody.CustodyError):
                    custody.require_released(self.root, self.attempt)
                self.assertNotIn('open', self.native.events)

    def test_wrong_attempt_job_name_or_session_type_deny(self):
        job = custody.create_and_assign(self.root, self.attempt, Child())
        lease = custody._read_lease(self.path)
        for key, value in (('attempt', 'a' * 32), ('job_name', 'Local\\other'),
                           ('session_id', True), ('child_pid', True)):
            with self.subTest(field=key):
                self.path.write_text(json.dumps(dict(lease, **{key: value})), encoding='ascii')
                with self.assertRaises(custody.CustodyError):
                    custody.require_released(self.root, self.attempt)
        job.close()

    def test_open_or_query_failure_is_not_release(self):
        def fail(*args):
            raise OSError('private error must not escape')
        self.native.open = fail
        with self.assertRaisesRegex(custody.CustodyError, '^connection_capture_custody_refused$'):
            custody.require_released(self.root, self.attempt)
        self.native.open = lambda name: 42
        self.native.active = fail
        with self.assertRaises(custody.CustodyError):
            custody.require_released(self.root, self.attempt)
        self.assertEqual(self.native.events[-1], 'close')

    def test_invalid_attempt_or_exited_child_cannot_create_job(self):
        for attempt in ('../escape', 'A' * 32, '', '1' * 33):
            with self.assertRaises(custody.CustodyError):
                custody.create_and_assign(self.root, attempt, Child())
        child = Child()
        child.poll = lambda: 0
        with self.assertRaises(custody.CustodyError):
            custody.create_and_assign(self.root, self.attempt, child)
        self.assertFalse(self.path.exists())


def bounded_line(stream, timeout=10):
    result = queue.Queue(maxsize=1)
    def read():
        try:
            result.put(stream.readline(4097))
        except BaseException as error:
            result.put(error)
    threading.Thread(target=read, daemon=True).start()
    value = result.get(timeout=timeout)
    if type(value) is not bytes or not value.endswith(b'\n') or len(value) > 4096:
        raise AssertionError('native_probe_reply_invalid')
    return json.loads(value)


@unittest.skipUnless(os.name == 'nt', 'Windows native process/job probe')
class WindowsProcessTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='capture-custody-native-', dir=TEMP_ROOT)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / '.private').mkdir()
        self.attempt = secrets.token_hex(16)
        self.flags = getattr(subprocess, 'CREATE_NO_WINDOW', 0)

    def launch(self, source, *args):
        return subprocess.Popen([sys.executable, '-I', '-S', '-B', '-c', source, *map(str, args)],
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            creationflags=self.flags)

    def finish(self, child, job=None):
        if job is not None:
            job.close()
        if child.poll() is None:
            child.kill()
        child.wait(timeout=5)
        for stream in (child.stdout, child.stderr):
            if stream is not None:
                stream.close()

    def test_native_struct_layout_and_live_job_denial_then_release(self):
        self.assertEqual(ctypes.sizeof(custody._Accounting), 48)
        self.assertEqual(custody._Accounting.ActiveProcesses.offset, 40)
        if ctypes.sizeof(ctypes.c_void_p) == 8:
            self.assertEqual(ctypes.sizeof(custody._BasicLimit), 64)
            self.assertEqual(ctypes.sizeof(custody._ExtendedLimit), 144)
        child = self.launch('import time; time.sleep(60)')
        job = None
        try:
            job = custody.create_and_assign(self.root, self.attempt, child)
            with self.assertRaises(custody.CustodyError):
                custody.require_released(self.root, self.attempt)
            child.kill()
            child.wait(timeout=5)
            self.assertTrue(custody.require_released(self.root, self.attempt))
            job.close()
            self.assertTrue(custody.require_released(self.root, self.attempt))
        finally:
            self.finish(child, job)

    def process_api(self):
        dll = ctypes.WinDLL('kernel32', use_last_error=True)
        dll.OpenProcess.argtypes = [custody.DWORD, custody.BOOL, custody.DWORD]
        dll.OpenProcess.restype = custody.HANDLE
        dll.WaitForSingleObject.argtypes = [custody.HANDLE, custody.DWORD]
        dll.WaitForSingleObject.restype = custody.DWORD
        dll.CloseHandle.argtypes = [custody.HANDLE]
        dll.CloseHandle.restype = custody.BOOL
        return dll

    def test_controller_death_terminates_admitted_child_without_serial(self):
        source = r'''
import json,pathlib,subprocess,sys,time
sys.path.insert(0,sys.argv[1])
import connection_capture_custody as custody
child=subprocess.Popen([sys.executable,'-I','-S','-B','-c','import time; time.sleep(60)'],
    stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,
    creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
try:
    job=custody.create_and_assign(pathlib.Path(sys.argv[2]),sys.argv[3],child)
except BaseException:
    child.kill(); child.wait(timeout=5); raise
print(json.dumps({'pid':child.pid,'serial_loaded':any(x=='serial' or x.startswith('serial.') for x in sys.modules)}),flush=True)
time.sleep(60)
'''
        owner = self.launch(source, ROOT / 'tools', self.root, self.attempt)
        dll, handle = self.process_api(), None
        try:
            reply = bounded_line(owner.stdout)
            self.assertFalse(reply['serial_loaded'])
            handle = dll.OpenProcess(0x00100000, False, reply['pid'])  # SYNCHRONIZE only.
            self.assertTrue(handle)
            self.assertEqual(dll.WaitForSingleObject(handle, 0), 258)  # WAIT_TIMEOUT.
            with self.assertRaises(custody.CustodyError):
                custody.require_released(self.root, self.attempt)
            owner.kill()
            owner.wait(timeout=5)
            self.assertEqual(dll.WaitForSingleObject(handle, 5000), 0)  # WAIT_OBJECT_0.
            self.assertTrue(custody.require_released(self.root, self.attempt))
        finally:
            self.finish(owner)
            if handle is not None:
                dll.CloseHandle(handle)

    def test_close_terminates_admitted_child_and_its_descendant(self):
        # The child cannot spawn until the parent's job assignment is complete.
        source = r'''
import json,subprocess,sys,time
sys.stdin.buffer.read(1)
child=subprocess.Popen([sys.executable,'-I','-S','-B','-c','import time; time.sleep(60)'],
    stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,
    creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
print(json.dumps({'pid':child.pid}),flush=True)
time.sleep(60)
'''
        child = subprocess.Popen([sys.executable, '-I', '-S', '-B', '-c', source],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=self.flags)
        dll, handle, job = self.process_api(), None, None
        try:
            job = custody.create_and_assign(self.root, self.attempt, child)
            child.stdin.write(b'1')
            child.stdin.flush()
            reply = bounded_line(child.stdout)
            handle = dll.OpenProcess(0x00100000, False, reply['pid'])
            self.assertTrue(handle)
            with self.assertRaises(custody.CustodyError):
                custody.require_released(self.root, self.attempt)
            job.close()
            child.wait(timeout=5)
            self.assertEqual(dll.WaitForSingleObject(handle, 5000), 0)
            self.assertTrue(custody.require_released(self.root, self.attempt))
        finally:
            self.finish(child, job)
            child.stdin.close()
            if handle is not None:
                dll.CloseHandle(handle)


if __name__ == '__main__':
    unittest.main(verbosity=2)
