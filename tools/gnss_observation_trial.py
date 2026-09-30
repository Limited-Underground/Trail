"""OT-0101e one-device GNSS observation custody engine. Import is inert.

The verified operator/runtime and private filesystem are the trust boundary.
An interrupted execute can only be recovered with a new restore-only grant.
No device identity, coordinates, NMEA sentence, or exception text is journaled.
"""
from pathlib import Path
import os
import time

import security_policy_execution as common
import security_policy_input_readback as nvs_parser

TrialError = common.ExecutionError
need, sha, canonical, decode = common.need, common.sha, common.canonical, common.decode
SPANS = {'bootloader': (0, 32768), 'partition': (0x8000, 4096),
         'ota': (0x9000, 8192), 'application': (0x10000, 733184), 'nvs': (0xd000, 12288)}
CANDIDATE = {'bytes': 591456, 'sha256': '2808d63b610619a69e14bd1809a56d921bded6efc2720fb08fd204aaaff9feb2'}
CONNECTION_CANDIDATES = {
    'A': {'bytes': 593200, 'sha256': '4351080e407ae8bf33370737e9313ecfb8ec6865e5bb1ed076d2fd094bc85c72'},
    'B': {'bytes': 593232, 'sha256': '6db05a3453870dbcf6c4c9583cd66ef5c64f25bc5b06a7b4eb9b5ae74d161388'},
    'A_STACK_8192': {'bytes': 593200, 'sha256': '56a1737e5fcdcca0514aba7f615b9934efc0f025d656b393b29633c10ec808b1'},
}
# The 3072-byte partition_table/partition-table.bin from the OT-0101e target
# hashes to PARTITION_SHA after FF padding to its 4096-byte protected span.
# This is a build pin, not proof of the selected device's live partition.
PARTITION_SHA = 'b7bbaf702afd377973aa2371f288bcea50548865d10e2cdada4d5e7f98a91601'
OTA_SHA = '7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f'
ACTIONS = ['rom_preflight', 'application_write', 'candidate_boot', 'gnss_observation',
           'application_restore', 'full_nvs_restore', 'original_reset']
RECOVERY_ACTIONS = ['rom_preflight', 'application_restore', 'full_nvs_restore', 'original_reset']
CONNECTION_ACTIONS = ['rom_preflight', 'application_write', 'candidate_boot', 'connection_capture',
                      'application_restore', 'full_nvs_restore', 'original_reset']
CONNECTION_RESULTS = ('ready_clock_observed', 'protocol_info_failure', 'not_ready',
                      'cancelled', 'timeout', 'capture_failed')
LIFECYCLE_ACTIONS = ['rom_preflight', 'application_write', 'candidate_boot',
                     'lifecycle_observation', 'one_candidate_warm_restart',
                     'application_restore', 'full_nvs_restore', 'original_reset']
ACTIVE = 'gnss-observation-active.lock'


def descriptor(raw):
    return {'bytes': len(raw), 'sha256': sha(raw)}


def validate_request(request):
    try:
        raw = canonical(request)
        need(len(raw) <= 4096, 'request_invalid')
        obj = decode(raw)
        diagnostic = obj.get('schema') == 'OT0101E-CONNECTION-REQUEST-1'
        lifecycle = obj.get('schema') == 'OT0101E-LIFECYCLE-REQUEST-1'
        need(set(obj) == {'schema', 'runtime_sha256', 'device_binding', 'candidate', 'protected',
                          'original_prefix', 'boot_compatibility', 'case'} |
             ({'profile'} if diagnostic or lifecycle else set()) |
             ({'plan', 'approved_revision'} if lifecycle else set()),
             'request_invalid')
        expected = CONNECTION_CANDIDATES.get(obj.get('profile')) if diagnostic or lifecycle else CANDIDATE
        need(((diagnostic and expected is not None and obj['case'] == 'capture-and-restore') or
              (lifecycle and obj['profile'] == 'A_STACK_8192' and obj['case'] == 'lifecycle-and-restore') or
              (not diagnostic and obj['schema'] == 'OT0101E-GNSS-REQUEST-1' and obj['case'] == 'observe-and-restore'))
             and obj['boot_compatibility'] == 'reviewed-ot0101e-image-header-compatibility'
             and common.HEX64.fullmatch(obj['runtime_sha256'])
             and common.HEX64.fullmatch(obj['device_binding']) and obj['candidate'] == expected,
             'request_invalid')
        if lifecycle:
            from gnss_lifecycle_observation import PLAN
            need(canonical(obj['plan']) == canonical(PLAN) and type(obj['approved_revision']) is int
                 and obj['approved_revision'] >= 2, 'request_invalid')
        need(set(obj['protected']) == {'bootloader', 'partition', 'ota'}, 'request_invalid')
        for name, expected in {**obj['protected'], 'original_prefix': obj['original_prefix']}.items():
            size = 589824 if name == 'original_prefix' else SPANS[name][1]
            need(set(expected) == {'bytes', 'sha256'} and type(expected['bytes']) is int
                 and expected['bytes'] == size and common.HEX64.fullmatch(expected['sha256']), 'request_invalid')
        need(obj['protected']['partition']['sha256'] == PARTITION_SHA
             and obj['protected']['ota']['sha256'] == OTA_SHA, 'boot_selection_invalid')
        return obj
    except Exception:
        raise TrialError('request_invalid') from None


