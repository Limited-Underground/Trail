"""Reviewed owner-shell admission and neutral readiness; no device authority.

The supported launch is the owner's own terminal on WinSta0\\Default. There is
no desktop-switch, token replacement, arbitrary executable, shell command or
grant issuer. Operational launch delegates unchanged to runtime.launch only
after the same reviewed path has a fresh explicit human visibility receipt.
"""
import hashlib
import math
import os
from pathlib import Path
import secrets
import sys
import time

import enrollment_candidate_runtime as runtime

ROOT = Path(__file__).resolve().parents[1]
STRATEGY = 'owner-shell-winsta0-default-inheritance'
MAX_AGE = 900.0
NEUTRAL_STEP_SECONDS = 300.0
WORDS = ('BIRD', 'LAKE', 'PINE', 'ROCK', 'TREE', 'WIND')
EXAMPLES = ('0123456789ABCDEF', 'FEDCBA9876543210',
            'A5A5A5A5A5A5A5A5', '5A5A5A5A5A5A5A5A')
SOURCE_NAMES = runtime.SOURCE_NAMES + ('enrollment_candidate_interactive_launch.py',
    'enrollment_candidate_visible_session.py', 'enrollment_candidate_owner_workflow.py')
RESUME_SOURCE_NAMES = SOURCE_NAMES + ('enrollment_candidate_predevice_resume.py',)
SETTINGS_NAME = 'enrollment_candidate_settings_successor.py'
FULL_STAGE = 'ot0238c-full-successor-admission-20261012'
REVOKE_STAGE = 'ot0238e-revoke-admission-20261013'
SETTINGS_SOURCE_NAMES = RESUME_SOURCE_NAMES + (SETTINGS_NAME,)
SETTINGS_AFTER_PINS = {'enrollment_candidate_rom_adapter.py': {'bytes': 70957, 'sha256': 'cc207e17bea17355fd9228cd221235c2a7ce5d777d0cbafde6134df8e98c8f4b'}, 'enrollment_candidate_original_capture.py': {'bytes': 56311, 'sha256': '866192e7dd6ebf95e47d5dc519ff9a475bc74ebd50f57a0a7e48bfa0e23f04e1'}, 'enrollment_candidate_capture_runner.py': {'bytes': 23027, 'sha256': 'fbaf3d0fc820210fbac6371cdf7c2ebea1bdb8744738612bfba0480785abbd02'}}
# Reviewed capture-runner bytes require a protected route; they do not select
# the full mode. Runtime is shared by historical routes and is enforced only
# by the explicit full profile/cumulative transition, not by route detection.
REVIEWED_CORE_AFTER_PINS = {
    'enrollment_candidate_capture_runner.py': dict(bytes=23998,
        sha256='6a86517612a6171df25d921639ce796ec947a02674ec6ae8f38249eaec154f3f')}
# Detection precedes schema selection, so a cleanup runtime cannot downgrade
# its binding to a historical schema to evade the mandatory recovery hold.
CLEANUP_AFTER_PINS = {
    'enrollment_candidate_rom_adapter.py': dict(bytes=69318,
        sha256='5a340c902e51afa3e3024712c0517f3a1d3ff291aa542d7ed37dcc33b0fda391'),
    'enrollment_candidate_original_capture.py': dict(bytes=55424,
        sha256='e614b8ac1c02512e90aa60bf1b3efbaa4e935b12bbd6815e85962860eb861c27'),
    'enrollment_candidate_capture_runner.py': dict(bytes=22439,
        sha256='f92cb5929c9994f5e258252179db60d8f3baafefdc9653d57f93c31c14f0d150')}


class AdmissionError(RuntimeError):
    """Fixed non-identifying categories only."""


def need(value, category):
    if not value:
        raise AdmissionError(category)


def finite(value):
    return type(value) in (int, float) and math.isfinite(value)


def reference(path):
    path = runtime.regular(path)
    return dict(path=str(path), **runtime.descriptor(path.read_bytes()))


def read_reference(ref, *, private=None):
    need(type(ref) is dict and set(ref) == {'path', 'bytes', 'sha256'}, 'binding_invalid')
    return runtime.read_pin(ref['path'], {k: ref[k] for k in ('bytes', 'sha256')}, private=private)


def save_once(path, value):
    path = Path(path)
    runtime.regular(path.parent, directory=True)
    runtime._write_once(path, runtime.canonical(value) + b'\n')
    return reference(path)


def binding_core(value):
    return {k: v for k, v in value.items() if k != 'review'}


