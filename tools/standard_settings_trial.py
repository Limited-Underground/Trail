"""One-use OT-0304 standard/settings read trial and complete-state restoration.

Import is inert. The verified operator, callback and private filesystem are the
trust boundary. No grant issuer, device selection, phone operation or general
flash API is supplied. A result describes the bounded existing-pair observation;
it does not attest runtime profile/capabilities or fresh first-use acceptance.
"""
from pathlib import Path
import os
import secrets
import time

import security_policy_execution as common
from ble_confirmation_trial import safe_reset_marker, write_once

TrialError = common.ExecutionError
need, sha, canonical, decode = common.need, common.sha, common.canonical, common.decode
SPANS = {'bootloader': (0, 32768), 'partition': (0x8000, 4096),
         'ota': (0x9000, 8192), 'nvs': (0xd000, 12288),
         'application': (0x10000, 733184),
         'state_0': (0xf00000, 524288), 'state_1': (0xf80000, 524288)}
PROTECTED = ('bootloader', 'partition', 'ota')
RESTORABLE = ('application', 'nvs', 'state_0', 'state_1')
CANDIDATE = {'bytes': 602096, 'sha256': '33c27be5dea55e38276eafe96f278504353ca5ab1749bec0e5e95db1668e5193'}
EXPECTED = {
    'bootloader': '002a0a4b5ea5329c93c39c105099490a01b0ed7d4bd167ee3a5b60322dd0b5b3',
    'partition': 'b7bbaf702afd377973aa2371f288bcea50548865d10e2cdada4d5e7f98a91601',
    'ota': '7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f',
    'application': '0b86589ef8ea4d4883bcf535b5d3732bf24189237b5b02803961af7c22115bf7'}
ACTIONS = ['rom_preflight', 'complete_original_capture', 'application_write',
           'candidate_boot', 'public_settings_read_observation', 'application_restore',
           'full_nvs_restore', 'state_restore', 'original_reset']
RECOVERY_ACTIONS = ['rom_preflight', 'application_restore', 'full_nvs_restore',
                    'state_restore', 'original_reset']
ACTIVE = 'standard-settings-active.lock'
PREFIX = 'standard-settings-'
OBSERVATIONS = ('not_observed', 'passed', 'failed', 'unavailable')
FAILURES = ('none', 'preflight_failed', 'candidate_failed', 'observation_failed', 'restore_failed')
EVENTS = {'preflight_idle', 'recovery_idle', 'claim_intent', 'capture_complete', 'complete_bound', 'candidate_write_intent', 'candidate_verified',
          'candidate_boot_intent', 'candidate_booted', 'observation_intent',
          'observation_closed', 'trial_failed', 'recovery_authorized',
          'recovery_claim_intent', 'restoration_intent', 'restore_failed',
          'original_boot_intent', 'original_booted', 'closed'} | {
              'restore_' + name + suffix for name in RESTORABLE
              for suffix in ('_intent', '_verified')}


def descriptor(raw):
    return {'bytes': len(raw), 'sha256': sha(raw)}


def _hex(value, pattern=common.HEX64):
    return type(value) is str and bool(pattern.fullmatch(value))


def _descriptor(value, size):
    return (type(value) is dict and set(value) == {'bytes', 'sha256'}
            and type(value['bytes']) is int and value['bytes'] == size
            and _hex(value['sha256']))


def _source_path(raw, root=None):
    need(type(raw) is str and 0 < len(raw) <= 1024, 'original_source_invalid')
    source = Path(raw)
    need(source.is_absolute() and source.suffix == '.bin' and '.private' in source.parts
         and '..' not in source.parts,
         'original_source_invalid')
    if root is not None:
        active = Path(root).absolute()
        project = next((part for part in (active, *active.parents)
                        if part.name.lower() == 'opentrail'), active)
        need(source.is_relative_to(project / '.private')
             and source.resolve().is_relative_to((project / '.private').resolve()), 'original_source_invalid')
    return source


def _read_source(raw, expected, root):
    source = _source_path(raw, root)
    for part in (source, *source.parents):
        stat = part.lstat()
        need(not part.is_symlink() and not (getattr(stat, 'st_file_attributes', 0) & 0x400),
             'original_source_invalid')
    need(source.is_file() and source.stat().st_size == expected['bytes'], 'original_source_invalid')
    captured = source.read_bytes()
    need(descriptor(captured) == expected, 'original_capture_changed')
    return captured