def validate_grant(request, raw, expected_sha, operation, utc):
    try:
        need(type(raw) is bytes and len(raw) <= 4096 and sha(raw) == expected_sha, 'authority_invalid')
        grant = decode(raw)
        need(set(grant) == {'schema', 'attempt', 'request_sha256', 'runtime_sha256', 'operation',
                           'origin_attempt', 'attempt_count', 'actions', 'issued_utc', 'expires_utc'}, 'authority_invalid')
        diagnostic = request['schema'] == 'OT0101E-CONNECTION-REQUEST-1'
        lifecycle = request['schema'] == 'OT0101E-LIFECYCLE-REQUEST-1'
        schema = ('OT0101E-LIFECYCLE-GRANT-1' if lifecycle else
                  'OT0101E-CONNECTION-GRANT-1' if diagnostic else 'OT0101E-GNSS-GRANT-1')
        actions = LIFECYCLE_ACTIONS if lifecycle else CONNECTION_ACTIONS if diagnostic else ACTIONS
        need(grant['schema'] == schema and common.HEX32.fullmatch(grant['attempt'])
             and grant['request_sha256'] == sha(canonical(request))
             and grant['runtime_sha256'] == request['runtime_sha256']
             and grant['operation'] == operation and type(grant['attempt_count']) is int
             and grant['attempt_count'] == 1
             and grant['actions'] == (actions if operation == 'execute' else RECOVERY_ACTIONS)
             and type(grant['issued_utc']) is int and type(grant['expires_utc']) is int
             and 0 < grant['expires_utc'] - grant['issued_utc'] <= 3600
             and grant['issued_utc'] <= utc() < grant['expires_utc'], 'authority_invalid')
        need(grant['origin_attempt'] is None if operation == 'execute' else
             bool(common.HEX32.fullmatch(grant['origin_attempt'])), 'authority_invalid')
        return grant
    except Exception:
        raise TrialError('authority_invalid') from None


def inspect_baseline(request, captured):
    need(set(captured) == set(SPANS), 'capture_invalid')
    for name, (_, size) in SPANS.items():
        need(type(captured[name]) is bytes and len(captured[name]) == size, 'capture_invalid')
    for name, expected in request['protected'].items():
        need(descriptor(captured[name]) == expected, 'protected_mismatch')
    need(descriptor(captured['application'][:589824]) == request['original_prefix'], 'original_mismatch')
    nvs_parser._parse(captured['nvs'], False)
    safe_reset_marker(captured['nvs'])
    return True


def validate_connection_observation(result):
    need(type(result) is dict and set(result) == {'schema', 'result', 'capture'} and
         result['schema'] == 'OT0101E-CONNECTION-OBSERVATION-1' and
         result['result'] in CONNECTION_RESULTS, 'observation_invalid')
    capture = result['capture']
    need(capture is None or (type(capture) is dict and set(capture) == {'bytes', 'sha256'} and
         type(capture['bytes']) is int and 0 < capture['bytes'] <= 2 * 1024 * 1024 and
         type(capture['sha256']) is str and common.HEX64.fullmatch(capture['sha256'])), 'observation_invalid')
    need(result['result'] in ('capture_failed', 'timeout', 'cancelled') or capture is not None,
         'observation_invalid')
    return result


