"""Host-only six-span custody orchestration for the isolated OTCAND1 target.

Imports are inert. There is no ROM adapter, probe, serial opener or grant issuer.
Admission records must be produced by a separately source-reviewed adapter;
synthetic adapters prove orchestration only. Reviewed request/grant hashes are
input pins, not signatures or a substitute for fresh owner hardware authority.
"""
from dataclasses import dataclass
from contextlib import contextmanager
import hashlib
import json
import math
import os
from pathlib import Path
import re
import threading
import time

from enrollment_candidate_controller import CASES, CheckpointAck, Clock, TrialResult

ROLES = ('A', 'B')
SPANS = {'bootloader': (0, 32768), 'partition': (0x8000, 4096),
         'otadata': (0x9000, 8192), 'nvs': (0xd000, 12288),
         'application': (0x10000, 733184), 'ota0_prefix': (0x500000, 16384)}
RESTORE_ORDER = ('nvs', 'ota0_prefix', 'partition', 'otadata', 'application', 'bootloader')
FROZEN_APPLICATION = {'bytes': 637520,
    'sha256': '93cd4e6d9011d5877cb02e9f0384958239c52a08a28f45700279bc2962cd8e0d'}
HEX64, HEX32 = re.compile('[0-9a-f]{64}'), re.compile('[0-9a-f]{32}')
ACTIVE = 'enrollment-candidate-active.json'
_PROCESS = threading.Lock()
_CLOSURE = threading.local()


class Error(RuntimeError):
    """Fixed non-identifying category only."""


def need(value, category):
    if not value:
        raise Error(category)


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode('ascii')


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def descriptor(raw):
    return {'bytes': len(raw), 'sha256': sha(raw)}


def decode(raw):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            need(key not in result, 'record_invalid')
            result[key] = value
        return result
    try:
        return json.loads(raw, object_pairs_hook=unique,
                          parse_constant=lambda _: (_ for _ in ()).throw(Error('record_invalid')))
    except Error:
        raise
    except BaseException:
        raise Error('record_invalid') from None


def _pin(value, size=None):
    return (type(value) is dict and set(value) == {'bytes', 'sha256'}
            and type(value['bytes']) is int and value['bytes'] > 0
            and (size is None or value['bytes'] == size)
            and type(value['sha256']) is str and HEX64.fullmatch(value['sha256']) is not None)


def validate_request(request):
    need(type(request) is dict and set(request) == {
        'schema', 'runtime_sha256', 'case', 'group', 'images', 'roles'}, 'request_invalid')
    need(request['schema'] == 'OT-CANDIDATE-REQUEST-1' and request['case'] in CASES
         and type(request['runtime_sha256']) is str and HEX64.fullmatch(request['runtime_sha256'])
         and type(request['group']) is int and 0 < request['group'] < 1 << 64, 'request_invalid')
    need(type(request['images']) is dict and set(request['images']) == {'application', 'partition'}
         and _pin(request['images']['application'])
         and request['images']['application']['bytes'] <= SPANS['application'][1]
         and _pin(request['images']['partition'], SPANS['partition'][1]), 'image_invalid')
    need(type(request['roles']) is dict and set(request['roles']) == set(ROLES), 'pair_invalid')
    for row in request['roles'].values():
        need(type(row) is dict and set(row) == {'device_binding', 'originals'}
             and type(row['device_binding']) is str and HEX64.fullmatch(row['device_binding'])
             and type(row['originals']) is dict and set(row['originals']) == set(SPANS), 'request_invalid')
        for name, (_, size) in SPANS.items():
            need(_pin(row['originals'][name], size), 'original_pin_invalid')
        need(row['originals']['partition'] != request['images']['partition'], 'layout_not_isolated')
    need(request['roles']['A']['device_binding'] != request['roles']['B']['device_binding'], 'duplicate_identity')
    return decode(canonical(request))


def actions_for(request, operation):
    need(operation in ('execute', 'recover'), 'grant_invalid')
    return (['capture_both_six_spans', 'sequential_candidate_install',
             'explicit_first_candidate_nvs_provision', 'candidate_boot',
             'case:' + request['case'], 'restore_both_six_spans', 'original_boot']
            if operation == 'execute' else ['restore_unresolved_six_spans', 'original_boot'])


