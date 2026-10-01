"""Explicit isolated execute/recover composition; help/preflight are inert.

No request, profile, key or grant issuer is included. Action CLI invocations
require the real source-pinned capsule and a wholly pinned private package.
Test factories are available only to direct Python callers, never CLI options.
Recovery's usual-screen ACK is a separate runner observation; it never changes
the frozen custody result or its journal's owner_confirmed flag.
"""
import argparse
from dataclasses import dataclass, field
import hashlib
import json
import math
import os
from pathlib import Path
import re
import sys
import time

from enrollment_candidate_runtime import (Assembly, canonical, decode, descriptor,
    pin_valid, read_pin, regular, verify_assembly)
from enrollment_candidate_controller import Clock, Controller, ControllerError, CheckpointAck, ROLES
from enrollment_candidate_custody import (ACTIVE, Budget, CustodyResult, validate_request,
    validate_grant, execute, recover, Error as CustodyError)
from enrollment_candidate_operator import CandidateOperator, CheckpointUI, CapturedReferences, OperatorError
from enrollment_candidate_rom_adapter import Runtime, HardwareLease, ROMBackend, DeviceProfile, opaque_identity, identity, AdapterError
from enrollment_candidate_usb_client import ClientError, NO_ARGUMENTS, HEX_ARGUMENTS
from enrollment_candidate_runtime import RuntimeErrorFixed

# A lexical word is not a trusted diagnostic. Only these reviewed fixed codes
# and owned exception types may cross a durable/public result boundary.
PUBLIC_CATEGORIES = frozenset('''activation_failed activation_invalid admission_invalid
artifact_write_failed assembly_changed assembly_invalid backend_invalid base_changed
base_policy_conflict bootloader_changed candidate_boot_uncertain capture_incomplete
capture_readback_failed case_invalid checkpoint_group_invalid checkpoint_inactive
checkpoint_invalid checkpoint_reentry checkpoint_token_invalid child_refused client_busy
clock_invalid controller_busy controller_used custody_held custody_operation_failed
custody_result_invalid deadline_expired deadline_extended deadline_invalid duplicate_identity
endpoint_invalid event_invalid evidence_root_invalid execution_not_admitted expected_refusal_missing
file_changed final_readback_failed grant_changed grant_expired grant_invalid grant_used
handles_not_closed hardware_lease_busy host_clock_invalid human_confirmation_missing
identity_binding_invalid identity_invalid identity_guard image_changed image_invalid invalid_command
invalid_decimal invalid_public_value invalid_read invalid_response isolated_caller_required
journal_invalid journal_interrupted journal_pending layout_not_isolated lease_terminal line_overflow
mode_invalid operator_used original_boot_state_uncertain original_changed original_layout_required
original_mismatch original_pin_invalid original_reset_uncertain output_exists output_invalid
owner_confirmation_missing package_changed package_invalid pair_invalid partial_write
passive_constructor_opened passive_control_lines_invalid passive_generation_invalid passive_handle_reused
passive_handles_unconfirmed passive_identity_guard passive_open_failed passive_owner_active
passive_owner_terminal passive_reentry passive_route_changed passive_route_invalid path_invalid
pending_reset_storage pin_invalid private_path_invalid private_root_invalid private_view_failed
private_view_not_cleared profile_invalid protected_changed read_invalid readiness_required
receipt_exists receipt_failed record_failed record_invalid recovery_archive_missing recovery_binding_invalid
reference_duplicate reference_generation reference_invalid reference_missing reference_unchecked request_invalid
reset_intent_unverified restart_failed restart_not_admitted restoration_unconfirmed restore_not_admitted
restore_readback_failed rom_adapter_refused rom_close_unconfirmed rom_hold_unverified rom_operation_failed
rom_owner_required runner_operation_failed runner_refused runtime_admission_failed runtime_binding_invalid
runtime_busy serial_operation_failed source_pins_changed source_pins_invalid source_root_invalid
startup_noise_exceeded status_mismatch target_refused trial_result_invalid unexpected_response runner_attempt_used
unexpected_response_tail unsolicited_response write_invalid write_readback_failed controller_operation_failed
activation_expired hardware_lease_failed checkpoint_sample_failed'''.split())
PUBLIC_STAGES = frozenset({'composition', 'ownership', 'execute', 'recover', 'owner_confirmation',
    'passive_cleanup', 'private_view', 'receipt', 'cleanup', 'restart', 'journal', 'custody', 'candidate_case'} |
    {prefix + '_' + role for prefix in ('capture', 'install', 'restore', 'open') for role in ROLES} |
    {verb.lower() + '_' + role for verb in NO_ARGUMENTS | set(HEX_ARGUMENTS) | {'BEGIN', 'SENDSTATUS'} for role in ROLES} |
    {kind + '_' + roles for kind in ('fingerprint_local', 'fingerprint', 'transcript', 'reset_gesture', 'usual_screen')
     for roles in ('A', 'B', 'AB')})


