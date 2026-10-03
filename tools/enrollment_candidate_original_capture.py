"""Explicit six-span original acquisition and a live, per-role custody handoff.

Imports and constructors are inert. This module issues no grants, substitutes
no original hashes, and has no flash-write or erase operation. Private receipts
cannot recreate the in-process handoff after interruption. A new release-only
grant may release unadopted originals; uncertain original boot is never retried.
"""
from dataclasses import dataclass
import hmac
import math
import os
from pathlib import Path
import re
import time

from enrollment_candidate_controller import Clock, ControllerError
from enrollment_candidate_custody import (ACTIVE as CANDIDATE_ACTIVE, Budget,
    SPANS, ROLES, canonical, decode, descriptor, sha, validate_request as candidate_request,
    _validate_state as candidate_state, Error as CustodyError)
import enrollment_candidate_rom_adapter as rom


ACTIVE = 'enrollment-original-active.json'
ACTIONS = ['guard_original', 'read_six_spans', 'reset_original']
HEX64, HEX32 = re.compile('[0-9a-f]{64}'), re.compile('[0-9a-f]{32}')
_CATEGORIES = frozenset('''capture_operation_failed request_invalid grant_invalid grant_expired
grant_used capture_session_used capture_incomplete capture_readback_failed original_changed
private_root_invalid private_path_invalid record_failed journal_pending journal_invalid
custody_held runtime_binding_invalid identity_binding_invalid profile_invalid pair_invalid
admission_invalid rom_close_unconfirmed original_reset_uncertain original_boot_state_uncertain
hardware_lease_busy handles_not_closed freshness_invalid handoff_invalid handoff_used
candidate_custody_unresolved deadline_expired deadline_extended deadline_invalid host_clock_invalid
pending_reset_storage rom_adapter_refused rom_operation_failed rom_owner_required runtime_busy
passive_owner_active capture_authority_invalid capture_operation_refused capture_terminal
capture_claim_refused capture_changed capture_reset_refused'''.split())


class CaptureError(RuntimeError):
    """Fixed, non-identifying refusal category."""


def need(value, category):
    if not value:
        raise CaptureError(category)


def _category(error):
    if type(error) in (CaptureError, rom.AdapterError, ControllerError, CustodyError):
        args = error.args
        if len(args) == 1 and type(args[0]) is str and args[0] in _CATEGORIES:
            return args[0]
    return 'capture_operation_failed'


def validate_request(value):
    need(type(value) is dict and set(value) == {'schema', 'runtime_sha256', 'roles'}
        and value['schema'] == 'OT-ORIGINAL-CAPTURE-REQUEST-1'
        and type(value['runtime_sha256']) is str and HEX64.fullmatch(value['runtime_sha256'])
        and type(value['roles']) is dict and set(value['roles']) == set(ROLES), 'request_invalid')
    for row in value['roles'].values():
        need(type(row) is dict and set(row) == {'device_binding', 'profile_evidence_sha256'}
            and all(type(v) is str and HEX64.fullmatch(v) for v in row.values()), 'request_invalid')
    need(value['roles']['A']['device_binding'] != value['roles']['B']['device_binding'], 'pair_invalid')
    return decode(canonical(value))