def safe_reset_marker(raw):
    namespaces, live = nvs_parser._parse(raw, False)
    reset_indices = {index for index, name in namespaces.items() if name == b'ot_reset_v1'}
    # Ordinary startup creates an empty marker namespace. No live marker key
    # may remain: restore()/continue_cleanup() runs before eval reset guards.
    need(not any(index in reset_indices for index, _, _ in live), 'pending_reset_storage')
    return True


def path(root, name):
    return common.private_path(root, name)


def write_once(destination, raw):
    fd = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'wb', buffering=0) as stream:
        need(stream.write(raw) == len(raw), 'durable_write_failed')
        os.fsync(stream.fileno())
    need(destination.read_bytes() == raw, 'durable_write_failed')


class Journal:
    def __init__(self, root, attempt):
        need(bool(common.HEX32.fullmatch(attempt)), 'attempt_invalid')
        self.root, self.attempt = root, attempt
        self.path = path(root, f'gnss-observation-{attempt}.json')

    def save(self, state):
        # A failed/crashed temporary write leaves the previous durable state intact.
        temporary = path(self.root, f'gnss-observation-{self.attempt}.pending')
        need(not temporary.exists(), 'journal_pending_recovery_required')
        raw = canonical(state) + b'\n'
        write_once(temporary, raw)
        os.replace(temporary, self.path)
        need(self.path.read_bytes() == raw, 'durable_write_failed')

    def load(self):
        raw = self.path.read_bytes()
        need(len(raw) <= 32768, 'journal_invalid')
        state = decode(raw)
        need(state['attempt'] == self.attempt and state['schema'] == 'OT0101E-GNSS-JOURNAL-1', 'journal_invalid')
        return state

    def event(self, state, event):
        state['events'].append(event)
        self.save(state)


def checked(backend, binding):
    need(backend.reverify(binding) is True, 'device_binding_changed')


def _originals(root, journal, state):
    need(set(state['captures']) == set(SPANS), 'capture_incomplete')
    captured = {}
    for name, (_, size) in SPANS.items():
        raw = path(root, f'gnss-observation-{journal.attempt}-{name}.bin').read_bytes()
        need(len(raw) == size and descriptor(raw) == state['captures'][name], 'original_capture_changed')
        captured[name] = raw
    return captured


def _restore(root, request, backend, journal, state, *, recovery=False):
    captured = _originals(root, journal, state)
    if recovery:
        backend.bind_recovery_originals(captured)
    else:
        backend.bind_originals(captured)
    # Recheck all protected spans before a restoration write; never overwrite them.
    for name in ('bootloader', 'partition', 'ota'):
        checked(backend, request['device_binding'])
        need(backend.read(*SPANS[name]) == captured[name], 'protected_changed')
    for name in ('application', 'nvs'):
        checked(backend, request['device_binding'])
        journal.event(state, 'restore_' + name + '_intent')
        backend.write(SPANS[name][0], captured[name])
        checked(backend, request['device_binding'])
        need(backend.read(*SPANS[name]) == captured[name], 'restore_readback_failed')
        journal.event(state, 'restore_' + name + '_verified')
    # Independent final sweep catches cross-span corruption caused by later writes.
    for name in SPANS:
        checked(backend, request['device_binding'])
        need(backend.read(*SPANS[name]) == captured[name], 'final_readback_failed')
    journal.event(state, 'original_reset_intent')
    checked(backend, request['device_binding'])
    need(backend.reset_original() is True, 'original_reset_uncertain')
    journal.event(state, 'restored_and_reset')
    state['closed'] = True
    journal.save(state)
    path(root, ACTIVE).unlink()
    return {'restored': True, 'observation': state['observation'], 'attempt': journal.attempt}


def _hold(backend):
    try:
        backend.hold()
    except BaseException:
        pass


