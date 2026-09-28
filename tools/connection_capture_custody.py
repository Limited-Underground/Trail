"""Windows collector lifetime guard; import is inert and never loads serial.

The caller creates a payload-blocked child, assigns it here, and only then sends
capture input. For cleanup it closes the job to terminate assigned processes,
kills/waits the child as needed, and calls require_released before any ROM
operation. The OS also terminates the job's processes if the controller dies.
Private leases are never removed here.
"""
import ctypes
import json
import os
from pathlib import Path
import re


SCHEMA = 'OT0101E-CONNECTION-CAPTURE-LEASE-1'
MAX_LEASE_BYTES = 1024
JOB_OBJECT_QUERY = 0x0004
JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x2000
ERROR_FILE_NOT_FOUND = 2
ERROR_ALREADY_EXISTS = 183
_MISSING = object()
DWORD = ctypes.c_uint32
HANDLE = ctypes.c_void_p
BOOL = ctypes.c_int32
SIZE_T = ctypes.c_size_t


class CustodyError(RuntimeError):
    """Fixed-category refusal; contains no device data or private path."""


def _need(value):
    if not value:
        raise CustodyError('connection_capture_custody_refused')


# Native field order/types from Microsoft's winnt.h documentation. SIZE_T and
# ULONG_PTR are pointer-sized; DWORD stays 32-bit on both Windows ABIs.
class _BasicLimit(ctypes.Structure):
    _fields_ = [('PerProcessUserTimeLimit', ctypes.c_int64),
                ('PerJobUserTimeLimit', ctypes.c_int64), ('LimitFlags', DWORD),
                ('MinimumWorkingSetSize', SIZE_T), ('MaximumWorkingSetSize', SIZE_T),
                ('ActiveProcessLimit', DWORD), ('Affinity', SIZE_T),
                ('PriorityClass', DWORD), ('SchedulingClass', DWORD)]


class _IoCounters(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint64) for name in (
        'ReadOperationCount', 'WriteOperationCount', 'OtherOperationCount',
        'ReadTransferCount', 'WriteTransferCount', 'OtherTransferCount')]


class _ExtendedLimit(ctypes.Structure):
    _fields_ = [('BasicLimitInformation', _BasicLimit), ('IoInfo', _IoCounters),
                ('ProcessMemoryLimit', SIZE_T), ('JobMemoryLimit', SIZE_T),
                ('PeakProcessMemoryUsed', SIZE_T), ('PeakJobMemoryUsed', SIZE_T)]


class _Accounting(ctypes.Structure):
    _fields_ = [('TotalUserTime', ctypes.c_int64), ('TotalKernelTime', ctypes.c_int64),
                ('ThisPeriodTotalUserTime', ctypes.c_int64),
                ('ThisPeriodTotalKernelTime', ctypes.c_int64),
                ('TotalPageFaultCount', DWORD), ('TotalProcesses', DWORD),
                ('ActiveProcesses', DWORD), ('TotalTerminatedProcesses', DWORD)]


class _Windows:
    def __init__(self):
        _need(os.name == 'nt')
        self.dll = ctypes.WinDLL('kernel32', use_last_error=True)
        signatures = {
            'CreateJobObjectW': ([ctypes.c_void_p, ctypes.c_wchar_p], HANDLE),
            'OpenJobObjectW': ([DWORD, BOOL, ctypes.c_wchar_p], HANDLE),
            'SetInformationJobObject': ([HANDLE, ctypes.c_int, ctypes.c_void_p, DWORD], BOOL),
            'QueryInformationJobObject': ([HANDLE, ctypes.c_int, ctypes.c_void_p, DWORD,
                                           ctypes.POINTER(DWORD)], BOOL),
            'AssignProcessToJobObject': ([HANDLE, HANDLE], BOOL),
            'IsProcessInJob': ([HANDLE, HANDLE, ctypes.POINTER(BOOL)], BOOL),
            'GetProcessId': ([HANDLE], DWORD),
            'ProcessIdToSessionId': ([DWORD, ctypes.POINTER(DWORD)], BOOL),
            'CloseHandle': ([HANDLE], BOOL),
        }
        for name, (arguments, result) in signatures.items():
            function = getattr(self.dll, name)
            function.argtypes, function.restype = arguments, result

    def session(self, pid):
        value = DWORD()
        _need(self.dll.ProcessIdToSessionId(pid, ctypes.byref(value)))
        return value.value

    def close(self, handle):
        _need(self.dll.CloseHandle(handle))

    def create(self, name):
        ctypes.set_last_error(0)
        handle = self.dll.CreateJobObjectW(None, name)  # Non-inheritable handle.
        error = ctypes.get_last_error()
        _need(handle)
        if error != 0:
            self.close(handle)  # Do not change limits of an existing named job.
            raise CustodyError('connection_capture_custody_refused')
        return handle

    def configure(self, handle):
        limits = _ExtendedLimit()
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        _need(self.dll.SetInformationJobObject(handle, 9, ctypes.byref(limits), ctypes.sizeof(limits)))
        actual, size = _ExtendedLimit(), DWORD()
        _need(self.dll.QueryInformationJobObject(handle, 9, ctypes.byref(actual),
              ctypes.sizeof(actual), ctypes.byref(size)))
        _need(size.value == ctypes.sizeof(actual) and
              actual.BasicLimitInformation.LimitFlags == JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE)

    def assign(self, handle, child):
        process = int(child._handle)  # Exact Popen handle, never a reopened/reused PID.
        _need(self.dll.GetProcessId(process) == child.pid)
        _need(self.dll.AssignProcessToJobObject(handle, process))
        admitted = BOOL()
        _need(self.dll.IsProcessInJob(process, handle, ctypes.byref(admitted)) and admitted.value)

    def open(self, name):
        ctypes.set_last_error(0)
        handle = self.dll.OpenJobObjectW(JOB_OBJECT_QUERY, False, name)
        if not handle:
            _need(ctypes.get_last_error() == ERROR_FILE_NOT_FOUND)
            return None
        return handle

    def active(self, handle):
        value, size = _Accounting(), DWORD()
        _need(self.dll.QueryInformationJobObject(handle, 1, ctypes.byref(value),
              ctypes.sizeof(value), ctypes.byref(size)))
        _need(size.value == ctypes.sizeof(value))
        return value.ActiveProcesses


