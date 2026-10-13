"""Inert passive/operator seams for the approved isolated OTCAND1 trial.

Construction does not import serial, enumerate/open USB, or issue authority.
Explicit activation requires a separately reviewed request, owner grant and
pinned Runtime/HardwareLease. Production serial_api must admit its isolated
capsule interpreter/path/module origins; a manifest check in an ambient Python
process is insufficient. The custody/controller cores own writes, restoration,
restart authorization and durable typed ACK receipts. This is an adapter seam,
not a ready physical CLI, a signer, or a grant issuer.

PrivateView is a trusted local transient view: show(prompt), nonblocking poll()
returning None or an explicit CheckpointAck, and close() -> True after clearing
its display. It must not log/capture prompt references or manufacture ACKs from
sample replies. A separately reviewed view and runtime entrypoint remain gates.
"""
from dataclasses import dataclass, field, replace
import math
import re
import time

from enrollment_candidate_controller import CheckpointAck, Clock, Controller, ControllerError, StartupResult, ROLES, sampled_duration_ns
from enrollment_candidate_custody import Budget, validate_grant, validate_request
from enrollment_candidate_usb_client import (Endpoint, Reply, ClientError, ReviewState,
    REVIEW_REFUSALS, validate_startup_snapshot, validate_query_snapshot)


PROGRESS_PHASES = frozenset(('runtime_verify', 'route', 'serial_api',
    'serial_config', 'lease_attach', 'serial_open', 'identity_guard', 'boot_observation', 'hello', 'bootstatus', 'begin'))

QUERY_BOUNDARIES = frozenset(('owner_check', 'progress', 'endpoint', 'postcheck', 'returned'))


class _QueryObservation:
    """Observation-only clock: never drives permission, expiry or acceptance."""
    def __init__(self, clock, phase, role, generation):
        self.clock, self.started, self.last = clock, None, None
        self.row = dict(phase=phase, role=role, generation=generation,
            boundary='owner_check', entry_allowance_ns=None, prelude_elapsed_ns=None,
            elapsed_ns=None, timing_valid=True, transport=None)
        self.started = self.sample()

    def sample(self):
        try:
            value = self.clock()
            if (type(value) is not int or not 0 <= value <= 0x7fffffffffffffff
                    or (self.last is not None and value < self.last)):
                raise ValueError()
            self.last = value
            return value
        except BaseException:
            self.row['timing_valid'] = False
            return None

    def elapsed(self):
        now = self.sample()
        return now - self.started if self.row['timing_valid'] and self.started is not None else None

    def allowance(self, deadline, sampled):
        if (type(deadline) in (int, float) and type(sampled) in (int, float)
                and math.isfinite(deadline) and math.isfinite(sampled)):
            self.row['entry_allowance_ns'] = sampled_duration_ns(deadline - sampled)

    def finish(self):
        self.row['elapsed_ns'] = self.elapsed()
        if not self.row['timing_valid']:
            self.row['prelude_elapsed_ns'] = self.row['elapsed_ns'] = None
        return self.row


class OperatorError(ControllerError):
    """Fixed categories only; no device, fingerprint or transport detail."""


def need(value, category):
    if not value:
        raise OperatorError(category)


def _fixed(function, category):
    try:
        return function()
    except BaseException:
        raise OperatorError(category) from None


@dataclass(frozen=True)
class PrivatePrompt:
    kind: str
    roles: tuple
    token: str
    deadline: float
    instructions: tuple
    group: int
    # Only the trusted transient view receives these; repr is safe to inspect.
    references: tuple = field(repr=False)
    device_only: bool = False


class CapturedReferences:
    """Transient device EXPORT identity, subsequently checked on its MY ID page.

    Candidate encoding is LE uint32 version/profile followed by identity32.
    The target renders the identity itself as four uppercase 16-digit rows,
    not a hash. Capturing bytes is not proof of what a physical screen showed.
    Only explicit local-screen confirmation makes a reference available for
    subsequent peer comparison. Clear retires the owner-held references.
    Python cannot guarantee erasure of strings previously copied by a view.
    """
    def __init__(self):
        self.generation, self._keys, self.checked, self.peer_checked = 0, {}, False, set()

    def begin(self, generation):
        need(type(generation) is int and generation > self.generation, 'reference_generation')
        self.clear()
        self.generation = generation

    def capture(self, role, generation, reply):
        need(role in ROLES and generation == self.generation and not self.checked,
             'reference_generation')
        need(type(reply) is Reply and reply.kind == 'CANDIDATE' and len(reply.values) == 1,
             'reference_invalid')
        value = reply.values[0]
        need(type(value) is str and re.fullmatch('[0-9a-f]{80}', value), 'reference_invalid')
        raw = bytes.fromhex(value)
        need(raw[:8] == b'\x01\0\0\0\x01\0\0\0' and any(raw[8:]), 'reference_invalid')
        need(role not in self._keys, 'reference_duplicate')
        self._keys[role] = bytearray(raw[8:])

    def rows(self, role):
        need(role in self._keys, 'reference_missing')
        text = self._keys[role].hex().upper()
        return tuple(text[i:i + 16] for i in range(0, 64, 16))

    def local(self):
        need(set(self._keys) == set(ROLES) and self._keys['A'] != self._keys['B'],
             'reference_missing')
        return tuple((role, self.rows(role)) for role in ROLES)

    def peer(self, role):
        need(self.checked and role in ROLES and role not in self.peer_checked,
             'reference_unchecked')
        other = 'B' if role == 'A' else 'A'
        return ((other, self.rows(other)),)

    def confirmed(self, kind, roles):
        if kind == 'fingerprint_local':
            need(tuple(roles) == ROLES, 'checkpoint_invalid')
            self.local()
            self.checked = True
        elif kind == 'fingerprint':
            need(len(roles) == 1, 'checkpoint_invalid')
            self.peer(roles[0])
            self.peer_checked.add(roles[0])
            if self.peer_checked == set(ROLES):
                self.clear()

    def clear(self):
        for value in self._keys.values():
            value[:] = b'\0' * len(value)
        self._keys.clear()
        self.checked, self.peer_checked = False, set()