def admit_binding(path, sha256, *, assembly_verifier=runtime.verify_assembly):
    """Read-only exact closure admission, prior to loading any session code."""
    path = runtime.regular(path)
    private = runtime.regular(ROOT / '.private', directory=True)
    need(path.resolve().is_relative_to(private.resolve()), 'binding_invalid')
    raw = path.read_bytes()
    need(len(raw) <= 32768 and runtime.descriptor(raw)['sha256'] == sha256, 'binding_changed')
    value = runtime.decode(raw)
    if type(value) is dict:
        sources = value.get('sources')
        cleanup_sources = type(sources) is dict and any(sources.get(name) == pin
            for name, pin in CLEANUP_AFTER_PINS.items())
        settings_sources = type(sources) is dict and (SETTINGS_NAME in sources or any(
            sources.get(name) == pin for name, pin in SETTINGS_AFTER_PINS.items()) or any(
            sources.get(name) == pin for name, pin in REVIEWED_CORE_AFTER_PINS.items()))
        settings_parent = type(value.get('session')) is dict and Path(value['session'].get('path', '')).parent == (
            ROOT / '.private/ot0238c-settings-check-20261011/prestage-v2')
        full_parent = type(value.get('session')) is dict and Path(value['session'].get('path', '')).parent in (
            ROOT / '.private' / FULL_STAGE / 'prestage',
            ROOT / '.private' / REVOKE_STAGE / 'prestage')
        cleanup_parent = type(value.get('session')) is dict and Path(value['session'].get('path', '')).parent == (
            ROOT / '.private/ot0238c-launcher-recovery-20261011/prestage')
        need(not (cleanup_sources or cleanup_parent or settings_sources or settings_parent or full_parent)
            or value.get('schema') == 'OT-VISIBLE-SESSION-BINDING-2', 'recovery_gate_invalid')
    base_keys = {'schema', 'worktree', 'host', 'assembly', 'session', 'sources', 'review',
        'readiness_root', 'session_nonce', 'session_output'}
    need(type(value) is dict and ((value.get('schema') == 'OT-VISIBLE-SESSION-BINDING-1' and set(value) == base_keys)
        or (value.get('schema') == 'OT-VISIBLE-SESSION-BINDING-2' and set(value) == base_keys | {'predevice'}))
        and Path(value['worktree']).resolve() == ROOT.resolve()
        and type(value['session_nonce']) is str
        and __import__('re').fullmatch('[0-9a-f]{32}', value['session_nonce']), 'binding_invalid')
    read_reference(value['host'])
    need(Path(value['host']['path']).resolve() == Path(sys.executable).resolve()
         and sys.dont_write_bytecode and not sys.flags.optimize, 'host_changed')
    expected_sources = (SETTINGS_SOURCE_NAMES if settings_sources or settings_parent or full_parent else
        RESUME_SOURCE_NAMES if value['schema'] == 'OT-VISIBLE-SESSION-BINDING-2' else SOURCE_NAMES)
    need(type(value['sources']) is dict and set(value['sources']) == set(expected_sources), 'binding_invalid')
    for name, pin in value['sources'].items():
        runtime.read_pin(ROOT / 'tools' / name, pin)
    if value['schema'] == 'OT-VISIBLE-SESSION-BINDING-2':
        from enrollment_candidate_predevice_resume import require_recovery_gate
        require_recovery_gate(value)
    read_reference(value['session'], private=private)
    session = Path(value['session']['path'])
    need(session.name == 'session.template.py'
         and session.parent in (ROOT / '.private/ot0238c-visible-launch-20261005/prestage',
             ROOT / '.private/ot0238c-button-confirmation-20261005/prestage',
             ROOT / '.private/ot0238c-readability-20261006/prestage',
             ROOT / '.private/ot0238c-neutral-time-20261006/prestage',
             ROOT / '.private/ot0238c-launch-binding-20261008/prestage',
             ROOT / '.private/ot0238c-predevice-resume-20261008/prestage',
             ROOT / '.private/ot0238c-historical-readiness-20261009/prestage',
             ROOT / '.private/ot0238c-retained-preparation-20261009/prestage',
             ROOT / '.private/ot0238c-restart-package-20261010/prestage',
             ROOT / '.private/ot0238c-settings-check-20261011/prestage-v2',
              ROOT / '.private' / FULL_STAGE / 'prestage',
              ROOT / '.private' / REVOKE_STAGE / 'prestage'), 'session_invalid')
    output = runtime.regular(value['session_output'], directory=True)
    owning_root = session.parent.parent
    need(output == owning_root / 'sessions' / value['session_nonce'],
         'session_output_invalid')
    assembly_ref = value['assembly']
    read_reference(assembly_ref, private=private)
    assembly = assembly_verifier(assembly_ref['path'], assembly_ref['sha256'])
    need(assembly.worktree.resolve() == ROOT.resolve(), 'binding_invalid')
    for name in runtime.SOURCE_NAMES:
        need(assembly.manifest['files']['policy/' + name] == value['sources'][name], 'sources_changed')
    readiness = runtime.regular(value['readiness_root'], directory=True)
    need(readiness.resolve().is_relative_to(private.resolve())
         and not readiness.resolve().is_relative_to(session.parent.resolve())
         and not session.parent.resolve().is_relative_to(readiness.resolve())
         and not readiness.resolve().is_relative_to(assembly.root.resolve()), 'readiness_root_invalid')
    need(not readiness.resolve().is_relative_to(output.resolve())
         and not output.resolve().is_relative_to(readiness.resolve()), 'readiness_root_invalid')
    review = runtime.decode(read_reference(value['review'], private=private))
    need(type(review) is dict and set(review) == {'schema', 'binding_core_sha256', 'outcome',
        'findings', 'hardware_access', 'live_grants_created'}
        and review['schema'] == 'OT-VISIBLE-SESSION-REVIEW-1'
        and review['binding_core_sha256'] == runtime.descriptor(runtime.canonical(binding_core(value)))['sha256']
        and review['outcome'] == 'no_unresolved_findings' and review['findings'] == []
        and review['hardware_access'] is False and review['live_grants_created'] is False, 'review_required')
    if value.get('schema') == 'OT-VISIBLE-SESSION-BINDING-2':
        from enrollment_candidate_predevice_resume import manifest
        manifest(value)
    return value, reference(path)