def validate_request(request):
    try:
        obj = decode(canonical(request))
        keys = {'schema', 'runtime_sha256', 'device_binding', 'candidate', 'protected',
                'original_prefix', 'boot_compatibility', 'case'}
        need(type(obj) is dict and set(obj) in (keys, keys | {'original_sources'}), 'request_invalid')
        need(obj['schema'] == 'OT0304-STANDARD-REQUEST-1'
             and obj['case'] == 'standard-settings-read-and-restore'
             and obj['boot_compatibility'] == 'inferred-ot208-image-header-compatibility'
             and _hex(obj['runtime_sha256']) and _hex(obj['device_binding'])
             and obj['candidate'] == CANDIDATE, 'request_invalid')
        need(type(obj['protected']) is dict and set(obj['protected']) == set(PROTECTED), 'request_invalid')
        for name in PROTECTED:
            need(_descriptor(obj['protected'][name], SPANS[name][1])
                 and obj['protected'][name]['sha256'] == EXPECTED[name], 'request_invalid')
        need(_descriptor(obj['original_prefix'], SPANS['application'][1])
             and obj['original_prefix']['sha256'] == EXPECTED['application'], 'request_invalid')
        sources = obj.get('original_sources', {})
        need(type(sources) is dict and set(sources).issubset(SPANS), 'request_invalid')
        for name, source in sources.items():
            need(type(source) is dict and set(source) == {'path', 'bytes', 'sha256'}
                 and _descriptor({key: source[key] for key in ('bytes', 'sha256')}, SPANS[name][1]),
                 'request_invalid')
            _source_path(source['path'])
            if name in EXPECTED:
                need(source['sha256'] == EXPECTED[name], 'request_invalid')
        return obj
    except Exception:
        raise TrialError('request_invalid') from None


def validate_grant(request, raw, expected_sha, operation, utc):
    try:
        need(type(raw) is bytes and len(raw) <= 4096 and _hex(expected_sha)
             and sha(raw) == expected_sha and operation in ('execute', 'recover'), 'authority_invalid')
        obj = decode(raw)
        need(type(obj) is dict and set(obj) == {'schema', 'attempt', 'request_sha256',
             'runtime_sha256', 'operation', 'origin_attempt', 'attempt_count', 'actions',
             'issued_utc', 'expires_utc'}, 'authority_invalid')
        need(obj['schema'] == 'OT0304-STANDARD-GRANT-1' and _hex(obj['attempt'], common.HEX32)
             and obj['request_sha256'] == sha(canonical(request))
             and obj['runtime_sha256'] == request['runtime_sha256']
             and obj['operation'] == operation and type(obj['attempt_count']) is int
             and obj['attempt_count'] == 1
             and obj['actions'] == (ACTIONS if operation == 'execute' else RECOVERY_ACTIONS)
             and type(obj['issued_utc']) is int and type(obj['expires_utc']) is int
             and 0 < obj['expires_utc'] - obj['issued_utc'] <= 3600
             and obj['issued_utc'] <= utc() < obj['expires_utc'], 'authority_invalid')
        need(obj['origin_attempt'] is None if operation == 'execute' else
             _hex(obj['origin_attempt'], common.HEX32), 'authority_invalid')
        return obj
    except Exception:
        raise TrialError('authority_invalid') from None


def path(root, name):
    return common.private_path(root, name)