def validate_grant(request, raw, pin, operation, utc):
    need(type(raw) is bytes and len(raw) <= 8192 and type(pin) is str
        and HEX64.fullmatch(pin) and sha(raw) == pin, 'grant_invalid')
    value = decode(raw)
    need(type(value) is dict and set(value) == {'schema', 'attempt', 'request_sha256',
        'runtime_sha256', 'operation', 'origin_attempt', 'attempt_count', 'actions',
        'issued_utc', 'capture_expires_utc', 'cleanup_expires_utc'}
        and value['schema'] == 'OT-ORIGINAL-CAPTURE-GRANT-1'
        and operation in ('capture', 'release') and value['operation'] == operation
        and type(value['attempt']) is str and HEX32.fullmatch(value['attempt'])
        and value['request_sha256'] == sha(canonical(request))
        and value['runtime_sha256'] == request['runtime_sha256']
        and type(value['attempt_count']) is int and value['attempt_count'] == 1
        and value['actions'] == ACTIONS, 'grant_invalid')
    need(all(type(value[k]) is int for k in
        ('issued_utc', 'capture_expires_utc', 'cleanup_expires_utc'))
        and value['issued_utc'] < value['capture_expires_utc'] <= value['cleanup_expires_utc']
        and value['cleanup_expires_utc'] - value['issued_utc'] <= 3600, 'grant_invalid')
    need(value['origin_attempt'] is None if operation == 'capture' else
        type(value['origin_attempt']) is str and HEX32.fullmatch(value['origin_attempt'])
        and value['origin_attempt'] != value['attempt'], 'grant_invalid')
    now = utc()
    need(type(now) in (int, float) and math.isfinite(now)
        and value['issued_utc'] <= now < value['capture_expires_utc'], 'grant_expired')
    return value


def _regular(path, directory=False):
    path = Path(path)
    need(path.is_absolute(), 'private_path_invalid')
    for part in (path, *path.parents):
        need(not part.is_symlink() and not getattr(part, 'is_junction', lambda: False)(), 'private_path_invalid')
    need(path.is_dir() if directory else path.is_file(), 'private_path_invalid')
    return path


def _path(root, name):
    _regular(root, True)
    need(type(name) is str and Path(name).name == name, 'private_path_invalid')
    path = root / name
    need(not path.is_symlink() and not getattr(path, 'is_junction', lambda: False)(), 'private_path_invalid')
    return path


def _once(path, raw):
    with path.open('xb', buffering=0) as stream:
        need(stream.write(raw) == len(raw), 'record_failed')
        os.fsync(stream.fileno())
    need(_regular(path).read_bytes() == raw, 'record_failed')


class _Journal:
    def __init__(self, root, attempt):
        self.root, self.attempt = root, attempt
        self.file = _path(root, 'enrollment-original-' + attempt + '.json')
        self.pending = self.file.with_suffix('.pending')

    def save(self, state):
        need(not self.pending.exists(), 'journal_pending')
        raw = canonical(state) + b'\n'
        _once(self.pending, raw)
        os.replace(self.pending, self.file)
        need(_regular(self.file).read_bytes() == raw, 'record_failed')

    def original(self, role, name):
        return _path(self.root, f'enrollment-original-{self.attempt}-{role}-{name}.bin')

    @property
    def pins_path(self):
        return _path(self.root, 'enrollment-original-' + self.attempt + '-pins.json')


@dataclass(frozen=True)
class CaptureResult:
    attempt: str
    outcome: str
    capture_complete: bool
    first_failure: tuple | None
    cleanup_failure: tuple | None
    roles: dict
    custody_released: bool
    journal_path: Path | None
    pins_path: Path | None