def native_admission():
    """Only desktop, power, local policy and process-token reads; no devices."""
    need(os.name == 'nt', 'desktop_unavailable')
    import ctypes as c
    from ctypes import wintypes as w
    import uuid
    import winreg
    from enrollment_candidate_private_view import _Win32Api, ensure_input_desktop
    try:
        ensure_input_desktop()
        api = _Win32Api()
        def name(handle):
            out, required = c.create_unicode_buffer(256), w.DWORD()
            need(api.user.GetUserObjectInformationW(handle, 2, out, c.sizeof(out), c.byref(required)),
                 'desktop_unavailable')
            return out.value.casefold()
        need(name(api.user.GetProcessWindowStation()) == 'winsta0'
             and name(api.user.GetThreadDesktop(api.kernel.GetCurrentThreadId())) == 'default',
             'desktop_unavailable')
        area = api.RECT()
        need(api.user.SystemParametersInfoW(0x0030, 0, c.byref(area), 0)
             and area.right - area.left >= 900 and area.bottom - area.top >= 700, 'display_unavailable')
        session = w.DWORD()
        need(api.kernel.ProcessIdToSessionId(api.kernel.GetCurrentProcessId(), c.byref(session)),
             'desktop_unavailable')
        kernel = c.WinDLL('kernel32', use_last_error=True)
        advapi = c.WinDLL('advapi32', use_last_error=True)
        kernel.GetCurrentProcess.restype, kernel.GetCurrentProcess.argtypes = w.HANDLE, []
        kernel.CloseHandle.restype, kernel.CloseHandle.argtypes = w.BOOL, [w.HANDLE]
        advapi.OpenProcessToken.restype, advapi.OpenProcessToken.argtypes = w.BOOL, [w.HANDLE, w.DWORD, c.POINTER(w.HANDLE)]
        advapi.GetTokenInformation.restype = w.BOOL
        advapi.GetTokenInformation.argtypes = [w.HANDLE, c.c_int, w.LPVOID, w.DWORD, c.POINTER(w.DWORD)]
        class Luid(c.Structure):
            _fields_ = [('low', w.DWORD), ('high', w.LONG)]
        class Statistics(c.Structure):
            _fields_ = [('token', Luid), ('authentication', Luid), ('expiry', c.c_longlong),
                ('type', c.c_int), ('level', c.c_int), ('charged', w.DWORD), ('available', w.DWORD),
                ('groups', w.DWORD), ('privileges', w.DWORD), ('modified', Luid)]
        token, stats, returned = w.HANDLE(), Statistics(), w.DWORD()
        need(advapi.OpenProcessToken(kernel.GetCurrentProcess(), 8, c.byref(token)), 'desktop_unavailable')
        closed = False
        try:
            need(advapi.GetTokenInformation(token, 10, c.byref(stats), c.sizeof(stats), c.byref(returned))
                 and returned.value == c.sizeof(stats), 'desktop_unavailable')
            authentication = runtime.descriptor(runtime.canonical([stats.authentication.low,
                stats.authentication.high]))['sha256']
        finally:
            closed = bool(kernel.CloseHandle(token))
        need(closed, 'desktop_unavailable')
        class PowerStatus(c.Structure):
            _fields_ = [('ac', w.BYTE), ('flag', w.BYTE), ('percent', w.BYTE), ('reserved', w.BYTE),
                       ('life', w.DWORD), ('full', w.DWORD)]
        kernel.GetSystemPowerStatus.restype, kernel.GetSystemPowerStatus.argtypes = w.BOOL, [c.POINTER(PowerStatus)]
        power = PowerStatus()
        need(kernel.GetSystemPowerStatus(c.byref(power)) and power.ac == 1, 'power_unavailable')
        powr = c.WinDLL('powrprof', use_last_error=True)
        class Guid(c.Structure):
            _fields_ = [('a', w.DWORD), ('b', w.WORD), ('d', w.WORD), ('tail', w.BYTE * 8)]
        def guid(value):
            return Guid.from_buffer_copy(uuid.UUID(value).bytes_le)
        pg = c.POINTER(Guid)
        powr.PowerGetActiveScheme.restype, powr.PowerGetActiveScheme.argtypes = w.DWORD, [w.HKEY, c.POINTER(pg)]
        powr.PowerReadACValueIndex.restype, powr.PowerReadACValueIndex.argtypes = w.DWORD, [w.HKEY, pg, pg, pg, c.POINTER(w.DWORD)]
        kernel.LocalFree.restype, kernel.LocalFree.argtypes = w.HANDLE, [w.HANDLE]
        scheme = pg()
        need(powr.PowerGetActiveScheme(None, c.byref(scheme)) == 0 and bool(scheme), 'power_unavailable')
        try:
            for group, setting in (('7516b95f-f776-4464-8c53-06167f40cc99', '3c0bc021-c8a8-4e07-a973-6b14cbcb2b7e'),
                                   ('238c9fa8-0aad-41ed-83f4-97be242c8f20', '29f6c1db-86da-48c5-9fdb-f2b67b1f44da')):
                g, s, index = guid(group), guid(setting), w.DWORD()
                need(powr.PowerReadACValueIndex(None, scheme, c.byref(g), c.byref(s), c.byref(index)) == 0
                     and index.value == 0, 'power_unavailable')
        finally:
            need(not kernel.LocalFree(c.cast(scheme, w.HANDLE)), 'power_unavailable')
        def registry(hive, key, field):
            try:
                with winreg.OpenKey(winreg.HKEY_CURRENT_USER if hive == 'user' else winreg.HKEY_LOCAL_MACHINE, key) as opened:
                    value, kind = winreg.QueryValueEx(opened, field)
            except FileNotFoundError:
                return None
            need(kind in (winreg.REG_SZ, winreg.REG_DWORD), 'lock_policy_unavailable')
            return value, 'sz' if kind == winreg.REG_SZ else 'dword'
        effective_active = w.BOOL()
        need(api.user.SystemParametersInfoW(0x0010, 0, c.byref(effective_active), 0), 'lock_policy_unavailable')
        need(effective_active.value in (0, 1), 'lock_policy_unavailable')
        running = w.BOOL()
        need(api.user.SystemParametersInfoW(0x0072, 0, c.byref(running), 0), 'lock_policy_unavailable')
        need(running.value in (0, 1), 'lock_policy_unavailable')
        admit_lock_policy(registry, bool(effective_active.value), bool(running.value))
        return {'strategy': STRATEGY, 'session_id': session.value, 'logon_sha256': authentication,
                'input_desktop': True, 'ac_online': True, 'display_never': True, 'sleep_never': True,
                'no_screensaver_or_inactivity_lock': True, 'work_area_sufficient': True}
    except AdmissionError:
        raise
    except BaseException:
        raise AdmissionError('desktop_unavailable') from None