@common.single_process
def execute(root, request, grant_raw, grant_sha, candidate, backend, observe, *, utc=time.time):
    request = validate_request(request)
    grant = validate_grant(request, grant_raw, grant_sha, 'execute', utc)
    need(type(candidate) is bytes and descriptor(candidate) == request['candidate'], 'candidate_changed')
    # Share the historic process lease, and refuse any old durable hardware custody.
    need(not any(Path(root, '.private').glob('*-active.lock')), 'custody_held')
    common.exclusive(path(root, 'gnss-observation-used-' + grant['attempt']), {'grant_sha256': grant_sha})
    journal = Journal(root, grant['attempt'])
    state = {'schema': 'OT0101E-GNSS-JOURNAL-1', 'attempt': grant['attempt'], 'request': request,
             'captures': {}, 'events': [], 'observation': 'not_observed', 'closed': False}
    journal.save(state)
    common.exclusive(path(root, ACTIVE), {'attempt': grant['attempt'], 'request_sha256': sha(canonical(request))})
    try:
        journal.event(state, 'claim_intent')
        need(backend.claim(request['device_binding']) is True, 'device_claim_failed')
        captured = {}
        for name, span in SPANS.items():
            checked(backend, request['device_binding'])
            raw = backend.read(*span)
            need(type(raw) is bytes and len(raw) == span[1], 'capture_invalid')
            write_once(path(root, f'gnss-observation-{journal.attempt}-{name}.bin'), raw)
            state['captures'][name] = descriptor(raw)
            journal.save(state)
            captured[name] = raw
        inspect_baseline(request, captured)
        backend.bind_originals(captured)
        # Deadline is rechecked after potentially lengthy backups, before mutation.
        validate_grant(request, grant_raw, grant_sha, 'execute', utc)
        checked(backend, request['device_binding'])
        padded = candidate + b'\xff' * (SPANS['application'][1] - len(candidate))
        journal.event(state, 'candidate_write_intent')
        backend.write(SPANS['application'][0], padded)
        checked(backend, request['device_binding'])
        need(backend.read(*SPANS['application']) == padded, 'candidate_readback_failed')
        validate_grant(request, grant_raw, grant_sha, 'execute', utc)
        journal.event(state, 'candidate_boot_intent')
        checked(backend, request['device_binding'])
        need(backend.boot_candidate() is True, 'candidate_boot_uncertain')
        validate_grant(request, grant_raw, grant_sha, 'execute', utc)
        diagnostic = request['schema'] == 'OT0101E-CONNECTION-REQUEST-1'
        lifecycle = request['schema'] == 'OT0101E-LIFECYCLE-REQUEST-1'
        journal.event(state, 'lifecycle_observation_intent' if lifecycle else
                      'connection_capture_intent' if diagnostic else 'gnss_observation_intent')
        if lifecycle:
            state['lifecycle_cases'] = []
            state['startup_observations'] = {}
            def record_startup(stage):
                value = getattr(backend, 'startup_diagnostics', None)
                if value is not None:
                    from ble_startup_diagnostics import validate_startup_result
                    validate_startup_result(value)
                    raw = canonical(value) + b'\n'
                    need(len(raw) <= 16384, 'startup_observation_invalid')
                    name = f'lifecycle-startup-{grant["attempt"]}-{stage}.json'
                    write_once(path(root, name), raw)
                    state['startup_observations'][stage] = descriptor(raw)
                    journal.save(state)
                    need(not any(item['category'] in ('self_check_fail', 'runtime_fail', 'panic',
                             'interrupt_watchdog', 'task_watchdog', 'stack_canary', 'stack_overflow')
                             for item in value['markers']), 'startup_fault')
            record_startup('initial')
            def record_case(item):
                from gnss_lifecycle_observation import validate_cases
                value = state['lifecycle_cases'] + [item]
                validate_cases(value)
                state['lifecycle_cases'] = value
                journal.save(state)
            def warm_restart(prefix):
                from gnss_lifecycle_observation import validate_restart_prefix
                validate_restart_prefix(prefix)
                need(prefix == state['lifecycle_cases'], 'restart_phase_invalid')
                need('candidate_warm_restart_intent' not in state['events'], 'restart_already_used')
                current_grant = validate_grant(request, grant_raw, grant_sha, 'execute', utc)
                need(current_grant['expires_utc'] - utc() >= 720, 'restart_budget_invalid')
                journal.event(state, 'candidate_warm_restart_intent')
                checked(backend, request['device_binding'])
                validate_grant(request, grant_raw, grant_sha, 'execute', utc)
                # Existing transport rechecks exact candidate bytes before boot.
                need(backend.boot_candidate() is True, 'candidate_warm_restart_uncertain')
                journal.event(state, 'candidate_warm_restart_verified')
                record_startup('warm')
                return True
            result = observe(warm_restart, record_case)
            from gnss_lifecycle_observation import validate_result
            validate_result(result)
            need(result['cases'] == state['lifecycle_cases'], 'observation_invalid')
            need(result['outcome'] != 'passed' or
                 'candidate_warm_restart_verified' in state['events'], 'observation_invalid')
        else:
            result = observe()
        if diagnostic:
            validate_connection_observation(result)
        elif lifecycle:
            pass
        elif type(result) is dict:
            statuses = ('fix_observed', 'no_fix_observed', 'stale_observed',
                        'invalid_observed', 'unavailable')
            need(set(result) == {'first_status', 'second_status', 'clock_advanced'}
                 and result['first_status'] in statuses and
                 result['second_status'] in statuses and
                 type(result['clock_advanced']) is bool, 'observation_invalid')
        else:
            need(result in ('fix_observed', 'no_fix_observed', 'stale_observed',
                            'invalid_observed', 'unavailable', 'cancelled', 'timeout',
                            'connection_not_ready'),
                 'observation_invalid')
        state['observation'] = result
        journal.event(state, 'lifecycle_observed' if lifecycle else
                      'connection_observed' if diagnostic else 'gnss_observed')
    except BaseException:
        # Any uncertain application mutation is recoverable, never retried.
        state['observation'] = 'trial_failed'
        try:
            journal.event(state, 'trial_failed')
        except BaseException:
            _hold(backend)
            raise TrialError('custody_held_recovery_required') from None
        if 'candidate_write_intent' not in state['events']:
            _hold(backend)
            raise TrialError('custody_held_prewrite') from None
    try:
        return _restore(root, request, backend, journal, state)
    except BaseException:
        _hold(backend)
        raise TrialError('custody_held_recovery_required') from None


