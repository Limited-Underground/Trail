"""Nonradio two-role custody preparation; import is inert, no grant issuer.

Transport injection: distinct existing ble_confirmation_trial_transport.Transport
instances, each constructed with its exact padded candidate and opaque identity.
Bridge owns passive handles and implements call/close/assert_idle. ROM operations
are forbidden until closure is positively established. Recovery needs no candidate.
The lock-neutral restoration sequence is mechanically derived from OT218 _restore:
protected checks, application/NVS writes and readbacks, independent all-span sweep,
then original reset. Historical engine and transport remain unchanged.
"""
from pathlib import Path
import os
import time
import ble_confirmation_trial as base

common = base.common
need, sha, canonical, decode = base.need, base.sha, base.canonical, base.decode
Error = base.TrialError
SPANS = base.SPANS
ACTIVE = 'pair-confirmation-active.lock'
ROLES = ('A', 'B')
ACTIONS = ['capture_both', 'sequential_candidate_write', 'candidate_boot',
           'nonradio_pair_bridge', 'restore_both', 'original_reset']
RECOVERY_ACTIONS = ['restore_both', 'original_reset']
OUTCOMES = ('local_confirmed', 'refused', 'cancelled', 'unavailable', 'timeout')
PAIR_NAMESPACES = {b'ot230_boot', b'ot230_role', b'ot230_tx', b'ot230_rx'}


def inspect_baseline(request, captured):
    base.inspect_baseline(request, captured)
    namespaces, _ = base.nvs_parser._parse(captured['nvs'], False)
    need(PAIR_NAMESPACES.isdisjoint(namespaces.values()), 'pair_storage_not_fresh')
    return True


def validate_request(request):
    need(type(request) is dict and set(request) == {'schema', 'runtime_sha256',
         'bridge_sha256', 'case', 'roles'}, 'request_invalid')
    need(request['schema'] == 'OT-PAIR-REQUEST-1' and request['case'] in ('nonradio-confirm-and-restore', 'radio-confirm-and-restore'), 'request_invalid')
    need(all(type(request[k]) is str and common.HEX64.fullmatch(request[k]) for k in ('runtime_sha256', 'bridge_sha256')), 'request_invalid')
    need(type(request['roles']) is dict and set(request['roles']) == set(ROLES), 'request_invalid')
    for row in request['roles'].values():
        need(type(row) is dict and set(row) == {'device_binding', 'candidate', 'protected', 'original_prefix'}, 'request_invalid')
        need(type(row['device_binding']) is str and common.HEX64.fullmatch(row['device_binding']), 'request_invalid')
        d = row['candidate']
        need(type(d) is dict and set(d) == {'bytes', 'sha256'} and type(d['bytes']) is int and 0 < d['bytes'] <= SPANS['application'][1] and type(d['sha256']) is str and common.HEX64.fullmatch(d['sha256']), 'candidate_invalid')
        need(type(row['protected']) is dict and set(row['protected']) == {'bootloader', 'partition', 'ota'}, 'request_invalid')
        for name, value in dict(row['protected'], original_prefix=row['original_prefix']).items():
            size = 589824 if name == 'original_prefix' else SPANS[name][1]
            need(type(value) is dict and set(value) == {'bytes', 'sha256'} and type(value['bytes']) is int and value['bytes'] == size and type(value['sha256']) is str and common.HEX64.fullmatch(value['sha256']), 'request_invalid')
        need(row['protected']['partition']['sha256'] == base.PARTITION_SHA and row['protected']['ota']['sha256'] == base.OTA_SHA, 'layout_invalid')
    need(request['roles']['A']['device_binding'] != request['roles']['B']['device_binding'], 'duplicate_identity')
    return decode(canonical(request))


def actions_for(request, operation):
    need(operation in ('execute', 'recover'), 'grant_invalid')
    if operation == 'recover':
        return list(RECOVERY_ACTIONS)
    need(request['case'] in ('nonradio-confirm-and-restore', 'radio-confirm-and-restore'), 'request_invalid')
    return [action if action != 'nonradio_pair_bridge' or request['case'] == 'nonradio-confirm-and-restore'
            else 'bounded_radio_pair_bridge' for action in ACTIONS]


def validate_grant(request, raw, pin, operation, utc):
    # Preserve OT218 one-use/time/action checks with pair-specific fixed actions.
    need(type(raw) is bytes and len(raw) <= 4096 and sha(raw) == pin, 'grant_invalid')
    g = decode(raw)
    need(set(g) == {'schema', 'attempt', 'request_sha256', 'runtime_sha256', 'operation', 'origin_attempt', 'attempt_count', 'actions', 'issued_utc', 'expires_utc'}, 'grant_invalid')
    need(g['schema'] == 'OT-PAIR-GRANT-1' and type(g['attempt']) is str and common.HEX32.fullmatch(g['attempt']), 'grant_invalid')
    need(g['request_sha256'] == sha(canonical(request)) and g['runtime_sha256'] == request['runtime_sha256'] and g['operation'] == operation and type(g['attempt_count']) is int and g['attempt_count'] == 1, 'grant_invalid')
    need(g['actions'] == actions_for(request, operation), 'grant_invalid')
    need(type(g['issued_utc']) is int and type(g['expires_utc']) is int and 0 < g['expires_utc'] - g['issued_utc'] <= 3600 and g['issued_utc'] <= utc() < g['expires_utc'], 'grant_invalid')
    need(g['origin_attempt'] is None if operation == 'execute' else type(g['origin_attempt']) is str and common.HEX32.fullmatch(g['origin_attempt']), 'grant_invalid')
    return g