def validate_grant(request, raw, pin, operation, utc):
    need(type(raw) is bytes and len(raw) <= 8192 and type(pin) is str and sha(raw) == pin, 'grant_invalid')
    value = decode(raw)
    need(type(value) is dict and set(value) == {'schema', 'attempt', 'request_sha256',
         'runtime_sha256', 'operation', 'origin_attempt', 'attempt_count', 'actions',
         'issued_utc', 'execute_expires_utc', 'restore_expires_utc'}, 'grant_invalid')
    need(value['schema'] == 'OT-CANDIDATE-GRANT-1' and type(value['attempt']) is str
         and HEX32.fullmatch(value['attempt']) and value['request_sha256'] == sha(canonical(request))
         and value['runtime_sha256'] == request['runtime_sha256'] and value['operation'] == operation
         and type(value['attempt_count']) is int and value['attempt_count'] == 1
         and value['actions'] == actions_for(request, operation), 'grant_invalid')
    need(all(type(value[k]) is int for k in ('issued_utc', 'execute_expires_utc', 'restore_expires_utc'))
         and value['issued_utc'] < value['execute_expires_utc'] <= value['restore_expires_utc']
         and value['restore_expires_utc'] - value['issued_utc'] <= 3600, 'grant_invalid')
    now = utc()
    need(type(now) in (int, float) and math.isfinite(now)
         and value['issued_utc'] <= now < value['execute_expires_utc'], 'grant_expired')
    need(value['origin_attempt'] is None if operation == 'execute' else
         type(value['origin_attempt']) is str and HEX32.fullmatch(value['origin_attempt'])
         and value['origin_attempt'] != value['attempt'], 'grant_invalid')
    return value


@dataclass(frozen=True)
class Admission:
    device_binding: str
    model: str
    flash_bytes: int
    security: str
    layout_sha256: str
    boot_selection: str
    rom_held: bool


@dataclass(frozen=True)
class CustodyResult:
    attempt: str
    observation: str
    first_failure: tuple | None
    roles: dict
    custody_released: bool
    owner_confirmed: bool = False


def _path(root, name):
    root = Path(root)
    need(root.is_absolute() and root.is_dir(), 'private_root_invalid')
    for item in (root, *root.parents):
        need(not item.is_symlink() and not getattr(item, 'is_junction', lambda: False)(), 'private_root_invalid')
    need(type(name) is str and name and Path(name).name == name, 'private_path_invalid')
    target = root / name
    need(not target.is_symlink() and not getattr(target, 'is_junction', lambda: False)(), 'private_path_invalid')
    return target


def _once(path, raw):
    with path.open('xb', buffering=0) as stream:
        need(stream.write(raw) == len(raw), 'record_failed')
        os.fsync(stream.fileno())
    need(path.read_bytes() == raw, 'record_failed')


class Journal:
    def __init__(self, root, attempt):
        self.root, self.attempt = Path(root), attempt
        self.file = _path(root, 'enrollment-candidate-' + attempt + '.json')
        self.pending = _path(root, 'enrollment-candidate-' + attempt + '.pending')

    def save(self, state):
        need(not self.pending.exists(), 'journal_pending')
        raw = canonical(state) + b'\n'
        _once(self.pending, raw)
        os.replace(self.pending, self.file)
        need(self.file.read_bytes() == raw, 'record_failed')

    def event(self, state, role, event):
        state['roles'][role]['events'].append(event)
        self.save(state)

    def original(self, role, name):
        return _path(self.root, f'enrollment-candidate-{self.attempt}-{role}-{name}.bin')