def _native():
    return _Windows()


def _name(attempt):
    _need(type(attempt) is str and re.fullmatch(r'[0-9a-f]{32}', attempt))
    return 'Local\\OT0101E.ConnectionCapture.' + attempt


def _lease_path(root, attempt):
    _name(attempt)
    root = Path(root).absolute()
    private = root / '.private'
    target = private / ('connection-capture-' + attempt + '.lease.json')
    _need(root.is_dir() and private.is_dir())
    for item in (target, *target.parents):
        if item.exists() or item.is_symlink():
            _need(not item.is_symlink() and not (getattr(item.lstat(), 'st_file_attributes', 0) & 0x400))
    return target


def _read_lease(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            _need(key not in result)
            result[key] = value
        return result
    with path.open('rb') as stream:
        raw = stream.read(MAX_LEASE_BYTES + 1)
    _need(0 < len(raw) <= MAX_LEASE_BYTES)
    return json.loads(raw, object_pairs_hook=unique,
                      parse_constant=lambda _: (_ for _ in ()).throw(CustodyError('connection_capture_custody_refused')))


class CaptureJob:
    def __init__(self, native, handle):
        self._native, self._handle = native, handle

    def close(self):
        """Close only this controller's handle; this alone is not release proof."""
        if self._handle is not None:
            self._native.close(self._handle)
            self._handle = None


def create_and_assign(root, attempt, child):
    """Persist the lease before creating/admitting a payload-blocked child.

    On failure the caller still owns kill/wait of its child. No capture payload
    may be sent unless this returns. The lease stays for later recovery checks.
    """
    native, handle = None, None
    try:
        path = _lease_path(root, attempt)
        native = _native()
        _need(type(child.pid) is int and 0 < child.pid <= 0xffffffff and child.poll() is None)
        session = native.session(os.getpid())
        _need(native.session(child.pid) == session)
        lease = {'schema': SCHEMA, 'attempt': attempt, 'job_name': _name(attempt),
                 'session_id': session, 'child_pid': child.pid}
        raw = json.dumps(lease, sort_keys=True, separators=(',', ':')).encode('ascii') + b'\n'
        _need(len(raw) <= MAX_LEASE_BYTES)
        with path.open('xb', buffering=0) as stream:
            _need(stream.write(raw) == len(raw))
            os.fsync(stream.fileno())
        _need(_read_lease(path) == lease)
        handle = native.create(lease['job_name'])
        native.configure(handle)
        native.assign(handle, child)
        return CaptureJob(native, handle)
    except BaseException:
        if native is not None and handle is not None:
            native.close(handle)
        raise CustodyError('connection_capture_custody_refused') from None


def require_released(root, attempt):
    """Permit ROM only after a same-session named-job query proves release.

    A missing lease is the pre-capture recovery case: admission could not have
    happened without first persisting it. Still query the deterministic name.
    Malformed/inaccessible leases, session changes, live jobs and API errors deny.
    """
    native, handle = None, None
    try:
        path = _lease_path(root, attempt)
        native = _native()
        session = native.session(os.getpid())
        try:
            lease = _read_lease(path)
        except FileNotFoundError:
            lease = _MISSING
        if lease is not _MISSING:
            _need(type(lease) is dict and set(lease) == {'schema', 'attempt', 'job_name', 'session_id', 'child_pid'})
            _need(lease['schema'] == SCHEMA and lease['attempt'] == attempt and lease['job_name'] == _name(attempt))
            _need(type(lease['session_id']) is int and lease['session_id'] == session)
            _need(type(lease['child_pid']) is int and 0 < lease['child_pid'] <= 0xffffffff)
        handle = native.open(_name(attempt))
        if handle is not None:
            _need(native.active(handle) == 0)
        return True
    except BaseException:
        raise CustodyError('connection_capture_custody_refused') from None
    finally:
        if native is not None and handle is not None:
            native.close(handle)
