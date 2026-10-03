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
from enrollment_candidate_controller import Clock, Controller, ControllerError, CheckpointAck, ROLES, CASES
from enrollment_candidate_custody import (ACTIVE, Budget, CustodyResult, validate_request,
    validate_grant, execute, recover, Error as CustodyError)
from enrollment_candidate_operator import (CandidateOperator, CheckpointUI, CapturedReferences,
    OperatorError, PROGRESS_PHASES, QUERY_BOUNDARIES)
from enrollment_candidate_rom_adapter import Runtime, HardwareLease, ROMBackend, DeviceProfile, opaque_identity, identity, AdapterError
from enrollment_candidate_usb_client import (ClientError, NO_ARGUMENTS, HEX_ARGUMENTS,
    validate_query_snapshot, validate_startup_snapshot)
from enrollment_candidate_runtime import RuntimeErrorFixed
from enrollment_candidate_original_capture import CaptureError, _CATEGORIES as CAPTURE_CATEGORIES

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
activation_expired hardware_lease_failed checkpoint_sample_failed capture_handoff_invalid
capture_release_unconfirmed handoff_changed handoff_invalid handoff_used reply_invalid
startup_observation_disabled'''.split()) | CAPTURE_CATEGORIES
PUBLIC_STAGES = frozenset({'composition', 'ownership', 'execute', 'recover', 'owner_confirmation',
    'passive_cleanup', 'private_view', 'receipt', 'cleanup', 'capture_cleanup', 'startup_summary',
    'boot_observation_A', 'boot_observation_B',
    'restart', 'journal', 'custody', 'candidate_case'} |
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
    cleanup_failure: tuple | None = None
    restoration_failures: dict | None = None


class _BoundBackend:
    """Clamp frozen custody calls to the original admission ceilings."""
    def __init__(self, backend, execute_deadline, restore_deadline, restoring, clock, adopt=None):
        self.backend, self.execution, self.restoration = backend, execute_deadline, restore_deadline
        self.restoring, self.claimed, self.clock = restoring, False, clock
        self.adopt = adopt
        self.restoration_failure = None

    def _limit(self, deadline):
        need(type(deadline) in (int, float) and math.isfinite(deadline), 'deadline_invalid')
        value = min(deadline, self.restoration if self.restoring else self.execution)
        self.clock.check(value)
        return value

    def _invoke(self, method, deadline, *args):
        boundary = 'limit'
        try:
            limit = self._limit(deadline)
            boundary = 'call'
            result = getattr(self.backend, method)(*args, limit)
            boundary = 'postcheck'
            self.clock.check(limit)
            return result
        except BaseException as error:
            if self.restoring and self.restoration_failure is None:
                # Attribute the caught failure at its actual boundary; never
                # sample a later clock to guess which check rejected it.
                self.restoration_failure = dict(method=method, boundary=boundary,
                    domain='restoration', category=_category(error))
            raise

    def claim(self, binding, deadline):
        # Frozen custody writes ACTIVE and this role's claim_intent before
        # reaching here. The acquisition coordinator verifies those durable
        # records and transfers only this role, before any candidate claim I/O.
        if not self.claimed and self.adopt is not None:
            need(self.adopt() is True, 'capture_handoff_invalid')
        self.restoring = self.restoring or self.claimed
        self.claimed = True
        return self._invoke('claim', deadline, binding)

    def guard(self, binding, deadline):
        return self._invoke('guard', deadline, binding)

    @property
    def supports_guarded_read(self):
        return getattr(self.backend, 'supports_guarded_read', False) is True

    def guarded_read(self, binding, offset, size, deadline):
        return self._invoke('guarded_read', deadline, binding, offset, size)

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
    def __init__(self, operator, execution, restoration, clock, release_capture=None):
        self.operator, self.execution, self.restoration, self.clock = operator, execution, restoration, clock
        self.release_capture = release_capture

    def run(self, case, group, deadline):
        limit = min(deadline, self.execution)
        self.clock.check(limit)
        result = self.operator.run(case, group, limit)
        from enrollment_candidate_custody import _trial_result
        _trial_result(result, case)
        try:
            self.clock.check(limit)
        except ControllerError:
            # Preserve a validated rejection from its original check. This
            # cannot turn expired or rolled-back execution into a success;
            # all restoration checks keep their original separate ceiling.
            if result.outcome != 'failed':
                raise
        return result

    def startup_probe(self, role, generation, deadline):
        limit = min(deadline, self.execution)
        self.clock.check(limit)
        result = self.operator.startup_probe(role, generation, limit)
        if result.first_failure is None:
            self.clock.check(limit)
        return result

    def confirm_original(self, deadline):
        limit = min(deadline, self.restoration)
        self.clock.check(limit)
        # An untouched comparison role may still be acquisition-owned in ROM.
        # Release it before asking for the pair's usual-screen observation.
        if self.release_capture is not None:
            self.release_capture()
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
    def __init__(self, root, attempt, request_sha, grant_sha, clock, execution, restoration, *, case=None):
        self.path = root / ('enrollment-candidate-runner-' + attempt + '-events.jsonl')
        self.clock, self.execution, self.restoration = clock, execution, restoration
        self.binding = {'request_sha256': request_sha, 'grant_sha256': grant_sha}
        _write_once(self.path, b'')
        self.count = 0
        self.progress_count = 0
        self.case, self.startup_count = case, 0
        self.max_generation = 2 if case in ('retained_rekey', 'recovery_after_A_commit',
            'recovery_after_B_commit') else 1
        self.query_summary_count = 0
        self.startup_counts, self.probe_summaries = {}, set()

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
            need(self.case != 'startup_A' or value['kind'] == 'usual_screen', 'event_invalid')
            result.update(schema=value['schema'], kind=value['kind'], roles=value['roles'],
                          token_sha256=hashlib.sha256(value['token'].encode('ascii')).hexdigest())
            ceiling = self.restoration if value['kind'] == 'usual_screen' else self.execution
            if 'deadline' in value:
                need(type(value['deadline']) in (int, float) and math.isfinite(value['deadline'])
                     and value['deadline'] <= ceiling, 'event_invalid')
        elif value.get('schema') == 'OT-CANDIDATE-PROGRESS-1':
            # Fixed pre-operation phases only; no transport/private values.
            need(set(value) == {'schema', 'phase', 'role', 'generation'}
                 and type(value['phase']) is str and value['phase'] in PROGRESS_PHASES
                 and type(value['role']) is str and value['role'] in ROLES
                 and type(value['generation']) is int and 1 <= value['generation'] <= self.max_generation
                 and self.progress_count < 96, 'event_invalid')
            need(self.case != 'startup_A' or (value['role'] == 'A' and value['generation'] == 1
                 and value['phase'] != 'begin'),
                 'event_invalid')
            need(value['phase'] != 'boot_observation' or self.case in CASES, 'event_invalid')
            result.update(schema=value['schema'], phase=value['phase'],
                          role=value['role'], generation=value['generation'])
            ceiling = self.execution
        elif value.get('schema') == 'OT-CANDIDATE-STARTUP-1':
            need(self.case in CASES and set(value) == {'schema', 'phase', 'role', 'generation', 'value'}
                 and type(value['phase']) is str and value['role'] in ROLES
                 and type(value['generation']) is int and 1 <= value['generation'] <= self.max_generation,
                 'event_invalid')
            key = (value['role'], value['generation'])
            need(self.case != 'startup_A' or key == ('A', 1), 'event_invalid')
            count = self.startup_counts.get(key, 0)
            need(count < 2, 'event_invalid')
            if count == 0:
                need(value['phase'] == 'hello' and type(value['value']) is str
                     and value['value'] in ('ready', 'refused'), 'event_invalid')
            else:
                need((value['phase'] == 'bootstatus' and type(value['value']) is int and 0 <= value['value'] <= 9)
                     or (value['phase'] == 'bootstatus_failed' and type(value['value']) is str
                         and value['value'] == 'failed'), 'event_invalid')
            result.update(schema=value['schema'], phase=value['phase'], role=value['role'],
                          generation=value['generation'], value=value['value'])
            ceiling = self.execution
        elif value.get('schema') in ('OT-CANDIDATE-QUERY-SUMMARY-2', 'OT-CANDIDATE-QUERY-SUMMARY-3'):
            scoped = value['schema'] == 'OT-CANDIDATE-QUERY-SUMMARY-3'
            role, generation = value.get('role', 'A'), value.get('generation', 1)
            key = (role, generation)
            need((scoped and self.case in CASES - {'startup_A'} and role in ROLES
                  and type(generation) is int and 1 <= generation <= self.max_generation
                  and key not in self.probe_summaries)
                 or (not scoped and self.case == 'startup_A' and self.query_summary_count == 0),
                 'event_invalid')
            need(set(value) == {'schema', 'startup', 'queries', 'boot_observation', 'diagnostic_failure'}
                 | ({'role', 'generation'} if scoped else set()) and type(value['queries']) is list
                 and len(value['queries']) <= 2, 'event_invalid')
            diagnostic_failure = value['diagnostic_failure']
            need(diagnostic_failure is None or (type(diagnostic_failure) is str and diagnostic_failure in
                 ('startup_snapshot', 'query_snapshot', 'query_finish')), 'event_invalid')
            startup = value['startup']
            need(startup is None or (type(startup) is dict and set(startup) == {
                'shared_allowance_ns', 'open_sampled_elapsed_ns', 'open_returned',
                'hello_allowance_ns', 'controller_boundary'}), 'event_invalid')
            if startup is not None:
                for name in ('shared_allowance_ns', 'open_sampled_elapsed_ns', 'hello_allowance_ns'):
                    need(startup[name] is None or (type(startup[name]) is int
                         and 0 <= startup[name] <= 0x7fffffffffffffff), 'event_invalid')
                need(type(startup['open_returned']) is bool
                     and type(startup['controller_boundary']) is str
                     and startup['controller_boundary'] in ('not_entered', 'precheck', 'endpoint', 'postcheck', 'returned'),
                     'event_invalid')
            queries = []
            fields = {'phase', 'role', 'generation', 'boundary', 'entry_allowance_ns',
                      'prelude_elapsed_ns', 'elapsed_ns', 'timing_valid', 'transport'}
            for index, row in enumerate(value['queries']):
                need(type(row) is dict and set(row) == fields
                     and type(row['phase']) is str and row['phase'] == ('hello', 'bootstatus')[index]
                     and type(row['role']) is str and row['role'] == role
                     and type(row['generation']) is int and row['generation'] == generation
                     and type(row['boundary']) is str and row['boundary'] in QUERY_BOUNDARIES
                     and type(row['timing_valid']) is bool, 'event_invalid')
                for name in ('entry_allowance_ns', 'prelude_elapsed_ns', 'elapsed_ns'):
                    need(row[name] is None or (type(row[name]) is int
                         and 0 <= row[name] <= 0x7fffffffffffffff), 'event_invalid')
                need(row['timing_valid'] or (row['prelude_elapsed_ns'] is None
                     and row['elapsed_ns'] is None), 'event_invalid')
                need(row['prelude_elapsed_ns'] is None or (row['elapsed_ns'] is not None
                     and row['prelude_elapsed_ns'] <= row['elapsed_ns']), 'event_invalid')
                if row['transport'] is not None:
                    need(row['boundary'] in ('endpoint', 'postcheck', 'returned'), 'event_invalid')
                    try:
                        transport = validate_query_snapshot(row['transport'])
                    except BaseException:
                        raise RunnerError('event_invalid') from None
                else:
                    need(row['boundary'] in ('owner_check', 'progress') or
                         diagnostic_failure in ('query_snapshot', 'query_finish'), 'event_invalid')
                    transport = None
                queries.append(dict(row, transport=transport))
            result.update(schema=value['schema'], startup=dict(startup) if startup is not None else None,
                          diagnostic_failure=diagnostic_failure, queries=queries)
            boot = value['boot_observation']
            try:
                result['boot_observation'] = validate_startup_snapshot(boot) if boot is not None else None
            except BaseException:
                raise RunnerError('event_invalid') from None
            if scoped:
                result.update(role=role, generation=generation)
            ceiling = self.execution
        elif value.get('stage') == 'authenticated_status':
            need(self.case != 'startup_A' and set(value) == {'stage', 'source', 'destination', 'value'}
                 and value['source'] in ROLES and value['destination'] in ROLES
                 and type(value['value']) is int, 'event_invalid')
            result.update(stage='authenticated_status', source=value['source'], destination=value['destination'])
            ceiling = self.execution
        else:
            need(self.case != 'startup_A' and set(value) == {'stage', 'role', 'command'} and value['stage'] == 'expected_refusal'
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
        # A durable row owns its sequence even if the post-write check expires.
        self.count += 1
        if value.get('schema') == 'OT-CANDIDATE-PROGRESS-1':
            self.progress_count += 1
        if value.get('schema') == 'OT-CANDIDATE-STARTUP-1':
            self.startup_count += 1
            self.startup_counts[(value['role'], value['generation'])] = count + 1
        if value.get('schema') in ('OT-CANDIDATE-QUERY-SUMMARY-2', 'OT-CANDIDATE-QUERY-SUMMARY-3'):
            self.query_summary_count += 1
            if scoped:
                self.probe_summaries.add(key)
        self.clock.check(ceiling)
        return True


def _category(error):
    if type(error) in (RunnerError, RuntimeErrorFixed, ControllerError, CustodyError,
                        OperatorError, AdapterError, ClientError, CaptureError):
        args = error.args
        if len(args) == 1 and type(args[0]) is str and args[0] in PUBLIC_CATEGORIES:
            return args[0]
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
        'first_failure': _fault(result.first_failure), 'cleanup_failure': _fault(result.cleanup_failure),
        'roles': result.roles,
        'custody_released': result.custody_released, 'owner_confirmed': result.owner_confirmed}


def _snapshot_custody(result, attempt):
    row = _safe_custody(result)
    need(row['attempt'] == attempt, 'custody_result_invalid')
    # Copy the accepted role projection; later cleanup callbacks cannot alter
    # the returned result or introduce unreviewed diagnostic payloads.
    return CustodyResult(row['attempt'], row['observation'], row['first_failure'],
        decode(canonical(row['roles'])), row['custody_released'], row['owner_confirmed'], row['cleanup_failure'])


def run(package_path, package_sha256, mode, *, utc=time.time, monotonic=time.monotonic,
        runtime_factory=Runtime, lease_factory=HardwareLease, backend_factory=ROMBackend,
        operator_factory=CandidateOperator, view_factory=_view_factory,
        assembly_verifier=verify_assembly, require_isolated=False,
        execute_deadline=None, restore_deadline=None,
        capture_session=None, held_runtime=None, held_lease=None, handoff_validator=None):
    prepared = preflight(package_path, package_sha256, utc=utc, monotonic=monotonic,
                         assembly_verifier=assembly_verifier)
    need(mode in ('execute', 'recover') and prepared.operation == mode, 'mode_invalid')
    need((capture_session is None) == (held_runtime is None) == (held_lease is None)
         and (capture_session is None or mode == 'execute'), 'capture_handoff_invalid')
    need(handoff_validator is None or (capture_session is not None and callable(handoff_validator)),
         'capture_handoff_invalid')
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
    runtime, lease, events = held_runtime, held_lease, None
    custody, owner, released, fault, view, operator = None, False, False, None, None, None
    cleanup_fault = None
    backends = {}
    stage = 'composition'
    capture_release_attempted = False
    def release_capture():
        nonlocal capture_release_attempted
        if capture_session is not None and not capture_release_attempted:
            capture_release_attempted = True
            capture_session.release()
        need(capture_session is None or not capture_session.held, 'capture_release_unconfirmed')
    try:
        if capture_session is None:
            runtime = runtime_factory(prepared.assembly.manifest_path, prepared.assembly.manifest_sha256,
                                      private, monotonic=monotonic)
            lease = lease_factory(private, monotonic=monotonic)
        identities, profiles = dict(prepared.identities), dict(prepared.profiles)
        backends = {role: _BoundBackend(backend_factory(runtime, lease, request=prepared.request,
            role=role, images=dict(prepared.images), binding_key=prepared.binding_key,
            expected_identity=identities[role], device_profile=profiles[role], recovery_only=mode == 'recover'),
            execution, restoration, mode == 'recover', clock,
            adopt=(lambda role=role: capture_session.transfer_role(role,
                prepared.evidence_root, prepared.request, grant['attempt']))
                if capture_session is not None else None) for role in ROLES}
        events = _Events(prepared.evidence_root, grant['attempt'], hashlib.sha256(prepared.request_raw).hexdigest(),
                         prepared.grant_sha256, clock, execution, restoration, case=request['case'])
        # Constructor callbacks cannot replace a pinned input before ownership.
        need(hashlib.sha256(regular(prepared.package_path).read_bytes()).hexdigest() == package_sha256, 'package_changed')
        for ref in decode(prepared.refs_raw).values():
            _ref(ref, private)
        assembly_verifier(prepared.assembly.assembly_path, prepared.assembly.assembly_sha256)
        stage = 'ownership'
        # The OS lease is file-only; no device activity precedes exact authority.
        clock.check(execution)
        if capture_session is None:
            need(lease.acquire(execution) is True, 'hardware_lease_busy')
        if mode == 'execute':
            view = view_factory()
            def restart(deadline):
                clock.check(min(deadline, execution))
                need(lease.assert_idle() is True, 'handles_not_closed')
                for role in ROLES:
                    need(backends[role].restart_candidate(min(deadline, execution)) is True, 'restart_failed')
                    need(backends[role].close() is True, 'rom_close_unconfirmed')
                    probe = operator.startup_probe(role, 2, min(deadline, execution))
                    need(probe.first_failure is None and probe.handles_closed, 'restart_failed')
                    need(lease.assert_idle() is True, 'handles_not_closed')
                clock.check(min(deadline, execution))
                return True
            operator = operator_factory(runtime, lease, identities, prepared.binding_key,
                opaque_identity, view, restart, record=events, monotonic=monotonic)
            need(operator.activate(prepared.request, prepared.grant_raw, prepared.grant_sha256,
                 execution, utc=utc) is True, 'activation_failed')
            if capture_session is not None:
                # Fresh immutable receipt checks, complete current six-span
                # sweeps and same held-ROM owners precede candidate _used.
                need(capture_session.verify(prepared.request, runtime=runtime, lease=lease,
                    identities=identities, binding_key=prepared.binding_key,
                    profiles=profiles, deadline=execution) is True, 'capture_handoff_invalid')
                if handoff_validator is not None:
                    need(handoff_validator() is True, 'handoff_changed')
                need(hashlib.sha256(regular(prepared.package_path).read_bytes()).hexdigest() == package_sha256,
                     'package_changed')
                for ref in decode(prepared.refs_raw).values():
                    _ref(ref, private)
                assembly_verifier(prepared.assembly.assembly_path, prepared.assembly.assembly_sha256)
                clock.check(execution)
            stage = 'execute'
            custody = _snapshot_custody(execute(prepared.evidence_root, prepared.request, prepared.grant_raw,
                prepared.grant_sha256, dict(prepared.images), backends,
                _BoundOperator(operator, execution, restoration, clock, release_capture),
                utc=utc, monotonic=monotonic), grant['attempt'])
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
        if custody is not None:
            cleanup_fault = _fault(custody.cleanup_failure) or cleanup_fault
        # Passive close only, no retry/reset/restore outside frozen custody.
        if operator is not None:
            try:
                if operator.close() is not True or operator.assert_idle() is not True:
                    cleanup_fault = cleanup_fault or ('passive_cleanup', 'handles_not_closed')
                    fault = fault or cleanup_fault
            except BaseException:
                cleanup_fault = cleanup_fault or ('passive_cleanup', 'handles_not_closed')
                fault = fault or cleanup_fault
        if view is not None:
            try:
                if view.close() is not True:
                    cleanup_fault = cleanup_fault or ('private_view', 'private_view_not_cleared')
                    fault = fault or cleanup_fault
            except BaseException:
                cleanup_fault = cleanup_fault or ('private_view', 'private_view_not_cleared')
                fault = fault or cleanup_fault
        try:
            release_capture()
        except BaseException as error:
            cleanup_fault = cleanup_fault or ('capture_cleanup', _category(error))
            fault = fault or cleanup_fault
        try:
            released = lease is not None and lease.close() is True
        except BaseException:
            released = False
        if lease is not None and not released:
            cleanup_fault = cleanup_fault or ('cleanup', 'handles_not_closed')
    try:
        clock.check(restoration)
    except BaseException as error:
        cleanup_fault = cleanup_fault or ('cleanup', _category(error))
        fault = fault or cleanup_fault
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
        'first_failure': first, 'runner_failure': fault, 'cleanup_failure': cleanup_fault,
        'custody': _safe_custody(custody),
        'owner_observed': owner, 'lease_released': released,
        'events': descriptor(regular(events.path).read_bytes()) if events is not None else None}
    restoration_failures = {role: dict(backend.restoration_failure)
        for role, backend in backends.items() if backend.restoration_failure is not None}
    methods = {'claim', 'guard', 'guarded_read', 'read', 'write', 'hold_rom',
               'boot_candidate', 'restart_candidate', 'reset_original'}
    # Never expose arbitrary backend exception text or mutable callback data.
    restoration_failures = {role: dict(method=row['method'], boundary=row['boundary'],
        domain='restoration', category=row['category']) for role, row in restoration_failures.items()
        if set(row) == {'method', 'boundary', 'domain', 'category'}
        and type(row['method']) is str and row['method'] in methods
        and type(row['boundary']) is str and row['boundary'] in ('limit', 'call', 'postcheck')
        and type(row['domain']) is str and row['domain'] == 'restoration'
        and type(row['category']) is str and row['category'] in PUBLIC_CATEGORIES}
    receipt['restoration_failures'] = restoration_failures
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
        cleanup_fault = cleanup_fault or ('receipt', _category(error))
        fault = fault or cleanup_fault
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
            receipt.update(outcome=outcome, first_failure=first, runner_failure=fault,
                           cleanup_failure=cleanup_fault)
            _write_once(receipt_path, canonical(receipt) + b'\n')
        except BaseException:
            receipt_path = None
    return RunnerResult(mode, outcome, first, custody, owner, released, receipt_path,
                        fault, cleanup_fault, restoration_failures)


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