class CheckpointUI:
    """Bounded nonblocking private view; actual target sampling never implies ACK.

    Controller durably records the checkpoint before this callback, and its ACK
    after this callback but before returning CheckpointAck to custody. This
    adapter accepts an exact typed ACK only from explicit human view input and
    returns the controller's frozen dictionary contract. It never persists rows.
    """
    def __init__(self, references, view, *, group=None, role_labels=None, monotonic=time.monotonic, wait=time.sleep):
        self.references, self.view, self.clock, self.wait = references, view, Clock(monotonic), wait
        self.active, self.failed, self.group = False, False, group
        self.clear_verified = True
        # Labels are presentation only and must come from the caller's bound
        # role context. Generic devices retain A/B; never infer bench identity.
        labels = dict(zip(ROLES, ROLES)) if role_labels is None else role_labels
        need(type(labels) is dict and set(labels) == set(ROLES)
             and all(type(label) is str and 1 <= len(label) <= 24
                     and label == label.strip()
                     and all(32 <= ord(char) <= 126 for char in label)
                     for label in labels.values())
             and labels['A'] != labels['B'], 'checkpoint_invalid')
        self.role_labels = dict(labels)

    def _device(self, role):
        label = self.role_labels[role]
        return role if label == role else role + ' (' + label + ')'

    def __call__(self, point, sample):
        if type(point) is dict and point.get('schema') == 'OT-CANDIDATE-DEVICE-CHECKPOINT-1':
            return self._device_checkpoint(point, sample)
        if self.active:
            self.failed = True
            self.references.clear()
            raise OperatorError('checkpoint_reentry')
        need(not self.failed, 'checkpoint_reentry')
        self.active = True
        shown, cleared, clear_attempted = False, False, False
        try:
            need(type(point) is dict and set(point) == {'schema', 'kind', 'roles', 'token', 'deadline'}
                 and point['schema'] == 'OT-CANDIDATE-CHECKPOINT-1', 'checkpoint_invalid')
            kind, roles, token, deadline = (point[k] for k in ('kind', 'roles', 'token', 'deadline'))
            need(type(roles) is tuple and roles and len(set(roles)) == len(roles)
                 and all(role in ROLES for role in roles) and type(token) is str
                 and re.fullmatch('[0-9a-f]{32}', token), 'checkpoint_invalid')
            self.clock.check(deadline)
            rows = ()
            if kind == 'fingerprint_local':
                need(roles == ROLES, 'checkpoint_invalid')
                rows = self.references.local()
                instructions = ('Compare all four MY ID rows on ' + self._device('A') + ' and ' + self._device('B') + ' with their saved references below.',
                                'Check OT-ID1, INVITER on A, MEMBER on B and the selected group.',
                                'Do not press Button 1 at this checkpoint.',
                                'If both MY ID pages match, click the computer confirmation button. Otherwise cancel.')
            elif kind == 'fingerprint':
                need(len(roles) == 1, 'checkpoint_invalid')
                rows = self.references.peer(roles[0])
                instructions = ('On device ' + self._device(roles[0]) + ', compare all four PEER ID rows with ' + self._device('B' if roles[0] == 'A' else 'A') + ' saved reference below.',
                                'Check OT-ID1, group and local role. Start with Button 1 not held down.',
                                'If all match: hold Button 1 (top button) about 1 second, then let go. Never hold 10 seconds (reset). Otherwise cancel.',
                                'Only AFTER the physical Button 1 press, click the computer confirmation button.')
            elif kind == 'transcript':
                need(roles == ROLES, 'checkpoint_invalid')
                instructions = ('Compare the COMPLETE code on ' + self._device('A') + ' and ' + self._device('B') + ' COMPARE CODE pages; check OT-CODE1 and group.',
                                'Start with Button 1 not held down on either device.',
                                'If all match: on EACH device hold Button 1 (top button) about 1 second, then let go. Never hold 10 seconds (reset). Otherwise cancel.',
                                'Only AFTER both physical Button 1 presses, click the computer confirmation button.')
            elif kind == 'reset_gesture':
                need(roles == ('A',), 'checkpoint_invalid')
                instructions = ('On ' + self._device('A') + ', hold Button 1 (top button) for 10 seconds to request the reset prompt.',
                                'Release, then press and release within the confirmation window.',
                                'Acknowledge only the physical gesture; later RESETSTATUS must independently verify intent.')
            elif kind == 'usual_screen':
                need(roles == ROLES, 'checkpoint_invalid')
                instructions = ('Inspect the usual original screen on BOTH devices: ' + self._device('A') + ' and ' + self._device('B') + ', after restoration.',
                                'If both usual screens are back, click the computer confirmation button. Otherwise cancel.')
            else:
                raise OperatorError('checkpoint_invalid')
            need(type(self.group) is int and 0 < self.group < 1 << 64, 'checkpoint_group_invalid')
            prompt = PrivatePrompt(kind, roles, token, deadline, instructions, self.group, rows)
            shown = True
            self.clear_verified = False
            need(_fixed(lambda: self.view.show(prompt), 'private_view_failed') is True, 'private_view_failed')
            need(not self.failed, 'checkpoint_reentry')
            while True:
                self.clock.check(deadline)
                # sample returns accepted POLL/CONFIRM/RESETSTATUS observations,
                # never permission to produce a human confirmation.
                _fixed(sample, 'checkpoint_sample_failed')
                self.clock.check(deadline)
                need(not self.failed, 'checkpoint_reentry')
                ack = _fixed(self.view.poll, 'private_view_failed')
                self.clock.check(deadline)
                need(not self.failed, 'checkpoint_reentry')
                if ack is not None:
                    need(type(ack) is CheckpointAck and ack.schema == 'OT-CANDIDATE-ACK-1'
                         and ack.kind == kind and ack.roles == roles and ack.token == token,
                         'human_confirmation_missing')
                    clear_attempted = True
                    need(_fixed(self.view.close, 'private_view_failed') is True, 'private_view_not_cleared')
                    cleared = self.clear_verified = True
                    self.clock.check(deadline)
                    need(not self.failed, 'checkpoint_reentry')
                    self.references.confirmed(kind, roles)
                    return {'kind': kind, 'roles': roles, 'token': token, 'confirmed': True}
                remaining = deadline - self.clock.now()
                need(remaining > 0, 'deadline_expired')
                _fixed(lambda: self.wait(min(.02, remaining)), 'private_view_failed')
        except BaseException:
            self.failed = True
            self.references.clear()
            raise
        finally:
            if shown and not clear_attempted:
                try:
                    if self.view.close() is not True:
                        self.failed = True
                    else:
                        self.clear_verified = True
                except BaseException:
                    self.failed = True
            self.active = False

    def _device_checkpoint(self, point, sample):
        need(not self.active and not self.failed, 'checkpoint_reentry')
        self.active = True
        shown, cleared, clear_attempted = False, False, False
        try:
            need(set(point) == {'schema', 'kind', 'roles', 'token', 'deadline', 'initial'}, 'checkpoint_invalid')
            kind, roles, token, deadline = (point[k] for k in ('kind', 'roles', 'token', 'deadline'))
            purpose = {'fingerprint_local': 'LOCAL', 'fingerprint': 'PEER', 'transcript': 'CODE'}.get(kind)
            need(purpose is not None and roles in (ROLES, ('A',), ('B',))
                 and (kind != 'fingerprint' or len(roles) == 1)
                 and type(self.group) is int and 0 < self.group < 1 << 64, 'checkpoint_invalid')
            scopes = {}
            def board_sample():
                try:
                    return sample()
                except ClientError as error:
                    allowed = {'review_' + reason.lower() for reason in REVIEW_REFUSALS} | {
                        'target_refused', 'review_response_invalid'}
                    if type(error) is ClientError and len(error.args) == 1 and error.args[0] in allowed:
                        raise OperatorError(error.args[0]) from None
                    raise OperatorError('checkpoint_sample_failed') from None
                except ControllerError as error:
                    if type(error) is ControllerError and error.args in (('deadline_expired',),
                            ('host_clock_invalid',), ('review_scope_invalid',), ('checkpoint_inactive',), ('record_failed',)):
                        raise OperatorError(error.args[0]) from None
                    raise OperatorError('checkpoint_sample_failed') from None
            def observed(values):
                need(type(values) is tuple and 0 < len(values) <= len(roles), 'device_confirmation_missing')
                states = {}
                for value in values:
                    need(type(value) is ReviewState and value.token == token and value.purpose == purpose
                         and value.role in ('1', '2'), 'review_scope_invalid')
                    role = 'A' if value.role == '1' else 'B'
                    need(role in roles and role not in states and (role not in scopes or scopes[role] == value.scope),
                         'review_scope_invalid')
                    scopes[role] = value.scope
                    states[role] = value
                return states
            states = observed(point['initial'])
            refs = dict(self.references.local()) if kind == 'fingerprint_local' else (
                dict(self.references.peer(roles[0])) if kind == 'fingerprint' else {})
            def presentation(states):
                role = next((r for r in roles if r not in states or states[r].state != 'CONFIRMED'), roles[-1])
                state = states.get(role)
                gesture = 'WAIT_RELEASE' if state is None else state.gesture
                name = self._device(role)
                if kind == 'fingerprint_local':
                    compare = 'Compare all four MY ID rows on ' + name + ' with its saved reference below.'
                    labels = 'Check OT-ID1, group and INVITER on A / MEMBER on B.'
                    rows = ((role, refs[role]),)
                elif kind == 'fingerprint':
                    other = 'B' if role == 'A' else 'A'
                    compare = 'Compare all four PEER ID rows on ' + name + ' with ' + self._device(other) + ' saved reference below.'
                    labels = 'Check OT-ID1, group and local role.'
                    rows = ((other, refs[other]),)
                else:
                    compare = 'Compare the COMPLETE code and group on BOTH boards; check OT-CODE1.'
                    labels = 'Confirm only if both complete codes match.'
                    rows = ()
                action = {
                    'WAIT_RELEASE': name + ': release Button 1 (top button), then wait.',
                    'READY': name + ': hold Button 1 (top button) about 2 seconds, then let go.',
                    'PRESSING': name + ': keep Button 1 held about 2 seconds total, then let go.',
                    'ACCEPTED': name + ': physical confirmation accepted. Advancing.',
                }.get(gesture)
                need(action is not None, 'review_scope_invalid')
                return PrivatePrompt(kind, (role,), token, deadline,
                    (compare, labels, action, 'Never hold 10 seconds. Cancel refuses this checkpoint.'), self.group, rows, True)
            prompt = presentation(states)
            self.clock.check(deadline)
            self.clear_verified = False
            shown = True
            need(_fixed(lambda: self.view.show(prompt), 'private_view_failed') is True, 'private_view_failed')
            while True:
                self.clock.check(deadline)
                states = observed(board_sample())
                self.clock.check(deadline)
                # Cancel is processed before accepting even a completed target
                # receipt. A computer click can never supply device authority.
                need(_fixed(self.view.poll, 'private_view_failed') is None, 'device_confirmation_source')
                updated = presentation(states)
                if updated != prompt:
                    update = getattr(self.view, 'update', None)
                    if update is not None:
                        need(_fixed(lambda: update(updated), 'private_view_failed') is True, 'private_view_failed')
                    prompt = updated
                if set(states) == set(roles) and all(s.state == 'CONFIRMED' for s in states.values()):
                    clear_attempted = True
                    need(_fixed(self.view.close, 'private_view_failed') is True, 'private_view_not_cleared')
                    cleared = self.clear_verified = True
                    self.clock.check(deadline)
                    self.references.confirmed(kind, roles)
                    return {'kind': kind, 'roles': roles, 'token': token, 'device_confirmed': True}
                remaining = deadline - self.clock.now()
                need(remaining > 0, 'deadline_expired')
                _fixed(lambda: self.wait(min(.02, remaining)), 'private_view_failed')
        except BaseException:
            self.failed = True
            self.references.clear()
            raise
        finally:
            if shown and not cleared and not clear_attempted:
                try:
                    self.clear_verified = self.view.close() is True
                except BaseException:
                    self.clear_verified = False
            self.active = False