class RunnerError(RuntimeError):
    """Fixed categories; no private input or native diagnostics."""


def need(value, category='runner_refused'):
    if not value:
        raise RunnerError(category)


def _view_factory():
    from enrollment_candidate_private_view import WindowsPrivateView
    return WindowsPrivateView()


def _ref(ref, private):
    need(type(ref) is dict and set(ref) == {'path', 'bytes', 'sha256'}
         and pin_valid({k: ref[k] for k in ('bytes', 'sha256')}), 'reference_invalid')
    return read_pin(ref['path'], {k: ref[k] for k in ('bytes', 'sha256')}, private=private)


@dataclass(frozen=True)
class Prepared:
    package_path: Path
    package_sha256: str
    operation: str
    evidence_root: Path
    assembly: Assembly
    execute_deadline: float
    restore_deadline: float
    request_raw: bytes = field(repr=False)
    grant_raw: bytes = field(repr=False)
    grant_sha256: str = field(repr=False)
    images: tuple = field(repr=False)
    identities: tuple = field(repr=False)
    binding_key: bytes = field(repr=False)
    profiles: tuple = field(repr=False)
    refs_raw: bytes = field(repr=False)

    @property
    def request(self):
        return decode(self.request_raw)

    @property
    def grant(self):
        return decode(self.grant_raw)