class Journal:
    """Atomic snapshots: an unsuccessful pending save authorizes no next effect."""
    def __init__(self, root, attempt, owner):
        need(_hex(attempt, common.HEX32), 'journal_invalid')
        self.root, self.attempt, self.owner = root, attempt, owner
        self.path = path(root, PREFIX + attempt + '.json')
        self.pending = path(root, PREFIX + attempt + '.pending')
        self.healthy, self.require_lease = True, False

    def check_owner(self):
        need(self.healthy, 'journal_unhealthy')
        if self.require_lease:
            need(decode(path(self.root, ACTIVE).read_bytes()) == self.owner, 'active_attempt_changed')

    def validate(self, state):
        need(type(state) is dict and set(state) == {'schema', 'attempt', 'request',
             'grant_sha256', 'captures', 'sources', 'state_aggregate', 'events', 'observation',
             'observation_closed', 'challenge', 'evidence_sha256', 'failure',
             'preflight_evidence_sha256', 'recovery_idle_evidence_sha256',
             'restoration', 'recovery_grants', 'closed'}, 'journal_invalid')
        need(state['schema'] == 'OT0304-STANDARD-JOURNAL-1'
             and state['attempt'] == self.attempt and validate_request(state['request']) == state['request']
             and _hex(state['grant_sha256']) and type(state['captures']) is dict
             and set(state['captures']).issubset(SPANS)
             and type(state['sources']) is dict and set(state['sources']) == set(state['captures'])
             and type(state['events']) is list and len(state['events']) <= 96
             and all(type(event) is str and event in EVENTS for event in state['events'])
             and type(state['observation_closed']) is bool and type(state['closed']) is bool
             and type(state['observation']) is str and state['observation'] in OBSERVATIONS
             and type(state['failure']) is str and state['failure'] in FAILURES,
             'journal_invalid')
        need(type(state['restoration']) is str and state['restoration'] in ('pending', 'failed', 'restored'),
             'journal_invalid')
        for name, value in state['captures'].items():
            need(_descriptor(value, SPANS[name][1]), 'journal_invalid')
            source = state['sources'][name]
            local = str(path(self.root, PREFIX + self.attempt + '-' + name + '.bin'))
            pinned = state['request'].get('original_sources', {}).get(name, {}).get('path')
            need(source == local or source == pinned, 'journal_invalid')
            _source_path(source, self.root)
        need(state['state_aggregate'] is None or _descriptor(state['state_aggregate'], 1048576), 'journal_invalid')
        need(state['challenge'] is None or _hex(state['challenge'], common.HEX32), 'journal_invalid')
        need(state['evidence_sha256'] is None or _hex(state['evidence_sha256']), 'journal_invalid')
        for field in ('preflight_evidence_sha256', 'recovery_idle_evidence_sha256'):
            need(state[field] is None or _hex(state[field]), 'journal_invalid')
        need(type(state['recovery_grants']) is list and len(state['recovery_grants']) <= 8, 'journal_invalid')
        attempts = []
        for row in state['recovery_grants']:
            need(type(row) is dict and set(row) == {'attempt', 'sha256'}
                 and _hex(row['attempt'], common.HEX32) and _hex(row['sha256'])
                 and row['attempt'] != self.attempt and row['attempt'] not in attempts, 'journal_invalid')
            attempts.append(row['attempt'])
        events = state['events']
        need(events.count('recovery_authorized') == len(state['recovery_grants']), 'journal_invalid')
        for event in ('preflight_idle', 'claim_intent', 'capture_complete', 'complete_bound', 'candidate_write_intent', 'candidate_verified',
                      'candidate_boot_intent', 'candidate_booted', 'observation_intent',
                      'observation_closed', 'original_boot_intent', 'original_booted', 'closed'):
            need(events.count(event) <= 1, 'journal_invalid')
        for event, predecessor in (
            ('complete_bound', 'capture_complete'), ('candidate_write_intent', 'complete_bound'), ('candidate_verified', 'candidate_write_intent'),
            ('candidate_boot_intent', 'candidate_verified'), ('candidate_booted', 'candidate_boot_intent'),
            ('observation_intent', 'candidate_booted'), ('original_booted', 'original_boot_intent'),
            ('closed', 'original_booted')):
            if event in events:
                need(predecessor in events and events.index(predecessor) < events.index(event), 'journal_invalid')
        if 'capture_complete' in events:
            need(set(state['captures']) == set(SPANS) and state['state_aggregate'] is not None, 'journal_invalid')
        if 'observation_intent' in events:
            need(state['challenge'] is not None, 'journal_invalid')
            if state['observation_closed']:
                need('observation_closed' in events or 'recovery_idle' in events, 'journal_invalid')
        else:
            need(state['challenge'] is None, 'journal_invalid')
        if 'preflight_idle' in events:
            need(state['preflight_evidence_sha256'] is not None, 'journal_invalid')
        if 'recovery_idle' in events:
            need(state['recovery_idle_evidence_sha256'] is not None, 'journal_invalid')
        if 'claim_intent' in events:
            need('preflight_idle' in events and events.index('preflight_idle') < events.index('claim_intent'),
                 'journal_invalid')
        need(events.count('recovery_idle') <= len(state['recovery_grants']), 'journal_invalid')
        if state['observation'] in ('passed', 'failed', 'unavailable'):
            need('observation_closed' in events and state['evidence_sha256'] is not None, 'journal_invalid')
        if 'original_boot_intent' in events:
            need('capture_complete' in events and state['observation_closed'], 'journal_invalid')
            need(all(event in ('original_booted', 'closed', 'recovery_authorized')
                     for event in events[events.index('original_boot_intent') + 1:]), 'journal_invalid')
        if state['closed']:
            need('closed' in events and 'original_booted' in events
                 and state['restoration'] == 'restored', 'journal_invalid')

    def save(self, state):
        try:
            self.check_owner()
            self.validate(state)
            raw = canonical(state) + b'\n'
            need(len(raw) <= 32768 and not self.pending.exists(), 'journal_invalid')
            write_once(self.pending, raw)
            os.replace(self.pending, self.path)
            need(self.path.read_bytes() == raw, 'durable_write_failed')
        except BaseException:
            self.healthy = False
            raise TrialError('journal_write_failed') from None

    def load(self):
        try:
            self.check_owner()
            raw = self.path.read_bytes()
            need(len(raw) <= 32768 and raw.endswith(b'\n'), 'journal_invalid')
            state = decode(raw)
            self.validate(state)
            need(state['grant_sha256'] == self.owner['grant_sha256']
                 and sha(canonical(state['request'])) == self.owner['request_sha256']
                 and state['request']['runtime_sha256'] == self.owner['runtime_sha256'], 'journal_invalid')
            return state
        except Exception:
            raise TrialError('journal_invalid') from None

    def event(self, state, event):
        state['events'].append(event)
        self.save(state)


