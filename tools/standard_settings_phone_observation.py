"""Bounded, read-only OT-0304 phone observation, with no import-time I/O.

The caller supplies the unchanged private Observation owner to ``run``. This
module supplies its Lines/emit seams; only that owner creates protocol ACKs and
receipts. Device access requires an explicit, context-bound grant and fresh APK,
ADB and model verification. No CLI, device discovery, installation, pairing,
settings writes, reset, wake or lock-screen bypass is provided. A separately
granted process reopen is permitted only after independently observed Idle.
Captured XML and logs stay in memory; receipts contain predicates and hashes.
Host replay does not establish Android/device acceptance or runtime capabilities.
"""
from dataclasses import dataclass
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import time
import xml.etree.ElementTree as ET

PACKAGE = 'io.github.nbjelanovic.otclient.v1test'
ACTIVITY = PACKAGE + '/io.github.nbjelanovic.otclient.V1TestMainActivity'
OBSERVATION_SECONDS = 600
CLOSE_SECONDS = 300
SCOPE = 'OT0304-saved-pair-settings-read-idle-reopen-and-disconnect-1'
READS = {'name': 'Read device', 'region': 'Read region', 'public': 'Read public settings'}
NOTICES = {'name': 'Device name read back.',
           'region': 'Region read back. Radio TX remains disabled.',
           'public': 'Public settings read back.'}
PENDING = {'name': 'Waiting for device readback…',
           'region': 'Waiting for region readback...',
           'public': 'Waiting for public settings readback...'}
CONTROLS = frozenset((*READS.values(), 'Find device', 'Start Bluetooth device service',
    'Edit saved name or region', 'Disconnect Bluetooth device', 'Cancel connection',
    'Stop reconnecting', 'Back to Device'))
CLOSURE_CONTROLS = ('Disconnect Bluetooth device', 'Cancel connection', 'Stop reconnecting')
REGIONS = frozenset(('US915', 'EU868', 'AU915', 'EU433', 'CN470', 'AS923-1',
                    'AS923-2', 'AS923-3', 'AS923-4', 'KR920', 'IN865', 'RU864'))
STAGES = frozenset(('CONNECTION_ATTEMPT_STARTED', 'RECONNECT_ATTEMPT_STARTED',
    'RETURNING_OWNER_DISCOVERY_STARTED', 'GATT_LINK_ESTABLISHED',
    'PROTECTED_PROTOCOL_INFO_REQUESTED', 'PROTECTED_PROTOCOL_INFO_ACCEPTED',
    'PROTECTED_PROTOCOL_INFO_REJECTED', 'ATT_MTU_NEGOTIATED',
    'STREAM_SUBSCRIPTION_ESTABLISHED', 'AUTHORIZATION_STARTED', 'AUTHORIZATION_PENDING',
    'AUTHORIZATION_ACCEPTED', 'AUTHORIZATION_DENIED', 'AUTHORIZATION_UNAVAILABLE',
    'AUTHORIZATION_UNCERTAIN', 'SNAPSHOT_REQUESTED', 'SNAPSHOT_ACCEPTED',
    'SNAPSHOT_REJECTED', 'READY_REACHED', 'DISCONNECT_OBSERVED', 'TRANSACTION_FAILED'))
HEX32 = re.compile(r'[0-9a-f]{32}\Z')
HEX64 = re.compile(r'[0-9a-f]{64}\Z')


class PhoneError(RuntimeError):
    """Only constant categories, never raw subprocess/UI/exception text."""


def need(condition, code='phone_observation_refused'):
    if not condition:
        raise PhoneError(code)


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()


def regular(path):
    path = Path(path)
    for item in (path, *path.parents):
        need(not item.is_symlink() and not getattr(item, 'is_junction', lambda: False)(), 'unsafe_path')
    return path


def write_once(path, raw):
    path = regular(path)
    with path.open('xb', buffering=0) as stream:
        need(stream.write(raw) == len(raw), 'evidence_write_failed')
        os.fsync(stream.fileno())
    need(path.read_bytes() == raw, 'evidence_write_failed')


def number(value):
    need(type(value) is str and re.fullmatch(r'0|[1-9][0-9]{0,18}', value), 'trace_invalid')
    result = int(value)
    need(result <= 2**63 - 1, 'trace_invalid')
    return result


def finite(value):
    return type(value) in (int, float) and math.isfinite(value)


def remaining(clock, deadline):
    now = clock()
    need(finite(now) and finite(deadline) and now < deadline, 'phone_deadline')
    return deadline - now


def context_valid(context):
    need(type(context) is dict and set(context) == {'schema', 'attempt', 'request_sha256', 'challenge'}
         and context['schema'] == 'OT0304-STANDARD-OBSERVATION-CONTEXT-1'
         and type(context['attempt']) is str and HEX32.fullmatch(context['attempt'])
         and type(context['challenge']) is str and HEX32.fullmatch(context['challenge'])
         and type(context['request_sha256']) is str and HEX64.fullmatch(context['request_sha256']), 'context_invalid')