def preflight(package_path, package_sha256, *, utc=time.time, monotonic=time.monotonic,
              assembly_verifier=verify_assembly):
    """Nonconsuming admission only; no lease, view, serial or SDK import."""
    clock = Clock(monotonic)
    start, started_utc = clock.now(), utc()
    need(type(started_utc) in (int, float) and math.isfinite(started_utc), 'clock_invalid')
    path = regular(package_path)
    raw = path.read_bytes()
    need(len(raw) <= 32768 and type(package_sha256) is str
         and hashlib.sha256(raw).hexdigest() == package_sha256, 'package_changed')
    package = decode(raw)
    need(type(package) is dict and set(package) == {'schema', 'operation', 'evidence_root',
         'assembly', 'request', 'grant', 'images', 'identities', 'binding_key', 'profiles'}
         and package['schema'] == 'OT-CANDIDATE-RUNNER-1'
         and package['operation'] in ('execute', 'recover'), 'package_invalid')
    assembly_ref = package['assembly']
    need(type(assembly_ref) is dict and set(assembly_ref) == {'path', 'bytes', 'sha256'}
         and pin_valid({k: assembly_ref[k] for k in ('bytes', 'sha256')}), 'reference_invalid')
    assembly = assembly_verifier(assembly_ref['path'], assembly_ref['sha256'])
    need(type(assembly) is Assembly, 'assembly_invalid')
    private = regular(assembly.worktree / '.private', directory=True)
    need(path.resolve().is_relative_to(private.resolve()), 'path_invalid')
    _ref(assembly_ref, private)
    evidence_root = regular(package['evidence_root'], directory=True)
    need(evidence_root.resolve().is_relative_to(private.resolve()) and evidence_root != private,
         'evidence_root_invalid')
    sidecar = decode(_ref(assembly_ref, private))
    base_ref = sidecar['base']
    base_manifest = decode(_ref(base_ref, private))
    need(not any(evidence_root.resolve().is_relative_to(Path(root).resolve())
                 for root in (assembly.root, base_manifest['root'])), 'evidence_root_invalid')
    refs = {name: package[name] for name in ('assembly', 'request', 'grant', 'identities',
                                          'binding_key', 'profiles')}
    data = {name: _ref(ref, private) for name, ref in refs.items()}
    request = validate_request(decode(data['request']))
    grant = validate_grant(request, data['grant'], refs['grant']['sha256'], package['operation'], utc)
    need(request['runtime_sha256'] == assembly.manifest_sha256, 'runtime_binding_invalid')
    need(grant['issued_utc'] <= started_utc < grant['execute_expires_utc'], 'grant_expired')
    execute_deadline = start + grant['execute_expires_utc'] - started_utc
    restore_deadline = start + grant['restore_expires_utc'] - started_utc
    clock.check(execute_deadline)
    need(type(package['images']) is dict and set(package['images']) == {'application', 'partition'}, 'image_invalid')
    images = {}
    for name, ref in package['images'].items():
        raw_image = _ref(ref, private)
        if name == 'partition':
            need(len(raw_image) in (3072, 4096), 'image_invalid')
            raw_image = raw_image.ljust(4096, b'\xff')
        need(descriptor(raw_image) == request['images'][name], 'image_changed')
        images[name] = raw_image
        refs['image_' + name] = ref
    identities = decode(data['identities'])
    need(type(identities) is dict and set(identities) == {'schema', 'roles'}
         and identities['schema'] == 'OT-CANDIDATE-IDENTITIES-1'
         and type(identities['roles']) is dict and set(identities['roles']) == set(ROLES), 'identity_invalid')
    key = data['binding_key']
    need(len(key) == 32, 'identity_invalid')
    bound = {role: identity(value) for role, value in identities['roles'].items()}
    need(bound['A'] != bound['B'], 'identity_invalid')
    profiles = decode(data['profiles'])
    need(type(profiles) is dict and set(profiles) == {'schema', 'roles'}
         and profiles['schema'] == 'OT-CANDIDATE-PROFILES-1'
         and type(profiles['roles']) is dict and set(profiles['roles']) == set(ROLES), 'profile_invalid')
    checked_profiles = {}
    for role in ROLES:
        binding = request['roles'][role]['device_binding']
        need(opaque_identity(key, bound[role]) == binding, 'identity_binding_invalid')
        row = profiles['roles'][role]
        need(type(row) is dict and set(row) == {'model', 'device_binding', 'flash_bytes', 'evidence_sha256'}
             and row['model'] == 'heltec_v4_esp32s3' and row['device_binding'] == binding
             and type(row['flash_bytes']) is int and row['flash_bytes'] == 16777216
             and type(row['evidence_sha256']) is str and re.fullmatch('[0-9a-f]{64}', row['evidence_sha256']),
             'profile_invalid')
        checked_profiles[role] = DeviceProfile(**row)
    need(not (evidence_root / ('enrollment-candidate-used-' + grant['attempt'])).exists(), 'grant_used')
    prefix = 'enrollment-candidate-runner-' + grant['attempt']
    need(not any((evidence_root / (prefix + suffix)).exists()
         or (evidence_root / (prefix + suffix)).is_symlink()
         for suffix in ('-events.jsonl', '.json', '.pending', '.unaccepted')), 'runner_attempt_used')
    if package['operation'] == 'execute':
        need(not (evidence_root / ACTIVE).exists(), 'custody_held')
    else:
        active = decode(regular(evidence_root / ACTIVE).read_bytes())
        need(active == {'attempt': grant['origin_attempt'],
             'request_sha256': hashlib.sha256(canonical(request)).hexdigest()}, 'recovery_binding_invalid')
    clock.check(execute_deadline)
    return Prepared(path, package_sha256, package['operation'], evidence_root, assembly,
        execute_deadline, restore_deadline, canonical(request), data['grant'], refs['grant']['sha256'],
        tuple(images.items()), tuple(bound.items()), key, tuple(checked_profiles.items()), canonical(refs))