def _owner(request, attempt, grant_sha):
    return {'schema': 'OT0304-STANDARD-LEASE-1', 'attempt': attempt,
            'request_sha256': sha(canonical(request)), 'runtime_sha256': request['runtime_sha256'],
            'grant_sha256': grant_sha}


def _consume(root, grant, grant_sha):
    destination = path(root, PREFIX + 'used-' + grant['attempt'])
    value = {'grant_sha256': grant_sha}
    common.exclusive(destination, value)
    need(destination.read_bytes() == canonical(value) + b'\n', 'durable_write_failed')


def _checked(backend, request, journal):
    journal.check_owner()
    need(backend.reverify(request['device_binding']) is True, 'device_binding_changed')


def _read(backend, name, bound, *, capture_recovery=False):
    offset, size = SPANS[name]
    raw = (backend.read_bound_state_chunk(offset, size) if bound else
           backend.read_recovery_state_chunk(offset, size) if capture_recovery else
           backend.read_state_chunk(offset, size)) if name.startswith('state_') else backend.read(offset, size)
    need(type(raw) is bytes and len(raw) == size, 'capture_invalid')
    return raw


def _baseline(request, captured):
    for name in PROTECTED:
        need(descriptor(captured[name]) == request['protected'][name], 'protected_mismatch')
    need(captured['ota'] == b'\xff' * SPANS['ota'][1], 'boot_selection_invalid')
    need(descriptor(captured['application']) == request['original_prefix'], 'original_mismatch')
    safe_reset_marker(captured['nvs'])