class CaptureSession:
    def __init__(self, private_root, evidence_root, request, grant_raw, grant_sha256, *,
                 runtime, lease, identities, binding_key, profiles, utc=time.time,
                 monotonic=time.monotonic, backend_factory=None,
                 capture_deadline=None, cleanup_deadline=None):
        self.private, self.root = Path(private_root), Path(evidence_root)
        self._request = canonical(validate_request(request))
        need(type(grant_raw) is bytes, 'grant_invalid')
        self._grant_raw, self._grant_sha = grant_raw, grant_sha256
        self.runtime, self.lease = runtime, lease
        need(type(identities) is dict and set(identities) == set(ROLES)
            and type(profiles) is dict and set(profiles) == set(ROLES)
            and type(binding_key) is bytes and len(binding_key) == 32, 'pair_invalid')
        self._identities = {r: rom.identity(identities[r]) for r in ROLES}
        self._key, self._profiles = binding_key, dict(profiles)
        self._bindings(self._identities, binding_key, self._profiles)
        self.utc, self.monotonic, self.clock = utc, monotonic, Clock(monotonic)
        self.factory = backend_factory
        self._state, self._journal, self._budget = None, None, None
        self._backends, self._started, self._verified = {}, False, False
        self._candidate = None
        self._release_used = False
        for ceiling in (capture_deadline, cleanup_deadline):
            need(ceiling is None or (type(ceiling) in (int, float) and math.isfinite(ceiling)), 'deadline_extended')
        self._capture_ceiling, self._cleanup_ceiling = capture_deadline, cleanup_deadline

    def __repr__(self):
        return '<OTCAND1 original capture session>'

    @property
    def request(self):
        return decode(self._request)

    @property
    def pins(self):
        return {r: decode(canonical(self._state['roles'][r]['pins'])) for r in ROLES} if self._state else {}

    @property
    def journal_path(self):
        return self._journal.file if self._journal else None

    @property
    def capture_deadline(self):
        return self._budget.execute if self._budget else self._capture_ceiling

    @property
    def cleanup_deadline(self):
        return self._budget.restore if self._budget else self._cleanup_ceiling

    @property
    def held(self):
        return self._state is not None and self._state['phase'] != 'released'

    @property
    def result(self):
        state = self._state
        if state is None:
            return CaptureResult('', 'not_started', False, None, None, {}, False, None, None)
        released = state['phase'] == 'released'
        outcome = 'released' if released and not state['first_failure'] else 'failed' if released else \
            'captured' if state['capture_complete'] and not state['first_failure'] and not state['cleanup_failure'] else 'held'
        return CaptureResult(state['attempt'], outcome, state['capture_complete'],
            tuple(state['first_failure']) if state['first_failure'] else None,
            tuple(state['cleanup_failure']) if state['cleanup_failure'] else None,
            decode(canonical(state['roles'])), released, self._journal.file,
            self._journal.pins_path if state['receipt'] else None)

    def _bindings(self, identities, key, profiles):
        request = self.request
        need(identities['A'] != identities['B'], 'pair_invalid')
        for role in ROLES:
            row, profile = request['roles'][role], profiles[role]
            need(hmac.compare_digest(rom.opaque_identity(key, identities[role]), row['device_binding']), 'identity_binding_invalid')
            need(type(profile) is rom.DeviceProfile and profile.model == 'heltec_v4_esp32s3'
                and profile.device_binding == row['device_binding'] and type(profile.flash_bytes) is int
                and profile.flash_bytes == 16777216
                and profile.evidence_sha256 == row['profile_evidence_sha256'], 'profile_invalid')

    def _paths(self):
        _regular(self.private, True)
        need(self.private.name == '.private', 'private_root_invalid')
        _regular(self.root, True)
        need(self.root.resolve().is_relative_to(self.private.resolve()) and self.root != self.private,
            'private_root_invalid')
        need(self.runtime.manifest_sha256 == self.request['runtime_sha256']
            and Path(self.runtime.private_root).resolve() == self.private.resolve()
            and Path(self.lease.private_root).resolve() == self.private.resolve(), 'runtime_binding_invalid')
        manifest = self.runtime.verify(self._budget.check())
        need(not self.root.resolve().is_relative_to(Path(manifest['root']).resolve()), 'private_root_invalid')

    def _authority(self, grant, role):
        return {'schema': 'OT-CANDIDATE-CAPTURE-AUTHORITY-1',
            'runtime_sha256': self.request['runtime_sha256'], 'request_sha256': sha(self._request),
            'grant_sha256': self._grant_sha, 'attempt': grant['attempt'], 'role': role,
            'device_binding': self.request['roles'][role]['device_binding'], 'actions': list(ACTIONS),
            'capture_deadline': self._budget.execute, 'cleanup_deadline': self._budget.restore}

    def _make_backends(self, grant):
        factory = self.factory or rom.CaptureROMBackend
        for role in ROLES:
            self._backends[role] = factory(self.runtime, self.lease, authority=self._authority(grant, role),
                role=role, binding_key=self._key, expected_identity=self._identities[role],
                device_profile=self._profiles[role])
        need(self._backends['A'] is not self._backends['B'], 'pair_invalid')

    def _start(self, operation):
        need(not self._started, 'capture_session_used')
        self._started = True
        grant = validate_grant(self.request, self._grant_raw, self._grant_sha, operation, self.utc)
        budget_grant = dict(grant, execute_expires_utc=grant['capture_expires_utc'],
            restore_expires_utc=grant['cleanup_expires_utc'])
        self._budget = Budget(budget_grant, self.utc, self.monotonic)
        if self._capture_ceiling is not None:
            self._budget.execute = min(self._budget.execute, self._capture_ceiling)
        if self._cleanup_ceiling is not None:
            self._budget.restore = min(self._budget.restore, self._cleanup_ceiling)
        need(self._budget.execute <= self._budget.restore, 'deadline_extended')
        self._paths()
        used = _path(self.root, 'enrollment-original-used-' + grant['attempt'])
        need(not used.exists(), 'grant_used')
        need(self.lease.acquire(self._budget.check()) is True, 'hardware_lease_busy')
        if operation == 'capture':
            # Refuse known original/candidate custody while holding the OS
            # lease, before the acquisition grant's one-use marker is spent.
            need(not _path(self.root, ACTIVE).exists()
                and not _path(self.root, CANDIDATE_ACTIVE).exists(), 'custody_held')
        _once(used, canonical({'grant_sha256': self._grant_sha}) + b'\n')
        return grant

    def _failure(self, stage, error, cleanup=False):
        field = 'cleanup_failure' if cleanup else 'first_failure'
        if self._state[field] is None:
            self._state[field] = [stage, _category(error)]

    def _admit(self, admission, role):
        need(type(admission) is rom.CaptureAdmission and admission.device_binding ==
            self.request['roles'][role]['device_binding'] and admission.model == 'heltec_v4_esp32s3'
            and type(admission.flash_bytes) is int and admission.flash_bytes == 16777216
            and admission.security == 'verified-read-only' and admission.boot_selection == 'verified-factory'
            and admission.rom_held is True and type(admission.layout_sha256) is str
            and HEX64.fullmatch(admission.layout_sha256), 'admission_invalid')
        need(self.lease.holds_rom(self.request['roles'][role]['device_binding']) is True, 'handoff_invalid')
        known = self._state['roles'][role]['pins'].get('partition')
        need(known is None or admission.layout_sha256 == known['sha256'], 'original_changed')
        return admission

    def _read(self, role, name, cleanup=False, deadline=None):
        backend = self._backends[role]
        binding = self.request['roles'][role]['device_binding']
        def limit():
            value = self._budget.check(cleanup)
            if deadline is not None:
                value = min(value, deadline)
                self.clock.check(value)
            return value
        if getattr(backend, 'supports_guarded_read', False) is True:
            value = backend.guarded_read(binding, *SPANS[name], limit())
            limit()
            need(type(value) is tuple and len(value) == 3, 'capture_readback_failed')
            before, raw, after = value
            self._admit(before, role)
            need(type(raw) is bytes and len(raw) == SPANS[name][1], 'capture_readback_failed')
            self._admit(after, role)
            return raw
        self._admit(backend.guard(binding, limit()), role)
        raw = backend.read(*SPANS[name], limit())
        limit()
        need(type(raw) is bytes and len(raw) == SPANS[name][1], 'capture_readback_failed')
        self._admit(backend.guard(binding, limit()), role)
        return raw

    def _raw(self, role, name):
        pin = self._state['roles'][role]['pins'][name]
        raw = _regular(self._journal.original(role, name)).read_bytes()
        need(len(raw) == SPANS[name][1] and descriptor(raw) == pin, 'original_changed')
        return raw

    def capture(self):
        grant = self._start('capture')
        self._journal = _Journal(self.root, grant['attempt'])
        self._state = {'schema': 'OT-ORIGINAL-CAPTURE-JOURNAL-1', 'attempt': grant['attempt'],
            'request': self.request, 'grant_sha256': self._grant_sha, 'phase': 'capturing',
            'capture_complete': False, 'first_failure': None, 'cleanup_failure': None, 'receipt': None,
            'candidate': None, 'roles': {r: {'owner': 'capture', 'entered': False, 'pins': {},
                'reset_intent': False, 'original_boot_allowed': False, 'handles_closed': False}
                for r in ROLES}}
        stage = 'capture_setup'
        try:
            self._journal.save(self._state)
            _once(_path(self.root, ACTIVE), canonical({'attempt': grant['attempt'], 'request_sha256': sha(self._request)}) + b'\n')
            self._make_backends(grant)
            for role in ROLES:
                stage = 'claim_' + role
                row, backend = self._state['roles'][role], self._backends[role]
                row['entered'] = True
                self._journal.save(self._state)
                self._admit(backend.claim(self.request['roles'][role]['device_binding'], self._budget.check()), role)
                for name in SPANS:
                    stage = 'capture_' + role + '_' + name
                    raw = self._read(role, name)
                    _once(self._journal.original(role, name), raw)
                    row['pins'][name] = descriptor(raw)
                    self._journal.save(self._state)
                for name in SPANS:
                    stage = 'repeat_' + role + '_' + name
                    need(self._read(role, name) == self._raw(role, name), 'capture_readback_failed')
                need(backend.complete is True and backend.pins == row['pins'], 'capture_incomplete')
                need(backend.close() is True and backend.assert_idle() is True, 'rom_close_unconfirmed')
                row['handles_closed'] = True
                self._journal.save(self._state)
            receipt = {'schema': 'OT-ORIGINAL-CAPTURE-PINS-1', 'attempt': grant['attempt'],
                'request_sha256': sha(self._request), 'runtime_sha256': self.request['runtime_sha256'],
                'roles': {r: {'device_binding': self.request['roles'][r]['device_binding'],
                    'profile_evidence_sha256': self.request['roles'][r]['profile_evidence_sha256'],
                    'originals': self.pins[r]} for r in ROLES}}
            receipt_raw = canonical(receipt) + b'\n'
            _once(self._journal.pins_path, receipt_raw)
            self._state.update(capture_complete=True, phase='held', receipt=descriptor(receipt_raw))
            self._journal.save(self._state)
            self._budget.check()
        except BaseException as error:
            self._failure(stage, error)
            self.release()
        return self.result

    def _receipt(self):
        need(self._state['capture_complete'] is True and self._state['receipt'] is not None,
            'capture_incomplete')
        raw = _regular(self._journal.pins_path).read_bytes()
        need(descriptor(raw) == self._state['receipt'], 'original_changed')
        value = decode(raw)
        expected = {'schema': 'OT-ORIGINAL-CAPTURE-PINS-1', 'attempt': self._state['attempt'],
            'request_sha256': sha(self._request), 'runtime_sha256': self.request['runtime_sha256'],
            'roles': {r: {'device_binding': self.request['roles'][r]['device_binding'],
                'profile_evidence_sha256': self.request['roles'][r]['profile_evidence_sha256'],
                'originals': self.pins[r]} for r in ROLES}}
        need(value == expected and decode(_regular(self._journal.file).read_bytes()) == self._state
            and not self._journal.pending.exists(), 'journal_invalid')
        need(decode(_regular(_path(self.root, ACTIVE)).read_bytes()) ==
            {'attempt': self._state['attempt'], 'request_sha256': sha(self._request)}, 'journal_invalid')
        for role in ROLES:
            for name in SPANS:
                self._raw(role, name)

    def verify(self, request, *, runtime, lease, identities, binding_key, profiles, deadline):
        need(self._state is not None and self._state['phase'] == 'held' and not self._verified,
            'handoff_used')
        stage = 'freshness_setup'
        try:
            checked = candidate_request(request)
            need(runtime is self.runtime and lease is self.lease and binding_key == self._key
                and identities == self._identities and profiles == self._profiles, 'handoff_invalid')
            self._bindings(identities, binding_key, profiles)
            need(checked['runtime_sha256'] == self.request['runtime_sha256'], 'runtime_binding_invalid')
            self.clock.check(deadline)
            self._receipt()
            for role in ROLES:
                need(checked['roles'][role] == {'device_binding': self.request['roles'][role]['device_binding'],
                    'originals': self.pins[role]} and self._state['roles'][role]['owner'] == 'capture'
                    and self.lease.holds_rom(self.request['roles'][role]['device_binding']) is True,
                    'freshness_invalid')
                for name in SPANS:
                    stage = 'freshness_' + role + '_' + name
                    self.clock.check(deadline)
                    need(self._read(role, name, deadline=deadline) == self._raw(role, name), 'original_changed')
            self.clock.check(deadline)
            self._budget.check()
            self._candidate, self._verified = canonical(checked), True
            self._state['phase'] = 'verified'
            self._journal.save(self._state)
            return True
        except BaseException as error:
            self._failure(stage, error)
            raise CaptureError(_category(error)) from None

    def transfer_role(self, role, candidate_root, request, attempt):
        need(role in ROLES and self._verified and self._candidate == canonical(candidate_request(request))
            and self._state['roles'][role]['owner'] == 'capture', 'handoff_invalid')
        self._budget.check()
        need(self.lease.holds_rom(self.request['roles'][role]['device_binding']) is True, 'handoff_invalid')
        root = _regular(candidate_root, True)
        need(root.resolve().is_relative_to(self.private.resolve()) and root != self.private
            and type(attempt) is str and HEX32.fullmatch(attempt), 'handoff_invalid')
        active = decode(_regular(_path(root, CANDIDATE_ACTIVE)).read_bytes())
        need(active == {'attempt': attempt, 'request_sha256': sha(self._candidate)}, 'handoff_invalid')
        state = candidate_state(decode(_regular(_path(root, 'enrollment-candidate-' + attempt + '.json')).read_bytes()),
            decode(self._candidate), attempt)
        need(not _path(root, 'enrollment-candidate-' + attempt + '.pending').exists()
            and 'claim_intent' in state['roles'][role]['events'] and state['closed'] is False,
            'handoff_invalid')
        custody = {'root': str(root), 'attempt': attempt, 'request': decode(self._candidate)}
        need(self._state['candidate'] is None or self._state['candidate'] == custody, 'handoff_invalid')
        self._state['candidate'] = custody
        self._state['roles'][role]['owner'] = 'candidate'
        self._state['phase'] = 'mixed'
        self._journal.save(self._state)
        return True

    def _candidate_settled(self, role):
        value = self._state['candidate']
        need(type(value) is dict and set(value) == {'root', 'attempt', 'request'}, 'journal_invalid')
        root = _regular(value['root'], True)
        need(root.resolve().is_relative_to(self.private.resolve()), 'private_path_invalid')
        attempt = value['attempt']
        need(type(attempt) is str and HEX32.fullmatch(attempt), 'journal_invalid')
        state = candidate_state(decode(_regular(_path(root, 'enrollment-candidate-' + attempt + '.json')).read_bytes()),
            candidate_request(value['request']), attempt)
        checked = candidate_request(value['request'])
        need(checked['runtime_sha256'] == self.request['runtime_sha256']
            and all(checked['roles'][r] == {'device_binding': self.request['roles'][r]['device_binding'],
                'originals': self.pins[r]} for r in ROLES), 'journal_invalid')
        row = state['roles'][role]
        return (not _path(root, 'enrollment-candidate-' + attempt + '.pending').exists()
            and row['restore_verified'] is True and row['original_boot_allowed'] is True
            and row['handles_closed'] is True)

    def _finish_release(self):
        """Finish only the durable ledger/marker closure; never access a board."""
        try:
            self._state['phase'] = 'released'
            self._journal.save(self._state)
            marker = _path(self.root, ACTIVE)
            if marker.exists():
                need(decode(_regular(marker).read_bytes()) == {'attempt': self._state['attempt'],
                    'request_sha256': sha(self._request)}, 'journal_invalid')
                marker.unlink()
            need(not marker.exists(), 'record_failed')
            # Publish the settled ledger again after marker absence is verified.
            self._journal.save(self._state)
        except BaseException as error:
            self._state['phase'] = 'held'
            self._failure('release_record', error, True)

    def release(self):
        need(self._state is not None, 'capture_session_used')
        if self._release_used:
            return self.result
        self._release_used = True
        for role in ROLES:
            row = self._state['roles'][role]
            try:
                if row['owner'] == 'candidate':
                    if self._candidate_settled(role):
                        row['owner'] = 'candidate-released'
                    else:
                        raise CaptureError('candidate_custody_unresolved')
                    self._journal.save(self._state)
                    continue
                if row['owner'] == 'candidate-released':
                    continue
                if not row['entered']:
                    row.update(original_boot_allowed=True, handles_closed=True)
                    self._journal.save(self._state)
                    continue
                if row['original_boot_allowed']:
                    need(self._backends[role].close() is True and self._backends[role].assert_idle() is True,
                        'rom_close_unconfirmed')
                    row['handles_closed'] = True
                    self._journal.save(self._state)
                    continue
                need(not row['reset_intent'], 'original_boot_state_uncertain')
                backend = self._backends[role]
                # Reset is the only acquisition operation allowed after its
                # capture ceiling. The worker freshly compares its observed
                # pins, sweeps missing spans twice, and checks safe NVS itself.
                for name in row['pins']:
                    self._raw(role, name)
                row['reset_intent'] = True
                self._journal.save(self._state)
                need(backend.reset_original(self._budget.check(True)) is True, 'original_reset_uncertain')
                self._budget.check(True)
                row['original_boot_allowed'] = True
                self._journal.save(self._state)
                need(backend.close() is True and backend.assert_idle() is True, 'rom_close_unconfirmed')
                row['handles_closed'] = True
                self._journal.save(self._state)
            except BaseException as error:
                self._failure('release_' + role, error, True)
                try:
                    self._journal.save(self._state)
                except BaseException:
                    pass
        settled = all(row['owner'] == 'candidate-released' or
            (row['original_boot_allowed'] and row['handles_closed']) for row in self._state['roles'].values())
        if settled:
            self._finish_release()
        return self.result