@dataclass(frozen=True)
class RunnerResult:
    operation: str
    outcome: str
    first_failure: tuple | None
    custody: CustodyResult | None
    owner_observed: bool
    lease_released: bool
    receipt_path: Path | None
    runner_failure: tuple | None = None


class _BoundBackend:
    """Clamp frozen custody calls to the original admission ceilings."""
    def __init__(self, backend, execute_deadline, restore_deadline, restoring, clock):
        self.backend, self.execution, self.restoration = backend, execute_deadline, restore_deadline
        self.restoring, self.claimed, self.clock = restoring, False, clock

    def _limit(self, deadline):
        need(type(deadline) in (int, float) and math.isfinite(deadline), 'deadline_invalid')
        value = min(deadline, self.restoration if self.restoring else self.execution)
        self.clock.check(value)
        return value

    def _invoke(self, method, deadline, *args):
        limit = self._limit(deadline)
        result = getattr(self.backend, method)(*args, limit)
        self.clock.check(limit)
        return result

    def claim(self, binding, deadline):
        self.restoring = self.restoring or self.claimed
        self.claimed = True
        return self._invoke('claim', deadline, binding)

    def guard(self, binding, deadline):
        return self._invoke('guard', deadline, binding)

    def read(self, offset, size, deadline):
        return self._invoke('read', deadline, offset, size)

    def write(self, offset, raw, deadline):
        return self._invoke('write', deadline, offset, raw)

    def hold_rom(self, deadline):
        return self._invoke('hold_rom', deadline)

    def boot_candidate(self, deadline):
        need(not self.restoring, 'execution_not_admitted')
        return self._invoke('boot_candidate', deadline)

    def restart_candidate(self, deadline):
        need(not self.restoring, 'execution_not_admitted')
        return self._invoke('restart_candidate', deadline)

    def reset_original(self, deadline):
        need(self.restoring, 'restore_not_admitted')
        return self._invoke('reset_original', deadline)

    def close(self):
        return self.backend.close()

    def assert_idle(self):
        return self.backend.assert_idle()


class _BoundOperator:
    def __init__(self, operator, execution, restoration, clock):
        self.operator, self.execution, self.restoration, self.clock = operator, execution, restoration, clock

    def run(self, case, group, deadline):
        limit = min(deadline, self.execution)
        self.clock.check(limit)
        result = self.operator.run(case, group, limit)
        self.clock.check(limit)
        return result

    def confirm_original(self, deadline):
        limit = min(deadline, self.restoration)
        self.clock.check(limit)
        result = self.operator.confirm_original(limit)
        self.clock.check(limit)
        return result

    def close(self):
        return self.operator.close()

    def assert_idle(self):
        return self.operator.assert_idle()


def _write_once(path, raw, *, on_create=None):
    need(not path.exists() and not path.is_symlink(), 'receipt_exists')
    with path.open('xb', buffering=0) as stream:
        if on_create is not None:
            on_create(path)
        need(stream.write(raw) == len(raw), 'receipt_failed')
        os.fsync(stream.fileno())
    need(path.read_bytes() == raw, 'receipt_failed')