def _capture(root, request, backend, journal, state, *, recovery=False):
    if recovery:
        need('candidate_write_intent' not in state['events'], 'capture_incomplete')
    captured = {}
    for name in SPANS:
        _checked(backend, request, journal)
        raw = _read(backend, name, False, capture_recovery=recovery)
        _checked(backend, request, journal)
        need(_read(backend, name, False, capture_recovery=recovery) == raw, 'capture_repeat_mismatch')
        if name in PROTECTED:
            need(descriptor(raw) == request['protected'][name], 'protected_mismatch')
            if name == 'ota':
                need(raw == b'\xff' * SPANS['ota'][1], 'boot_selection_invalid')
        if name == 'application':
            need(descriptor(raw) == request['original_prefix'], 'original_mismatch')
        if name == 'nvs':
            safe_reset_marker(raw)
        destination = path(root, PREFIX + journal.attempt + '-' + name + '.bin')
        source = str(destination)
        if name in state['sources']:
            source = state['sources'][name]
            need(_read_source(source, state['captures'][name], root) == raw, 'prewrite_original_changed')
        elif name in request.get('original_sources', {}):
            pinned = request['original_sources'][name]
            expected = {key: pinned[key] for key in ('bytes', 'sha256')}
            previous = _read_source(pinned['path'], expected, root)
            if previous == raw:
                source = pinned['path']
            else:
                need(name not in EXPECTED, 'prewrite_original_changed')
        if source == str(destination) and destination.exists():
            # Only an interrupted prewrite capture may have an uncommitted file.
            need(destination.read_bytes() == raw, 'prewrite_original_changed')
        elif source == str(destination):
            write_once(destination, raw)
        if name in state['captures']:
            need(state['captures'][name] == descriptor(raw), 'prewrite_original_changed')
        state['captures'][name] = descriptor(raw)
        state['sources'][name] = source
        journal.save(state)
        captured[name] = raw
    _baseline(request, captured)
    state['state_aggregate'] = descriptor(captured['state_0'] + captured['state_1'])
    if 'capture_complete' not in state['events']:
        journal.event(state, 'capture_complete')
    else:
        journal.save(state)
    return captured


def _originals(root, request, journal, state):
    need(set(state['captures']) == set(SPANS), 'capture_incomplete')
    captured = {}
    for name, (_, size) in SPANS.items():
        raw = _read_source(state['sources'][name], state['captures'][name], root)
        need(len(raw) == size, 'original_capture_changed')
        captured[name] = raw
    need(descriptor(captured['state_0'] + captured['state_1']) == state['state_aggregate'], 'original_capture_changed')
    _baseline(request, captured)
    return captured


def _release(root, journal, state):
    state['closed'] = True
    if 'closed' not in state['events']:
        journal.event(state, 'closed')
    else:
        journal.save(state)
    journal.check_owner()
    path(root, ACTIVE).unlink()
    return {'attempt': journal.attempt, 'restored': True, 'observation': state['observation'],
            'failure': state['failure'], 'closed': True,
            'restoration': state['restoration'],
            'runtime_profile_capabilities': 'unknown'}


def _restore(root, request, backend, journal, state, *, recovery):
    need(state['observation_closed'], 'observation_closure_required')
    need('original_boot_intent' not in state['events'], 'original_boot_reconciliation_required')
    captured = _originals(root, request, journal, state)
    if recovery:
        need(backend.bind_complete_recovery_originals(captured) is True, 'complete_binding_failed')
    else:
        # The real complete transport admits exactly one normal binding, before
        # mutation. Rebinding after candidate writes is deliberately refused.
        need('complete_bound' in state['events'], 'complete_binding_failed')
    state['restoration'] = 'pending'
    journal.event(state, 'restoration_intent')
    for name in PROTECTED:
        _checked(backend, request, journal)
        need(_read(backend, name, True) == captured[name], 'protected_changed')
    for name in RESTORABLE:
        _checked(backend, request, journal)
        if _read(backend, name, True) != captured[name]:
            journal.event(state, 'restore_' + name + '_intent')
            journal.check_owner()
            if name.startswith('state_'):
                backend.restore_state_chunk(SPANS[name][0], captured[name])
            else:
                backend.write(SPANS[name][0], captured[name])
            _checked(backend, request, journal)
            need(_read(backend, name, True) == captured[name], 'restore_readback_failed')
            journal.event(state, 'restore_' + name + '_verified')
    # The transport performs the ONE independent seven-region sweep and fences
    # further operations before invoking this durable original-boot barrier.
    def before_reset():
        journal.event(state, 'original_boot_intent')
    journal.check_owner()
    need(backend.reset_complete_original(before_reset=before_reset) is True, 'original_boot_uncertain')
    state['restoration'] = 'restored'
    journal.event(state, 'original_booted')
    return _release(root, journal, state)


def _closed_observation(journal, state, close):
    if state['observation_closed']:
        return True
    try:
        result = close() if close is not None else None
        closed = (type(result) is dict and set(result) == {'closed', 'evidence_sha256'}
                  and result['closed'] is True and _hex(result['evidence_sha256']))
    except BaseException:
        closed = False
    if closed:
        state['observation_closed'] = True
        state['evidence_sha256'] = result['evidence_sha256']
        journal.event(state, 'observation_closed')
    return closed