class Budget:
    def __init__(self, grant, utc, monotonic):
        self.clock, self.utc, self.grant = Clock(monotonic), utc, grant
        # Sample monotonic first: slow UTC acquisition can shorten, never
        # lengthen, the correlation used to derive the fixed ceilings.
        now, now_utc = self.clock.now(), utc()
        need(type(now_utc) in (int, float) and math.isfinite(now_utc)
             and grant['issued_utc'] <= now_utc < grant['execute_expires_utc'], 'grant_expired')
        # Derived once. No call, poll, retry or restoration step extends them.
        self.execute = now + grant['execute_expires_utc'] - now_utc
        self.restore = now + grant['restore_expires_utc'] - now_utc
        self.last_utc = now_utc

    def check(self, restoring=False):
        ceiling = self.restore if restoring else self.execute
        self.clock.check(ceiling)
        now = self.utc()
        limit = self.grant['restore_expires_utc'] if restoring else self.grant['execute_expires_utc']
        need(type(now) in (int, float) and math.isfinite(now) and now >= self.last_utc
             and self.grant['issued_utc'] <= now < limit, 'grant_expired')
        self.last_utc = now
        return ceiling


def _admit(value, request, role, restoring=False):
    row = request['roles'][role]
    need(type(value) is Admission and value.device_binding == row['device_binding']
         and value.model == 'heltec_v4_esp32s3' and type(value.flash_bytes) is int
         and value.flash_bytes == 16777216 and value.security == 'verified-write-permitted'
         and value.rom_held is True and type(value.layout_sha256) is str
         and HEX64.fullmatch(value.layout_sha256), 'admission_invalid')
    normal = (value.boot_selection == 'verified-factory' and value.layout_sha256 in
              (row['originals']['partition']['sha256'], request['images']['partition']['sha256']))
    # The ROM recovery producer must separately establish identity/security and
    # the pinned fixed restore offsets even if a known write tore the table.
    # This outcome admits ONLY restoration; it never admits candidate boot.
    recovery = restoring and value.boot_selection == 'verified-rom-restore-only'
    need(normal or recovery, 'admission_invalid')
    return value


def _guard(backend, request, role, budget, restoring=False):
    deadline = budget.check(restoring)
    result = _admit(backend.guard(request['roles'][role]['device_binding'], deadline), request, role, restoring)
    budget.check(restoring)
    return result


def _read(backend, request, role, name, budget, restoring=False):
    _guard(backend, request, role, budget, restoring)
    value = backend.read(*SPANS[name], budget.check(restoring))
    _guard(backend, request, role, budget, restoring)
    need(type(value) is bytes and len(value) == SPANS[name][1], 'read_invalid')
    return value


def _claim(backend, request, role, budget, restoring=False):
    value = backend.claim(request['roles'][role]['device_binding'], budget.check(restoring))
    _admit(value, request, role, restoring)
    budget.check(restoring)
    _guard(backend, request, role, budget, restoring)


def _capture(journal, state, role, backend, budget, restoring=False):
    expected = state['request']['roles'][role]['originals']
    for name in SPANS:
        value = _read(backend, state['request'], role, name, budget, restoring)
        need(descriptor(value) == expected[name], 'original_mismatch')
        dest = journal.original(role, name)
        if dest.exists():
            need(dest.read_bytes() == value, 'original_changed')
        else:
            _once(dest, value)
        state['roles'][role]['captures'][name] = descriptor(value)
        journal.save(state)
    # Independent second device read and persisted-file hash precede any write.
    for name in SPANS:
        value = _read(backend, state['request'], role, name, budget, restoring)
        need(value == journal.original(role, name).read_bytes()
             and descriptor(value) == expected[name], 'capture_readback_failed')
    return _originals(journal, state, role)


def _originals(journal, state, role):
    row = state['roles'][role]
    need(set(row['captures']) == set(SPANS), 'capture_incomplete')
    result = {}
    for name, (_, size) in SPANS.items():
        raw = journal.original(role, name).read_bytes()
        need(type(raw) is bytes and len(raw) == size
             and descriptor(raw) == row['captures'][name]
             and row['captures'][name] == state['request']['roles'][role]['originals'][name], 'original_changed')
        result[name] = raw
    return result


