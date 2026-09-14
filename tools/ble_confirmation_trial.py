"""OT-218 one-device BLE trial custody engine. Import is inert; no grant issuer.

The verified operator/runtime and private filesystem are the trust boundary.
An interrupted execute can only be recovered with a new restore-only grant.
No private identity, BLE payload or exception text is journaled.
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
CANDIDATE = {'bytes': 730928, 'sha256': '444591760db347c9ce395287ce0cad315d6f2fe736e633170e2c1064b58da3d6'}
PARTITION_SHA = 'b7bbaf702afd377973aa2371f288bcea50548865d10e2cdada4d5e7f98a91601'
OTA_SHA = '7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f'
NAMESPACES = {b'ot216_boot', b'ot216_ia', b'ot216_ib', b'ot216_ta', b'ot216_tb', b'ot216_ra', b'ot216_rb'}
ACTIONS = ['rom_preflight', 'application_write', 'candidate_boot', 'ble_confirm_observation',
           'application_restore', 'full_nvs_restore', 'original_reset']
RECOVERY_ACTIONS = ['rom_preflight', 'application_restore', 'full_nvs_restore', 'original_reset']
ACTIVE = 'ble-confirmation-active.lock'


def descriptor(raw):
    return {'bytes': len(raw), 'sha256': sha(raw)}


def validate_request(request):
    try:
        obj = decode(canonical(request))
        need(set(obj) == {'schema', 'runtime_sha256', 'device_binding', 'candidate', 'protected',
                          'original_prefix', 'boot_compatibility', 'case'}, 'request_invalid')
        need(obj['schema'] == 'OT218-BLE-REQUEST-1' and obj['case'] == 'confirm-and-restore'
             and obj['boot_compatibility'] == 'inferred-ot208-image-header-compatibility'
             and common.HEX64.fullmatch(obj['runtime_sha256'])
             and common.HEX64.fullmatch(obj['device_binding']) and obj['candidate'] == CANDIDATE,
             'request_invalid')
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
        need(grant['schema'] == 'OT218-BLE-GRANT-1' and common.HEX32.fullmatch(grant['attempt'])
             and grant['request_sha256'] == sha(canonical(request))
             and grant['runtime_sha256'] == request['runtime_sha256']
             and grant['operation'] == operation and type(grant['attempt_count']) is int
             and grant['attempt_count'] == 1
             and grant['actions'] == (ACTIONS if operation == 'execute' else RECOVERY_ACTIONS)
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
    namespaces, live = nvs_parser._parse(captured['nvs'], False)
    need(NAMESPACES.isdisjoint(namespaces.values()), 'evaluation_storage_not_fresh')
    safe_reset_marker(captured['nvs'])
    return True


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
        self.path = path(root, f'ble-confirmation-{attempt}.json')

    def save(self, state):
        # A failed/crashed temporary write leaves the previous durable state intact.
        temporary = path(self.root, f'ble-confirmation-{self.attempt}.pending')
        need(not temporary.exists(), 'journal_pending_recovery_required')
        raw = canonical(state) + b'\n'
        write_once(temporary, raw)
        os.replace(temporary, self.path)
        need(self.path.read_bytes() == raw, 'durable_write_failed')

    def load(self):
        raw = self.path.read_bytes()
        need(len(raw) <= 32768, 'journal_invalid')
        state = decode(raw)
        need(state['attempt'] == self.attempt and state['schema'] == 'OT218-BLE-JOURNAL-1', 'journal_invalid')
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
        raw = path(root, f'ble-confirmation-{journal.attempt}-{name}.bin').read_bytes()
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
    need(type(candidate) is bytes and descriptor(candidate) == CANDIDATE, 'candidate_changed')
    # Share the historic process lease, and refuse any old durable hardware custody.
    need(not any(Path(root, '.private').glob('*-active.lock')), 'custody_held')
    common.exclusive(path(root, 'ble-confirmation-used-' + grant['attempt']), {'grant_sha256': grant_sha})
    journal = Journal(root, grant['attempt'])
    state = {'schema': 'OT218-BLE-JOURNAL-1', 'attempt': grant['attempt'], 'request': request,
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
            write_once(path(root, f'ble-confirmation-{journal.attempt}-{name}.bin'), raw)
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
        journal.event(state, 'confirmation_observation_intent')
        result = observe()
        need(result in ('local_confirmed', 'refused', 'cancelled', 'unavailable', 'timeout'), 'observation_invalid')
        state['observation'] = result
        journal.event(state, 'confirmation_observed')
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
    common.exclusive(path(root, 'ble-confirmation-used-' + grant['attempt']), {'grant_sha256': grant_sha})
    try:
        pending = path(root, f'ble-confirmation-{journal.attempt}.pending')
        if pending.exists():
            # Preserve an interrupted snapshot, but never adopt an uncommitted
            # event: no operation may follow an unsuccessful journal.save.
            os.replace(pending, path(root, f'ble-confirmation-{grant["attempt"]}.interrupted'))
        need(backend.claim(request['device_binding']) is True, 'device_claim_failed')
        journal.event(state, 'recovery_claimed')
        if 'candidate_write_intent' not in state['events']:
            # Capture interruption cannot have changed flash. Finish actual
            # captures, verify prior bytes, and return to the original image.
            for name, span in SPANS.items():
                checked(backend, request['device_binding'])
                raw = backend.read(*span)
                need(type(raw) is bytes and len(raw) == span[1], 'capture_invalid')
                destination = path(root, f'ble-confirmation-{journal.attempt}-{name}.bin')
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