class PassiveEndpoint(Endpoint):
    """Actual frozen Endpoint plus private capture and exact lease close proof."""
    def __init__(self, handle, guard, owner, token, role, generation, *, probe=False):
        enabled = probe or owner.request['case'] == 'startup_A' and role == 'A' and generation == 1
        super().__init__(handle, guard, monotonic=owner.monotonic,
                         observation_clock=owner.observation_clock, startup_diagnostics=enabled)
        self.owner, self.token, self.role, self.generation = owner, token, role, generation
        self.probe = probe
        self._close_attempted, self._release_verified = False, False
        self.query_observations, self._observing = [], False
        self._observation_reentered = False
        self.boot_observation = None
        self.diagnostic_failure = None
        self.final_query_observation = None

    def _diagnostic_failed(self, phase):
        if self.diagnostic_failure is None:
            self.diagnostic_failure = phase

    def observe_startup(self, deadline):
        if self._observing:
            self.failed, self._refused = True, False
            self._observation_reentered = True
            raise ClientError('client_busy')
        self._observing = True
        self._observation_reentered = False
        completed, entered, diagnostic_failed = False, False, False
        try:
            self.owner.check(deadline)
            self.owner.report_progress('boot_observation', self.role, self.generation, deadline)
            entered = True
            result = super().observe_startup(deadline)
            self.owner.check(deadline)
            completed = True
            return result
        finally:
            try:
                if entered:
                    try:
                        self.boot_observation = self.startup_snapshot()
                    except BaseException:
                        self.boot_observation = None
                        self._diagnostic_failed('startup_snapshot')
                        diagnostic_failed = True
            finally:
                self._observing = False
            if self._observation_reentered and completed:
                self.failed, self._refused = True, False
                raise ClientError('client_busy')
            if diagnostic_failed and completed:
                self.failed, self._refused = True, False
                raise ClientError('invalid_response') from None

    def exchange(self, command, deadline):
        if command in ('COMMIT', 'READY'):
            # A rejection at the wrapper barrier cannot inherit a prior final
            # command's transport receipt.
            self.final_query_observation = None
        if self._observing:
            self.failed, self._refused = True, False
            self._observation_reentered = True
            raise ClientError('client_busy')
        if command in ('COMMIT', 'READY'):
            return self._final_exchange(command, deadline)
        observation = None
        if command in ('HELLO', 'BOOTSTATUS'):
            # Set the barrier BEFORE invoking the observation-only callback.
            self._observing = True
            self._observation_reentered = False
            observation = _QueryObservation(self.owner.observation_clock,
                command.lower(), self.role, self.generation)
        completed = False
        try:
            reply = self._exchange(command, deadline, observation)
            completed = True
            return reply
        finally:
            if observation is not None:
                diagnostic_failed = False
                try:
                    if observation.row['boundary'] in ('endpoint', 'postcheck', 'returned'):
                        try:
                            observation.row['transport'] = self.query_snapshot()
                        except BaseException:
                            observation.row['transport'] = None
                            self._diagnostic_failed('query_snapshot')
                            diagnostic_failed = True
                    try:
                        row = observation.finish()
                    except BaseException:
                        self._diagnostic_failed('query_finish')
                        diagnostic_failed = True
                        row = dict(phase=command.lower(), role=self.role, generation=self.generation,
                            boundary=observation.row['boundary'], entry_allowance_ns=None,
                            prelude_elapsed_ns=None, elapsed_ns=None, timing_valid=False,
                            transport=observation.row['transport'])
                    if self._observation_reentered and completed:
                        row['boundary'] = 'postcheck'
                    if len(self.query_observations) < 2:
                        self.query_observations.append(row)
                finally:
                    # Diagnostic callbacks cannot leave a live close barrier or
                    # erase an already-rejected command while collecting evidence.
                    self._observing = False
                if self._observation_reentered and completed:
                    raise ClientError('client_busy')
                if diagnostic_failed and completed:
                    self.failed, self._refused = True, False
                    raise ClientError('invalid_response') from None

    def _final_exchange(self, command, deadline):
        # Keep final timing separate from the fail-closed startup diagnostics.
        # No inline record callback, new guard, clock check or serial operation.
        self._observing, self._observation_reentered = True, False
        self.final_query_observation = None
        observation, completed, invalid_authority = None, False, False
        try:
            observation = _QueryObservation(self.owner.observation_clock,
                command.lower(), self.role, self.generation)
            reply = self._exchange(command, deadline, observation)
            completed = True
            return reply
        except BaseException as error:
            invalid_authority = error.args == ('host_clock_invalid',) or (
                observation is not None and observation.row['boundary'] in ('owner_check', 'postcheck')
                and error.args != ('deadline_expired',))
            raise
        finally:
            failure, transport = None, None
            try:
                if observation is not None:
                    if observation.row['boundary'] in ('endpoint', 'postcheck', 'returned'):
                        try:
                            transport = self.query_snapshot()
                        except BaseException:
                            failure = 'query_snapshot'
                    observation.row['transport'] = transport
                    try:
                        row = dict(observation.finish())
                    except BaseException:
                        failure = failure or 'query_finish'
                        row = dict(observation.row, timing_valid=False,
                            prelude_elapsed_ns=None, elapsed_ns=None)
                    row['sampled_remaining_ns'] = None
                    # The owner sample is deliberately distinct from endpoint
                    # allowance/elapsed: these clocks must not be subtracted.
                    sampled = self.owner.clock.last
                    if (not invalid_authority and type(sampled) in (int, float) and math.isfinite(sampled)
                            and type(deadline) in (int, float) and math.isfinite(deadline)):
                        row['sampled_remaining_ns'] = sampled_duration_ns(deadline - sampled)
                    if self._observation_reentered and completed:
                        row['boundary'] = 'postcheck'
                    row['diagnostic_failure'] = ('authority_sample_invalid' if invalid_authority else
                        failure or ('observer_reentry' if self._observation_reentered else
                            'observation_clock_invalid' if not row['timing_valid'] else None))
                    self.final_query_observation = row
            except BaseException:
                # Missing telemetry is explicit in the controller summary. It
                # never overwrites an already-owned command result or rejection.
                self.final_query_observation = None
            finally:
                self._observing = False
            if self._observation_reentered and completed:
                self.failed, self._refused = True, False
                raise ClientError('client_busy')

    def _exchange(self, command, deadline, observation):
        self.owner.check(deadline)
        if observation is not None:
            observation.allowance(deadline, self.owner.clock.last)
            observation.row['boundary'] = 'progress'
        if command == 'HELLO':
            self.owner.report_progress('hello', self.role, self.generation, deadline)
        elif command == 'BOOTSTATUS':
            self.owner.report_progress('bootstatus', self.role, self.generation, deadline)
        elif type(command) is str and command.split(' ', 1)[0] == 'BEGIN':
            self.owner.report_progress('begin', self.role, self.generation, deadline)
        if observation is not None:
            observation.row['prelude_elapsed_ns'] = observation.elapsed()
            observation.row['boundary'] = 'endpoint'
        reply = super().exchange(command, deadline)
        if observation is not None:
            observation.row['boundary'] = 'postcheck'
        self.owner.check(deadline)
        if command == 'EXPORT':
            try:
                self.owner.references.capture(self.role, self.generation, reply)
            except BaseException:
                self.failed = True
                raise
        if observation is not None:
            observation.row['boundary'] = 'returned'
        return reply

    def close(self):
        if self._observing:
            self.failed, self._refused = True, False
            self._observation_reentered = True
            raise ClientError('client_busy')
        if self._close_attempted:
            return self.closed is True and self._release_verified
        self._close_attempted = True
        try:
            closed = super().close() is True and self.handle.is_open is False
            self._release_verified = closed and self.owner.lease.release_passive(self.token, self.handle) is True
        except BaseException:
            self._release_verified = False
        if not self._release_verified:
            self.owner.close_uncertain = True
        self.owner.references.clear()
        return self._release_verified