def _write(journal, state, role, backend, name, raw, budget, restoring=False):
    prefix = 'restore_' if restoring else 'candidate_'
    need(type(raw) is bytes and len(raw) == SPANS[name][1], 'write_invalid')
    _guard(backend, state['request'], role, budget, restoring)
    journal.event(state, role, prefix + name + '_intent')
    _guard(backend, state['request'], role, budget, restoring)
    backend.write(SPANS[name][0], raw, budget.check(restoring))
    need(_read(backend, state['request'], role, name, budget, restoring) == raw, 'write_readback_failed')
    journal.event(state, role, prefix + name + '_verified')


def _backend_close(backend):
    failures = getattr(_CLOSURE, 'failures', set())
    if id(backend) in failures:
        return False
    try:
        result = backend.close() is True and backend.assert_idle() is True
    except BaseException:
        result = False
    if not result:
        failures.add(id(backend))
        _CLOSURE.failures = failures
    return result


def _failure(state, stage, error):
    if state['first_failure'] is None:
        category = str(error) if isinstance(error, Error) else 'custody_operation_failed'
        state['first_failure'] = [stage, category]


def _settled(row):
    return row['handles_closed'] and ((row['restore_verified'] and row['original_boot_allowed'])
                                      or row['settled_untouched'])


def _validate_state(state, request, attempt):
    need(type(state) is dict and set(state) == {'schema', 'attempt', 'request', 'observation',
         'first_failure', 'closed', 'owner_confirmed', 'bridge_open', 'roles'}
         and state['schema'] == 'OT-CANDIDATE-JOURNAL-1' and state['attempt'] == attempt
         and state['request'] == request and state['observation'] in ('not_observed', 'passed', 'failed')
         and all(type(state[k]) is bool for k in ('closed', 'owner_confirmed', 'bridge_open'))
         and type(state['roles']) is dict and set(state['roles']) == set(ROLES), 'journal_invalid')
    fault = state['first_failure']
    need(fault is None or (type(fault) is list and len(fault) == 2 and
         all(type(value) is str and re.fullmatch('[A-Za-z_]{1,64}', value) for value in fault)), 'journal_invalid')
    allowed = {'claim_intent', 'restore_claim_intent', 'candidate_boot_intent', 'candidate_boot_verified',
               'six_span_sweep_verified', 'original_reset_intent', 'original_boot_verified', 'rom_handles_closed'}
    allowed |= {'candidate_' + name + '_' + end for name in ('application', 'partition', 'ota0_prefix')
                for end in ('intent', 'verified')}
    allowed |= {'restore_' + name + '_' + end for name in RESTORE_ORDER
                for end in ('intent', 'verified')}
    for role, row in state['roles'].items():
        need(type(row) is dict and set(row) == {'captures', 'events', 'restore_verified',
             'original_boot_allowed', 'handles_closed', 'settled_untouched'}
             and type(row['captures']) is dict and set(row['captures']) <= set(SPANS)
             and type(row['events']) is list and all(type(e) is str and e in allowed for e in row['events'])
             and all(type(row[k]) is bool for k in ('restore_verified', 'original_boot_allowed',
                                                   'handles_closed', 'settled_untouched')), 'journal_invalid')
        for name, pin in row['captures'].items():
            need(_pin(pin, SPANS[name][1]) and pin == request['roles'][role]['originals'][name], 'journal_invalid')
        events = row['events']
        if not events:
            need(not row['captures'] and not row['restore_verified'] and not row['original_boot_allowed']
                 and (not row['handles_closed'] or row['settled_untouched']), 'journal_invalid')
        else:
            need(events[0] == 'claim_intent' and not row['settled_untouched'], 'journal_invalid')
        for index, event in enumerate(events):
            prior = events[:index]
            if event.startswith('candidate_') and event.endswith('_verified'):
                need(event[:-8] + 'intent' in prior, 'journal_invalid')
            if event.startswith('restore_') and event.endswith('_verified') and event != 'restore_bootloader_verified':
                need(event[:-8] + 'intent' in prior, 'journal_invalid')
            if event == 'candidate_boot_verified':
                need(all('candidate_' + name + '_verified' in prior for name in
                         ('application', 'partition', 'ota0_prefix')), 'journal_invalid')
            if event in ('six_span_sweep_verified', 'original_reset_intent', 'original_boot_verified'):
                need(set(row['captures']) == set(SPANS), 'journal_invalid')
            if event == 'original_reset_intent':
                need('six_span_sweep_verified' in prior, 'journal_invalid')
            if event == 'original_boot_verified':
                need('original_reset_intent' in prior, 'journal_invalid')
        need(row['restore_verified'] == ('six_span_sweep_verified' in events), 'journal_invalid')
        need(row['original_boot_allowed'] == ('original_boot_verified' in events)
             and (not row['original_boot_allowed'] or row['restore_verified']), 'journal_invalid')
        need(not row['settled_untouched'] or (not events and not row['captures'] and row['handles_closed']), 'journal_invalid')
        need(not row['handles_closed'] or row['settled_untouched']
             or 'candidate_boot_verified' in events or 'rom_handles_closed' in events, 'journal_invalid')
        if row['original_boot_allowed'] and row['handles_closed']:
            need('rom_handles_closed' in events and
                 max(i for i, e in enumerate(events) if e == 'rom_handles_closed') >
                 max(i for i, e in enumerate(events) if e == 'original_boot_verified'), 'journal_invalid')
    need(not state['closed'] or (not state['bridge_open'] and all(_settled(row) for row in state['roles'].values())), 'journal_invalid')
    need(not state['owner_confirmed'] or state['closed'], 'journal_invalid')
    return state