def _idle_proof(close):
    result = close() if close is not None else None
    need(type(result) is dict and set(result) == {'closed', 'evidence_sha256'}
         and result['closed'] is True and _hex(result['evidence_sha256']), 'phone_idle_required')
    return result['evidence_sha256']


@common.single_process
def execute(root, request, grant_raw, grant_sha, candidate, backend, observe, *,
            utc=time.time, challenge_factory=lambda: secrets.token_hex(16), close_observation=None,
            initial_phone_idle=None):
    request = validate_request(request)
    grant = validate_grant(request, grant_raw, grant_sha, 'execute', utc)
    need(type(candidate) is bytes and descriptor(candidate) == CANDIDATE, 'candidate_changed')
    for source in request.get('original_sources', {}).values():
        _read_source(source['path'], {key: source[key] for key in ('bytes', 'sha256')}, root)
    need(not any(Path(root, '.private').glob('*-active.lock')), 'custody_held')
    _consume(root, grant, grant_sha)
    owner = _owner(request, grant['attempt'], grant_sha)
    journal = Journal(root, grant['attempt'], owner)
    state = {'schema': 'OT0304-STANDARD-JOURNAL-1', 'attempt': grant['attempt'],
             'request': request, 'grant_sha256': grant_sha, 'captures': {}, 'sources': {}, 'state_aggregate': None,
             'events': [], 'observation': 'not_observed', 'observation_closed': False,
             'challenge': None, 'evidence_sha256': None, 'failure': 'none',
             'preflight_evidence_sha256': None, 'recovery_idle_evidence_sha256': None,
             'restoration': 'pending',
             'recovery_grants': [], 'closed': False}
    journal.save(state)
    common.exclusive(path(root, ACTIVE), owner)
    journal.require_lease = True
    stage = 'preflight_failed'
    try:
        state['preflight_evidence_sha256'] = _idle_proof(initial_phone_idle)
        state['observation_closed'] = True
        journal.event(state, 'preflight_idle')
        validate_grant(request, grant_raw, grant_sha, 'execute', utc)
        journal.event(state, 'claim_intent')
        need(backend.claim(request['device_binding']) is True, 'device_claim_failed')
        captured = _capture(root, request, backend, journal, state)
        need(backend.bind_complete_originals(captured) is True, 'complete_binding_failed')
        journal.event(state, 'complete_bound')
        stage = 'candidate_failed'
        padded = candidate + b'\xff' * (SPANS['application'][1] - len(candidate))
        _checked(backend, request, journal)
        validate_grant(request, grant_raw, grant_sha, 'execute', utc)
        journal.event(state, 'candidate_write_intent')
        validate_grant(request, grant_raw, grant_sha, 'execute', utc)
        backend.write(SPANS['application'][0], padded)
        _checked(backend, request, journal)
        need(_read(backend, 'application', True) == padded, 'candidate_readback_failed')
        journal.event(state, 'candidate_verified')
        def before_boot():
            validate_grant(request, grant_raw, grant_sha, 'execute', utc)
            journal.event(state, 'candidate_boot_intent')
            validate_grant(request, grant_raw, grant_sha, 'execute', utc)
        journal.check_owner()
        need(backend.boot_candidate(before_boot=before_boot) is True, 'candidate_boot_uncertain')
        journal.event(state, 'candidate_booted')
        stage = 'observation_failed'
        validate_grant(request, grant_raw, grant_sha, 'execute', utc)
        challenge = challenge_factory()
        need(_hex(challenge, common.HEX32), 'observation_invalid')
        state['challenge'], state['observation_closed'] = challenge, False
        journal.event(state, 'observation_intent')
        context = {'schema': 'OT0304-STANDARD-OBSERVATION-CONTEXT-1', 'attempt': journal.attempt,
                   'request_sha256': sha(canonical(request)), 'challenge': challenge}
        result = observe(dict(context))
        need(type(result) is dict and set(result) == {'result', 'closed', 'challenge', 'evidence_sha256'}
             and type(result['result']) is str and result['result'] in ('passed', 'failed', 'unavailable')
             and result['closed'] is True and result['challenge'] == challenge
             and _hex(result['evidence_sha256']), 'observation_invalid')
        state['observation'], state['observation_closed'] = result['result'], True
        state['evidence_sha256'] = result['evidence_sha256']
        journal.event(state, 'observation_closed')
    except BaseException:
        close = close_observation or getattr(observe, 'close', None)
        if not state['observation_closed'] and 'observation_intent' in state['events']:
            # Callback closure is resource cleanup; it never invokes ROM here.
            if not journal.healthy:
                try:
                    if close is not None:
                        close()
                except BaseException:
                    pass
                raise TrialError('custody_held_recovery_required') from None
            if not _closed_observation(journal, state, close):
                state['failure'] = stage
                journal.event(state, 'trial_failed')
                raise TrialError('custody_held_observation_closure_required') from None
        if not journal.healthy:
            raise TrialError('custody_held_recovery_required') from None
        state['failure'] = stage
        journal.event(state, 'trial_failed')
        if 'capture_complete' not in state['events']:
            # Partial capture has no candidate effects. A fresh recovery grant
            # must finish verified custody before the guarded original restart.
            raise TrialError('custody_held_prewrite') from None
    try:
        return _restore(root, request, backend, journal, state, recovery=False)
    except BaseException:
        if journal.healthy and 'original_boot_intent' not in state['events']:
            state['restoration'] = 'failed'
            journal.event(state, 'restore_failed')
        # No hold() call: it is a ROM command, including after disk failure.
        raise TrialError('custody_held_recovery_required') from None