class PassiveEndpoints:
    """Explicit authorized activation; one shared lease with the ROM backends.

    identity_binding is the reviewed transport opaque_identity(key, identity),
    never an unreviewed mapping. Raw identities and binding key are transient.
    No implicit reset, ROM invocation or retry exists in this owner.
    """
    def __init__(self, runtime, lease, identities, binding_key, identity_binding,
                 references, *, monotonic=time.monotonic, progress=lambda event: True,
                 observation_clock=time.perf_counter_ns):
        self.runtime, self.lease, self.references = runtime, lease, references
        self.identities, self.binding_key, self.identity_binding = dict(identities), binding_key, identity_binding
        self.monotonic, self.clock = monotonic, Clock(monotonic)
        self.observation_clock = observation_clock
        self.progress, self._reporting = progress, False
        self.activated, self.attempted, self.close_uncertain = False, False, False
        self.budget, self.deadline, self.request = None, None, None
        self.generation, self.endpoints, self.opened = 0, [], set()
        self.probed, self.probe_generation, self.opening_probe = set(), 0, False
        self._opening, self.terminal = False, False

    def activate(self, request, grant_raw, grant_sha, deadline, *, utc=time.time):
        need(not self.attempted, 'operator_used')
        self.attempted = True
        request = _fixed(lambda: validate_request(request), 'activation_invalid')
        grant = _fixed(lambda: validate_grant(request, grant_raw, grant_sha, 'execute', utc), 'activation_invalid')
        need(type(self.binding_key) is bytes and len(self.binding_key) == 32
             and set(self.identities) == set(ROLES)
             and self.runtime.manifest_sha256 == request['runtime_sha256'], 'activation_invalid')
        for role in ROLES:
            need(_fixed(lambda: self.identity_binding(self.binding_key, self.identities[role]), 'identity_binding_invalid')
                 == request['roles'][role]['device_binding'], 'identity_binding_invalid')
        budget = _fixed(lambda: Budget(grant, utc, self.monotonic), 'activation_invalid')
        self.clock.check(deadline)
        # Independent custody sampling can produce a slightly different ceiling.
        # Keep the earlier limit; no later activation or call renews it.
        deadline = min(deadline, budget.execute)
        self.request, self.budget, self.deadline = request, budget, deadline
        self.check(deadline)
        manifest = _fixed(lambda: self.runtime.verify(deadline), 'runtime_admission_failed')
        need(type(manifest) is dict, 'runtime_admission_failed')
        need(_fixed(lambda: self.lease.acquire(deadline), 'hardware_lease_failed') is True, 'activation_invalid')
        self.check(deadline)
        self.activated = True
        return True

    def check(self, deadline):
        need(self.budget is not None and not self.close_uncertain and not self.terminal, 'passive_owner_terminal')
        self.clock.check(deadline)
        need(deadline <= self.deadline and self.runtime.manifest_sha256 == self.request['runtime_sha256'],
             'deadline_extended')
        _fixed(self.budget.check, 'activation_expired')
        self.clock.check(self.deadline)

    def report_progress(self, phase, role, generation, deadline):
        """Durable last-attempted boundary only; no command or private values.

        The actual cases use at most two generations. Recording never grants
        authority or changes a deadline. Rejection, callback expiry or reentry
        is terminal before the marked operation; cleanup remains available.
        """
        if self._reporting:
            self.terminal = True
            raise OperatorError('record_failed')
        self.check(deadline)
        need(phase in PROGRESS_PHASES and role in ROLES and type(generation) is int
             and 1 <= generation <= 2, 'passive_generation_invalid')
        need(phase != 'boot_observation' or (role, generation) in self.probed or
             (self.request['case'] == 'startup_A' and role == 'A' and generation == 1),
             'passive_generation_invalid')
        self._reporting = True
        try:
            event = {'schema': 'OT-CANDIDATE-PROGRESS-1', 'phase': phase,
                     'role': role, 'generation': generation}
            need(_fixed(lambda: self.progress(event), 'record_failed') is True, 'record_failed')
            self.check(deadline)
        except BaseException:
            self.terminal = True
            self.references.clear()
            raise
        finally:
            self._reporting = False

    def __call__(self, role, generation, deadline):
        return self._open(role, generation, deadline, probe=False)

    def probe(self, role, generation, deadline):
        return self._open(role, generation, deadline, probe=True)

    def _open(self, role, generation, deadline, *, probe):
        if self._opening:
            self.terminal = True
            raise OperatorError('passive_reentry')
        need(self.activated and role in ROLES and type(generation) is int
             and 1 <= generation <= 2 and generation in (self.generation, self.generation + 1)
             and (role, generation) not in (self.probed if probe else self.opened), 'passive_generation_invalid')
        self.check(deadline)
        if probe:
            need(self.assert_idle(), 'handles_not_closed')
        if generation != self.generation:
            need(self.assert_idle(), 'handles_not_closed')
            if not probe:
                self.references.begin(generation)
                self.generation = generation
        (self.probed if probe else self.opened).add((role, generation))
        self.opening_probe, self.probe_generation = probe, generation
        lease_deadline = self.deadline
        deadline = self.clock.ceiling(deadline, 60)
        handle, token, attached = None, None, False
        self._opening = True
        try:
            self.report_progress('runtime_verify', role, generation, deadline)
            self.runtime.verify(deadline)
            self.report_progress('route', role, generation, deadline)
            route = self.runtime.route(self.identities[role], deadline)
            need(type(route) is str and re.fullmatch(r'COM[1-9][0-9]{0,3}', route), 'passive_route_invalid')
            self.report_progress('serial_api', role, generation, deadline)
            serial_factory, comports = self.runtime.serial_api(deadline)
            self.check(deadline)
            self.report_progress('serial_config', role, generation, deadline)
            handle = serial_factory(port=None, baudrate=115200, timeout=0,
                                    write_timeout=0, exclusive=True)
            if any(endpoint.handle is handle for endpoint in self.endpoints):
                # Do not re-close or reopen a retired SDK object.
                handle = None
                raise OperatorError('passive_handle_reused')
            need(handle.is_open is False, 'passive_constructor_opened')
            handle.dtr = False
            handle.rts = False
            need(handle.is_open is False and handle.dtr is False and handle.rts is False,
                 'passive_control_lines_invalid')
            self.report_progress('lease_attach', role, generation, deadline)
            token = self.lease.begin_passive(self.request['roles'][role]['device_binding'],
                2 * generation - (1 if probe else 0), deadline)
            self.lease.attach_passive(token, handle, route)
            attached = True
            handle.port = route
            self.check(deadline)
            self.report_progress('serial_open', role, generation, deadline)
            handle.open()
            self.check(deadline)
            need(handle.is_open is True and handle.dtr is False and handle.rts is False,
                 'passive_open_failed')
            def guard():
                self.check(lease_deadline)
                ports = list(comports())
                routes = [port for port in ports if port.device == route]
                matches = [port for port in ports if port.vid == 0x303a and port.pid == 0x1001
                           and self.identity_binding(self.binding_key, port.serial_number)
                           == self.request['roles'][role]['device_binding']]
                need(len(routes) == len(matches) == 1 and routes[0] is matches[0],
                     'passive_route_changed')
                self.check(lease_deadline)
                return self.lease.guard_passive(token, handle) is True
            endpoint = PassiveEndpoint(handle, guard, self, token, role, generation, probe=probe)
            self.report_progress('identity_guard', role, generation, deadline)
            need(guard(), 'passive_identity_guard')
            self.endpoints.append(endpoint)
            # The frozen Controller must send fresh HELLO on this new endpoint.
            return endpoint
        except BaseException:
            if handle is not None:
                try:
                    handle.close()
                    closed = handle.is_open is False
                    if attached:
                        closed = closed and self.lease.release_passive(token, handle) is True
                    if not closed:
                        self.close_uncertain = True
                except BaseException:
                    self.close_uncertain = True
            if token is not None and not attached:
                try:
                    if self.lease.abort_passive(token) is not True:
                        self.close_uncertain = True
                except BaseException:
                    self.close_uncertain = True
            self.activated = False
            self.references.clear()
            raise OperatorError('passive_open_failed') from None
        finally:
            self._opening = False

    def close(self):
        outcomes = [endpoint.close() is True for endpoint in self.endpoints]
        okay = all(outcomes)
        self.references.clear()
        return okay and self.assert_idle()

    def assert_idle(self):
        try:
            return not self.close_uncertain and all(
                endpoint.closed is True and endpoint.handle.is_open is False
                and endpoint._release_verified for endpoint in self.endpoints)
        except BaseException:
            self.close_uncertain = True
            return False