def _pending_progress(previous, pending):
    need(previous['schema'] == pending['schema'] and previous['attempt'] == pending['attempt']
         and previous['request'] == pending['request'], 'journal_invalid')
    need(previous['first_failure'] is None or pending['first_failure'] == previous['first_failure'], 'journal_invalid')
    need(not previous['closed'] or pending['closed'], 'journal_invalid')
    for role in ROLES:
        before, after = previous['roles'][role], pending['roles'][role]
        need(after['events'][:len(before['events'])] == before['events']
             and all(after['captures'].get(name) == pin for name, pin in before['captures'].items()), 'journal_invalid')
        need(not before['original_boot_allowed'] or after['original_boot_allowed'], 'journal_invalid')
    return pending


def _trial_result(result, case):
    expected = {'first': (8, 1, 4), 'retained_rekey': (16, 2, 5),
                'recovery_after_A_commit': (8, 2, 5), 'recovery_after_B_commit': (8, 2, 5),
                'cancel': (0, 1, 0), 'revoke': (8, 1, 4), 'reset_preparation': (8, 1, 5)}
    need(type(result) is TrialResult and result.case == case and result.outcome in ('passed', 'failed')
         and type(result.stage) is str and re.fullmatch('[A-Za-z_]{1,64}', result.stage)
         and type(result.handles_closed) is bool
         and all(type(value) is int and value >= 0 for value in
                 (result.status_transfers, result.generations, result.checkpoints))
         and result.status_transfers <= expected[case][0] and result.generations <= expected[case][1]
         and result.checkpoints <= expected[case][2] and type(result.refusals) is tuple, 'trial_result_invalid')
    need(result.first_failure is None or (type(result.first_failure) is tuple and len(result.first_failure) == 2
         and all(type(value) is str and re.fullmatch('[A-Za-z_]{1,64}', value) for value in result.first_failure)), 'trial_result_invalid')
    if result.outcome == 'passed':
        refusals = (('B', 'STATUS'),) if case == 'retained_rekey' else (('A', 'SENDSTATUS'),) if case == 'revoke' else ()
        need(result.first_failure is None and result.handles_closed and result.refusals == refusals
             and (result.status_transfers, result.generations, result.checkpoints) == expected[case], 'trial_result_invalid')
    else:
        need(result.first_failure is not None, 'trial_result_invalid')