@common.single_process
def recover(root, request, grant_raw, grant_sha, backend, *, utc=time.time):
    request = validate_request(request)
    grant = validate_grant(request, grant_raw, grant_sha, 'recover', utc)
    active = decode(path(root, ACTIVE).read_bytes())
    need(active == {'attempt': grant['origin_attempt'], 'request_sha256': sha(canonical(request))}, 'recovery_binding_invalid')
    journal = Journal(root, grant['origin_attempt'])
    state = journal.load()
    need(state['request'] == request and type(state['closed']) is bool, 'recovery_binding_invalid')
    common.exclusive(path(root, 'gnss-observation-used-' + grant['attempt']), {'grant_sha256': grant_sha})
    try:
        pending = path(root, f'gnss-observation-{journal.attempt}.pending')
        if pending.exists():
            # Preserve an interrupted snapshot, but never adopt an uncommitted
            # event: no operation may follow an unsuccessful journal.save.
            os.replace(pending, path(root, f'gnss-observation-{grant["attempt"]}.interrupted'))
        need(backend.claim(request['device_binding']) is True, 'device_claim_failed')
        journal.event(state, 'recovery_claimed')
        if 'candidate_write_intent' not in state['events']:
            # Capture interruption cannot have changed flash. Finish actual
            # captures, verify prior bytes, and return to the original image.
            for name, span in SPANS.items():
                checked(backend, request['device_binding'])
                raw = backend.read(*span)
                need(type(raw) is bytes and len(raw) == span[1], 'capture_invalid')
                destination = path(root, f'gnss-observation-{journal.attempt}-{name}.bin')
                if destination.exists():
                    need(destination.read_bytes() == raw, 'prewrite_original_changed')
                else:
                    write_once(destination, raw)
                if name in state['captures']:
                    need(state['captures'][name] == descriptor(raw), 'prewrite_original_changed')
                state['captures'][name] = descriptor(raw)
                journal.save(state)
            captured = _originals(root, journal, state)
            # Original reset does not boot the candidate, so retained evaluation
            # namespaces do not prevent restoring the pretrial running state.
            for name, expected in request['protected'].items():
                need(descriptor(captured[name]) == expected, 'protected_mismatch')
            need(descriptor(captured['application'][:589824]) == request['original_prefix'], 'original_mismatch')
            safe_reset_marker(captured['nvs'])
            backend.bind_recovery_originals(captured)
            journal.event(state, 'original_reset_intent')
            checked(backend, request['device_binding'])
            need(backend.reset_original() is True, 'original_reset_uncertain')
            journal.event(state, 'restored_and_reset')
            state['closed'] = True
            journal.save(state)
            path(root, ACTIVE).unlink()
            return {'restored': True, 'observation': state['observation'], 'attempt': journal.attempt}
        return _restore(root, request, backend, journal, state, recovery=True)
    except BaseException:
        _hold(backend)
        raise TrialError('custody_held_recovery_required') from None