def path(root, name):
    return base.path(root, name)


class Journal:
    def __init__(self, root, attempt):
        self.root, self.attempt = root, attempt
        self.file = path(root, 'pair-confirmation-' + attempt + '.json')
        self.pending = path(root, 'pair-confirmation-' + attempt + '.pending')

    def save(self, state):
        need(not self.pending.exists(), 'journal_pending')
        raw = canonical(state) + b'\n'
        base.write_once(self.pending, raw)
        os.replace(self.pending, self.file)
        need(self.file.read_bytes() == raw, 'journal_failed')

    def event(self, state, role, event):
        state['roles'][role]['events'].append(event)
        self.save(state)

    def original_path(self, role, name):
        return path(self.root, f'pair-confirmation-{self.attempt}-{role}-{name}.bin')


def originals(journal, state, role):
    row = state['roles'][role]
    need(set(row['captures']) == set(SPANS), 'capture_incomplete')
    result = {}
    for name, (_, size) in SPANS.items():
        raw = journal.original_path(role, name).read_bytes()
        need(len(raw) == size and base.descriptor(raw) == row['captures'][name], 'original_changed')
        result[name] = raw
    return result


def capture(journal, state, role, backend):
    request = state['request']['roles'][role]
    for name, span in SPANS.items():
        base.checked(backend, request['device_binding'])
        raw = backend.read(*span)
        need(type(raw) is bytes and len(raw) == span[1], 'capture_invalid')
        dest = journal.original_path(role, name)
        if dest.exists():
            need(dest.read_bytes() == raw, 'prewrite_original_changed')
        else:
            base.write_once(dest, raw)
        previous = state['roles'][role]['captures'].get(name)
        need(previous is None or previous == base.descriptor(raw), 'prewrite_original_changed')
        state['roles'][role]['captures'][name] = base.descriptor(raw)
        journal.save(state)
    return originals(journal, state, role)


def restore_role(journal, state, role, backend, *, recovery=False):
    row = state['roles'][role]
    request = state['request']['roles'][role]
    if row['restored'] or not row['events']:
        return
    # Even claim/read can leave the original in ROM: restore its running state.
    if 'candidate_write_intent' not in row['events']:
        captured = capture(journal, state, role, backend)
        for name, expected in request['protected'].items():
            need(base.descriptor(captured[name]) == expected, 'protected_mismatch')
        need(base.descriptor(captured['application'][:589824]) == request['original_prefix'], 'original_mismatch')
        base.safe_reset_marker(captured['nvs'])
    else:
        captured = originals(journal, state, role)
    if recovery:
        backend.bind_recovery_originals(captured)
    else:
        backend.bind_originals(captured)
    for name in ('bootloader', 'partition', 'ota'):
        base.checked(backend, request['device_binding'])
        need(backend.read(*SPANS[name]) == captured[name], 'protected_changed')
    if 'candidate_write_intent' in row['events']:
        for name in ('application', 'nvs'):
            base.checked(backend, request['device_binding'])
            journal.event(state, role, 'restore_' + name + '_intent')
            backend.write(SPANS[name][0], captured[name])
            base.checked(backend, request['device_binding'])
            need(backend.read(*SPANS[name]) == captured[name], 'restore_readback_failed')
            journal.event(state, role, 'restore_' + name + '_verified')
    for name in SPANS:
        base.checked(backend, request['device_binding'])
        need(backend.read(*SPANS[name]) == captured[name], 'final_readback_failed')
    journal.event(state, role, 'original_reset_intent')
    base.checked(backend, request['device_binding'])
    need(backend.reset_original() is True, 'original_reset_uncertain')
    row['restored'] = True
    journal.event(state, role, 'restored_and_reset')


def restore_all(journal, state, backends):
    failed = False
    for role in ROLES:
        try:
            restore_role(journal, state, role, backends[role])
        except BaseException:
            failed = True
    need(not failed, 'custody_held_recovery_required')
    state['closed'] = True
    journal.save(state)
    path(journal.root, ACTIVE).unlink()
    return {'restored': True, 'observation': state['observation'], 'attempt': journal.attempt}