def _restore_role(journal, state, role, backend, budget):
    row = state['roles'][role]
    if row['original_boot_allowed'] and row['handles_closed']:
        return
    if row['original_boot_allowed']:
        # Original firmware has run. Even a verified boot followed by failed
        # ROM-handle closure never authorizes rewriting its newly changed NVS.
        row['handles_closed'] = _backend_close(backend)
        need(row['handles_closed'], 'rom_close_unconfirmed')
        journal.event(state, role, 'rom_handles_closed')
        return
    if not row['events']:
        need(not row['captures'], 'journal_invalid')
        row['restore_verified'] = False
        row['handles_closed'] = _backend_close(backend)
        row['settled_untouched'] = row['handles_closed']
        journal.save(state)
        return
    row['handles_closed'] = False
    journal.event(state, role, 'restore_claim_intent')
    _claim(backend, state['request'], role, budget, True)
    _guard(backend, state['request'], role, budget, True)
    need(backend.hold_rom(budget.check(True)) is True, 'rom_hold_unverified')
    _guard(backend, state['request'], role, budget, True)
    captured = (_originals(journal, state, role) if set(row['captures']) == set(SPANS)
                else _capture(journal, state, role, backend, budget, True))
    # Once original boot was attempted, normal firmware may have changed NVS.
    # An ambiguous durable reset checkpoint never authorizes overwriting it.
    boot_uncertain = ('original_reset_intent' in row['events']
                      and 'original_boot_verified' not in row['events'])
    mutated = any(event.startswith('candidate_') and event.endswith('_intent') for event in row['events'])
    if boot_uncertain:
        for name in SPANS:
            need(_read(backend, state['request'], role, name, budget, True) == captured[name],
                 'original_boot_state_uncertain')
    elif mutated:
        # Bootloader is never a candidate write. An unexpected change is held
        # for separately authorized repair instead of silently widening writes.
        need(_read(backend, state['request'], role, 'bootloader', budget, True)
             == captured['bootloader'], 'bootloader_changed')
        for name in RESTORE_ORDER:
            if name == 'bootloader':
                need(_read(backend, state['request'], role, name, budget, True)
                     == captured[name], 'restore_readback_failed')
                journal.event(state, role, 'restore_bootloader_verified')
            else:
                _write(journal, state, role, backend, name, captured[name], budget, True)
    # Complete independent sweep, including unchanged protected spans.
    for name in SPANS:
        need(_read(backend, state['request'], role, name, budget, True) == captured[name],
             'final_readback_failed')
    row['restore_verified'] = True
    journal.event(state, role, 'six_span_sweep_verified')
    journal.event(state, role, 'original_reset_intent')
    admission = _guard(backend, state['request'], role, budget, True)
    _admit(admission, state['request'], role)
    need(admission.layout_sha256 == state['request']['roles'][role]['originals']['partition']['sha256'],
         'original_layout_required')
    need(backend.reset_original(budget.check(True)) is True, 'original_reset_uncertain')
    budget.check(True)
    row['original_boot_allowed'] = True
    journal.event(state, role, 'original_boot_verified')
    row['handles_closed'] = _backend_close(backend)
    need(row['handles_closed'], 'rom_close_unconfirmed')
    journal.event(state, role, 'rom_handles_closed')


def _result(state):
    return CustodyResult(state['attempt'], state['observation'],
                        tuple(state['first_failure']) if state['first_failure'] else None,
                        {role: {key: state['roles'][role][key] for key in
                         ('restore_verified', 'original_boot_allowed', 'handles_closed', 'settled_untouched')}
                         for role in ROLES}, state['closed'], state['owner_confirmed'])


def _restore_all(journal, state, backends, budget):
    for role in ROLES:
        try:
            _restore_role(journal, state, role, backends[role], budget)
        except BaseException as error:
            _failure(state, 'restore_' + role, error)
            # Close only this owner's ROM handle; no reset or retry.
            state['roles'][role]['handles_closed'] = _backend_close(backends[role])
            try:
                if state['roles'][role]['handles_closed'] and state['roles'][role]['events']:
                    journal.event(state, role, 'rom_handles_closed')
                else:
                    journal.save(state)
            except BaseException:
                pass
    complete = all(_settled(row) for row in state['roles'].values())
    if complete:
        state['closed'] = True
        journal.save(state)
        _path(journal.root, ACTIVE).unlink()
    return complete