def admit_lock_policy(read, native_active, native_running=False):
    """Read-only effective saver semantics, independent of dormant preferences.

    Windows' EnableScreenSaver policy runs a saver only when an executable is
    configured and its timeout is nonzero. Control Panel's None removes the
    SCRNSAVE.EXE value. Password protection alone does not create a timer.
    read(hive, key, field) returns None for absent, or (value, 'sz'/'dword').
    Native API failures, malformed records and registry read errors refuse.
    """
    need(type(native_active) is bool and type(native_running) is bool and not native_running,
         'lock_policy_unavailable')
    def number(record, *, seconds=False, dword_only=False):
        if record is None:
            return None
        need(type(record) is tuple and len(record) == 2, 'lock_policy_unavailable')
        value, kind = record
        if kind == 'dword':
            need(type(value) is int and 0 <= value <= 0xffffffff, 'lock_policy_unavailable')
            result = value
        else:
            need(not dword_only and kind == 'sz' and type(value) is str
                 and __import__('re').fullmatch('[0-9]{1,10}', value), 'lock_policy_unavailable')
            result = int(value)
            need(result <= 0xffffffff, 'lock_policy_unavailable')
        need(seconds or result in (0, 1), 'lock_policy_unavailable')
        return result
    def executable(record):
        if record is None:
            return False
        need(type(record) is tuple and len(record) == 2 and record[1] == 'sz'
             and type(record[0]) is str and len(record[0]) <= 32767
             and all(ord(ch) >= 32 for ch in record[0]), 'lock_policy_unavailable')
        if not record[0]:
            return False
        # A nonempty configured value is conservatively treated as executable;
        # never excuse a timer because a file currently appears inaccessible.
        need(record[0].strip() and record[0] == record[0].strip(), 'lock_policy_unavailable')
        return True
    try:
        local = 'Control Panel\\Desktop'
        policy = 'Software\\Policies\\Microsoft\\Windows\\Control Panel\\Desktop'
        number(read('user', local, 'ScreenSaveActive'))
        number(read('user', local, 'ScreenSaverIsSecure'))
        configured = executable(read('user', local, 'SCRNSAVE.EXE'))
        number(read('user', local, 'ScreenSaveTimeOut'), seconds=True)
        active = native_active
        for hive in ('user', 'machine'):
            enforced = number(read(hive, policy, 'ScreenSaveActive'))
            number(read(hive, policy, 'ScreenSaverIsSecure'))
            configured = executable(read(hive, policy, 'SCRNSAVE.EXE')) or configured
            number(read(hive, policy, 'ScreenSaveTimeOut'), seconds=True)
            # An enforced activation cannot be waived by a transient API flag.
            active = active or enforced == 1
        # Do not admit a configured active saver based on a zero registry
        # timeout: the effective SPI timeout may differ from that preference.
        # The supported None case needs no executable and no running saver.
        need(not (active and configured), 'lock_policy_unavailable')
        inactivity = number(read('machine', 'SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System',
                                 'InactivityTimeoutSecs'), seconds=True, dword_only=True)
        dynamic = number(read('user', 'Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon', 'EnableGoodbye'),
                         dword_only=True)
        need(inactivity in (None, 0) and dynamic in (None, 0), 'lock_policy_unavailable')
    except AdmissionError:
        raise
    except BaseException:
        raise AdmissionError('lock_policy_unavailable') from None
    return True


def _valid_scope(value):
    need(type(value) is dict and set(value) == {'strategy', 'session_id', 'logon_sha256', 'input_desktop',
        'ac_online', 'display_never', 'sleep_never', 'no_screensaver_or_inactivity_lock', 'work_area_sufficient'}
        and value['strategy'] == STRATEGY and type(value['session_id']) is int and value['session_id'] > 0
        and type(value['logon_sha256']) is str and runtime.HEX64.fullmatch(value['logon_sha256'])
        and all(value[k] is True for k in ('input_desktop', 'ac_online', 'display_never', 'sleep_never',
            'no_screensaver_or_inactivity_lock', 'work_area_sufficient')), 'admission_invalid')
    return value


def _labeled_hex_rows(rows):
    """Display-only coordinates, split after COL 08; cells align with units."""
    need(type(rows) in (tuple, list) and len(rows) == 4
         and all(type(row) is str and __import__('re').fullmatch('[0-9A-F]{16}', row) for row in rows),
         'neutral_layout_invalid')
    def grouped(cells):
        return ' '.join(cells[:8]) + '  | ' + ' '.join(cells[8:])
    lines = ['COL    ' + grouped([f'{number:02d}' for number in range(1, 17)])]
    lines += [f'ROW {number}  ' + grouped([f'{character:>2}' for character in row])
              for number, row in enumerate(rows, 1)]
    return lines


