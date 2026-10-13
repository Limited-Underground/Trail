"""Host-only recording and entry gate for unresolved manual recovery.

No device, SDK, grant, reset, release, adoption or retained-source authority is
available here. The only accepted disposition remains operationally blocked.
An eventual physical reconciliation receipt requires separately reviewed code.
"""
import ctypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re

SCHEMA = 'OT-MANUAL-RECOVERY-DISPOSITION-1'
STATUS = 'manual_recovery_observed_baseline_unresolved'
SPANS = {'application': 733184, 'bootloader': 32768, 'nvs': 12288,
         'ota0_prefix': 16384, 'otadata': 8192, 'partition': 4096}
PROVENANCE = {'candidate_result', 'capture_result', 'closure_review', 'events',
              'owner_ack', 'restore_journal', 'source_journal'}
BASE_REFS = {'journal', 'active_marker', 'capture_result', 'candidate_result',
             'candidate_events', 'capture_package', 'candidate_package',
             'capture_grant', 'candidate_grant', 'candidate_request', 'pins',
             'handoff', 'handoff_receipt', 'owner_observation', 'capture_request',
             'process_evidence'}


class RecoveryDispositionError(RuntimeError):
    """Fixed, non-identifying refusal category."""


def need(ok, category='recovery_evidence_invalid'):
    if not ok:
        raise RecoveryDispositionError(category)


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'),
                      allow_nan=False).encode('utf-8')


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def decode(raw):
    def unique(pairs):
        value = {}
        for key, item in pairs:
            need(key not in value)
            value[key] = item
        return value
    try:
        return json.loads(raw, object_pairs_hook=unique,
                          parse_constant=lambda _: need(False))
    except (UnicodeError, ValueError):
        raise RecoveryDispositionError('recovery_evidence_invalid') from None


def regular(path, private, directory=False):
    path, private = Path(path), Path(private)
    need(path.is_absolute() and private.is_absolute(), 'recovery_path_invalid')
    for part in (path, *path.parents, private, *private.parents):
        need(not part.is_symlink() and not part.is_junction(), 'recovery_path_invalid')
    need(private.name == '.private' and private.is_dir()
         and path.resolve().is_relative_to(private.resolve()), 'recovery_path_invalid')
    need(path.is_dir() if directory else path.is_file(), 'recovery_path_invalid')
    return path


def reference(path, private):
    path = regular(path, private)
    raw = path.read_bytes()
    return {'path': str(path), 'bytes': len(raw), 'sha256': sha(raw)}


def read_ref(ref, private):
    need(type(ref) is dict and set(ref) == {'path', 'bytes', 'sha256'}
         and type(ref['path']) is str and type(ref['bytes']) is int
         and 0 <= ref['bytes'] <= 16 * 1024 * 1024
         and type(ref['sha256']) is str and re.fullmatch('[0-9a-f]{64}', ref['sha256']))
    raw = regular(ref['path'], private).read_bytes()
    need(len(raw) == ref['bytes'] and sha(raw) == ref['sha256'], 'recovery_evidence_changed')
    return raw


def _names(attempt, candidate_attempt):
    return {'journal': f'enrollment-original-{attempt}.json',
            'active_marker': 'enrollment-original-active.json',
            'capture_result': f'enrollment-original-runner-{attempt}.json',
            'candidate_result': f'enrollment-candidate-runner-{candidate_attempt}.json',
            'candidate_events': f'enrollment-candidate-runner-{candidate_attempt}-events.jsonl',
            'capture_package': 'capture-package.json', 'candidate_package': 'candidate-package.json',
            'capture_grant': 'capture-grant.json', 'candidate_grant': 'candidate-grant.json',
            'candidate_request': 'candidate-request.json',
            'pins': f'enrollment-original-{attempt}-pins.json',
            'handoff': 'handoff.json', 'handoff_receipt': f'enrollment-original-{attempt}-handoff.json',
            'owner_observation': 'owner-manual-recovery-observation-20261011.json'}


def _no_transfer(session, attempt):
    # Runner refusal receipts are allowed. Any actual candidate journal or
    # pending/interrupted transfer needs another disposition path and review.
    for path in session.iterdir():
        name = path.name
        need(not name.endswith(('.pending', '.interrupted', '.transfer'))
             and not re.fullmatch(r'enrollment-candidate-[0-9a-f]{32}\.json', name)
             and name != 'enrollment-candidate-active.json', 'recovery_custody_unresolved')
    need(not (session / f'enrollment-original-{attempt}.pending').exists(),
         'recovery_custody_unresolved')