class _Events:
    def __init__(self, root, attempt, request_sha, grant_sha, clock, execution, restoration):
        self.path = root / ('enrollment-candidate-runner-' + attempt + '-events.jsonl')
        self.clock, self.execution, self.restoration = clock, execution, restoration
        self.binding = {'request_sha256': request_sha, 'grant_sha256': grant_sha}
        _write_once(self.path, b'')
        self.count = 0

    def __call__(self, value):
        need(type(value) is dict, 'event_invalid')
        result = dict(self.binding, sequence=self.count)
        if value.get('schema') in ('OT-CANDIDATE-CHECKPOINT-1', 'OT-CANDIDATE-ACK-1'):
            need(set(value) == ({'schema', 'kind', 'roles', 'token', 'deadline'}
                 if value['schema'] == 'OT-CANDIDATE-CHECKPOINT-1' else {'schema', 'kind', 'roles', 'token'})
                 and value['kind'] in ('fingerprint_local', 'fingerprint', 'transcript', 'reset_gesture', 'usual_screen')
                 and type(value['roles']) is tuple and value['roles']
                 and all(role in ROLES for role in value['roles']) and type(value['token']) is str
                 and re.fullmatch('[0-9a-f]{32}', value['token']), 'event_invalid')
            result.update(schema=value['schema'], kind=value['kind'], roles=value['roles'],
                          token_sha256=hashlib.sha256(value['token'].encode('ascii')).hexdigest())
            ceiling = self.restoration if value['kind'] == 'usual_screen' else self.execution
            if 'deadline' in value:
                need(type(value['deadline']) in (int, float) and math.isfinite(value['deadline'])
                     and value['deadline'] <= ceiling, 'event_invalid')
        elif value.get('stage') == 'authenticated_status':
            need(set(value) == {'stage', 'source', 'destination', 'value'}
                 and value['source'] in ROLES and value['destination'] in ROLES
                 and type(value['value']) is int, 'event_invalid')
            result.update(stage='authenticated_status', source=value['source'], destination=value['destination'])
            ceiling = self.execution
        else:
            need(set(value) == {'stage', 'role', 'command'} and value['stage'] == 'expected_refusal'
                 and value['role'] in ROLES and value['command'] in ('STATUS', 'SENDSTATUS'), 'event_invalid')
            result.update(stage='expected_refusal', role=value['role'], command=value['command'])
            ceiling = self.execution
        self.clock.check(ceiling)
        raw = canonical(result) + b'\n'
        prior = regular(self.path).read_bytes()
        with self.path.open('ab', buffering=0) as stream:
            need(stream.write(raw) == len(raw), 'receipt_failed')
            os.fsync(stream.fileno())
        need(self.path.read_bytes() == prior + raw, 'receipt_failed')
        self.clock.check(ceiling)
        self.count += 1
        return True


def _category(error):
    if type(error) in (RunnerError, RuntimeErrorFixed, ControllerError, CustodyError,
                        OperatorError, AdapterError, ClientError) and str(error) in PUBLIC_CATEGORIES:
        return str(error)
    return 'runner_operation_failed'


def _fault(value):
    if value is None:
        return None
    if type(value) not in (tuple, list) or len(value) != 2 or not all(type(v) is str for v in value):
        return ('custody', 'runner_operation_failed')
    return (value[0] if value[0] in PUBLIC_STAGES else 'custody',
            value[1] if value[1] in PUBLIC_CATEGORIES else 'runner_operation_failed')


def _safe_custody(result):
    if result is None:
        return None
    need(type(result) is CustodyResult, 'custody_result_invalid')
    need(type(result.attempt) is str and re.fullmatch('[0-9a-f]{32}', result.attempt)
        and result.observation in ('not_observed', 'passed', 'failed')
        and type(result.custody_released) is bool and type(result.owner_confirmed) is bool
        and type(result.roles) is dict and set(result.roles) == set(ROLES), 'custody_result_invalid')
    fields = {'restore_verified', 'original_boot_allowed', 'handles_closed', 'settled_untouched'}
    for row in result.roles.values():
        need(type(row) is dict and set(row) == fields and all(type(v) is bool for v in row.values()), 'custody_result_invalid')
        need(not row['original_boot_allowed'] or row['restore_verified'], 'custody_result_invalid')
        need(not row['settled_untouched'] or (row['handles_closed'] and not row['restore_verified']
             and not row['original_boot_allowed']), 'custody_result_invalid')
        if result.custody_released:
            need(row['handles_closed'] and ((row['restore_verified'] and row['original_boot_allowed'])
                 or row['settled_untouched']), 'custody_result_invalid')
    need(not result.owner_confirmed or result.custody_released, 'custody_result_invalid')
    return {'attempt': result.attempt, 'observation': result.observation,
        'first_failure': _fault(result.first_failure), 'roles': result.roles,
        'custody_released': result.custody_released, 'owner_confirmed': result.owner_confirmed}