@common.single_process
def recover(root, request, grant_raw, grant_sha, backend, *, utc=time.time, close_observation=None):
    request = validate_request(request)
    grant = validate_grant(request, grant_raw, grant_sha, 'recover', utc)
    try:
        owner = decode(path(root, ACTIVE).read_bytes())
        need(type(owner) is dict and set(owner) == {'schema', 'attempt', 'request_sha256',
             'runtime_sha256', 'grant_sha256'} and _hex(owner['grant_sha256'])
             and owner == _owner(request, grant['origin_attempt'], owner['grant_sha256']),
             'recovery_binding_invalid')
        journal = Journal(root, grant['origin_attempt'], owner)
        journal.require_lease = True
        state = journal.load()
        need(state['request'] == request, 'recovery_binding_invalid')
        # An uncertain original boot could have advanced NVS/state. Never replay
        # pre-boot captures, claim the device or consume a grant before review.
        need('original_boot_intent' not in state['events'] or 'original_booted' in state['events'],
             'original_boot_reconciliation_required')
        if 'original_booted' in state['events']:
            pass  # Closure only: snapshots must never be replayed after boot.
        elif 'capture_complete' in state['events']:
            _originals(root, request, journal, state)
        else:
            for source in request.get('original_sources', {}).values():
                _read_source(source['path'], {key: source[key] for key in ('bytes', 'sha256')}, root)
            for name, expected in state['captures'].items():
                _read_source(state['sources'][name], expected, root)
        _consume(root, grant, grant_sha)
        if journal.pending.exists():
            need(journal.pending.stat().st_size <= 32768, 'journal_invalid')
            destination = path(root, PREFIX + grant['attempt'] + '.interrupted')
            need(not destination.exists(), 'journal_invalid')
            os.replace(journal.pending, destination)
        state['recovery_grants'].append({'attempt': grant['attempt'], 'sha256': grant_sha})
        journal.event(state, 'recovery_authorized')
        if 'original_booted' in state['events']:
            return _release(root, journal, state)
        state['recovery_idle_evidence_sha256'] = _idle_proof(close_observation)
        state['observation_closed'] = True
        journal.event(state, 'recovery_idle')
        validate_grant(request, grant_raw, grant_sha, 'recover', utc)
        journal.event(state, 'recovery_claim_intent')
        need(backend.claim(request['device_binding']) is True, 'device_claim_failed')
        if 'capture_complete' not in state['events']:
            need('candidate_write_intent' not in state['events'], 'capture_incomplete')
            _capture(root, request, backend, journal, state, recovery=True)
        return _restore(root, request, backend, journal, state, recovery=True)
    except BaseException as error:
        if isinstance(error, TrialError) and str(error) == 'original_boot_reconciliation_required':
            raise
        raise TrialError('custody_held_recovery_required') from None