def validate_evidence(refs, private, session, attempt, candidate_attempt, predecessor_pid):
    need(type(attempt) is str and re.fullmatch('[0-9a-f]{32}', attempt)
         and type(candidate_attempt) is str and re.fullmatch('[0-9a-f]{32}', candidate_attempt)
         and attempt != candidate_attempt and (predecessor_pid is None
         or type(predecessor_pid) is int and 0 < predecessor_pid <= 0xffffffff))
    session = regular(session, private, True)
    expected = BASE_REFS | {'provenance_' + n for n in PROVENANCE}
    expected |= {f'original_{r}_{n}' for r in ('A', 'B') for n in SPANS}
    need(type(refs) is dict and set(refs) == expected)
    raw = {key: read_ref(ref, private) for key, ref in refs.items()}
    for key, name in _names(attempt, candidate_attempt).items():
        need(Path(refs[key]['path']) == session / name, 'recovery_path_invalid')
    _no_transfer(session, attempt)
    journal = decode(raw['journal'])
    need(type(journal) is dict and set(journal) == {'schema', 'attempt', 'candidate',
         'capture_complete', 'cleanup_failure', 'first_failure', 'grant_sha256', 'phase',
         'receipt', 'request', 'roles'}
         and journal['schema'] == 'OT-ORIGINAL-CAPTURE-JOURNAL-1'
         and journal['attempt'] == attempt and journal['candidate'] is None
         and journal['phase'] == 'held' and journal['capture_complete'] is True
         and journal['first_failure'] == ['freshness_A_nvs', 'capture_changed']
         and journal['cleanup_failure'] == ['release_A', 'rom_adapter_refused']
         and type(journal['roles']) is dict and set(journal['roles']) == {'A', 'B'})
    request = journal['request']
    need(type(request) is dict and set(request) == {'schema', 'runtime_sha256', 'roles', 'retained'}
         and request['schema'] == 'OT-RETAINED-ORIGINAL-REQUEST-1'
         and type(request['roles']) is dict and set(request['roles']) == {'A', 'B'}
         and type(request['retained']) is dict and set(request['retained']) == PROVENANCE
         and decode(raw['capture_request']) == request)
    request_sha = sha(canonical(request))
    need(decode(raw['active_marker']) == {'attempt': attempt, 'request_sha256': request_sha})
    need(journal['grant_sha256'] == sha(raw['capture_grant'])
         and journal['receipt'] == {k: refs['pins'][k] for k in ('bytes', 'sha256')})
    for name, ref in request['retained'].items():
        need(ref == refs['provenance_' + name])
    for role in ('A', 'B'):
        row = journal['roles'][role]
        need(type(row) is dict and set(row) == {'owner', 'entered', 'handles_closed',
             'original_boot_allowed', 'pins', 'reset_intent'}
             and row['owner'] == 'capture' and row['entered'] is True
             and row['handles_closed'] is True and row['original_boot_allowed'] is False
             and row['reset_intent'] is True and type(row['pins']) is dict
             and set(row['pins']) == set(SPANS))
        for name, count in SPANS.items():
            ref = refs[f'original_{role}_{name}']
            need(Path(ref['path']) == session / f'enrollment-original-{attempt}-{role}-{name}.bin'
                 and count == ref['bytes']
                 and row['pins'][name] == {k: ref[k] for k in ('bytes', 'sha256')})
    pins = decode(raw['pins'])
    need(pins == {'schema': 'OT-ORIGINAL-CAPTURE-PINS-1', 'attempt': attempt,
         'request_sha256': request_sha, 'runtime_sha256': request['runtime_sha256'],
         'roles': {r: dict(request['roles'][r], originals=journal['roles'][r]['pins']) for r in ('A', 'B')}})
    capture, candidate = decode(raw['capture_result']), decode(raw['candidate_result'])
    need(capture.get('schema') == 'OT-ORIGINAL-CAPTURE-RUNNER-RESULT-1'
         and capture.get('attempt') == attempt and capture.get('operation') == 'capture'
         and capture.get('outcome') == 'held' and capture.get('capture_complete') is True
         and capture.get('capture_custody_released') is False and capture.get('lease_released') is False
         and capture.get('request_sha256') == request_sha
         and capture.get('grant_sha256') == sha(raw['capture_grant'])
         and capture.get('package_sha256') == sha(raw['capture_package'])
         and capture.get('first_failure') == journal['first_failure']
         and capture.get('capture_cleanup_failure') == journal['cleanup_failure'])
    need(candidate.get('schema') == 'OT-CANDIDATE-RUNNER-RESULT-1'
         and candidate.get('attempt') == candidate_attempt and candidate.get('operation') == 'execute'
         and candidate.get('outcome') == 'failed' and candidate.get('custody') is None
         and candidate.get('lease_released') is False and candidate.get('owner_observed') is False
         and candidate.get('request_sha256') == sha(canonical(decode(raw['candidate_request'])))
         and candidate.get('grant_sha256') == sha(raw['candidate_grant'])
         and candidate.get('package_sha256') == sha(raw['candidate_package'])
         and candidate.get('first_failure') == ['ownership', 'capture_changed']
         and candidate.get('events') == {k: refs['candidate_events'][k] for k in ('bytes', 'sha256')}
         and raw['candidate_events'] == b'')
    c_request = decode(raw['candidate_request'])
    need(c_request.get('roles') == {r: {'device_binding': request['roles'][r]['device_binding'],
         'originals': journal['roles'][r]['pins']} for r in ('A', 'B')}
         and c_request.get('runtime_sha256') == request['runtime_sha256'])
    for key in ('capture', 'candidate'):
        package, grant = decode(raw[key + '_package']), decode(raw[key + '_grant'])
        expected_request = refs['capture_request'] if key == 'capture' else refs['candidate_request']
        need(package.get('request') == expected_request and package.get('grant') == refs[key + '_grant']
             and package.get('evidence_root') == str(session)
             and grant.get('attempt') == (attempt if key == 'capture' else candidate_attempt)
             and grant.get('request_sha256') == sha(canonical(decode(read_ref(expected_request, private)))))
    need(decode(raw['handoff']) == refs['candidate_package'])
    need(decode(raw['handoff_receipt']) == {'schema': 'OT-ORIGINAL-CAPTURE-HANDOFF-1',
         'candidate_package': refs['candidate_package'],
         'handoff': {k: refs['handoff'][k] for k in ('bytes', 'sha256')}})
    observation = decode(raw['owner_observation'])
    need(type(observation) is dict and observation.get('schema') == 'OT-OWNER-MANUAL-RECOVERY-OBSERVATION-1'
         and observation.get('task') == 'OT-0238c'
         and observation.get('limits') == {'candidate_installation': 'not_reached',
         'physical_acceptance': 'not_earned', 'byte_restoration': 'not_verified_by_manual_screen_check',
         'automated_custody': 'unresolved', 'new_trial_authorized': False}
         and type(observation.get('authorization')) is str and bool(observation['authorization'])
         and type(observation.get('observations')) is list and len(observation['observations']) == 2
         and [r.get('role') for r in observation['observations']] == ['A', 'B']
         and all(r.get('usual_screen') == 'owner_confirmed' for r in observation['observations']))
    process = decode(raw['process_evidence'])
    need(type(process) is dict and process.get('schema') == 'OT-PREDECESSOR-PROCESS-EVIDENCE-1'
         and process.get('pid') == predecessor_pid and process.get('session') == str(session)
         and process.get('capture_attempt') == attempt
         and (process.get('observed_dead') is True and process.get('identity_basis')
              == 'prior_observed_owner_launch_pid' if predecessor_pid is not None
              else process.get('observed_dead') is False and process.get('identity_basis')
              == 'unknown_no_durable_pid_record'))
    return journal