def _snapshot_custody(result, attempt):
    row = _safe_custody(result)
    need(row['attempt'] == attempt, 'custody_result_invalid')
    # Copy the accepted role projection; later cleanup callbacks cannot alter
    # the returned result or introduce unreviewed diagnostic payloads.
    return CustodyResult(row['attempt'], row['observation'], row['first_failure'],
        decode(canonical(row['roles'])), row['custody_released'], row['owner_confirmed'])


def run(package_path, package_sha256, mode, *, utc=time.time, monotonic=time.monotonic,
        runtime_factory=Runtime, lease_factory=HardwareLease, backend_factory=ROMBackend,
        operator_factory=CandidateOperator, view_factory=_view_factory,
        assembly_verifier=verify_assembly, require_isolated=False,
        execute_deadline=None, restore_deadline=None):
    prepared = preflight(package_path, package_sha256, utc=utc, monotonic=monotonic,
                         assembly_verifier=assembly_verifier)
    need(mode in ('execute', 'recover') and prepared.operation == mode, 'mode_invalid')
    if require_isolated:
        verify_assembly(prepared.assembly.assembly_path, prepared.assembly.assembly_sha256, require_isolated=True)
    execution, restoration = prepared.execute_deadline, prepared.restore_deadline
    for supplied in (execute_deadline, restore_deadline):
        need(supplied is None or (type(supplied) in (int, float) and math.isfinite(supplied)), 'deadline_invalid')
    if execute_deadline is not None:
        execution = min(execution, execute_deadline)
    if restore_deadline is not None:
        restoration = min(restoration, restore_deadline)
    need(execution <= restoration, 'deadline_invalid')
    clock = Clock(monotonic)
    clock.check(execution)
    # Recheck every immutable input snapshot immediately before composition and
    # ownership. Factories receive independent copies, never caller mappings.
    private = prepared.assembly.worktree / '.private'
    need(hashlib.sha256(regular(prepared.package_path).read_bytes()).hexdigest() == package_sha256, 'package_changed')
    for ref in decode(prepared.refs_raw).values():
        _ref(ref, private)
    assembly_verifier(prepared.assembly.assembly_path, prepared.assembly.assembly_sha256)
    clock.check(execution)
    request, grant = prepared.request, prepared.grant
    need(validate_grant(request, prepared.grant_raw, prepared.grant_sha256, mode, utc) == grant, 'grant_changed')
    runtime, lease, events = None, None, None
    custody, owner, released, fault, view, operator = None, False, False, None, None, None
    stage = 'composition'
    try:
        runtime = runtime_factory(prepared.assembly.manifest_path, prepared.assembly.manifest_sha256,
                                  private, monotonic=monotonic)
        lease = lease_factory(private, monotonic=monotonic)
        identities, profiles = dict(prepared.identities), dict(prepared.profiles)
        backends = {role: _BoundBackend(backend_factory(runtime, lease, request=prepared.request,
            role=role, images=dict(prepared.images), binding_key=prepared.binding_key,
            expected_identity=identities[role], device_profile=profiles[role], recovery_only=mode == 'recover'),
            execution, restoration, mode == 'recover', clock) for role in ROLES}
        events = _Events(prepared.evidence_root, grant['attempt'], hashlib.sha256(prepared.request_raw).hexdigest(),
                         prepared.grant_sha256, clock, execution, restoration)
        # Constructor callbacks cannot replace a pinned input before ownership.
        need(hashlib.sha256(regular(prepared.package_path).read_bytes()).hexdigest() == package_sha256, 'package_changed')
        for ref in decode(prepared.refs_raw).values():
            _ref(ref, private)
        assembly_verifier(prepared.assembly.assembly_path, prepared.assembly.assembly_sha256)
        stage = 'ownership'
        # The OS lease is file-only; no device activity precedes exact authority.
        clock.check(execution)
        need(lease.acquire(execution) is True, 'hardware_lease_busy')
        if mode == 'execute':
            view = view_factory()
            def restart(deadline):
                clock.check(min(deadline, execution))
                need(lease.assert_idle() is True, 'handles_not_closed')
                for role in ROLES:
                    need(backends[role].restart_candidate(min(deadline, execution)) is True, 'restart_failed')
                clock.check(min(deadline, execution))
                return True
            operator = operator_factory(runtime, lease, identities, prepared.binding_key,
                opaque_identity, view, restart, record=events, monotonic=monotonic)
            need(operator.activate(prepared.request, prepared.grant_raw, prepared.grant_sha256,
                 execution, utc=utc) is True, 'activation_failed')
            stage = 'execute'
            custody = _snapshot_custody(execute(prepared.evidence_root, prepared.request, prepared.grant_raw,
                prepared.grant_sha256, dict(prepared.images), backends,
                _BoundOperator(operator, execution, restoration, clock), utc=utc, monotonic=monotonic), grant['attempt'])
            owner = custody.owner_confirmed is True
        else:
            stage = 'recover'
            custody = _snapshot_custody(recover(prepared.evidence_root, prepared.request, prepared.grant_raw,
                prepared.grant_sha256, backends,
                lambda: runtime.assert_idle() is True and lease.assert_idle() is True,
                utc=utc, monotonic=monotonic), grant['origin_attempt'])
            if custody.custody_released:
                stage = 'owner_confirmation'
                need(all(row['handles_closed'] and ((row['restore_verified'] and row['original_boot_allowed'])
                     or row['settled_untouched'])
                     for row in custody.roles.values()) and runtime.assert_idle() is True
                     and lease.assert_idle() is True, 'restoration_unconfirmed')
                clock.check(restoration)
                view = view_factory()
                human = CheckpointUI(CapturedReferences(), view, group=request['group'], monotonic=monotonic)
                def forbidden(*args):
                    raise RunnerError('execution_not_admitted')
                observer = Controller(forbidden, human, forbidden, monotonic=monotonic, record=events)
                # Public confirm_original requires a prior trial run. Directly
                # use its same typed gate without inventing used/running state.
                ack = observer._human('usual_screen', ROLES, None, restoration)
                need(type(ack) is CheckpointAck and ack.schema == 'OT-CANDIDATE-ACK-1'
                     and ack.kind == 'usual_screen' and ack.roles == ROLES, 'owner_confirmation_missing')
                clock.check(restoration)
                owner = True
        clock.check(restoration)
    except BaseException as error:
        fault = (stage, _category(error))
    finally:
        # Passive close only, no retry/reset/restore outside frozen custody.
        if operator is not None:
            try:
                if operator.close() is not True or operator.assert_idle() is not True:
                    fault = fault or ('passive_cleanup', 'handles_not_closed')
            except BaseException:
                fault = fault or ('passive_cleanup', 'handles_not_closed')
        if view is not None:
            try:
                if view.close() is not True:
                    fault = fault or ('private_view', 'private_view_not_cleared')
            except BaseException:
                fault = fault or ('private_view', 'private_view_not_cleared')
        try:
            released = lease is not None and lease.close() is True
        except BaseException:
            released = False
    try:
        clock.check(restoration)
    except BaseException as error:
        fault = fault or ('cleanup', _category(error))
    first = _fault(custody.first_failure) if custody is not None and custody.first_failure is not None else fault
    complete = custody is not None and custody.custody_released and released
    if complete and not owner:
        fault = fault or ('owner_confirmation', 'owner_confirmation_missing')
        first = first or fault
    held = (custody is not None and not complete) or (prepared.evidence_root / ACTIVE).exists()
    outcome = ('held' if held else 'failed' if not complete or fault is not None or not owner else
               'recovered' if mode == 'recover' else 'passed' if custody.observation == 'passed' and first is None else 'failed')
    receipt = {'schema': 'OT-CANDIDATE-RUNNER-RESULT-1', 'operation': mode, 'outcome': outcome,
        'package_sha256': package_sha256, 'assembly_sha256': prepared.assembly.assembly_sha256,
        'request_sha256': hashlib.sha256(prepared.request_raw).hexdigest(),
        'grant_sha256': prepared.grant_sha256, 'attempt': grant['attempt'],
        'execute_deadline': execution, 'restore_deadline': restoration,
        'first_failure': first, 'runner_failure': fault, 'custody': _safe_custody(custody),
        'owner_observed': owner, 'lease_released': released,
        'events': descriptor(regular(events.path).read_bytes()) if events is not None else None}
    receipt_path = prepared.evidence_root / ('enrollment-candidate-runner-' + grant['attempt'] + '.json')
    created = set()
    try:
        pending = receipt_path.with_suffix('.pending')
        clock.check(restoration)
        _write_once(pending, canonical(receipt) + b'\n', on_create=created.add)
        clock.check(restoration)
        need(not receipt_path.exists(), 'receipt_exists')
        pending.rename(receipt_path)
        created.discard(pending)
        created.add(receipt_path)
        clock.check(restoration)
    except BaseException as error:
        fault = fault or ('receipt', _category(error))
        first = first or fault
        outcome = 'held' if held else 'failed'
        # Preserve any candidate result as explicitly unaccepted, then durably
        # report the failure. No late positive receipt is left published.
        try:
            unaccepted = receipt_path.with_suffix('.unaccepted')
            need(not unaccepted.exists(), 'receipt_exists')
            if receipt_path in created and receipt_path.exists():
                receipt_path.rename(unaccepted)
            elif receipt_path.with_suffix('.pending') in created and receipt_path.with_suffix('.pending').exists():
                receipt_path.with_suffix('.pending').rename(unaccepted)
            receipt.update(outcome=outcome, first_failure=first, runner_failure=fault)
            _write_once(receipt_path, canonical(receipt) + b'\n')
        except BaseException:
            receipt_path = None
    return RunnerResult(mode, outcome, first, custody, owner, released, receipt_path, fault)