class CandidateOperator:
    """One inert composition for custody.execute's frozen Controller interface.

    The record callback must durably accept fixed startup progress and controller category/token events
    and return True before notify. Neither event path receives private rows.
    restart is a separately source-reviewed custody operation; it is called
    only after verified passive release, under the original activation ceiling.
    The runner supplies cleanup_view_factory for one independent restoration
    observation. Without that explicit factory, a failed human UI remains terminal.
    """
    def __init__(self, runtime, lease, identities, binding_key, identity_binding,
                 view, restart, *, record, monotonic=time.monotonic,
                 wait=time.sleep, notify=lambda event: None,
                 observation_clock=time.perf_counter_ns, cleanup_view_factory=None, role_labels=None):
        self.references = CapturedReferences()
        self.passive = PassiveEndpoints(runtime, lease, identities, binding_key,
                                        identity_binding, self.references, monotonic=monotonic,
                                        progress=record, observation_clock=observation_clock)
        self.record = record
        self.human = CheckpointUI(self.references, view, role_labels=role_labels, monotonic=monotonic, wait=wait)
        self._restart = restart
        self.probes, self.probe_controllers = {}, {}
        self._probing = False
        self._cleanup_view_factory = cleanup_view_factory
        self.cleanup_human = None
        self._cleanup_used, self._cleanup_active = False, False
        self._cleanup_deadline = None
        self.controller = Controller(self.passive, self._checkpoint, self.restart,
                                     monotonic=monotonic, record=record, notify=notify)

    def _cleanup_check(self, deadline):
        need(not self.human.active and not self.controller.running and not self._probing
             and self.assert_idle() and self.passive.lease.assert_idle() is True,
             'handles_not_closed')
        self.passive.clock.check(deadline)
        _fixed(lambda: self.passive.budget.check(True), 'activation_expired')

    def _checkpoint(self, point, sample):
        if point.get('kind') != 'usual_screen' or self._cleanup_view_factory is None:
            return self.human(point, sample)
        need(self._cleanup_active and self.cleanup_human is None
             and point['deadline'] == self._cleanup_deadline, 'checkpoint_inactive')
        deadline = self._cleanup_deadline
        self._cleanup_check(deadline)
        # A previously uncertain clear cannot be repaired by retrying here.
        need(self.human.clear_verified, 'private_view_not_cleared')
        self.human.clear_verified = False
        need(_fixed(self.human.view.close, 'private_view_failed') is True,
             'private_view_not_cleared')
        self.human.clear_verified = True
        self._cleanup_check(deadline)
        view = _fixed(self._cleanup_view_factory, 'private_view_failed')
        need(view is not self.human.view, 'private_view_failed')
        self.cleanup_human = CheckpointUI(CapturedReferences(), view,
            group=self.human.group, role_labels=self.human.role_labels,
            monotonic=self.passive.monotonic, wait=self.human.wait)
        started = False
        try:
            self._cleanup_check(deadline)
            started = True
            result = self.cleanup_human(point, sample)
            self._cleanup_check(deadline)
            return result
        finally:
            # CheckpointUI owns show/clear; never retry an uncertain clear.
            if not started:
                self.cleanup_human.clear_verified = False
                try:
                    self.cleanup_human.clear_verified = view.close() is True
                except BaseException:
                    pass
            self._cleanup_active = False

    def activate(self, *args, **kwargs):
        result = self.passive.activate(*args, **kwargs)
        self.human.group = self.passive.request['group']
        self.controller.preparation_started = True
        return result

    def restart(self, deadline):
        self.passive.check(deadline)
        need(self.controller.assert_idle() is True and self.passive.assert_idle()
             and self.passive.lease.assert_idle() is True, 'handles_not_closed')
        self.references.clear()
        result = _fixed(lambda: self._restart(deadline), 'restart_failed')
        self.passive.check(deadline)
        need(all(self.probes.get((role, self.controller.generations + 1)) is True for role in ROLES),
             'readiness_required')
        return result is True

    def startup_probe(self, role, generation, deadline):
        """One closed diagnostic lease immediately after this role's boot.

        Success never admits enrollment: the later operation lease performs
        its own fresh queries. Neither these leases nor summaries renew time.
        """
        self.passive.clock.check(deadline)
        need(self.passive.deadline is not None, 'activation_invalid')
        deadline = min(deadline, self.passive.deadline)
        self.passive.check(deadline)
        case = self.passive.request['case']
        retained = case in ('retained_rekey', 'recovery_after_A_commit', 'recovery_after_B_commit')
        need(case != 'startup_A' and self.controller.first_failure is None
             and ((generation == 1 and not self.controller.used and not self.controller.running)
                  or (generation == 2 and retained and self.controller.running
                      and self.controller.stage == 'restart' and self.controller.generations == 1))
             and (role == 'A' or self.probes.get(('A', generation)) is True)
             and not self._probing and self.assert_idle() and role in ROLES
             and type(generation) is int and generation in (1, 2)
             and (role, generation) not in self.probes
             and generation == self.controller.generations + 1,
             'passive_generation_invalid')
        self._probing = True
        self.controller.preparation_started = True
        self.probes[(role, generation)] = False
        probe = Controller(lambda r, g, cap: self.passive.probe(r, generation, cap),
            self.human, self.restart, record=self.record, monotonic=self.passive.monotonic)
        self.probe_controllers[(role, generation)] = probe
        try:
            try:
                probe._startup(min(deadline, self.passive.deadline), role=role, generation=generation)
            except BaseException as error:
                if probe.first_failure is None:
                    probe.first_failure = (probe.stage, str(error) if type(error) in
                        (ClientError, ControllerError, OperatorError) else 'controller_operation_failed')
            closed = probe.close() and probe.assert_idle() and self.passive.assert_idle()
            if not closed and probe.first_failure is None:
                probe.first_failure = ('cleanup', 'handles_not_closed')
            try:
                self._record_query_summary(role=role, generation=generation, probe=probe)
            except BaseException:
                if probe.first_failure is None:
                    probe.first_failure = ('startup_summary', 'record_failed')
            self.probes[(role, generation)] = probe.first_failure is None and closed
            if probe.first_failure is not None and self.controller.first_failure is None:
                self.controller.first_failure = probe.first_failure
            return StartupResult(role, generation, probe.first_failure, closed)
        finally:
            self._probing = False

    def run(self, case, group, deadline):
        self.passive.clock.check(deadline)
        need(self.passive.deadline is not None, 'activation_invalid')
        deadline = min(deadline, self.passive.deadline)
        self.passive.check(deadline)
        need(self.passive.activated and case == self.passive.request['case']
             and group == self.passive.request['group'], 'activation_invalid')
        need(case == 'startup_A' or all(self.probes.get((role, 1)) is True for role in ROLES),
             'readiness_required')
        result = self.controller.run(case, group, deadline)
        if case == 'startup_A':
            try:
                self._record_query_summary()
            except BaseException:
                # A diagnostic record cannot erase a rejection already
                # returned by the controller, or authorize another command.
                if result.first_failure is None:
                    result = replace(result, outcome='failed', stage='startup_summary',
                                     first_failure=('startup_summary', 'record_failed'))
        elif self.controller.final_observations:
            try:
                self._record_final_summary()
            except BaseException:
                # Best-effort telemetry cannot replace the trial's first
                # failure, or turn an accepted exchange into a logger failure.
                pass
        return result

    def _record_final_summary(self):
        need(self.assert_idle(), 'handles_not_closed')
        self.passive.clock.check(self.passive.deadline)
        _fixed(self.passive.budget.check, 'activation_expired')
        need(_fixed(lambda: self.record(dict(schema='OT-CANDIDATE-FINAL-SUMMARY-1',
            queries=[dict(row) for row in self.controller.final_observations])),
            'record_failed') is True, 'record_failed')
        self.passive.clock.check(self.passive.deadline)
        _fixed(self.passive.budget.check, 'activation_expired')

    def _record_query_summary(self, *, role='A', generation=1, probe=None):
        # The query and startup caps have ended. Only the original execution
        # authority permits this one durable record after passive closure.
        need(self.assert_idle(), 'handles_not_closed')
        self.passive.clock.check(self.passive.deadline)
        _fixed(self.passive.budget.check, 'activation_expired')
        rows = [dict(row, transport=validate_query_snapshot(row['transport']) if row['transport'] is not None else None)
                for endpoint in self.passive.endpoints if endpoint.role == role and endpoint.generation == generation and endpoint.probe is (probe is not None)
                for row in endpoint.query_observations]
        need(len(rows) <= 2, 'record_failed')
        observations = [endpoint.boot_observation for endpoint in self.passive.endpoints
            if endpoint.role == role and endpoint.generation == generation and endpoint.probe is (probe is not None) and endpoint.boot_observation is not None]
        need(len(observations) <= 1, 'record_failed')
        boot = validate_startup_snapshot(observations[0]) if observations else None
        failures = [endpoint.diagnostic_failure for endpoint in self.passive.endpoints
            if endpoint.role == role and endpoint.generation == generation and endpoint.probe is (probe is not None) and endpoint.diagnostic_failure is not None]
        need(len(failures) <= 1, 'record_failed')
        owner = probe if probe is not None else self.controller
        metadata = {} if probe is None else {'role': role, 'generation': generation}
        need(_fixed(lambda: self.record(dict(metadata, schema='OT-CANDIDATE-QUERY-SUMMARY-3' if probe else 'OT-CANDIDATE-QUERY-SUMMARY-2',
             boot_observation=boot, diagnostic_failure=failures[0] if failures else None,
             startup=dict(owner.startup_observation) if owner.startup_observation is not None else None,
             queries=rows)), 'record_failed') is True, 'record_failed')
        self.passive.clock.check(self.passive.deadline)
        _fixed(self.passive.budget.check, 'activation_expired')

    def close(self):
        self.references.clear()
        core = self.controller.close() is True
        passive = self.passive.close() is True
        view = self.cleanup_human is None or (not self.cleanup_human.active
                                             and self.cleanup_human.clear_verified)
        return core and passive and view

    def assert_idle(self):
        return self.controller.assert_idle() is True and self.passive.assert_idle()

    def confirm_original(self, deadline):
        # Restoration uses the grant's original restore ceiling, not execute.
        need(self.passive.budget is not None and self.assert_idle(), 'handles_not_closed')
        self.passive.clock.check(deadline)
        deadline = min(deadline, self.passive.budget.restore)
        _fixed(lambda: self.passive.budget.check(True), 'activation_expired')
        if self._cleanup_view_factory is None:
            return self.controller.confirm_original(deadline)
        need(not self._cleanup_used, 'checkpoint_reentry')
        # Consume this one cleanup observation before any callback can reenter.
        self._cleanup_used = True
        self._cleanup_deadline = deadline
        self._cleanup_check(deadline)
        self._cleanup_active = True
        try:
            return self.controller.confirm_original(deadline)
        finally:
            self._cleanup_active = False