def recover_release(private_root, evidence_root, request, grant_raw, grant_sha256, **kwargs):
    """A new release-only grant can resolve saved, unadopted original custody.

    This never returns a handoff. A pending journal, uncertain reset or unresolved
    candidate owner remains held, even when the process hardware lock is free.
    """
    session = CaptureSession(private_root, evidence_root, request, grant_raw, grant_sha256, **kwargs)
    grant = validate_grant(session.request, grant_raw, grant_sha256, 'release', session.utc)
    _regular(session.private, True)
    _regular(session.root, True)
    need(session.private.name == '.private' and session.root != session.private
        and session.root.resolve().is_relative_to(session.private.resolve()), 'private_root_invalid')
    origin = grant['origin_attempt']
    session._journal = _Journal(session.root, origin)
    need(not session._journal.pending.exists(), 'journal_pending')
    state = decode(_regular(session._journal.file).read_bytes())
    need(type(state) is dict and set(state) == {'schema', 'attempt', 'request', 'grant_sha256',
        'phase', 'capture_complete', 'first_failure', 'cleanup_failure', 'receipt', 'candidate', 'roles'}
        and state['schema'] == 'OT-ORIGINAL-CAPTURE-JOURNAL-1' and state['attempt'] == origin
        and state['request'] == session.request and state['phase'] in ('capturing', 'held', 'verified', 'mixed', 'released')
        and type(state['grant_sha256']) is str and HEX64.fullmatch(state['grant_sha256'])
        and type(state['capture_complete']) is bool
        and type(state['roles']) is dict and set(state['roles']) == set(ROLES), 'journal_invalid')
    for field in ('first_failure', 'cleanup_failure'):
        value = state[field]
        need(value is None or (type(value) is list and len(value) == 2
            and type(value[0]) is str and re.fullmatch('[a-zA-Z_]+', value[0])
            and value[1] in _CATEGORIES), 'journal_invalid')
    need(decode(_regular(_path(session.root, ACTIVE)).read_bytes()) ==
        {'attempt': origin, 'request_sha256': sha(session._request)}, 'journal_invalid')
    need(decode(_regular(_path(session.root, 'enrollment-original-used-' + origin)).read_bytes()) ==
        {'grant_sha256': state['grant_sha256']}, 'journal_invalid')
    for row in state['roles'].values():
        need(type(row) is dict and set(row) == {'owner', 'entered', 'pins', 'reset_intent',
            'original_boot_allowed', 'handles_closed'} and row['owner'] in
            ('capture', 'candidate', 'candidate-released') and type(row['pins']) is dict
            and set(row['pins']).issubset(SPANS)
            and all(type(row[k]) is bool for k in ('entered', 'reset_intent',
                'original_boot_allowed', 'handles_closed')), 'journal_invalid')
        need(row['entered'] or (not row['pins'] and not row['reset_intent']), 'journal_invalid')
        need(row['owner'] == 'capture' or (row['entered'] and state['candidate'] is not None), 'journal_invalid')
        if not row['entered']:
            need(row['owner'] == 'capture' and row['original_boot_allowed'] == row['handles_closed'], 'journal_invalid')
        elif row['owner'] == 'capture':
            need(not row['original_boot_allowed'] or row['reset_intent'], 'journal_invalid')
        need(not row['reset_intent'] or row['original_boot_allowed'], 'original_boot_state_uncertain')
    session._state = state
    adopted = any(row['owner'] in ('candidate', 'candidate-released') for row in state['roles'].values())
    need((state['candidate'] is not None) == adopted, 'journal_invalid')
    for role in ROLES:
        for name, pin in state['roles'][role]['pins'].items():
            need(type(pin) is dict and set(pin) == {'bytes', 'sha256'}
                and type(pin['bytes']) is int and pin['bytes'] == SPANS[name][1]
                and type(pin['sha256']) is str and HEX64.fullmatch(pin['sha256']), 'journal_invalid')
            session._raw(role, name)
        if state['roles'][role]['owner'] in ('candidate', 'candidate-released'):
            need(state['capture_complete'] is True, 'journal_invalid')
            need(session._candidate_settled(role), 'candidate_custody_unresolved')
    if state['capture_complete']:
        need(all(set(row['pins']) == set(SPANS) for row in state['roles'].values()), 'journal_invalid')
        session._receipt()
    else:
        need(state['receipt'] is None, 'journal_invalid')
    closure_only = state['phase'] == 'released'
    if closure_only:
        # Released+matching ACTIVE is the exact interrupted closure shape.
        # All role/reset/candidate proofs above remain mandatory; a phase word
        # alone never admits a reboot or skips unresolved original custody.
        need(all(row['owner'] == 'candidate-released' or (row['owner'] == 'capture'
            and row['original_boot_allowed'] and row['handles_closed'])
            for row in state['roles'].values()), 'journal_invalid')
    # Validate saved custody and originals before fresh release authority is
    # consumed. Never replay the prior capture grant or reconstruct a handoff.
    grant = session._start('release')
    if closure_only:
        session._release_used = True
        session._finish_release()
        return session.result
    stage = 'release_setup'
    try:
        session._make_backends(grant)
        for role in ROLES:
            row = state['roles'][role]
            if row['owner'] == 'capture' and row['entered'] and not row['original_boot_allowed']:
                stage = 'release_compare_' + role
                session._admit(session._backends[role].claim(
                    session.request['roles'][role]['device_binding'], session._budget.check()), role)
                for name in row['pins']:
                    need(session._read(role, name) == session._raw(role, name), 'original_changed')
    except BaseException as error:
        # A changed or unobserved saved original cannot be safely rebooted from
        # a newly collected replacement pin. Preserve exact held custody.
        session._failure(stage, error)
        session._failure(stage, error, True)
        try:
            session._journal.save(session._state)
        except BaseException:
            pass
        return session.result
    return session.release()