def main(argv=None):
    class Parser(argparse.ArgumentParser):
        def error(self, message):
            raise RunnerError('runner_refused')
    parser = Parser(description='Source-pinned OTCAND1 runner; no authority issuer.')
    parser.add_argument('--mode', choices=('preflight', 'execute', 'recover'), required=True)
    parser.add_argument('--assembly', required=True)
    parser.add_argument('--assembly-sha256', required=True)
    parser.add_argument('--package')
    parser.add_argument('--package-sha256')
    parser.add_argument('--execute-deadline', type=float)
    parser.add_argument('--restore-deadline', type=float)
    mode = 'preflight'
    try:
        args = parser.parse_args(argv)
        mode = args.mode
        assembly = verify_assembly(args.assembly, args.assembly_sha256, require_isolated=True)
        need((args.package is None) == (args.package_sha256 is None), 'package_invalid')
        need((args.execute_deadline is None) == (args.restore_deadline is None), 'deadline_invalid')
        if args.package is not None:
            prepared = preflight(args.package, args.package_sha256)
            need(prepared.assembly.assembly_sha256 == assembly.assembly_sha256
                 and prepared.assembly.assembly_path == assembly.assembly_path, 'assembly_invalid')
        if mode == 'preflight':
            need(args.execute_deadline is None, 'deadline_invalid')
            outcome, category = 'ready', None
        else:
            need(args.package is not None, 'package_invalid')
            result = run(args.package, args.package_sha256, mode, require_isolated=True,
                execute_deadline=args.execute_deadline, restore_deadline=args.restore_deadline)
            outcome = result.outcome
            category = result.first_failure[1] if result.first_failure else None
    except SystemExit:
        raise
    except BaseException as error:
        outcome, category = 'failed', _category(error)
    print(json.dumps({'schema': 'OT-CANDIDATE-LAUNCH-1', 'mode': mode,
                      'outcome': outcome, 'category': category}, separators=(',', ':')))
    return 0 if outcome in ('ready', 'passed', 'recovered') else 1


if __name__ == '__main__':
    sys.exit(main())