def _text(kind, word=None, row=None, column=None):
    lines = ['TEST ONLY - no device action',
             'No devices, grants or enrollment authority.', 'Checkpoint: ' + kind]
    if kind == 'TEST_WORD':
        lines += ['1. Read the four-letter word below.', '2. Type the word in chat.',
                  '3. Click Word sent - continue.', '', 'WORD: ' + word, '', 'EXAMPLE ONLY:']
        lines += list(EXAMPLES)
    else:
        changed = list(EXAMPLES)
        current = changed[row][column]
        changed[row] = changed[row][:column] + ('0' if current != '0' else '1') + changed[row][column + 1:]
        lines += ['1. Compare LEFT and RIGHT. ONE character differs.',
                  '2. Find its printed ROW (1-4) and COL (01-16).',
                  '3. Type row, column in chat. Example: 2, 05.',
                  '4. Click Difference sent - continue.', 'EXAMPLE ONLY - LEFT:']
        lines += _labeled_hex_rows(EXAMPLES) + ['EXAMPLE ONLY - RIGHT:'] + _labeled_hex_rows(changed)
    need(len(lines) <= 19 and all(len(line) <= 57 for line in lines), 'neutral_layout_invalid')
    return '\r\n'.join(lines)


def probe(binding_path, binding_sha256, *, admission=native_admission, monotonic=time.monotonic,
          utc=time.time, sleep=time.sleep, backend_factory=None, binding_admitter=admit_binding):
    """Explicit neutral UI only. Proof is unusable until a separate human reply."""
    value, bound = binding_admitter(binding_path, binding_sha256)
    scope = _valid_scope(admission())
    from enrollment_candidate_controller import Clock
    from enrollment_candidate_private_view import _NeutralBackend
    factory = _NeutralBackend if backend_factory is None else backend_factory
    clock = Clock(monotonic)
    started = clock.now()
    started_utc = utc()
    need(finite(started_utc), 'clock_invalid')
    idle_until = started + 60.0
    while True:
        remaining = idle_until - clock.now()
        if remaining <= 0:
            break
        sleep(min(0.1, remaining))
    need(_valid_scope(admission()) == scope, 'desktop_changed')
    word = secrets.choice(WORDS)
    row, column = secrets.randbelow(4), secrets.randbelow(16)
    timings = []
    for kind, text in (('TEST_WORD', _text('TEST_WORD', word=word)),
                       ('TEST_COMPARE', _text('TEST_COMPARE', row=row, column=column))):
        begin = clock.now()
        deadline = begin + NEUTRAL_STEP_SECONDS
        backend = factory()
        cleared = False
        try:
            need(_valid_scope(admission()) == scope, 'desktop_changed')
            need(backend.open(text) is True, 'neutral_view_refused')
            text = None
            while True:
                clock.check(deadline)
                event = backend.poll()
                clock.check(deadline)
                if event is not None:
                    need(event == 'confirm', 'neutral_view_refused')
                    break
                sleep(0.01)
            elapsed = clock.now() - begin
        finally:
            cleared = backend.clear() is True
        need(cleared, 'neutral_clear_unverified')
        clock.check(deadline)
        timings.append({'kind': kind, 'elapsed_seconds': elapsed, 'explicit_click': True, 'cleared': True})
    need(_valid_scope(admission()) == scope, 'desktop_changed')
    finished, finished_utc = clock.now(), utc()
    need(finite(finished_utc) and abs((finished_utc - started_utc) - (finished - started)) <= 5,
         'clock_invalid')
    proof = {'schema': 'OT-NEUTRAL-VISIBILITY-PROOF-1', 'binding': bound,
        'scope': scope, 'started_monotonic': started, 'finished_monotonic': finished,
        'finished_utc': finished_utc, 'idle_seconds': 60.0, 'timings': timings,
        'word_sha256': hashlib.sha256(word.encode('ascii')).hexdigest(),
        'difference': [row + 1, column + 1], 'hardware_access': False, 'enrollment_authority': False}
    return save_once(Path(value['readiness_root']) / 'neutral-proof.json', proof)


def attest(binding_path, binding_sha256, owner_ref, *, admission=native_admission,
           monotonic=time.monotonic, utc=time.time, binding_admitter=admit_binding):
    """Bind actual chat transcription; this is readiness, never a hardware grant."""
    value, bound = binding_admitter(binding_path, binding_sha256)
    root = Path(value['readiness_root'])
    proof_ref = reference(root / 'neutral-proof.json')
    proof = runtime.decode(read_reference(proof_ref))
    _check_proof(proof, bound, admission, monotonic, utc)
    owner = runtime.decode(read_reference(owner_ref, private=root))
    need(type(owner) is dict and set(owner) == {'schema', 'proof', 'source', 'received_utc',
        'observed_word', 'difference', 'rows_comfortably_readable'}
        and owner['schema'] == 'OT-NEUTRAL-OWNER-REPLY-1' and owner['proof'] == proof_ref
        and owner['source'] == 'fresh_human_chat_reply'
        and finite(owner['received_utc']) and proof['finished_utc'] <= owner['received_utc'] <= utc()
        and type(owner['observed_word']) is str and owner['observed_word'] in WORDS
        and hashlib.sha256(owner['observed_word'].encode('ascii')).hexdigest() == proof['word_sha256']
        and owner['difference'] == proof['difference'] and owner['rows_comfortably_readable'] is True,
        'owner_readiness_refused')
    receipt = {'schema': 'OT-VISIBLE-READINESS-1', 'binding': bound, 'proof': proof_ref,
               'owner': owner_ref, 'hardware_authority': False, 'enrollment_acceptance': False}
    return save_once(root / 'readiness.json', receipt)


