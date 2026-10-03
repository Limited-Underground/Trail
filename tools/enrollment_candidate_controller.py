"""Injected OTCAND1 lifecycle controller; import and construction are inert.

No enumeration, serial opening, signer, grant issuer or hardware implementation
is supplied here. A checkpoint receipt is trusted operator input, never proof
that POLL/CONFIRM completed a gesture. Target transitions remain mandatory.
"""
from dataclasses import dataclass
import math
import re
import secrets
import time

from enrollment_candidate_usb_client import ClientError, Reply, validate_startup_snapshot

ROLES = ('A', 'B')
CASES = frozenset(('first', 'retained_rekey', 'recovery_after_A_commit',
                  'recovery_after_B_commit', 'cancel', 'revoke', 'reset_preparation', 'startup_A'))


class ControllerError(RuntimeError):
    """Fixed non-identifying category."""


def need(value, category):
    if not value:
        raise ControllerError(category)


def sampled_duration_ns(seconds):
    """Bound existing clock deltas without affecting an owned rejection."""
    if type(seconds) not in (int, float) or math.isnan(seconds):
        return None
    seconds = max(0, seconds)
    return 0x7fffffffffffffff if seconds >= 0x7fffffffffffffff / 1e9 else int(seconds * 1e9)


class Clock:
    def __init__(self, monotonic):
        self.clock, self.last = monotonic, None

    def now(self):
        now = self.clock()
        need(type(now) in (int, float) and math.isfinite(now)
             and (self.last is None or now >= self.last), 'host_clock_invalid')
        self.last = now
        return now

    def check(self, deadline):
        need(type(deadline) in (int, float) and math.isfinite(deadline)
             and self.now() < deadline, 'deadline_expired')

    def ceiling(self, deadline, seconds):
        self.check(deadline)
        return min(deadline, self.now() + seconds)


@dataclass(frozen=True)
class TrialResult:
    case: str
    outcome: str
    stage: str
    first_failure: tuple | None
    status_transfers: int
    generations: int
    handles_closed: bool
    checkpoints: int
    refusals: tuple = ()


@dataclass(frozen=True)
class StartupResult:
    role: str
    generation: int
    first_failure: tuple | None
    handles_closed: bool


@dataclass(frozen=True)
class CheckpointAck:
    schema: str
    kind: str
    roles: tuple
    token: str