def require_process_dead(pid):
    """Read-only Windows process query; PID reuse deliberately remains blocked."""
    need(os.name == 'nt', 'recovery_process_probe_unavailable')
    need(type(pid) is int and 0 < pid <= 0xffffffff)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
    kernel.OpenProcess.restype = ctypes.c_void_p
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    handle = kernel.OpenProcess(0x1000, False, pid)
    if handle:
        kernel.CloseHandle(handle)
        raise RecoveryDispositionError('recovery_predecessor_live')
    need(ctypes.get_last_error() == 87, 'recovery_process_probe_unavailable')
    return True


def probe_hardware_lock(private):
    """Acquire/release the existing byte lock without creating/writing its file."""
    need(os.name == 'nt', 'recovery_lock_probe_unavailable')
    import msvcrt
    path = regular(Path(private) / 'enrollment-candidate-hardware.lock', private)
    try:
        need(path.read_bytes() == b'\0', 'recovery_lock_invalid')
        with path.open('r+b', buffering=0) as handle:
            need(handle.read() == b'\0', 'recovery_lock_invalid')
            handle.seek(0)
            msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
            try:
                need(os.fstat(handle.fileno()).st_size == 1, 'recovery_lock_invalid')
            finally:
                handle.seek(0)
                msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
        need(path.read_bytes() == b'\0', 'recovery_lock_invalid')
    except OSError:
        raise RecoveryDispositionError('recovery_hardware_lock_busy') from None
    return {'path': str(path), 'bytes': 1, 'sha256': sha(b'\0'),
            'os_byte_lock_observed_idle': True, 'content_unchanged': True}