def _used(root, grant, pin):
    try:
        _once(_path(root, 'enrollment-candidate-used-' + grant['attempt']),
              canonical({'grant_sha256': pin}) + b'\n')
    except FileExistsError:
        raise Error('grant_used') from None


@contextmanager
def _ownership(root):
    """OS-held per-root lease; process death releases ownership, not custody."""
    need(_PROCESS.acquire(blocking=False), 'controller_busy')
    handle, locked = None, False
    try:
        handle = _path(root, 'enrollment-candidate-process.lock').open('a+b', buffering=0)
        handle.seek(0)
        try:
            if os.name == 'nt':
                import msvcrt
                msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            locked = True
        except OSError:
            raise Error('controller_busy') from None
        if os.fstat(handle.fileno()).st_size == 0:
            handle.write(b'\0')
            os.fsync(handle.fileno())
        _CLOSURE.failures = set()
        yield
    finally:
        if handle is not None:
            if locked:
                try:
                    handle.seek(0)
                    if os.name == 'nt':
                        import msvcrt
                        msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
                    else:
                        import fcntl
                        fcntl.flock(handle.fileno(), fcntl.LOCK_UN)
                finally:
                    handle.close()
            else:
                handle.close()
        _PROCESS.release()


def _backends(backends):
    need(type(backends) is dict and set(backends) == set(ROLES)
         and backends['A'] is not backends['B'], 'pair_invalid')
    for backend in backends.values():
        need(all(callable(getattr(backend, name, None)) for name in
             ('claim', 'guard', 'read', 'write', 'boot_candidate', 'hold_rom',
              'reset_original', 'close', 'assert_idle')), 'backend_invalid')
    return dict(backends)


def execute(root, request, grant_raw, grant_sha, images, backends, controller, *,
            utc=time.time, monotonic=time.monotonic):
    with _ownership(root):
        return _execute(root, request, grant_raw, grant_sha, images, backends,
                        controller, utc=utc, monotonic=monotonic)