class Controller:
    def __init__(self, endpoint_factory, checkpoint, restart, *,
                 monotonic=time.monotonic, token_factory=lambda: secrets.token_hex(16),
                 record, notify=lambda event: None):
        self.factory, self.human, self.restart = endpoint_factory, checkpoint, restart
        self.clock, self.tokens = Clock(monotonic), token_factory
        self.record, self.notify = record, notify
        self.endpoints, self.all_endpoints, self.retired = {}, [], set()
        self.used, self.running = False, False
        self.preparation_started = False
        self.stage, self.first_failure = 'not_started', None
        self.transfers, self.generations, self.checkpoints = 0, 0, 0
        self.refusals, self.seen_tokens = [], set()
        self._checkpoint_phase = None
        self._close_attempted, self._close_uncertain = set(), False
        self.startup_observation = None

    def _event(self, event, deadline):
        self.clock.check(deadline)
        need(self.record(dict(event)) is True, 'record_failed')
        self.clock.check(deadline)
        self.notify(dict(event))
        self.clock.check(deadline)

    def _send(self, role, command, deadline):
        self.stage = command.split(' ', 1)[0].lower() + '_' + role
        if self.startup_observation is not None:
            self.startup_observation['controller_boundary'] = 'precheck'
        self.clock.check(deadline)
        if self.startup_observation is not None:
            self.startup_observation['controller_boundary'] = 'endpoint'
        result = self.endpoints[role].exchange(command, deadline)
        if self.startup_observation is not None:
            self.startup_observation['controller_boundary'] = 'postcheck'
        self.clock.check(deadline)
        if self.startup_observation is not None:
            self.startup_observation['controller_boundary'] = 'returned'
        return result

    def _value(self, role, command, deadline):
        return self._send(role, command, deadline).values[0]

    def _open(self, deadline, *, roles=ROLES, hello=True):
        need(self.assert_idle(), 'handles_not_closed')
        self.generations += 1
        self.endpoints, self.retired = {}, set()
        for role in roles:
            startup = self.clock.ceiling(deadline, 60) if hello else deadline
            self.stage = 'open_' + role
            self.clock.check(deadline)
            endpoint = self.factory(role, self.generations, startup)
            need(endpoint is not None and endpoint not in self.all_endpoints
                 and callable(endpoint.exchange) and callable(endpoint.close), 'endpoint_invalid')
            self.endpoints[role] = endpoint
            self.all_endpoints.append(endpoint)
            self.clock.check(deadline)
            if hello:
                self.clock.check(startup)
                # An operation lease is a fresh admission. A prior closed boot
                # probe supplies no authorization and its replay has ended.
                reply = self._send(role, 'HELLO', self.clock.ceiling(startup, 5))
                need(type(reply) is Reply and reply.kind == 'READY' and reply.values == ('1',),
                     'reply_invalid')
                reply = self._send(role, 'BOOTSTATUS', self.clock.ceiling(startup, 5))
                need(type(reply) is Reply and reply.kind == 'BOOTSTATUS' and reply.values == ('0',),
                     'reply_invalid')

    def _startup(self, deadline, *, role='A', generation=1):
        """One diagnostic lease; never starts enrollment or sends wire cleanup."""
        startup = self.clock.ceiling(deadline, 60)
        sampled_start = self.clock.last
        self.startup_observation = dict(shared_allowance_ns=sampled_duration_ns(startup - sampled_start),
            open_sampled_elapsed_ns=0, open_returned=False, hello_allowance_ns=None,
            controller_boundary='not_entered')
        try:
            self._open(startup, roles=(role,), hello=False)
            self.startup_observation['open_returned'] = True
        finally:
            # Existing authority samples only. This is a sampled lower bound
            # if opening raises before its final check, not a physical timer.
            self.startup_observation['open_sampled_elapsed_ns'] = sampled_duration_ns(
                self.clock.last - sampled_start)
        self.stage = 'boot_observation_' + role
        self.clock.check(startup)
        observer = getattr(self.endpoints[role], 'observe_startup', None)
        need(callable(observer), 'endpoint_invalid')
        observation = validate_startup_snapshot(observer(startup))
        need(observation['milestone'] in ('loop', 'stopped'), 'reply_invalid')
        self.clock.check(startup)
        hello_deadline = self.clock.ceiling(startup, 5)
        self.startup_observation['hello_allowance_ns'] = sampled_duration_ns(
            hello_deadline - self.clock.last)
        try:
            reply = self._send(role, 'HELLO', hello_deadline)
        except ClientError as error:
            # Only the exact owned refusal leaves inspection reachable on the
            # existing terminal Endpoint. Timeout and other faults are close-only.
            if type(error) is not ClientError or error.args != ('target_refused',):
                raise
            self.first_failure = ('hello_' + role, 'target_refused')
            self.refusals.append((role, 'HELLO'))
            hello_value = 'refused'
        else:
            need(type(reply) is Reply and reply.kind == 'READY' and reply.values == ('1',),
                 'reply_invalid')
            hello_value = 'ready'
        self._event({'schema': 'OT-CANDIDATE-STARTUP-1', 'phase': 'hello',
                     'role': role, 'generation': generation, 'value': hello_value}, hello_deadline)
        self.stage = 'bootstatus_' + role
        try:
            query_deadline = self.clock.ceiling(startup, 5)
            reply = self._send(role, 'BOOTSTATUS', query_deadline)
            need(type(reply) is Reply and reply.kind == 'BOOTSTATUS'
                 and reply.values in tuple((str(stage),) for stage in range(10)), 'reply_invalid')
            stage = int(reply.values[0])
        except BaseException as error:
            if self.first_failure is None:
                category = (str(error) if type(error) in (ClientError, ControllerError)
                            else 'controller_operation_failed')
                self.first_failure = (self.stage, category)
            # Observation only, under the ORIGINAL total cap. No further wire
            # operation follows a failed query, even if this record also fails.
            self._event({'schema': 'OT-CANDIDATE-STARTUP-1', 'phase': 'bootstatus_failed',
                         'role': role, 'generation': generation, 'value': 'failed'}, startup)
            raise
        # Stage zero is also exposed after firmware containment; it cannot undo
        # a refused HELLO. READY with a nonzero stopped stage is contradictory.
        if hello_value == 'ready' and stage != 0:
            self.first_failure = ('bootstatus_' + role, 'reply_invalid')
        self._event({'schema': 'OT-CANDIDATE-STARTUP-1', 'phase': 'bootstatus',
                     'role': role, 'generation': generation, 'value': stage}, query_deadline)

    def _close_sessions(self, deadline):
        for role in ROLES:
            if role in self.endpoints and role not in self.retired:
                self._send(role, 'CLOSE', deadline)
                self.retired.add(role)

    def close(self):
        okay = True
        for endpoint in self.all_endpoints:
            try:
                if not endpoint.closed:
                    if id(endpoint) in self._close_attempted:
                        okay = False
                        continue
                    self._close_attempted.add(id(endpoint))
                    if endpoint.close() is not True:
                        self._close_uncertain = True
                        okay = False
                okay = (endpoint.closed is True and endpoint.handle.is_open is False) and okay
            except BaseException:
                self._close_uncertain = True
                okay = False
        return okay and not self._close_uncertain

    def assert_idle(self):
        try:
            return not self._close_uncertain and all(
                e.closed is True and e.handle.is_open is False for e in self.all_endpoints)
        except BaseException:
            return False

    def _restart(self, deadline):
        self._close_sessions(deadline)
        need(self.close() is True and self.assert_idle(), 'handles_not_closed')
        self.stage = 'restart'
        self.clock.check(deadline)
        need(self.restart(deadline) is True, 'restart_failed')
        self.clock.check(deadline)
        self._open(deadline)

    def _human(self, kind, roles, command, deadline):
        need(self._checkpoint_phase is None, 'checkpoint_reentry')
        self.stage = kind + '_' + ''.join(roles)
        self.clock.check(deadline)
        token = self.tokens()
        need(type(token) is str and re.fullmatch('[0-9a-f]{32}', token)
             and token not in self.seen_tokens, 'checkpoint_token_invalid')
        self.seen_tokens.add(token)
        point = {'schema': 'OT-CANDIDATE-CHECKPOINT-1', 'kind': kind,
                 'roles': tuple(roles), 'token': token, 'deadline': deadline}
        self._event(point, deadline)
        sampling, active = False, True
        generation, running = self.generations, self.running
        owned = {role: self.endpoints[role] for role in roles if role in self.endpoints}
        phase = object()
        self._checkpoint_phase = phase

        def sample():
            nonlocal sampling
            # A retained callable never gains authority over the next phase or
            # replacement leases. Check before clock, stage or wire side effects.
            need(active and self._checkpoint_phase is phase
                 and self.generations == generation and self.running is running
                 and all(self.endpoints.get(role) is endpoint for role, endpoint in owned.items())
                 and (kind != 'usual_screen' or self.assert_idle()), 'checkpoint_inactive')
            need(not sampling, 'checkpoint_reentry')
            sampling = True
            try:
                self.clock.check(deadline)
                return (() if command is None else
                        tuple(self._send(role, command, deadline) for role in roles))
            finally:
                sampling = False

        try:
            reply = self.human(dict(point), sample)
        finally:
            active = False
            self._checkpoint_phase = None
        self.clock.check(deadline)
        need(type(reply) is dict and set(reply) == {'kind', 'roles', 'token', 'confirmed'}
             and reply['kind'] == kind and tuple(reply['roles']) == tuple(roles)
             and reply['token'] == token and reply['confirmed'] is True,
             'human_confirmation_missing')
        self.checkpoints += 1
        self._event({'schema': 'OT-CANDIDATE-ACK-1', 'kind': kind,
                     'roles': tuple(roles), 'token': token}, deadline)
        return CheckpointAck('OT-CANDIDATE-ACK-1', kind, tuple(roles), token)

    def confirm_original(self, deadline):
        """Separate owner observation, only after all passive leases closed.

        The custody owner calls this only after its independent six-span sweep,
        original restart and ROM-handle closure. There is no USB sampling here.
        """
        need((self.used or self.preparation_started) and not self.running and self.assert_idle(), 'handles_not_closed')
        return self._human('usual_screen', ROLES, None, deadline)

    def _begin(self, mode, group, deadline):
        preparation = self.clock.ceiling(deadline, 120)
        recovery = self.clock.ceiling(preparation, 60) if mode == 2 else None
        for role, number in (('A', 1), ('B', 2)):
            self._send(role, f'BEGIN {mode} {number} {group}', preparation)
        return preparation, recovery

    def _offers(self, mode, preparation, recovery=None):
        if mode == 0:
            a = self._value('A', 'EXPORT', preparation)
            b = self._value('B', 'EXPORT', preparation)
            self._send('A', 'PEER ' + b, preparation)
            self._send('B', 'PEER ' + a, preparation)
            for role in ROLES:
                self._send(role, 'SHOWLOCAL', preparation)
            self._human('fingerprint_local', ROLES, None, preparation)
            offers = []
            for role in ROLES:
                self._send(role, 'SHOWPEER', preparation)
                self._human('fingerprint', (role,), 'POLL', preparation)
                offers.append(self._value(role, 'FINISH', preparation))
            return tuple(offers)
        if mode == 2:
            a = self._value('A', 'ARCHIVE', recovery)
            b = self._value('B', 'ARCHIVE', recovery)
            need(a != 'NONE' or b != 'NONE', 'recovery_archive_missing')
            if a == 'NONE':
                self._send('A', 'ACCEPTARCHIVE ' + b, recovery)
            if b == 'NONE':
                self._send('B', 'ACCEPTARCHIVE ' + a, recovery)
            a = self._value('A', 'RECOVERBEGIN', recovery)
            b = self._value('B', 'RECOVERBEGIN', recovery)
            ar = self._value('A', 'RECOVERSIGN ' + b, recovery)
            br = self._value('B', 'RECOVERSIGN ' + a, recovery)
            self._send('A', 'RECOVERFINISH ' + br, recovery)
            self._send('B', 'RECOVERFINISH ' + ar, recovery)
        retained = self.clock.ceiling(preparation, 60)
        a = self._value('A', 'RETAINBEGIN', retained)
        b = self._value('B', 'RETAINBEGIN', retained)
        ar = self._value('A', 'RETAINSIGN ' + b, retained)
        br = self._value('B', 'RETAINSIGN ' + a, retained)
        return (self._value('A', 'RETAINFINISH ' + br, retained),
                self._value('B', 'RETAINFINISH ' + ar, retained))

    def _controls(self, mode, group, deadline):
        preparation, recovery = self._begin(mode, group, deadline)
        a, b = self._offers(mode, preparation, recovery)
        ar = self._value('A', 'POSSESS ' + b, preparation)
        br = self._value('B', 'POSSESS ' + a, preparation)
        self._send('A', 'ACCEPTPOSSESS ' + br, preparation)
        self._send('B', 'ACCEPTPOSSESS ' + ar, preparation)
        activation = self.clock.ceiling(preparation, 60)
        a = self._value('A', 'MARK', activation)
        b = self._value('B', 'MARK', activation)
        self._send('A', 'PEERMARK ' + b, activation)
        self._send('B', 'PEERMARK ' + a, activation)
        invite = self._value('A', 'INVITE', activation)
        ar = self._value('A', 'SIGN ' + invite, activation)
        br = self._value('B', 'SIGN ' + invite, activation)
        self._send('A', 'BIND ' + br, activation)
        self._send('B', 'BIND ' + ar, activation)
        for source, dest in (('A', 'B'), ('B', 'A'), ('A', 'B')):
            self._send(dest, 'FRAME ' + self._value(source, 'NEXTFRAME', activation), activation)
        for role in ROLES:
            self._send(role, 'CONFIRM', activation)
        self._human('transcript', ROLES, 'CONFIRM', activation)
        for source, dest in (('A', 'B'), ('B', 'A'), ('A', 'B'), ('B', 'A')):
            self._send(dest, 'CONTROL ' + self._value(source, 'NEXTCONTROL', activation), activation)
        return activation

    def _activate(self, activation):
        for verb in ('COMMIT', 'READY'):
            for role in ROLES:
                self._send(role, verb, activation)

    def _statuses(self, deadline):
        old = None
        for value in range(1, 5):
            for source, dest in (('A', 'B'), ('B', 'A')):
                frame = self._value(source, f'SENDSTATUS {value}', deadline)
                reply = self._send(dest, 'STATUS ' + frame, deadline)
                need(reply.values == (str(value),), 'status_mismatch')
                self.transfers += 1
                if source == 'A':
                    old = frame
                self._event({'stage': 'authenticated_status', 'source': source,
                             'destination': dest, 'value': value}, deadline)
        return old

    def _expect_refusal(self, role, command, deadline):
        try:
            self._send(role, command, deadline)
        except ClientError as error:
            need(str(error) == 'target_refused', 'expected_refusal_missing')
            self.retired.add(role)
            self.refusals.append((role, command.split(' ', 1)[0]))
            self._event({'stage': 'expected_refusal', 'role': role,
                         'command': command.split(' ', 1)[0]}, deadline)
            return
        raise ControllerError('expected_refusal_missing')

    def run(self, case, group, deadline):
        need(not self.used and not self.running, 'controller_used')
        self.used, self.running = True, True
        try:
            need(case in CASES and type(group) is int and 0 < group < 1 << 64, 'case_invalid')
            self.clock.check(deadline)
            if case == 'startup_A':
                self._startup(deadline)
            else:
                self._open(deadline)
            if case == 'cancel':
                preparation, _ = self._begin(0, group, deadline)
                self._send('A', 'CANCEL', preparation)
                self.retired.add('A')
            elif case != 'startup_A':
                activation = self._controls(0, group, deadline)
                if case.startswith('recovery_after_'):
                    role = 'A' if case == 'recovery_after_A_commit' else 'B'
                    self._send(role, 'COMMIT', activation)
                    self._restart(deadline)
                    activation = self._controls(2, group, deadline)
                    self._activate(activation)
                    self._statuses(activation)
                else:
                    self._activate(activation)
                    old = self._statuses(activation)
                    if case == 'retained_rekey':
                        self._restart(deadline)
                        activation = self._controls(1, group, deadline)
                        self._activate(activation)
                        self._statuses(activation)
                        self._expect_refusal('B', 'STATUS ' + old, activation)
                    elif case == 'revoke':
                        self._send('A', 'REVOKE', activation)
                        self._expect_refusal('A', 'SENDSTATUS 1', activation)
                    elif case == 'reset_preparation':
                        self._human('reset_gesture', ('A',), 'RESETSTATUS', activation)
                        reply = self._send('A', 'RESETSTATUS', activation)
                        need(reply.values == ('3', '1'), 'reset_intent_unverified')
                        self.retired.add('A')
            if case != 'startup_A':
                self._close_sessions(deadline)
        except BaseException as error:
            category = str(error) if isinstance(error, (ClientError, ControllerError)) else 'controller_operation_failed'
            if self.first_failure is None:
                self.first_failure = (self.stage, category)
        finally:
            closed = self.close() and self.assert_idle()
            if not closed and self.first_failure is None:
                self.first_failure = ('cleanup', 'handles_not_closed')
            self.running = False
        return TrialResult(case, 'passed' if self.first_failure is None else 'failed',
                           self.stage, self.first_failure, self.transfers,
                           self.generations, closed, self.checkpoints, tuple(self.refusals))