def _check_proof(proof, bound, admission, monotonic, utc, *, fresh=True):
    need(type(proof) is dict and set(proof) == {'schema', 'binding', 'scope', 'started_monotonic',
        'finished_monotonic', 'finished_utc', 'idle_seconds', 'timings', 'word_sha256', 'difference',
        'hardware_access', 'enrollment_authority'} and proof['schema'] == 'OT-NEUTRAL-VISIBILITY-PROOF-1'
        and proof['binding'] == bound and proof['hardware_access'] is False
        and proof['enrollment_authority'] is False and proof['idle_seconds'] == 60.0
        and type(proof['word_sha256']) is str and runtime.HEX64.fullmatch(proof['word_sha256'])
        and type(proof['difference']) is list and len(proof['difference']) == 2
        and type(proof['difference'][0]) is int and 1 <= proof['difference'][0] <= 4
        and type(proof['difference'][1]) is int and 1 <= proof['difference'][1] <= 16,
        'readiness_invalid')
    need(_valid_scope(proof['scope']) == _valid_scope(admission()), 'desktop_changed')
    timings = proof['timings']
    need(type(timings) is list and len(timings) == 2, 'readiness_invalid')
    for timing, kind in zip(timings, ('TEST_WORD', 'TEST_COMPARE')):
        need(type(timing) is dict and set(timing) == {'kind', 'elapsed_seconds', 'explicit_click', 'cleared'}
             and timing['kind'] == kind and timing['explicit_click'] is True and timing['cleared'] is True
             and finite(timing['elapsed_seconds'])
             and 0 <= timing['elapsed_seconds'] < NEUTRAL_STEP_SECONDS, 'readiness_invalid')
    now, stamp = monotonic(), utc()
    need(all(finite(proof[k]) for k in ('started_monotonic', 'finished_monotonic', 'finished_utc'))
         and finite(now) and finite(stamp) and proof['finished_monotonic'] >= proof['started_monotonic'] + 60
         and 0 <= now - proof['finished_monotonic']
         and 0 <= stamp - proof['finished_utc']
         and (not fresh or (now - proof['finished_monotonic'] < MAX_AGE
              and stamp - proof['finished_utc'] < MAX_AGE))
         and abs((now - proof['finished_monotonic']) - (stamp - proof['finished_utc'])) <= 5,
         'readiness_expired')


def require_ready(binding_path, binding_sha256, *, admission=native_admission,
                  monotonic=time.monotonic, utc=time.time, binding_admitter=admit_binding):
    return _require_ready(binding_path, binding_sha256, admission=admission,
        monotonic=monotonic, utc=utc, binding_admitter=binding_admitter, fresh=True)


def _require_ready(binding_path, binding_sha256, *, admission=native_admission,
                   monotonic=time.monotonic, utc=time.time, binding_admitter=admit_binding, fresh):
    value, bound = binding_admitter(binding_path, binding_sha256)
    if value.get('schema') == 'OT-VISIBLE-SESSION-BINDING-2':
        from enrollment_candidate_predevice_resume import require
        return require(value, bound, admission=admission, monotonic=monotonic, utc=utc, fresh=fresh)
    root = Path(value['readiness_root'])
    receipt = runtime.decode(read_reference(reference(root / 'readiness.json')))
    need(type(receipt) is dict and set(receipt) == {'schema', 'binding', 'proof', 'owner',
        'hardware_authority', 'enrollment_acceptance'} and receipt['schema'] == 'OT-VISIBLE-READINESS-1'
        and receipt['binding'] == bound and receipt['hardware_authority'] is False
        and receipt['enrollment_acceptance'] is False, 'readiness_invalid')
    need(receipt['proof']['path'] == str(root / 'neutral-proof.json'), 'readiness_invalid')
    proof = runtime.decode(read_reference(receipt['proof'], private=root))
    _check_proof(proof, bound, admission, monotonic, utc, fresh=fresh)
    owner = runtime.decode(read_reference(receipt['owner'], private=root))
    need(type(owner) is dict and set(owner) == {'schema', 'proof', 'source', 'received_utc',
        'observed_word', 'difference', 'rows_comfortably_readable'}
        and owner['schema'] == 'OT-NEUTRAL-OWNER-REPLY-1' and owner['proof'] == receipt['proof']
        and owner['source'] == 'fresh_human_chat_reply'
        and finite(owner['received_utc']) and proof['finished_utc'] <= owner['received_utc'] <= utc()
        and type(owner['observed_word']) is str and owner['observed_word'] in WORDS
        and hashlib.sha256(owner['observed_word'].encode('ascii')).hexdigest() == proof['word_sha256']
        and owner['difference'] == proof['difference'] and owner['rows_comfortably_readable'] is True,
        'owner_readiness_refused')
    return value