@common.single_process
def execute(root, request, grant_raw, grant_sha, candidates, backends, bridge, *, utc=time.time):
    request = validate_request(request)
    need(type(candidates) is dict and type(backends) is dict and set(candidates) == set(backends) == set(ROLES), 'pair_invalid')
    candidates, backends = dict(candidates), dict(backends)
    need(backends['A'] is not backends['B'], 'pair_invalid')
    g = validate_grant(request, grant_raw, grant_sha, 'execute', utc)
    for role in ROLES:
        need(type(candidates[role]) is bytes and base.descriptor(candidates[role]) == request['roles'][role]['candidate'], 'candidate_changed')
    need(callable(bridge) and callable(bridge.close) and callable(bridge.assert_idle) and bridge.assert_idle() is True, 'bridge_busy')
    need(not any(Path(root, '.private').glob('*-active.lock')), 'custody_held')
    common.exclusive(path(root, 'pair-confirmation-used-' + g['attempt']), {'grant_sha256': grant_sha})
    journal = Journal(root, g['attempt'])
    state = {'schema': 'OT-PAIR-JOURNAL-1', 'attempt': g['attempt'], 'request': request,
             'observation': 'not_observed', 'closed': False, 'bridge_open': False,
             'roles': {r: {'captures': {}, 'events': [], 'restored': False} for r in ROLES}}
    journal.save(state)
    common.exclusive(path(root, ACTIVE), {'attempt': g['attempt'], 'request_sha256': sha(canonical(request))})
    try:
        for role in ROLES:
            backend = backends[role]
            journal.event(state, role, 'claim_intent')
            need(backend.claim(request['roles'][role]['device_binding']) is True, 'claim_failed')
            raw = capture(journal, state, role, backend)
            inspect_baseline(request['roles'][role], raw)
            backend.bind_originals(raw)
        for role in ROLES:
            backend = backends[role]
            validate_grant(request, grant_raw, grant_sha, 'execute', utc)
            base.checked(backend, request['roles'][role]['device_binding'])
            need(base.descriptor(candidates[role]) == request['roles'][role]['candidate'], 'candidate_changed')
            padded = candidates[role].ljust(SPANS['application'][1], b'\xff')
            journal.event(state, role, 'candidate_write_intent')
            backend.write(SPANS['application'][0], padded)
            base.checked(backend, request['roles'][role]['device_binding'])
            need(backend.read(*SPANS['application']) == padded, 'candidate_readback_failed')
            validate_grant(request, grant_raw, grant_sha, 'execute', utc)
            journal.event(state, role, 'candidate_boot_intent')
            need(backend.boot_candidate() is True, 'candidate_boot_uncertain')
        validate_grant(request, grant_raw, grant_sha, 'execute', utc)
        state['bridge_open'] = True
        journal.save(state)
        try:
            result = bridge()
            need(result in OUTCOMES, 'observation_invalid')
            state['observation'] = result
        finally:
            need(bridge.close() is True and bridge.assert_idle() is True, 'serial_close_unconfirmed')
            state['bridge_open'] = False
            journal.save(state)
    except BaseException:
        state['observation'] = 'trial_failed'
        # Do not enter ROM while passive handle closure is uncertain.
        need(not state['bridge_open'], 'custody_held_serial_unconfirmed')
    return restore_all(journal, state, backends)


@common.single_process
def recover(root, request, grant_raw, grant_sha, backends, passive_idle, *, utc=time.time):
    request = validate_request(request)
    need(type(backends) is dict and set(backends) == set(ROLES), 'pair_invalid')
    backends = dict(backends)
    need(backends['A'] is not backends['B'], 'pair_invalid')
    g = validate_grant(request, grant_raw, grant_sha, 'recover', utc)
    need(passive_idle() is True, 'serial_close_unconfirmed')
    need(decode(path(root, ACTIVE).read_bytes()) == {'attempt': g['origin_attempt'], 'request_sha256': sha(canonical(request))}, 'recovery_binding_invalid')
    journal = Journal(root, g['origin_attempt'])
    state = decode(journal.file.read_bytes())
    need(state['schema'] == 'OT-PAIR-JOURNAL-1' and state['attempt'] == g['origin_attempt'] and state['request'] == request and state['closed'] is False, 'journal_invalid')
    common.exclusive(path(root, 'pair-confirmation-used-' + g['attempt']), {'grant_sha256': grant_sha})
    if journal.pending.exists():
        os.replace(journal.pending, path(root, 'pair-confirmation-' + g['attempt'] + '.interrupted'))
    # Claim each role independently; one failure must not skip the other's restore.
    failures = False
    for role in ROLES:
        row = state['roles'][role]
        if row['restored'] or not row['events']:
            continue
        try:
            need(backends[role].claim(request['roles'][role]['device_binding']) is True, 'claim_failed')
            restore_role(journal, state, role, backends[role], recovery=True)
        except BaseException:
            failures = True
    need(not failures, 'custody_held_recovery_required')
    state['bridge_open'] = False
    return restore_all(journal, state, backends)