def create_disposition(private, session, attempt, candidate_attempt, process_evidence,
                       predecessor_pid, destination):
    """Write a new sidecar only after all host facts pass; never alter legacy files."""
    private, session = Path(private), Path(session)
    regular(session, private, True)
    destination = Path(destination)
    regular(destination.parent, private, True)
    need(destination.parent != session and not destination.exists(), 'recovery_output_invalid')
    need(not destination.is_symlink() and not destination.is_junction(), 'recovery_path_invalid')
    refs = {key: reference(session / name, private)
            for key, name in _names(attempt, candidate_attempt).items()}
    journal = decode(read_ref(refs['journal'], private))
    package = decode(read_ref(refs['capture_package'], private))
    refs['capture_request'] = package['request']
    refs['process_evidence'] = reference(process_evidence, private)
    for name, ref in journal['request']['retained'].items():
        refs['provenance_' + name] = ref
    for role in ('A', 'B'):
        for name in SPANS:
            refs[f'original_{role}_{name}'] = reference(
                session / f'enrollment-original-{attempt}-{role}-{name}.bin', private)
    validate_evidence(refs, private, session, attempt, candidate_attempt, predecessor_pid)
    if predecessor_pid is not None:
        require_process_dead(predecessor_pid)
    lock = probe_hardware_lock(private)
    if predecessor_pid is not None:
        require_process_dead(predecessor_pid)
    # Recheck all references after host probes before committing the sidecar.
    validate_evidence(refs, private, session, attempt, candidate_attempt, predecessor_pid)
    value = {'schema': SCHEMA, 'status': STATUS, 'private_root': str(private),
             'session': str(session), 'capture_attempt': attempt, 'candidate_attempt': candidate_attempt,
             'predecessor_pid': predecessor_pid, 'predecessor_observed_dead': predecessor_pid is not None,
             'recorded_at': datetime.now(timezone.utc).isoformat(), 'references': refs,
             'hardware_lock_probe': lock, 'automatic_custody_released': False,
             'byte_equality': 'unknown', 'candidate_eligible': False,
             'physical_acceptance_earned': False,
             'legacy_journal_and_marker_preserved': True,
             'next_gate': 'separately_reviewed_fresh_device_reconciliation_receipt'}
    raw = canonical(value) + b'\n'
    with destination.open('xb', buffering=0) as handle:
        need(handle.write(raw) == len(raw), 'recovery_record_failed')
        os.fsync(handle.fileno())
    need(destination.read_bytes() == raw, 'recovery_record_failed')
    return reference(destination, private)


def require_candidate_entry_allowed(disposition_path, expected_sha256, expected_private_root):
    """Always refuses the supported unresolved state, before any workflow import."""
    private = Path(expected_private_root)
    raw = regular(disposition_path, private).read_bytes()
    need(type(expected_sha256) is str and re.fullmatch('[0-9a-f]{64}', expected_sha256)
         and sha(raw) == expected_sha256, 'recovery_disposition_changed')
    value = decode(raw)
    need(type(value) is dict and set(value) == {'schema', 'status', 'private_root', 'session',
         'capture_attempt', 'candidate_attempt', 'predecessor_pid', 'predecessor_observed_dead',
         'recorded_at', 'references', 'hardware_lock_probe', 'automatic_custody_released',
         'byte_equality', 'candidate_eligible', 'physical_acceptance_earned',
         'legacy_journal_and_marker_preserved', 'next_gate'}
         and value['schema'] == SCHEMA and value['status'] == STATUS
         and value['private_root'] == str(private)
         and value['automatic_custody_released'] is False and value['byte_equality'] == 'unknown'
         and value['candidate_eligible'] is False and value['physical_acceptance_earned'] is False
         and value['legacy_journal_and_marker_preserved'] is True
         and value['predecessor_observed_dead'] is (value['predecessor_pid'] is not None)
         and value['next_gate'] == 'separately_reviewed_fresh_device_reconciliation_receipt')
    validate_evidence(value['references'], private, value['session'], value['capture_attempt'],
                      value['candidate_attempt'], value['predecessor_pid'])
    need(value['hardware_lock_probe'] == {'path': str(private / 'enrollment-candidate-hardware.lock'),
         'bytes': 1, 'sha256': sha(b'\0'), 'os_byte_lock_observed_idle': True,
         'content_unchanged': True})
    raise RecoveryDispositionError('recovery_baseline_unresolved')