def require_continuity(binding_path, binding_sha256, *, admission=native_admission,
                       monotonic=time.monotonic, utc=time.time, binding_admitter=admit_binding,
                       grant_validator=None):
    """Continue the started held attempt under its original ceilings, no renewal."""
    value = _require_ready(binding_path, binding_sha256, admission=admission,
        monotonic=monotonic, utc=utc, binding_admitter=binding_admitter, fresh=False)
    root = Path(value['session_output'])
    readiness_ref = reference(Path(value['readiness_root']) / 'readiness.json')
    if value.get('schema') == 'OT-VISIBLE-SESSION-BINDING-2':
        from enrollment_candidate_predevice_resume import anchor
        proof = anchor(value)
    else:
        proof = runtime.decode(read_reference(reference(Path(value['readiness_root']) / 'neutral-proof.json')))
    intent = runtime.decode(read_reference(reference(root / 'launch-intent.json')))
    need(type(intent) is dict and set(intent) == {'schema', 'utc', 'package', 'one_attempt',
        'visible_readiness', 'visible_binding_sha256', 'capture_grant', 'started_utc', 'started_monotonic', 'owner_authorization'}
        and intent['schema'] == 'OT0238C-LAUNCH-INTENT-1' and intent['one_attempt'] is True
        and intent['visible_readiness'] == readiness_ref
        and intent['visible_binding_sha256'] == binding_sha256
        and finite(intent['started_utc']) and finite(intent['started_monotonic'])
        and 0 <= intent['started_utc'] - proof['finished_utc'] < MAX_AGE
        and 0 <= intent['started_monotonic'] - proof['finished_monotonic'] < MAX_AGE,
        'continuity_refused')
    owner_authorization = runtime.decode(read_reference(intent['owner_authorization'], private=root))
    need(owner_authorization.get('schema') == 'OT0238C-V13-OWNER-AUTHORIZATION-1'
         and owner_authorization.get('approved') is True and owner_authorization.get('source') == 'fresh_human_reply'
         and owner_authorization.get('visible_binding_sha256') == binding_sha256
         and owner_authorization.get('session_nonce') == value['session_nonce']
         and owner_authorization.get('visible_readiness') == readiness_ref, 'continuity_refused')
    package = runtime.decode(read_reference(intent['package'], private=root))
    need(package.get('schema') == 'OT-ORIGINAL-CAPTURE-RUNNER-1' and package.get('operation') == 'capture'
         and package.get('evidence_root') == str(root) and package.get('assembly') == value['assembly']
         and package.get('grant') == intent['capture_grant'], 'continuity_refused')
    grant_raw = read_reference(intent['capture_grant'], private=root)
    request = runtime.decode(read_reference(package['request'], private=ROOT / '.private'))
    if grant_validator is None:
        from enrollment_candidate_original_capture import validate_grant
        grant_validator = validate_grant
    grant = grant_validator(request, grant_raw, intent['capture_grant']['sha256'], 'capture', utc)
    need(grant['capture_expires_utc'] - grant['issued_utc'] == 2100
         and grant['cleanup_expires_utc'] - grant['issued_utc'] == 3600
         and proof['finished_utc'] <= grant['issued_utc'] <= intent['started_utc']
         and grant['issued_utc'] <= intent['started_utc'] < grant['capture_expires_utc'], 'continuity_refused')
    now, stamp = monotonic(), utc()
    # Use the original grant UTC ceiling correlated to the original launch
    # monotonic sample; never calculate a renewed period from the current time.
    ceiling = intent['started_monotonic'] + grant['capture_expires_utc'] - intent['started_utc']
    need(finite(now) and finite(stamp) and intent['started_monotonic'] <= now < ceiling
         and abs((now - intent['started_monotonic']) - (stamp - intent['started_utc'])) <= 5,
         'continuity_expired')
    active = runtime.decode(read_reference(reference(root / 'enrollment-original-active.json')))
    journal = runtime.decode(read_reference(reference(root / ('enrollment-original-' + grant['attempt'] + '.json'))))
    need(active == {'attempt': grant['attempt'], 'request_sha256': grant['request_sha256']}
         and journal.get('attempt') == grant['attempt'] and journal.get('phase') == 'held'
         and journal.get('capture_complete') is True, 'continuity_refused')
    return value


def require_status(binding_path, binding_sha256, *, binding_admitter=admit_binding):
    """Read-only saved status remains inspectable after expiry/release/UI loss."""
    return binding_admitter(binding_path, binding_sha256)[0]


def launch(binding_path, binding_sha256, mode, package_path, package_sha256, *,
           readiness_checker=require_ready, runtime_launcher=runtime.launch):
    """Same interpreter/token/input desktop; unchanged runtime deadline/custody."""
    need(mode in ('capture', 'execute'), 'mode_invalid')
    bound = readiness_checker(binding_path, binding_sha256)
    return runtime_launcher(bound['assembly']['path'], bound['assembly']['sha256'], mode,
                            package_path, package_sha256, bench_presentation=True)