def _execute(root, request, grant_raw, grant_sha, images, backends, controller, *, utc, monotonic):
    request = validate_request(request)
    grant = validate_grant(request, grant_raw, grant_sha, 'execute', utc)
    budget, backends = Budget(grant, utc, monotonic), _backends(backends)
    need(type(images) is dict and set(images) == {'application', 'partition'}, 'image_invalid')
    images = dict(images)
    for name, raw in images.items():
        need(type(raw) is bytes and descriptor(raw) == request['images'][name], 'image_changed')
    need(all(callable(getattr(controller, name, None)) for name in ('run', 'close', 'assert_idle'))
         and controller.assert_idle() is True, 'passive_handles_unconfirmed')
    need(not _path(root, ACTIVE).exists(), 'custody_held')
    _used(root, grant, grant_sha)
    journal = Journal(root, grant['attempt'])
    state = {'schema': 'OT-CANDIDATE-JOURNAL-1', 'attempt': grant['attempt'], 'request': request,
             'observation': 'not_observed', 'first_failure': None, 'closed': False,
             'owner_confirmed': False, 'bridge_open': False,
             'roles': {role: {'captures': {}, 'events': [], 'restore_verified': False,
                             'original_boot_allowed': False, 'handles_closed': False,
                             'settled_untouched': False}
                       for role in ROLES}}
    journal.save(state)
    _once(_path(root, ACTIVE), canonical({'attempt': grant['attempt'],
          'request_sha256': sha(canonical(request))}) + b'\n')
    stage = 'capture_A'
    try:
        # Both complete independently verified captures precede the first write.
        for role in ROLES:
            stage = 'capture_' + role
            journal.event(state, role, 'claim_intent')
            _claim(backends[role], request, role, budget)
            original = _capture(journal, state, role, backends[role], budget)
            admission = _guard(backends[role], request, role, budget)
            need(admission.layout_sha256 == sha(original['partition']), 'original_layout_required')
        for role in ROLES:
            stage = 'install_' + role
            _originals(journal, state, role)
            for name, raw in (('application', images['application'].ljust(SPANS['application'][1], b'\xff')),
                              ('partition', images['partition']),
                              ('ota0_prefix', b'\xff' * SPANS['ota0_prefix'][1])):
                _write(journal, state, role, backends[role], name, raw, budget)
            for name in ('bootloader', 'nvs', 'otadata'):
                need(_read(backends[role], request, role, name, budget)
                     == journal.original(role, name).read_bytes(), 'protected_changed')
            journal.event(state, role, 'candidate_boot_intent')
            _guard(backends[role], request, role, budget)
            need(backends[role].boot_candidate(budget.check()) is True, 'candidate_boot_uncertain')
            budget.check()
            need(_backend_close(backends[role]), 'rom_close_unconfirmed')
            state['roles'][role]['handles_closed'] = True
            journal.event(state, role, 'candidate_boot_verified')
        stage = 'candidate_case'
        state['bridge_open'] = True
        journal.save(state)
        result = controller.run(request['case'], request['group'], budget.check())
        budget.check()
        _trial_result(result, request['case'])
        state['observation'] = result.outcome
        if result.first_failure is not None:
            state['first_failure'] = list(result.first_failure)
        journal.save(state)
    except BaseException as error:
        _failure(state, stage, error)
        state['observation'] = 'failed'
    try:
        passive_closed = controller.close() is True and controller.assert_idle() is True
    except BaseException:
        passive_closed = False
    if not passive_closed:
        _failure(state, 'passive_cleanup', Error('passive_handles_unconfirmed'))
        for role in ROLES:
            state['roles'][role]['handles_closed'] = False
        journal.save(state)
        return _result(state)
    state['bridge_open'] = False
    journal.save(state)
    complete = _restore_all(journal, state, backends, budget)
    if complete and callable(getattr(controller, 'confirm_original', None)):
        try:
            ack = controller.confirm_original(budget.check(True))
            need(type(ack) is CheckpointAck and ack.schema == 'OT-CANDIDATE-ACK-1'
                 and ack.kind == 'usual_screen' and ack.roles == ROLES and type(ack.token) is str
                 and HEX32.fullmatch(ack.token), 'owner_confirmation_missing')
            budget.check(True)
            state['owner_confirmed'] = True
            journal.save(state)
        except BaseException as error:
            _failure(state, 'owner_confirmation', error)
            journal.save(state)
    return _result(state)


def recover(root, request, grant_raw, grant_sha, backends, passive_idle, *,
            utc=time.time, monotonic=time.monotonic):
    with _ownership(root):
        request = validate_request(request)
        grant = validate_grant(request, grant_raw, grant_sha, 'recover', utc)
        budget, backends = Budget(grant, utc, monotonic), _backends(backends)
        need(callable(passive_idle) and passive_idle() is True, 'passive_handles_unconfirmed')
        need(decode(_path(root, ACTIVE).read_bytes()) == {'attempt': grant['origin_attempt'],
             'request_sha256': sha(canonical(request))}, 'recovery_binding_invalid')
        journal = Journal(root, grant['origin_attempt'])
        state = _validate_state(decode(journal.file.read_bytes()), request, grant['origin_attempt'])
        pending = None
        if journal.pending.exists():
            pending = _validate_state(decode(journal.pending.read_bytes()), request, grant['origin_attempt'])
            state = _pending_progress(state, pending)
            if state['first_failure'] is None:
                state['first_failure'] = ['journal', 'journal_interrupted']
        # Validate every existing original before fresh authority is consumed.
        for role in ROLES:
            if set(state['roles'][role]['captures']) == set(SPANS):
                _originals(journal, state, role)
        _used(root, grant, grant_sha)
        if journal.pending.exists():
            interrupted = _path(root, 'enrollment-candidate-' + grant['attempt'] + '.interrupted')
            need(not interrupted.exists(), 'journal_pending')
            os.replace(journal.pending, interrupted)
        state['bridge_open'] = False
        journal.save(state)
        _restore_all(journal, state, backends, budget)
        return _result(state)