@dataclass(frozen=True)
class Trace:
    session: int
    rows: tuple

    @classmethod
    def parse(cls, raw):
        need(type(raw) is bytes and 0 < len(raw) <= 65536 and raw.endswith(b'\n'), 'trace_invalid')
        try:
            lines = raw.decode('ascii').splitlines()
        except UnicodeError:
            raise PhoneError('trace_invalid') from None
        need(len(lines) <= 513, 'trace_invalid')
        header = lines[0].split('\t')
        need(len(header) == 3 and header[:2] == ['OTCL', '4'], 'trace_invalid')
        session = number(header[2])
        need(session > 0, 'trace_session_missing')
        previous_session, elapsed, ordinal, current = 0, 0, 0, []
        for line in lines[1:]:
            fields = tuple(line.split('\t'))
            need(len(fields) >= 4, 'trace_invalid')
            sid, stamp = number(fields[0]), number(fields[1])
            need(previous_session <= sid <= session and sid > 0, 'trace_invalid')
            if sid != previous_session:
                elapsed, ordinal = 0, 0
            need(stamp >= elapsed, 'trace_rollback')
            kind = fields[2]
            if kind in ('C', 'L'):
                choices = ('DISCONNECTED', 'CONNECTING', 'AUTHORIZING', 'READY', 'RECONNECTING', 'FAILED') if kind == 'C' else ('FOREGROUND', 'BACKGROUND', 'STOPPED')
                need(len(fields) == 4 and fields[3] in choices, 'trace_invalid')
            elif kind == 'S':
                need(len(fields) == 11 and fields[3] in STAGES, 'trace_invalid')
                transaction, order, generation, since, attempt = map(number, fields[4:9])
                need((order == ordinal + 1 if sid == session else order > ordinal) and (transaction > 0 or since == 0)
                     and attempt <= 2**31 - 1 and re.fullmatch(r'[A-Z_]{1,64}', fields[9])
                     and re.fullmatch(r'[A-Z_]{1,64}', fields[10]), 'trace_gap')
                ordinal = order
            elif kind == 'G':
                need(len(fields) == 18 and number(fields[3]) > 0 and number(fields[4]) > 0, 'trace_invalid')
                need(re.fullmatch(r'[A-Z_]{1,64}', fields[5]) and re.fullmatch(r'[A-Z_]{1,64}', fields[6]), 'trace_invalid')
                for index, bound in ((7, 60000), (8, 60000), (9, 60000), (10, 65535), (12, 512), (14, 3)):
                    need(fields[index] == 'UNAVAILABLE' or number(fields[index]) <= bound, 'trace_invalid')
                need(all(fields[index] in ('0', '1') for index in (11, 13, 15, 17))
                     and fields[16] in ('0', '1', 'UNAVAILABLE'), 'trace_invalid')
            else:
                raise PhoneError('trace_invalid')
            if sid == session:
                current.append(fields)
            previous_session, elapsed = sid, stamp
        need(current, 'trace_session_missing')
        return cls(session, tuple(current))

    def extends(self, previous):
        need(self.session == previous.session and self.rows[:len(previous.rows)] == previous.rows, 'trace_changed')

    def state(self, *, required=True):
        values = [row[3] for row in self.rows if row[2] == 'C']
        need(values or not required, 'trace_state_missing')
        return values[-1] if values else None

    def identity(self):
        stages = [row for row in self.rows if row[2] == 'S']
        need(self.state() == 'READY' and stages and stages[-1][3] == 'READY_REACHED', 'protected_ready_missing')
        final = stages[-1]
        pair = (number(final[4]), number(final[6]))
        need(pair[0] > 0, 'protected_ready_missing')
        transaction = [row for row in stages if (number(row[4]), number(row[6])) == pair]
        names = [row[3] for row in transaction]
        required = ('CONNECTION_ATTEMPT_STARTED', 'GATT_LINK_ESTABLISHED',
                    'PROTECTED_PROTOCOL_INFO_ACCEPTED', 'ATT_MTU_NEGOTIATED',
                    'STREAM_SUBSCRIPTION_ESTABLISHED', 'AUTHORIZATION_ACCEPTED',
                    'SNAPSHOT_ACCEPTED', 'READY_REACHED')
        indices = [names.index(value) if value in names else -1 for value in required]
        need(all(index >= 0 for index in indices) and indices == sorted(indices)
             and not any(name in ('DISCONNECT_OBSERVED', 'TRANSACTION_FAILED', 'RECONNECT_ATTEMPT_STARTED') for name in names), 'protected_ready_missing')
        return (self.session, *pair)


def bounds(raw):
    match = re.fullmatch(r'\[(\d{1,5}),(\d{1,5})\]\[(\d{1,5}),(\d{1,5})\]', raw)
    need(match is not None, 'ui_bounds_invalid')
    left, top, right, bottom = map(int, match.groups())
    need(0 <= left < right <= 10000 and 0 <= top < bottom <= 10000, 'ui_bounds_invalid')
    return left, top, right, bottom