def practice(binding_path, binding_sha256, *, readiness_checker=require_ready, admission=native_admission,
             monotonic=time.monotonic, utc=time.time, sleep=time.sleep, backend_factory=None):
    """Human-only four-prompt lower bound; never an enrollment feasibility claim.

    Six two-second gestures are modeled by waits. Never press actual device
    buttons during this exercise. USB/firmware/transport work remains unknown.
    """
    value = readiness_checker(binding_path, binding_sha256)
    from enrollment_candidate_controller import Clock
    from enrollment_candidate_private_view import _Win32Backend
    factory = _Win32Backend if backend_factory is None else backend_factory
    scope = _valid_scope(admission())
    rows_a = tuple(secrets.token_hex(8).upper() for _ in range(4))
    rows_b = tuple(secrets.token_hex(8).upper() for _ in range(4))
    code = secrets.token_hex(4).upper()
    clock = Clock(monotonic)
    started = clock.now()
    preparation = started + 120.0
    activation = None
    timings = []
    for kind, waits in (('MY_ID', 2), ('PEER_A', 1), ('PEER_B', 1), ('COMPARE_CODE', 2)):
        if kind == 'COMPARE_CODE':
            activation = min(preparation, clock.now() + 60.0)
        deadline = preparation if activation is None else activation
        clock.check(deadline)
        begin = clock.now()
        lines = ['TEST ONLY - no device action', '', 'Checkpoint: TEST_PRACTICE',
                 'Practice ' + kind + '; NO real buttons or device operations.', '',
                 '1. Compare the synthetic example rows below.',
                 '2. Click continue after comparing. Gestures are modeled by waits.', '']
        if kind == 'MY_ID':
            # Two pairs in columns preserve the actual font/available height.
            lines += ['EXAMPLE ONLY - INVITER A MY ID OT-ID1 G:0000000000000001', 'saved A                         screen A']
            lines += [r + '                ' + r for r in rows_a]
            lines += ['', 'EXAMPLE ONLY - MEMBER B MY ID OT-ID1 G:0000000000000001', 'saved B                         screen B']
            lines += [r + '                ' + r for r in rows_b]
        elif kind in ('PEER_A', 'PEER_B'):
            rows = rows_b if kind == 'PEER_A' else rows_a
            lines += ['EXAMPLE ONLY - PEER OT-ID1 G:0000000000000001', 'saved other device              screen peer']
            lines += [r + '                ' + r for r in rows]
        else:
            lines += ['EXAMPLE ONLY - OT-CODE1 G:0000000000000001', 'screen A: ' + code, 'screen B: ' + code]
        # MY ID has more rows than an operational prompt because the two real
        # device displays are simulated here. Font/renderer are identical.
        backend = factory()
        try:
            need(_valid_scope(admission()) == scope, 'desktop_changed')
            need(len(lines) <= 25 and all(len(line) <= 76 for line in lines), 'neutral_layout_invalid')
            need(backend.open('\r\n'.join(lines)) is True, 'neutral_view_refused')
            while True:
                clock.check(deadline)
                event = backend.poll()
                clock.check(deadline)
                if event is not None:
                    need(event == 'confirm', 'neutral_view_refused')
                    break
                sleep(0.01)
        finally:
            need(backend.clear() is True, 'neutral_clear_unverified')
        # Modeled waits consume the same human-only practice budget. They do
        # not stand in for an actual target gesture or transport acknowledgement.
        for _ in range(waits):
            sleep(2.0)
            clock.check(deadline)
        timings.append({'kind': kind, 'elapsed_seconds': clock.now() - begin,
                        'modeled_gestures': waits, 'explicit_click': True, 'cleared': True})
    finished = clock.now()
    need(_valid_scope(admission()) == scope, 'desktop_changed')
    # Reverify the complete closure after ending the human-only stopwatch.
    # This overhead never receives enrollment/preparation completion credit.
    readiness_checker(binding_path, binding_sha256)
    record = {'schema': 'OT-NEUTRAL-PRACTICE-1', 'outcome': 'completed',
              'binding_sha256': binding_sha256, 'finished_utc': utc(), 'timings': timings,
              'total_seconds': finished - started, 'remaining_preparation_seconds': preparation - finished,
              'remaining_activation_seconds': activation - finished, 'modeled_gesture_seconds': 12,
              'human_only_lower_bound': True, 'transport_and_device_cost_unknown': True,
              'enrollment_feasibility_accepted': False, 'hardware_access': False}
    return save_once(Path(value['readiness_root']) / 'practice.json', record)


def require_practice(value, binding_sha256):
    if value.get('schema') == 'OT-VISIBLE-SESSION-BINDING-2':
        from enrollment_candidate_predevice_resume import practice
        return practice(value)
    record = runtime.decode(read_reference(reference(Path(value['readiness_root']) / 'practice.json')))
    need(type(record) is dict and set(record) == {'schema', 'outcome', 'binding_sha256', 'finished_utc',
        'timings', 'total_seconds', 'remaining_preparation_seconds', 'remaining_activation_seconds',
        'modeled_gesture_seconds', 'human_only_lower_bound', 'transport_and_device_cost_unknown',
        'enrollment_feasibility_accepted', 'hardware_access'} and record['schema'] == 'OT-NEUTRAL-PRACTICE-1'
        and record['binding_sha256'] == binding_sha256 and record['outcome'] == 'completed'
        and record['modeled_gesture_seconds'] == 12 and record['human_only_lower_bound'] is True
        and record['transport_and_device_cost_unknown'] is True
        and record['enrollment_feasibility_accepted'] is False and record['hardware_access'] is False
        and finite(record['total_seconds']) and 12 <= record['total_seconds'] < 120
        and finite(record['remaining_preparation_seconds']) and record['remaining_preparation_seconds'] > 0
        and finite(record['remaining_activation_seconds']) and record['remaining_activation_seconds'] > 0,
        'practice_required')
    need(type(record['timings']) is list and len(record['timings']) == 4, 'practice_required')
    for step, kind, gestures in zip(record['timings'], ('MY_ID', 'PEER_A', 'PEER_B', 'COMPARE_CODE'), (2, 1, 1, 2)):
        need(type(step) is dict and set(step) == {'kind', 'elapsed_seconds', 'modeled_gestures', 'explicit_click', 'cleared'}
             and step['kind'] == kind and step['modeled_gestures'] == gestures
             and step['explicit_click'] is True and step['cleared'] is True
             and finite(step['elapsed_seconds']) and step['elapsed_seconds'] >= 2 * gestures, 'practice_required')
    return record