class Ui:
    def __init__(self, raw):
        need(type(raw) is bytes and 0 < len(raw) <= 1048576 and b'<!' not in raw, 'ui_invalid')
        try:
            self.root = ET.fromstring(raw)
        except ET.ParseError:
            raise PhoneError('ui_invalid') from None
        need(self.root.tag == 'hierarchy', 'ui_invalid')
        self.nodes = list(self.root.iter('node'))
        need(len(self.nodes) <= 4096 and any(n.get('package') == PACKAGE for n in self.nodes), 'ui_package_missing')
        need(not any(n.get('package') not in (PACKAGE, 'com.android.systemui', '') for n in self.nodes), 'ui_wrong_package')
        self.parents = {child: parent for parent in self.root.iter() for child in parent}
        self.texts = frozenset(n.get('text', '') for n in self.nodes if n.get('package') == PACKAGE)

    def matches(self, label):
        return [n for n in self.nodes if n.get('package') == PACKAGE and
                (n.get('text') == label or n.get('content-desc') == label)]

    def control(self, label):
        need(label in CONTROLS, 'control_not_scoped')
        matches = self.matches(label)
        need(len(matches) == 1, 'control_ambiguous_or_missing')
        node = matches[0]
        left, top, right, bottom = bounds(node.get('bounds', ''))
        point = ((left + right)//2, (top + bottom)//2)
        clickable = False
        while node is not self.root:
            need(node.get('enabled') == 'true', 'control_disabled')
            if node.get('package') == PACKAGE:
                area = bounds(node.get('bounds', ''))
                need(area[0] <= point[0] < area[2] and area[1] <= point[1] < area[3], 'control_not_visible')
                clickable |= node.get('clickable') == 'true'
            node = self.parents[node]
        need(clickable, 'control_not_clickable')
        return point

    def scroll_area(self):
        nodes = [n for n in self.nodes if n.get('package') == PACKAGE and n.get('scrollable') == 'true']
        need(len(nodes) == 1 and nodes[0].get('enabled') == 'true', 'scroll_ambiguous')
        return bounds(nodes[0].get('bounds', ''))


@dataclass(frozen=True)
class Frame:
    started: float
    ended: float
    ui: bytes
    trace_before: bytes
    trace_after: bytes
    foreground: bool = True


class AdbPhoneBackend:
    """Fixed ADB operations, inert until authorize is called with a fresh grant.

    The grant is an owner-authorized input, never generated by this module.
    Serial stays private. A subprocess is always bounded by the common deadline.
    Cleanup keeps its independent 300s budget after an observation grant expires.
    """
    def __init__(self, adb_path, serial, apk_path, grant, *, runner=subprocess.run,
                 clock=time.monotonic, utc=time.time, sleep=time.sleep):
        self.adb_path, self.apk_path = Path(adb_path), Path(apk_path)
        self.serial, self.grant = serial, grant
        self.runner, self.clock, self.utc, self.sleep = runner, clock, utc, sleep
        self.authorized, self.cleanup, self.closed, self.context = False, False, False, None
        self.reopen_attempted = False
        self.temporary = '/data/local/tmp/ot0304-phone-observation.xml'

    def _run(self, args, deadline):
        need(self.authorized and not self.closed, 'phone_authority_required')
        if not self.cleanup:
            need(self.utc() < self.grant['expires_utc'], 'phone_grant_expired')
        timeout = min(45, remaining(self.clock, deadline))
        try:
            result = self.runner([str(self.adb_path), '-s', self.serial, *args],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout, shell=False)
        except BaseException:
            raise PhoneError('phone_command_failed') from None
        remaining(self.clock, deadline)
        need(result.returncode == 0 and type(result.stdout) is bytes, 'phone_command_failed')
        return result.stdout

    def authorize(self, context, lane, deadline):
        context_valid(context)
        need(not self.authorized and not self.closed and lane in ('settings', 'preflight', 'recovery'), 'phone_authority_required')
        g = self.grant
        fields = {'schema', 'scope', 'attempt', 'request_sha256', 'challenge', 'lane',
                  'adb_sha256', 'apk_sha256', 'serial_sha256', 'model', 'issued_utc', 'expires_utc', 'allow_idle_reopen'}
        need(type(g) is dict and set(g) == fields and g['schema'] == 'OT0304-PHONE-GRANT-1'
             and g['scope'] == SCOPE and g['lane'] == lane
             and all(g[key] == context[key] for key in ('attempt', 'request_sha256', 'challenge'))
             and all(type(g[key]) is str and HEX64.fullmatch(g[key]) for key in ('adb_sha256', 'apk_sha256', 'serial_sha256'))
             and type(self.serial) is str and bool(re.fullmatch(r'[A-Za-z0-9._:-]{1,128}', self.serial))
             and sha(self.serial.encode()) == g['serial_sha256'] and g['model'] == 'SM-N986U'
             and type(g['allow_idle_reopen']) is bool
             and finite(g['issued_utc']) and finite(g['expires_utc'])
             and g['issued_utc'] <= self.utc() < min(g['issued_utc'] + 60, g['expires_utc'])
             and 0 < g['expires_utc'] - g['issued_utc'] <= 1200, 'phone_grant_invalid')
        need(sha(regular(self.adb_path).read_bytes()) == g['adb_sha256']
             and sha(regular(self.apk_path).read_bytes()) == g['apk_sha256'], 'phone_artifact_changed')
        self.authorized, self.context = True, dict(context)
        self.temporary = f"/data/local/tmp/ot0304-phone-{context['attempt']}-{lane}.xml"
        try:
            self._artifacts(deadline)
            self._visible(deadline)
        except BaseException:
            self.authorized = False
            raise PhoneError('phone_preflight_failed') from None
        return True

    def _artifacts(self, deadline):
        need(sha(regular(self.adb_path).read_bytes()) == self.grant['adb_sha256']
             and sha(regular(self.apk_path).read_bytes()) == self.grant['apk_sha256'], 'phone_artifact_changed')
        need(self._run(['shell', 'getprop', 'ro.product.model'], deadline).strip() == b'SM-N986U', 'phone_model_changed')
        paths = self._run(['shell', 'pm', 'path', PACKAGE], deadline).decode('ascii').strip().splitlines()
        need(len(paths) == 1 and re.fullmatch(r'package:/data/app/[A-Za-z0-9_./=+~-]+/base\.apk', paths[0]), 'phone_package_invalid')
        need(sha(self._run(['exec-out', 'cat', paths[0][8:]], deadline)) == self.grant['apk_sha256'], 'phone_installed_artifact_changed')

    def _visible(self, deadline, require_activity=False):
        raw = self._run(['shell', 'dumpsys', 'window', 'windows'], deadline)
        need(len(raw) <= 1048576 and not any(value in raw for value in
             (b'mShowingLockscreen=true', b'mDreamingLockscreen=true', b'isStatusBarKeyguard=true')), 'phone_locked')
        if require_activity:
            focused = [line for line in raw.splitlines() if b'mCurrentFocus=' in line]
            need(len(focused) == 1 and ACTIVITY.encode() in focused[0], 'phone_not_foreground')

    def launch(self, deadline):
        self._visible(deadline)
        result = self._run(['shell', 'am', 'start', '-W', '-n', ACTIVITY], deadline)
        need(b'Error' not in result and b'Status: ok' in result, 'phone_launch_failed')

    def reopen_after_idle(self, frames, deadline):
        need(self.authorized and not self.cleanup and not self.reopen_attempted
             and self.grant['allow_idle_reopen'] is True, 'phone_reopen_not_granted')
        need(type(frames) is tuple and len(frames) == 2 and all(type(frame) is Frame for frame in frames), 'phone_idle_proof_invalid')
        previous = None
        for frame in frames:
            ui, trace = Ui(frame.ui), Trace.parse(frame.trace_after)
            need(trace.state() == 'DISCONNECTED' and 'Bluetooth disconnected' in ui.texts
                 and not any(ui.matches(label) for label in CLOSURE_CONTROLS), 'phone_idle_proof_invalid')
            if previous is not None:
                need(frame.started > previous.ended, 'phone_idle_proof_invalid')
                trace.extends(Trace.parse(previous.trace_after))
            previous = frame
        self._artifacts(deadline)
        # Re-capture after artifact verification; neither an old proof nor a
        # restarted reconnect may authorize force-stop. This is one dispatch.
        fresh = self.capture(deadline)
        fresh_ui, fresh_trace = Ui(fresh.ui), Trace.parse(fresh.trace_after)
        fresh_trace.extends(Trace.parse(frames[-1].trace_after))
        need(fresh_trace.state() == 'DISCONNECTED' and 'Bluetooth disconnected' in fresh_ui.texts
             and not any(fresh_ui.matches(label) for label in CLOSURE_CONTROLS), 'phone_idle_proof_invalid')
        self._dispatch(fresh, deadline)
        self.reopen_attempted = True
        self._run(['shell', 'am', 'force-stop', PACKAGE], deadline)
        self.launch(deadline)
        return True

    def capture(self, deadline):
        start = self.clock()
        self._visible(deadline, require_activity=True)
        before = self._run(['exec-out', 'run-as', PACKAGE, 'cat', 'files/v1-connection-log'], deadline)
        self._run(['shell', 'uiautomator', 'dump', self.temporary], deadline)
        ui = self._run(['exec-out', 'cat', self.temporary], deadline)
        self._run(['shell', 'rm', '-f', self.temporary], deadline)
        after = self._run(['exec-out', 'run-as', PACKAGE, 'cat', 'files/v1-connection-log'], deadline)
        self._visible(deadline, require_activity=True)
        return Frame(start, self.clock(), ui, before, after)

    def _dispatch(self, frame, deadline):
        need(0 <= self.clock() - frame.ended <= 5, 'dispatch_frame_stale')
        self._visible(deadline, require_activity=True)
        current = Trace.parse(self._run(['exec-out', 'run-as', PACKAGE, 'cat', 'files/v1-connection-log'], deadline))
        need(current == Trace.parse(frame.trace_after), 'dispatch_trace_changed')
        need(0 <= self.clock() - frame.ended <= 5, 'dispatch_frame_stale')

    def tap(self, frame, label, deadline):
        need(label in CONTROLS and (not self.cleanup or label in CLOSURE_CONTROLS or label == 'Back to Device'), 'control_not_scoped')
        point = Ui(frame.ui).control(label)
        self._dispatch(frame, deadline)
        start = self.clock()
        self._run(['shell', 'input', 'tap', str(point[0]), str(point[1])], deadline)
        return start

    def scroll(self, frame, direction, deadline):
        need(direction in ('up', 'down'), 'scroll_not_scoped')
        left, top, right, bottom = Ui(frame.ui).scroll_area()
        self._dispatch(frame, deadline)
        x, inset = (left + right)//2, (bottom - top)//5
        first, last = (bottom-inset, top+inset) if direction == 'down' else (top+inset, bottom-inset)
        self._run(['shell', 'input', 'swipe', str(x), str(first), str(x), str(last), '450'], deadline)

    def settle(self, deadline):
        self.sleep(min(0.25, remaining(self.clock, deadline)))
        remaining(self.clock, deadline)

    def begin_cleanup(self):
        need(self.authorized and not self.closed, 'phone_authority_required')
        self.cleanup = True

    def release(self, deadline):
        if self.authorized and not self.closed:
            self.cleanup = True
            try:
                self._run(['shell', 'rm', '-f', self.temporary], deadline)
            finally:
                self.closed = True


class PhoneObserver:
    """One complete saved-pair sequence or closure lane, driven by real ACKs.

    Backend protocol: authorize(context,lane,deadline), launch(deadline),
    capture(deadline)->Frame, tap(frame,label,deadline)->dispatch monotonic time,
    scroll(frame,direction,deadline), settle(deadline), reopen_after_idle(frames,deadline),
    begin_cleanup(), release(deadline).
    ``run(factory,context,lane='settings')`` constructs the immutable Observation
    with this object as its Lines and emit seams. Factory is supplied by caller;
    importing or constructing either class cannot operate a phone.
    """
    def __init__(self, backend, directory, *, clock=time.monotonic, persistence=write_once):
        self.backend, self.directory, self.clock, self.persistence = backend, Path(directory), clock, persistence
        self.used, self.authorized, self.touched, self.idle = False, False, False, False
        self.context, self.owner, self.pending, self.awaiting = None, None, None, None
        self.lane, self.expected, self.seen = None, 0, set()
        self.trace, self.binding, self.last_frame = None, None, None
        self.deadline, self.close_deadline, self.last_clock = None, None, None
        self.sequence, self.failure, self.frames = 0, None, []
        self.cleanup_evidence = None
        self.closure_dispatch_attempted = False

    def _now(self):
        now = self.clock()
        need(finite(now) and (self.last_clock is None or now >= self.last_clock), 'phone_clock_rollback')
        self.last_clock = now
        return now

    def _save(self, kind, value):
        record = {'schema': 'OT0304-PHONE-EVIDENCE-1', 'attempt': self.context['attempt'],
                  'request_sha256': self.context['request_sha256'],
                  'challenge_sha256': sha(self.context['challenge'].encode()), 'lane': self.lane,
                  'sequence': self.sequence, 'kind': kind, 'value': value}
        path = self.directory / f"phone-{self.lane}-{self.context['attempt']}-{self.sequence:03}.json"
        raw = canonical(record) + b'\n'
        self.persistence(path, raw)
        need(regular(path).read_bytes() == raw, 'evidence_write_failed')
        self.sequence += 1
        return sha(raw)

    def _capture(self, deadline, *, protected=False, closure=False):
        remaining(self._now, deadline)
        frame = self.backend.capture(deadline)
        need(type(frame) is Frame and finite(frame.started) and finite(frame.ended)
             and frame.started <= frame.ended <= self._now() < deadline
             and (self.last_frame is None or frame.started >= self.last_frame.ended)
             and frame.foreground is True, 'capture_stale_or_late')
        ui, before, after = Ui(frame.ui), Trace.parse(frame.trace_before), Trace.parse(frame.trace_after)
        after.extends(before)
        if self.trace is not None and not closure:
            before.extends(self.trace)
        if protected:
            need(before.identity() == after.identity() == self.binding, 'connection_changed')
            fresh = after.rows[len(self.trace.rows) if self.trace is not None else len(before.rows):]
            need(not any(row[2] == 'L' and row[3] in ('BACKGROUND', 'STOPPED') for row in fresh), 'phone_lifecycle_changed')
        self.trace, self.last_frame = after, frame
        self.frames.append({'started_millis': int(frame.started*1000), 'ended_millis': int(frame.ended*1000),
                            'ui_sha256': sha(frame.ui), 'trace_before_sha256': sha(frame.trace_before),
                            'trace_after_sha256': sha(frame.trace_after)})
        need(len(self.frames) <= 256, 'capture_limit')
        return frame, ui, after

    def _tap(self, frame, label, deadline):
        Ui(frame.ui).control(label)
        remaining(self._now, deadline)
        dispatched = self.backend.tap(frame, label, deadline)
        need(finite(dispatched) and frame.ended <= dispatched <= self._now() < deadline, 'dispatch_late')
        self.touched = True
        self._save('dispatch', {'control': label, 'dispatch_millis': int(dispatched*1000),
                               'before_ui_sha256': sha(frame.ui), 'before_trace_sha256': sha(frame.trace_after),
                               'dispatch_only': True})
        return dispatched

    def _find(self, label, deadline, *, protected=False, closure=False):
        for direction in ('up', 'down'):
            for step in range(7):
                frame, ui, trace = self._capture(deadline, protected=protected, closure=closure)
                if ui.matches(label):
                    ui.control(label)
                    return frame, ui, trace
                if step < 6:
                    self.backend.scroll(frame, direction, deadline)
                    remaining(self._now, deadline)
        raise PhoneError('scoped_control_missing')

    def _ready(self, deadline):
        self.backend.launch(deadline)
        self.touched = True
        initial, ui, trace = self._capture(deadline)
        need(trace.state(required=False) != 'READY', 'fresh_ready_required')
        if not ui.matches('Find device'):
            need(trace.state(required=False) == 'DISCONNECTED' and 'Bluetooth disconnected' in ui.texts,
                 'saved_pair_chooser_required')
            proofs = []
            for _ in range(2):
                self.backend.settle(deadline)
                proof, proof_ui, proof_trace = self._capture(deadline)
                need(proof_trace.state() == 'DISCONNECTED' and 'Bluetooth disconnected' in proof_ui.texts
                     and not any(proof_ui.matches(label) for label in CLOSURE_CONTROLS), 'phone_idle_proof_invalid')
                proofs.append(proof)
            self._save('idle-before-reopen', {'phone_gatt_idle': True, 'reconnect_stopped': True,
                                              'session': trace.session, 'captures': self.frames})
            need(self.backend.reopen_after_idle(tuple(proofs), deadline) is True, 'phone_reopen_failed')
            old_session = trace.session
            # Only this verified and explicitly granted boundary permits a new
            # recording session; later session changes always reject READs.
            self.trace, self.last_frame = None, None
            initial, ui, trace = self._capture(deadline)
            need(trace.session > old_session and ui.matches('Find device')
                 and trace.state(required=False) != 'READY', 'phone_reopen_session_unverified')
        need(ui.matches('Find device'), 'saved_pair_chooser_required')
        frame, _, _ = self._find('Find device', deadline)
        self._tap(frame, 'Find device', deadline)
        frame, _, _ = self._find('Start Bluetooth device service', deadline)
        self._tap(frame, 'Start Bluetooth device service', deadline)
        while True:
            frame, ui, trace = self._capture(deadline)
            if trace.state(required=False) == 'READY':
                self.binding = trace.identity()
                need(len(trace.rows) > len(Trace.parse(initial.trace_after).rows), 'fresh_ready_required')
                break
            fresh = trace.rows[len(Trace.parse(initial.trace_after).rows):]
            need(trace.state(required=False) != 'FAILED' and not any(
                row[2] == 'S' and row[3] in ('RECONNECT_ATTEMPT_STARTED', 'TRANSACTION_FAILED') for row in fresh), 'saved_pair_failed')
            self.backend.settle(deadline)
        frame, _, _ = self._find('Edit saved name or region', deadline, protected=True)
        self._tap(frame, 'Edit saved name or region', deadline)
        # Clock synchronization shares the busy state; READ waits for an enabled control.
        while True:
            frame, ui, _ = self._capture(deadline, protected=True)
            if ui.matches(READS['name']):
                try:
                    ui.control(READS['name'])
                    break
                except PhoneError as error:
                    need(str(error) == 'control_disabled', str(error))
            else:
                self.backend.scroll(frame, 'up', deadline)
            self.backend.settle(deadline)
        return {'state': 'valid', 'current': True, 'connection': self.binding[1], 'generation': self.binding[2],
                'protocol_info': True, 'authorization': True, 'snapshot': True, 'ready': True}

    def _value(self, phase, texts):
        if phase == 'name':
            values = [text[22:] for text in texts if text.startswith('Last device readback: ')]
            return 'valid' if len(values) == 1 and values[0].strip() else None
        if phase == 'region':
            values = [text[14:] for text in texts if text.startswith('Saved choice: ')]
            return 'valid' if len(values) == 1 and values[0] in REGIONS else None
        values = [text[17:] for text in texts if text.startswith('Device readback: ')]
        visible = [text[18:] for text in texts if text.startswith('Saved visibility: ')]
        if len(values) != 1 or len(visible) != 1:
            return None
        if values[0] == 'Not configured':
            return 'absent' if visible[0] == 'On' else None
        return 'valid' if values[0].strip() and values[0] != 'Not confirmed' and visible[0] in ('On', 'Off') else None

    def _read_setting(self, phase, deadline):
        # Capture the shared notice above the values before dispatch. If it is
        # already this phase's success, only an observed pending transition can
        # distinguish the explicit READ from stale cached UI.
        baseline = set()
        for step in range(7):
            frame, ui, _ = self._capture(deadline, protected=True)
            baseline.update(text for text in ui.texts if text in (*NOTICES.values(), *PENDING.values(),
                'Display clock synchronized.', 'Read the device name before applying a change.'))
            if baseline:
                break
            self.backend.scroll(frame, 'up', deadline)
        need(len(baseline) == 1, 'baseline_notice_unknown')
        frame, dispatch_ui, _ = self._find(READS[phase], deadline, protected=True)
        latest = set(NOTICES.values()) & dispatch_ui.texts
        if latest:
            need(len(latest) == 1, 'baseline_notice_unknown')
            baseline = latest
        dispatched = self._tap(frame, READS[phase], deadline)
        pending, notice, state, direction, scrolls = False, False, None, 'up', 0
        while True:
            frame, ui, _ = self._capture(deadline, protected=True)
            need(frame.started >= dispatched, 'post_dispatch_capture_required')
            pending |= PENDING[phase] in ui.texts
            current_notice = set(NOTICES.values()) & ui.texts
            need(not current_notice or current_notice == {NOTICES[phase]}
                 or (not notice and current_notice == baseline), 'settings_result_rejected')
            if NOTICES[phase] in ui.texts and (pending or NOTICES[phase] not in baseline):
                notice = True
            # Values collected before this phase's success or pending transition
            # may be cached. Start accumulating only after fresh success evidence.
            if notice or (phase == 'public' and pending):
                found = self._value(phase, ui.texts)
                if found is not None:
                    need(state is None or state == found, 'settings_value_changed')
                    state = found
            if notice and state is not None:
                return {'state': state, 'current': True, 'connection': self.binding[1], 'generation': self.binding[2]}
            self.backend.settle(deadline)
            if PENDING[phase] not in ui.texts:
                if scrolls == 6:
                    direction, scrolls = ('down' if direction == 'up' else 'up'), 0
                self.backend.scroll(frame, direction, deadline)
                scrolls += 1

    def _cleanup(self, deadline):
        if self.idle:
            return self.cleanup_evidence
        self.backend.begin_cleanup()
        # A failed result capture does not grant closure. Find the disconnect
        # action independently, then require fresh DISCONNECTED and idle UI.
        dispatched, last = False, None
        for direction in ('up', 'down'):
            for step in range(7):
                frame, ui, trace = self._capture(deadline, closure=True)
                candidates = [label for label in CLOSURE_CONTROLS if ui.matches(label)]
                need(len(candidates) <= 1, 'closure_control_ambiguous')
                if candidates:
                    need(not self.closure_dispatch_attempted, 'closure_dispatch_outcome_unknown')
                    self.closure_dispatch_attempted = True
                    self._tap(frame, candidates[0], deadline)
                    dispatched = True
                    last = frame
                    break
                if trace.state() in ('DISCONNECTED', 'INACTIVE') and 'Bluetooth disconnected' in ui.texts:
                    last = frame
                    break
                if ui.matches('Back to Device'):
                    self._tap(frame, 'Back to Device', deadline)
                elif step < 6:
                    self.backend.scroll(frame, direction, deadline)
            if last is not None:
                break
        need(last is not None, 'phone_closure_unavailable')
        # Two captures separated by a settled poll ensure closure is current,
        # rather than retaining a pre-disconnect Idle or held reconnect screen.
        for _ in range(2):
            self.backend.settle(deadline)
            frame, ui, trace = self._capture(deadline, closure=True)
            trace.extends(Trace.parse(last.trace_after))
            need(frame.started >= last.ended and trace.state() == 'DISCONNECTED'
                 and 'Bluetooth disconnected' in ui.texts
                 and not any(ui.matches(label) for label in CLOSURE_CONTROLS), 'phone_not_idle')
            last = frame
        evidence = self._save('closure', {'phone_gatt_idle': True, 'reconnect_stopped': True,
            'disconnect_dispatched': dispatched, 'captures': self.frames, 'settings_writes': False})
        self.cleanup_evidence, self.idle = evidence, True
        return evidence

    def emit(self, raw, *, flush=True):
        try:
            message = json.loads(raw)
        except (ValueError, TypeError):
            raise PhoneError('owner_message_invalid') from None
        need(type(message) is dict and self.context is not None, 'owner_message_invalid')
        common = {'attempt': self.context['attempt'], 'challenge': self.context['challenge'], 'lane': self.lane}
        need(all(message.get(key) == value for key, value in common.items()), 'owner_context_changed')
        phase, token = message.get('phase'), message.get('token')
        need(type(token) is str and HEX32.fullmatch(token), 'owner_token_invalid')
        if message.get('schema') == 'OT0304-SETTINGS-CHECKPOINT-1':
            need(set(message) == {'schema', 'attempt', 'request_sha256', 'challenge', 'lane', 'phase', 'token', 'remaining_seconds', 'instruction'}
                 and message['request_sha256'] == self.context['request_sha256']
                 and type(message['remaining_seconds']) is int and message['remaining_seconds'] >= 0
                 and token not in self.seen, 'owner_checkpoint_invalid')
            if phase == 'closed':
                need(self.pending is None or self.pending['phase'] != 'closed', 'owner_checkpoint_duplicate')
                self.close_deadline = self._now() + CLOSE_SECONDS
                self.pending, self.awaiting = message, None
            else:
                need(self.lane == 'settings' and self.pending is None and self.awaiting is None
                     and self.expected < 4 and phase == ('ready', 'name', 'region', 'public')[self.expected], 'owner_checkpoint_order')
                self.pending = message
            self.seen.add(token)
        elif message.get('schema') == 'OT0304-SETTINGS-ACK-1':
            need(set(message) == {'schema', 'attempt', 'challenge', 'lane', 'phase', 'token', 'evidence_sha256'}
                 and self.awaiting is not None and phase == self.awaiting['phase']
                 and token == self.awaiting['token'] and type(message['evidence_sha256']) is str
                 and HEX64.fullmatch(message['evidence_sha256']), 'owner_ack_invalid')
            deadline = self.close_deadline if phase == 'closed' else self.deadline
            remaining(self._now, deadline)
            paths = list(regular(self.directory).glob(f"{self.lane}-{self.context['attempt']}-[0-9][0-9].json"))
            need(len(paths) <= 12, 'owner_ack_invalid')
            records = []
            for path in paths:
                content = regular(path).read_bytes()
                if sha(content) == message['evidence_sha256']:
                    need(len(content) <= 8192, 'owner_ack_invalid')
                    records.append(json.loads(content))
            need(len(records) == 1, 'owner_ack_not_durable')
            record = records[0]
            need(record.get('schema') == 'OT0304-SETTINGS-OBSERVATION-EVENT-1'
                 and record.get('attempt') == self.context['attempt']
                 and record.get('request_sha256') == self.context['request_sha256']
                 and record.get('challenge_sha256') == sha(self.context['challenge'].encode())
                 and record.get('lane') == self.lane and record.get('kind') == 'ack'
                 and record.get('value') == {'phase': phase, 'token_sha256': sha(token.encode()),
                                           'observation': self.awaiting['value']}, 'owner_ack_invalid')
            if phase != 'closed':
                self.expected += 1
            self.awaiting = None
        else:
            raise PhoneError('owner_message_invalid')

    def read(self, seconds):
        need(self.pending is not None and self.awaiting is None and finite(seconds) and seconds > 0, 'owner_read_invalid')
        checkpoint, self.pending = self.pending, None
        phase = checkpoint['phase']
        deadline = min(self._now() + seconds, self.close_deadline if phase == 'closed' else self.deadline)
        self.frames = []
        try:
            if not self.authorized:
                need(self.backend.authorize(self.context, self.lane, deadline) is True, 'phone_authority_required')
                self.authorized = True
                if phase == 'closed':
                    self.backend.launch(deadline)
                    self.touched = True
            if phase == 'closed':
                evidence = self._cleanup(deadline)
                value = {'phone_gatt_idle': True, 'reconnect_stopped': True, 'evidence_sha256': evidence}
            else:
                value = self._ready(deadline) if phase == 'ready' else self._read_setting(phase, deadline)
                evidence = self._save('phase', {'phase': phase, 'state': value['state'],
                    'session': self.binding[0], 'connection': self.binding[1], 'generation': self.binding[2],
                    'captures': self.frames, 'settings_writes': False, 'profile': None, 'capabilities': None})
                remaining(self._now, deadline)
                value['evidence_sha256'] = evidence
            reply = {key: checkpoint[key] for key in ('attempt', 'challenge', 'lane', 'phase', 'token')}
            reply['value'] = value
            self.awaiting = reply
            return reply
        except BaseException as error:
            if self.failure is None:
                self.failure = str(error) if isinstance(error, PhoneError) else 'phone_operation_failed'
                try:
                    self._save('failure', {'phase': phase, 'category': self.failure,
                                           'elapsed_millis': int(self._now()*1000)})
                except BaseException:
                    pass
            raise PhoneError(self.failure) from None

    def run(self, observation_factory, context, *, lane='settings'):
        need(not self.used, 'observer_reused')
        self.used = True
        context_valid(context)
        need(lane in ('settings', 'preflight', 'recovery') and regular(self.directory).is_dir(), 'observer_configuration_invalid')
        self.context, self.lane, self.deadline = dict(context), lane, self._now() + OBSERVATION_SECONDS
        self.owner = observation_factory(self.directory, self, emit=self.emit, clock=self.clock, lane=lane)
        try:
            return self.owner(context) if lane == 'settings' else self.owner.idle_proof(context)
        finally:
            if self.authorized:
                cleanup_deadline = self.close_deadline or self._now() + CLOSE_SECONDS
                if not self.idle:
                    try:
                        self.frames = []
                        self._cleanup(cleanup_deadline)
                    except BaseException:
                        pass
                try:
                    self.backend.release(cleanup_deadline)
                except BaseException:
                    pass

    def close(self):
        return self.owner.close() if self.owner is not None else None
