"""Source-shaped host replay; physical Android/ADB behaviour remains simulated.

These tests require no private inputs. The private frozen integration gate uses
the actual immutable Observation owner; ReplayOwner models its public protocol
for portable regressions here, without claiming that separate integration gate.
"""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import standard_settings_phone_observation as phone

CONTEXT = {'schema': 'OT0304-STANDARD-OBSERVATION-CONTEXT-1', 'attempt': 'a'*32,
           'request_sha256': 'b'*64, 'challenge': 'c'*32}


class Clock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now

    def advance(self, amount):
        self.now += amount


def xml_screen(texts=(), controls=(), *, disabled=(), duplicate=None,
               package=phone.PACKAGE, scrolling=True):
    root = ET.Element('hierarchy', rotation='0')
    app = ET.SubElement(root, 'node', package=package, enabled='true',
                        bounds='[0,0][1080,2200]', clickable='false')
    area = ET.SubElement(app, 'node', package=package, enabled='true',
                         bounds='[0,100][1080,2100]', scrollable=str(scrolling).lower(), clickable='false')
    for index, text in enumerate(texts):
        ET.SubElement(area, 'node', package=package, text=text, enabled='true',
                      clickable='false', bounds=f'[50,{150+index*60}][1000,{200+index*60}]')
    for index, label in enumerate(controls):
        button = ET.SubElement(area, 'node', package=package, text='', enabled=str(label not in disabled).lower(),
            clickable='true', bounds=f'[50,{1000+index*100}][1000,{1090+index*100}]')
        ET.SubElement(button, 'node', package=package, text=label, enabled='true', clickable='false',
                      bounds=f'[100,{1010+index*100}][900,{1080+index*100}]')
        if label == duplicate:
            ET.SubElement(area, 'node', package=package, text=label, enabled='true', clickable='true', bounds='[50,1900][900,2000]')
    return ET.tostring(root, encoding='utf-8', xml_declaration=True)


class FakeBackend:
    """Replay real route/busy/readback chronology, not instantaneous READ ACKs.

    Fake clock advances for captures, dispatches and saved-pair discovery. Scroll
    changes which result evidence is visible. Every capture includes the current
    schema4 trace before and after the UI read, matching production capture shape.
    """
    def __init__(self, clock, *, public='absent', fast=False, stale=False,
                 capture_cost=1.0, split_public=True, disconnect_failure=False):
        self.clock, self.public = clock, public
        self.fast, self.stale, self.capture_cost = fast, stale, capture_cost
        self.split_public, self.disconnect_failure = split_public, disconnect_failure
        self.authorized, self.released, self.cleanup = False, False, False
        self.actions, self.captures, self.scrolls = [], 0, 0
        self.stage, self.viewport, self.notice, self.pending = 'initial', 'top', '', None
        self.pending_seen, self.discovery_seen, self.clock_seen = False, False, False
        self.session, self.transaction, self.generation = 71, 5, 3
        self.ordinal, self.rows = 0, ['71\t0\tC\tDISCONNECTED']
        self.hook = None
        self.disabled_label, self.duplicate_label = None, None
        self.fail_capture_after = None
        self.connection_change, self.stale_frame = False, False
        self.capture_gap, self.wrong_package = False, False
        self.hold_reconnect = False
        self.allow_idle_reopen, self.reopen_attempted = False, False

    def authorize(self, context, lane, deadline):
        phone.context_valid(context)
        phone.remaining(self.clock, deadline)
        self.authorized = True
        self.actions.append('authorize:'+lane)
        return True

    def launch(self, deadline):
        phone.remaining(self.clock, deadline)
        self.actions.append('launch:'+phone.ACTIVITY)

    def _trace_stage(self, name, transaction=None):
        self.ordinal += 1
        self.rows.append(f'{self.session}\t{int(self.clock()*1000)}\tS\t{name}\t{self.transaction if transaction is None else transaction}\t{self.ordinal}\t{self.generation}\t0\t0\tNONE\tUNAVAILABLE')

    def _state(self, state):
        self.rows.append(f'{self.session}\t{int(self.clock()*1000)}\tC\t{state}')

    def _ready_trace(self):
        for value in ('CONNECTION_ATTEMPT_STARTED', 'GATT_LINK_ESTABLISHED',
                      'PROTECTED_PROTOCOL_INFO_REQUESTED', 'PROTECTED_PROTOCOL_INFO_ACCEPTED',
                      'ATT_MTU_NEGOTIATED', 'STREAM_SUBSCRIPTION_ESTABLISHED',
                      'AUTHORIZATION_STARTED', 'AUTHORIZATION_ACCEPTED',
                      'SNAPSHOT_REQUESTED', 'SNAPSHOT_ACCEPTED', 'READY_REACHED'):
            self._trace_stage(value)
        self._state('READY')

    def trace(self):
        return (f'OTCL\t4\t{self.session}\n'+'\n'.join(self.rows)+'\n').encode()

    def _ui(self):
        if self.stage == 'initial':
            return xml_screen(('Bluetooth disconnected',), ('Find device',))
        if self.stage == 'start':
            return xml_screen(('Bluetooth service ready to start',), ('Start Bluetooth device service',))
        if self.stage == 'finding':
            return xml_screen(('Checking for authorized device',), ('Stop reconnecting',))
        if self.stage == 'ready':
            return xml_screen(('Device status',), ('Edit saved name or region', 'Disconnect Bluetooth device'))
        if self.stage == 'idle':
            return xml_screen(('Bluetooth disconnected',), ())
        if self.stage == 'held':
            return xml_screen(('Reconnecting',), ('Stop reconnecting',))
        notice = self.notice
        disabled = set()
        if self.stage == 'clock':
            notice = 'Synchronizing the display clock…'
            disabled.add('Read device')
        if self.pending is not None:
            notice = phone.PENDING[self.pending]
            disabled.update(phone.READS.values())
        texts = [notice]
        controls = [*phone.READS.values(), 'Disconnect Bluetooth device']
        if self.pending is None and self.stage == 'editor':
            if notice == phone.NOTICES['name']:
                texts.append('Last device readback: Private replay name')
            if notice in (phone.NOTICES['region'], phone.NOTICES['public']):
                texts.append('Saved choice: US915')
            if notice == phone.NOTICES['public']:
                if self.split_public and self.viewport == 'top':
                    pass
                else:
                    texts += ['Device readback: '+('Not configured' if self.public == 'absent' else 'Private public name'),
                              'Saved visibility: '+('On' if self.public == 'absent' else 'Off')]
                    if self.split_public:
                        texts.remove(phone.NOTICES['public'])
        if self.disabled_label:
            disabled.add(self.disabled_label)
        return xml_screen(texts, controls, disabled=disabled, duplicate=self.duplicate_label,
                          package='com.android.settings' if self.wrong_package else phone.PACKAGE)

    def capture(self, deadline):
        self.captures += 1
        if self.fail_capture_after is not None and self.captures >= self.fail_capture_after and not self.cleanup:
            raise phone.PhoneError('phone_command_failed')
        if self.stage == 'finding':
            if self.discovery_seen:
                self._ready_trace()
                self.stage = 'ready'
            else:
                self.discovery_seen = True
        if self.stage == 'clock' and self.clock_seen:
            self.stage, self.notice = 'editor', phone.NOTICES['public']
        elif self.stage == 'clock':
            self.clock_seen = True
        if self.pending is not None:
            if self.fast or self.pending_seen:
                if not self.stale:
                    self.notice = phone.NOTICES[self.pending]
                self.pending = None
            else:
                self.pending_seen = True
        if self.connection_change and self.pending == 'region':
            self.generation += 1
            self._trace_stage('READY_REACHED')
        before = self.trace()
        start = self.clock()
        self.clock.advance(self.capture_cost)
        ui = self._ui()
        if self.wrong_package:
            ui = ui.replace(phone.PACKAGE.encode(), b'com.android.settings')
        after = self.trace()
        if self.capture_gap:
            after = after.replace(b'\t1\t3\t', b'\t9\t3\t', 1)
        if self.hook:
            self.hook(self)
        if self.stale_frame:
            start -= self.capture_cost*2
        return phone.Frame(start, self.clock(), ui, before, after)

    def tap(self, frame, label, deadline):
        phone.remaining(self.clock, deadline)
        phone.Ui(frame.ui).control(label)
        self.actions.append(label)
        start = self.clock()
        self.clock.advance(0.1)
        if label == 'Find device':
            self.stage = 'start'
        elif label == 'Start Bluetooth device service':
            self.stage = 'finding'
            self._trace_stage('RETURNING_OWNER_DISCOVERY_STARTED', transaction=0)
            self._state('CONNECTING')
        elif label == 'Edit saved name or region':
            self.stage = 'clock'
        elif label in phone.READS.values():
            phase = next(key for key, value in phone.READS.items() if value == label)
            if not self.stale:
                self.pending, self.pending_seen = phase, False
            self.viewport = 'bottom' if phase == 'public' else 'top'
        elif label in phone.CLOSURE_CONTROLS:
            self._trace_stage('DISCONNECT_OBSERVED')
            self._state('RECONNECTING' if self.hold_reconnect else 'DISCONNECTED')
            self.stage = 'held' if self.hold_reconnect else 'idle'
            self.pending = None
            if self.disconnect_failure:
                raise phone.PhoneError('phone_command_failed')
        if self.hook:
            self.hook(self)
        return start

    def scroll(self, frame, direction, deadline):
        phone.remaining(self.clock, deadline)
        phone.Ui(frame.ui).scroll_area()
        self.scrolls += 1
        self.clock.advance(0.45)
        self.viewport = 'top' if direction == 'up' else 'bottom'

    def settle(self, deadline):
        phone.remaining(self.clock, deadline)
        self.clock.advance(30 if self.stage == 'finding' else 0.25)

    def begin_cleanup(self):
        self.cleanup = True
        self.actions.append('begin_cleanup')

    def reopen_after_idle(self, frames, deadline):
        phone.need(self.allow_idle_reopen and not self.reopen_attempted, 'phone_reopen_not_granted')
        phone.need(all(phone.Trace.parse(frame.trace_after).state() == 'DISCONNECTED'
            and 'Bluetooth disconnected' in phone.Ui(frame.ui).texts for frame in frames), 'phone_idle_proof_invalid')
        self.reopen_attempted = True
        self.actions.append('idle_process_reopen')
        self.session += 1
        self.ordinal, self.rows = 0, [f'{self.session}\t0\tL\tFOREGROUND']
        self.stage = 'initial'
        self.clock.advance(0.5)
        return True

    def release(self, deadline):
        self.released = True
        self.actions.append('release')


class ReplayOwner:
    """Portable owner protocol double; durable event format matches the owner."""
    ack_mode = 'valid'
    def __init__(self, directory, lines, *, emit, clock, lane):
        self.directory, self.lines, self.emit, self.clock, self.lane = Path(directory), lines, emit, clock, lane
        self.index, self.token_index, self.context, self.closed = 0, 0, None, False
        self.close_attempted = False
        self.states, self.responses = {}, []

    def checkpoint(self, phase, deadline):
        token = f'{self.token_index+1:032x}'
        self.token_index += 1
        event = {**self.context, 'schema': 'OT0304-SETTINGS-CHECKPOINT-1', 'lane': self.lane,
                 'phase': phase, 'token': token, 'remaining_seconds': int(deadline-self.clock()), 'instruction': 'scoped'}
        self.emit(json.dumps(event), flush=True)
        reply = self.lines.read(deadline-self.clock())
        self.responses.append(reply)
        phone.need(self.clock() < deadline and reply['token'] == token)
        record = {'schema': 'OT0304-SETTINGS-OBSERVATION-EVENT-1',
            'attempt': self.context['attempt'], 'request_sha256': self.context['request_sha256'],
            'challenge_sha256': phone.sha(self.context['challenge'].encode()), 'lane': self.lane,
            'sequence': self.index, 'elapsed_millis': int(self.clock()*1000), 'kind': 'ack',
            'value': {'phase': phase, 'token_sha256': phone.sha(token.encode()), 'observation': reply['value']}}
        raw = phone.canonical(record)+b'\n'
        path = self.directory / f"{self.lane}-{self.context['attempt']}-{self.index:02}.json"
        self.index += 1
        mode = self.ack_mode if phase != 'closed' else 'valid'
        if mode != 'missing':
            phone.write_once(path, raw)
        if mode == 'late':
            self.lines.backend.clock.advance(601)
        if mode == 'wrong_kind':
            record['kind'] = 'checkpoint'
            raw = phone.canonical(record)+b'\n'
            path.write_bytes(raw)
        ack = {'schema': 'OT0304-SETTINGS-ACK-1', 'attempt': self.context['attempt'],
               'challenge': self.context['challenge'], 'lane': self.lane, 'phase': phase,
               'token': token, 'evidence_sha256': phone.sha(raw)}
        if mode == 'old_token':
            ack['token'] = 'f'*32
        if mode != 'silent':
            self.emit(json.dumps(ack), flush=True)
        return reply['value']

    def __call__(self, context):
        self.context = context
        deadline = self.clock()+600
        result = 'passed'
        try:
            for phase in ('ready', 'name', 'region', 'public'):
                value = self.checkpoint(phase, deadline)
                self.states[phase] = value['state']
        except BaseException:
            result = 'failed'
        self.close()
        return {'result': result, 'closed': self.closed, 'challenge': context['challenge'],
                'evidence_sha256': 'd'*64}

    def idle_proof(self, context):
        self.context = context
        return self.close()

    def close(self):
        if self.closed:
            return {'closed': True, 'evidence_sha256': 'd'*64}
        if self.close_attempted:
            return None
        self.close_attempted = True
        try:
            self.checkpoint('closed', self.clock()+300)
            self.closed = True
        except BaseException:
            return None
        return {'closed': True, 'evidence_sha256': 'd'*64}


class ObservationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.clock = Clock()
        self.backend = FakeBackend(self.clock)
        self.observer = phone.PhoneObserver(self.backend, self.directory, clock=self.clock)

    def tearDown(self):
        self.temp.cleanup()

    def run_sequence(self, factory=ReplayOwner, **kwargs):
        return self.observer.run(factory, dict(CONTEXT), **kwargs)

    def records(self):
        return [json.loads(path.read_bytes()) for path in self.directory.glob('phone-*.json')]

    def test_complete_saved_pair_split_public_absent_with_durable_ack(self):
        result = self.run_sequence()
        self.assertEqual(('passed', True), (result['result'], result['closed']))
        controls = [value for value in self.backend.actions if value in phone.CONTROLS]
        self.assertEqual(['Find device', 'Start Bluetooth device service', 'Edit saved name or region',
                          'Read device', 'Read region', 'Read public settings', 'Disconnect Bluetooth device'], controls)
        self.assertEqual(4, self.observer.expected)
        self.assertEqual('absent', self.observer.owner.states['public'])
        self.assertTrue(self.backend.released)
        self.assertGreater(self.clock(), 30)
        records = self.records()
        self.assertEqual(4, len([record for record in records if record['kind'] == 'phase']))
        public = next(record for record in records if record['kind'] == 'phase' and record['value']['phase'] == 'public')
        self.assertGreater(len(public['value']['captures']), 1)
        raw = b''.join(path.read_bytes() for path in self.directory.iterdir())
        self.assertNotIn(b'Private replay name', raw)
        self.assertNotIn(b'Private public name', raw)
        self.assertNotIn(b'US915', raw)

    def test_fast_result_accepts_changed_phase_notice_without_pending_frame(self):
        self.backend.fast = True
        self.assertEqual('passed', self.run_sequence()['result'])

    def test_named_public_visibility_off_is_valid(self):
        self.backend.public = 'valid'
        self.assertEqual('passed', self.run_sequence()['result'])
        self.assertEqual('valid', self.observer.owner.states['public'])

    def test_public_absent_visibility_off_is_not_valid(self):
        self.assertIsNone(self.observer._value('public', {'Device readback: Not configured', 'Saved visibility: Off'}))

    def test_same_phase_stale_notice_cannot_complete_a_read(self):
        original = self.backend.tap
        def stale_name(frame, label, deadline):
            if label == 'Edit saved name or region':
                result = original(frame, label, deadline)
                self.backend.clock_seen = True
                self.backend.stage, self.backend.notice = 'editor', phone.NOTICES['name']
                return result
            if label == 'Read device':
                self.backend.stale = True
            return original(frame, label, deadline)
        self.backend.tap = stale_name
        self.assertEqual('failed', self.run_sequence()['result'])
        self.assertNotIn('Read region', self.backend.actions)
        self.assertTrue(self.backend.released)

    def test_generation_change_refuses_region_and_still_disconnects(self):
        self.backend.connection_change = True
        result = self.run_sequence()
        self.assertEqual('failed', result['result'])
        self.assertNotIn('Read public settings', self.backend.actions)
        self.assertIn('Disconnect Bluetooth device', self.backend.actions)

    def test_pending_retained_name_or_region_value_cannot_supply_success(self):
        for phase, retained, invalid in (
            ('name', 'Last device readback: Retained private name', None),
            ('region', 'Saved choice: US915', 'Saved choice: Not configured')):
            with self.subTest(phase=phase), tempfile.TemporaryDirectory() as directory:
                clock = Clock()
                backend = FakeBackend(clock)
                observer = phone.PhoneObserver(backend, directory, clock=clock)
                original = backend._ui
                def shaped(phase=phase, retained=retained, invalid=invalid):
                    raw = original()
                    if backend.pending == phase:
                        return xml_screen((phone.PENDING[phase], retained),
                                          (*phone.READS.values(), 'Disconnect Bluetooth device'),
                                          disabled=phone.READS.values())
                    if backend.notice == phone.NOTICES[phase] and phone.READS[phase] in backend.actions:
                        texts = (phone.NOTICES[phase],) if invalid is None else (phone.NOTICES[phase], invalid)
                        return xml_screen(texts, (*phone.READS.values(), 'Disconnect Bluetooth device'))
                    return raw
                backend._ui = shaped
                result = observer.run(ReplayOwner, dict(CONTEXT))
                self.assertEqual('failed', result['result'])
                self.assertNotIn('Read region' if phase == 'name' else 'Read public settings', backend.actions)
                self.assertTrue(backend.released)

    def test_asynchronous_dispatch_may_briefly_retain_previous_notice(self):
        original = self.backend.capture
        delayed = set()
        def lag(deadline):
            phase = self.backend.pending
            if phase and phase not in delayed:
                delayed.add(phase)
                self.backend.pending = None
                frame = original(deadline)
                self.backend.pending, self.backend.pending_seen = phase, False
                return frame
            return original(deadline)
        self.backend.capture = lag
        self.assertEqual('passed', self.run_sequence()['result'])

    def test_background_between_protected_frames_refuses_read(self):
        def background(backend):
            if 'Read region' in backend.actions and not backend.cleanup and not any('\tL\tBACKGROUND' in row for row in backend.rows):
                backend.rows.append(f'{backend.session}\t{int(backend.clock()*1000)}\tL\tBACKGROUND')
                backend.rows.append(f'{backend.session}\t{int(backend.clock()*1000)}\tL\tFOREGROUND')
        self.backend.hook = background
        self.assertEqual('failed', self.run_sequence()['result'])
        self.assertNotIn('Read public settings', self.backend.actions)

    def test_reconnect_during_bound_phase_refuses_public_and_cleans_up(self):
        def reconnect(backend):
            if 'Read region' in backend.actions and not backend.cleanup and not any('\tRECONNECT_ATTEMPT_STARTED\t' in row for row in backend.rows):
                backend._trace_stage('RECONNECT_ATTEMPT_STARTED')
                backend._state('RECONNECTING')
        self.backend.hook = reconnect
        self.assertEqual('failed', self.run_sequence()['result'])
        self.assertNotIn('Read public settings', self.backend.actions)
        self.assertTrue(self.backend.released)

    def test_retained_idle_without_chooser_is_explicit_precondition_failure(self):
        self.backend.stage = 'idle'
        result = self.run_sequence()
        self.assertEqual('failed', result['result'])
        self.assertEqual('phone_reopen_not_granted', self.observer.failure)
        self.assertNotIn('Find device', self.backend.actions)

    def test_clean_chooser_lifecycle_only_trace_accepts_fresh_saved_pair_ready(self):
        self.backend.rows = ['71\t0\tL\tFOREGROUND']
        self.assertEqual('passed', self.run_sequence()['result'])

    def test_scoped_reopen_after_settled_idle_requires_new_session(self):
        self.backend.stage, self.backend.allow_idle_reopen = 'idle', True
        result = self.run_sequence()
        self.assertEqual('passed', result['result'])
        self.assertEqual(1, self.backend.actions.count('idle_process_reopen'))
        self.assertEqual(72, self.observer.binding[0])

    def test_uncertain_reopen_is_not_replayed_or_inferred_successful(self):
        self.backend.stage, self.backend.allow_idle_reopen = 'idle', True
        original = self.backend.reopen_after_idle
        def uncertain(frames, deadline):
            original(frames, deadline)
            raise phone.PhoneError('phone_command_failed')
        self.backend.reopen_after_idle = uncertain
        self.assertEqual('failed', self.run_sequence()['result'])
        self.assertEqual(1, self.backend.actions.count('idle_process_reopen'))
        self.assertNotIn('Find device', self.backend.actions)

    def test_reopen_without_new_recording_session_cannot_claim_ready(self):
        self.backend.stage, self.backend.allow_idle_reopen = 'idle', True
        def old_session(frames, deadline):
            self.backend.actions.append('idle_process_reopen')
            self.backend.stage = 'initial'
            return True
        self.backend.reopen_after_idle = old_session
        self.assertEqual('failed', self.run_sequence()['result'])
        self.assertNotIn('Find device', self.backend.actions)

    def test_session_change_refuses_current_phase(self):
        def change(backend):
            if 'Read region' in backend.actions and not backend.cleanup and backend.session == 71:
                backend.session = 72
                backend.rows = ['72\t0\tC\tDISCONNECTED']
                backend.ordinal = 0
        self.backend.hook = change
        self.assertEqual('failed', self.run_sequence()['result'])
        self.assertNotIn('Read public settings', self.backend.actions)

    def test_disabled_control_parent_is_not_enabled_by_text_leaf(self):
        self.backend.disabled_label = 'Read region'
        result = self.run_sequence()
        self.assertEqual('failed', result['result'])
        self.assertNotIn('Read region', self.backend.actions)

    def test_duplicate_exact_control_refused(self):
        self.backend.duplicate_label = 'Read region'
        self.assertEqual('failed', self.run_sequence()['result'])
        self.assertNotIn('Read region', self.backend.actions)

    def test_capture_overhead_counts_against_deadline_and_closure_is_independent(self):
        original = self.backend.capture
        def expensive(deadline):
            self.backend.capture_cost = 80 if not self.backend.cleanup else 1
            return original(deadline)
        self.backend.capture = expensive
        result = self.run_sequence()
        self.assertEqual('failed', result['result'])
        self.assertTrue(result['closed'])
        self.assertTrue(self.backend.released)
        self.assertGreater(self.clock(), 600)

    def test_capture_failure_does_not_advance_and_cleanup_runs(self):
        self.backend.fail_capture_after = 12
        result = self.run_sequence()
        self.assertEqual('failed', result['result'])
        self.assertIn('Disconnect Bluetooth device', self.backend.actions)
        self.assertTrue(self.backend.released)

    def test_stale_frame_refused(self):
        self.backend.stale_frame = True
        self.assertEqual('failed', self.run_sequence()['result'])
        self.assertNotIn('Read device', self.backend.actions)

    def test_wrong_package_refused(self):
        self.backend.wrong_package = True
        self.assertEqual('failed', self.run_sequence()['result'])
        self.assertNotIn('Find device', self.backend.actions)

    def test_already_ready_does_not_count_as_new_saved_pair_ready(self):
        self.backend.stage = 'ready'
        self.backend._ready_trace()
        self.assertEqual('failed', self.run_sequence()['result'])
        self.assertNotIn('Read device', self.backend.actions)

    def test_missing_wrong_kind_and_old_token_ack_stop_before_next_phase(self):
        for mode in ('missing', 'wrong_kind', 'old_token', 'silent'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as directory:
                clock, owner = Clock(), type('Owner', (ReplayOwner,), {'ack_mode': mode})
                backend = FakeBackend(clock)
                observer = phone.PhoneObserver(backend, directory, clock=clock)
                result = observer.run(owner, dict(CONTEXT))
                self.assertEqual('failed', result['result'])
                self.assertNotIn('Read device', backend.actions)
                self.assertTrue(backend.released)

    def test_ack_after_deadline_never_advances(self):
        owner = type('LateOwner', (ReplayOwner,), {'ack_mode': 'late'})
        result = self.run_sequence(owner)
        self.assertEqual('failed', result['result'])
        self.assertNotIn('Read device', self.backend.actions)
        self.assertTrue(result['closed'])

    def test_persistence_failure_after_read_dispatch_never_returns_reply(self):
        normal = phone.write_once
        def fail(path, raw):
            value = json.loads(raw)
            if value['kind'] == 'phase' and value['value']['phase'] == 'name':
                raise OSError('private failure string must not escape')
            normal(path, raw)
        self.observer.persistence = fail
        result = self.run_sequence()
        self.assertEqual('failed', result['result'])
        self.assertNotIn('Read region', self.backend.actions)
        self.assertEqual('phone_operation_failed', self.observer.failure)
        self.assertTrue(result['closed'])

    def test_uncertain_disconnect_is_not_replayed_and_fresh_idle_can_be_observed(self):
        self.backend.disconnect_failure = True
        result = self.run_sequence()
        self.assertEqual(1, self.backend.actions.count('Disconnect Bluetooth device'))
        self.assertFalse(result['closed'])
        self.assertTrue(self.observer.idle)
        self.assertTrue(self.backend.released)

    def test_held_reconnect_never_counts_as_closed(self):
        self.backend.hold_reconnect = True
        result = self.run_sequence()
        self.assertFalse(result['closed'])
        self.assertFalse(self.observer.idle)
        self.assertEqual(1, self.backend.actions.count('Disconnect Bluetooth device'))

    def test_preflight_and_recovery_closure_only_have_no_saved_pair_or_reads(self):
        for lane in ('preflight', 'recovery'):
            with self.subTest(lane=lane), tempfile.TemporaryDirectory() as directory:
                clock, backend = Clock(), None
                backend = FakeBackend(clock)
                observer = phone.PhoneObserver(backend, directory, clock=clock)
                result = observer.run(ReplayOwner, dict(CONTEXT), lane=lane)
                self.assertTrue(result['closed'])
                self.assertFalse(any(value in phone.READS.values() or value == 'Find device' for value in backend.actions))

    def test_context_and_duplicate_out_of_order_checkpoint_rejected(self):
        self.observer.context, self.observer.lane, self.observer.deadline = dict(CONTEXT), 'settings', 600
        message = {**CONTEXT, 'schema': 'OT0304-SETTINGS-CHECKPOINT-1', 'lane': 'settings',
                   'phase': 'name', 'token': '1'*32, 'remaining_seconds': 600, 'instruction': 'scoped'}
        with self.assertRaisesRegex(phone.PhoneError, 'owner_checkpoint_order'):
            self.observer.emit(json.dumps(message))
        message['phase'] = 'ready'
        self.observer.emit(json.dumps(message))
        with self.assertRaises(phone.PhoneError):
            self.observer.emit(json.dumps(message))
        self.assertEqual([], self.backend.actions)

    def test_one_use_observer(self):
        self.run_sequence()
        with self.assertRaisesRegex(phone.PhoneError, 'observer_reused'):
            self.run_sequence()

    def test_readback_predicates_do_not_accept_drafts_unknown_or_incomplete_public(self):
        self.assertIsNone(self.observer._value('region', {'Saved choice: Not read back'}))
        self.assertIsNone(self.observer._value('region', {'Saved choice: Unknown'}))
        self.assertIsNone(self.observer._value('public', {'Device readback: Private public name'}))
        self.assertIsNone(self.observer._value('name', {'Suggested name from the device you matched: Draft'}))


class AdbAdapterTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.adb, self.apk = self.directory/'adb.exe', self.directory/'test.apk'
        self.adb.write_bytes(b'host executable fixture')
        self.apk.write_bytes(b'APK fixture')
        self.clock = Clock()
        self.calls = []
        self.grant = {'schema': 'OT0304-PHONE-GRANT-1', 'scope': phone.SCOPE,
            **{key: CONTEXT[key] for key in ('attempt', 'request_sha256', 'challenge')}, 'lane': 'settings',
            'adb_sha256': phone.sha(self.adb.read_bytes()), 'apk_sha256': phone.sha(self.apk.read_bytes()),
            'serial_sha256': phone.sha(b'private-fixture-serial'), 'model': 'SM-N986U',
            'issued_utc': 1000, 'expires_utc': 2100, 'allow_idle_reopen': False}
        def runner(args, **kwargs):
            self.calls.append((args, kwargs))
            self.clock.advance(0.1)
            command = args[3:]
            if command == ['shell', 'getprop', 'ro.product.model']:
                raw = b'SM-N986U\n'
            elif command == ['shell', 'pm', 'path', phone.PACKAGE]:
                raw = b'package:/data/app/test/base.apk\n'
            elif command == ['exec-out', 'cat', '/data/app/test/base.apk']:
                raw = self.apk.read_bytes()
            elif command[:3] == ['shell', 'am', 'start']:
                raw = b'Status: ok\n'
            else:
                raw = ('mCurrentFocus=Window{ '+phone.ACTIVITY+' }\n').encode()
            return subprocess.CompletedProcess(args, 0, stdout=raw, stderr=b'')
        self.backend = phone.AdbPhoneBackend(self.adb, 'private-fixture-serial', self.apk, self.grant,
                        runner=runner, clock=self.clock, utc=lambda: 1000, sleep=self.clock.advance)

    def tearDown(self):
        self.temp.cleanup()

    def test_constructor_and_import_have_no_subprocess_or_device_operations(self):
        self.assertEqual([], self.calls)
        name = 'inert_phone_observer_test'
        spec = importlib.util.spec_from_file_location(name, ROOT/'tools/standard_settings_phone_observation.py')
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        try:
            with patch.object(subprocess, 'run', side_effect=AssertionError('import I/O')):
                spec.loader.exec_module(module)
        finally:
            sys.modules.pop(name, None)

    def test_authorization_revalidates_exact_model_apk_and_uses_selected_serial(self):
        self.assertTrue(self.backend.authorize(dict(CONTEXT), 'settings', 600))
        self.assertEqual(4, len(self.calls))
        for args, kwargs in self.calls:
            self.assertEqual([str(self.adb), '-s', 'private-fixture-serial'], args[:3])
            self.assertFalse(kwargs['shell'])
            self.assertLessEqual(kwargs['timeout'], 45)
        self.backend.launch(600)
        self.assertEqual(['shell', 'am', 'start', '-W', '-n', phone.ACTIVITY], self.calls[-1][0][3:])

    def test_missing_changed_expired_or_wrong_lane_grant_performs_no_adb(self):
        for field, value in (('scope', 'unscoped'), ('apk_sha256', '0'*64), ('challenge', '0'*32),
                             ('issued_utc', 900), ('expires_utc', 999), ('lane', 'recovery')):
            with self.subTest(field=field):
                changed = dict(self.grant)
                changed[field] = value
                backend = phone.AdbPhoneBackend(self.adb, 'private-fixture-serial', self.apk, changed,
                                runner=self.backend.runner, clock=self.clock, utc=lambda: 1000)
                with self.assertRaises(phone.PhoneError):
                    backend.authorize(dict(CONTEXT), 'settings', 600)
                self.assertEqual([], self.calls)

    def test_ungranted_backend_never_runs(self):
        with self.assertRaisesRegex(phone.PhoneError, 'phone_authority_required'):
            self.backend.launch(600)
        self.assertEqual([], self.calls)

    def test_command_timeout_is_remaining_global_deadline(self):
        self.backend.authorize(dict(CONTEXT), 'settings', 600)
        self.backend._run(['shell', 'getprop', 'ro.product.model'], self.clock()+2)
        self.assertEqual(2, self.calls[-1][1]['timeout'])

    def test_mismatched_model_and_installed_apk_refuse_before_launch(self):
        original = self.backend.runner
        for changed in ('model', 'installed'):
            with self.subTest(changed=changed):
                def bad(args, **kwargs):
                    result = original(args, **kwargs)
                    if changed == 'model' and args[3:] == ['shell', 'getprop', 'ro.product.model']:
                        result.stdout = b'Other phone'
                    if changed == 'installed' and args[3:] == ['exec-out', 'cat', '/data/app/test/base.apk']:
                        result.stdout = b'Other APK'
                    return result
                backend = phone.AdbPhoneBackend(self.adb, 'private-fixture-serial', self.apk, self.grant,
                                runner=bad, clock=self.clock, utc=lambda: 1000)
                with self.assertRaisesRegex(phone.PhoneError, 'phone_preflight_failed'):
                    backend.authorize(dict(CONTEXT), 'settings', 600)
                self.assertFalse(any('am' in args for args, _ in self.calls))

    def test_arbitrary_apply_save_pair_reset_and_device_settings_taps_are_forbidden(self):
        self.backend.authorize(dict(CONTEXT), 'settings', 600)
        frame = phone.Frame(0, self.clock(), xml_screen(), b'', b'')
        for label in ('Apply name', 'Apply region choice', 'Apply public settings', 'Add Device',
                      'Device settings', 'Factory reset', 'Save', 'Sync display clock'):
            with self.subTest(label=label), self.assertRaisesRegex(phone.PhoneError, 'control_not_scoped'):
                self.backend.tap(frame, label, 600)

    def idle_frames(self):
        trace = b'OTCL\t4\t71\n71\t0\tC\tDISCONNECTED\n'
        ui = xml_screen(('Bluetooth disconnected',))
        return (phone.Frame(0, 0.1, ui, trace, trace),
                phone.Frame(0.25, 0.35, ui, trace, trace))

    def prepared_reopen(self):
        self.grant['allow_idle_reopen'] = True
        self.backend.authorize(dict(CONTEXT), 'settings', 600)
        proofs = self.idle_frames()
        original = self.backend.runner
        def runner(args, **kwargs):
            if args[3:] == ['exec-out', 'run-as', phone.PACKAGE, 'cat', 'files/v1-connection-log']:
                self.calls.append((args, kwargs))
                self.clock.advance(0.1)
                return subprocess.CompletedProcess(args, 0, stdout=proofs[-1].trace_after, stderr=b'')
            return original(args, **kwargs)
        self.backend.runner = runner
        def capture(deadline):
            start = self.clock()
            self.clock.advance(0.1)
            return phone.Frame(start, self.clock(), proofs[-1].ui, proofs[-1].trace_before, proofs[-1].trace_after)
        self.backend.capture = capture
        return proofs

    def test_idle_reopen_requires_explicit_grant_before_any_force_stop(self):
        self.backend.authorize(dict(CONTEXT), 'settings', 600)
        with self.assertRaisesRegex(phone.PhoneError, 'phone_reopen_not_granted'):
            self.backend.reopen_after_idle(self.idle_frames(), 600)
        self.assertFalse(any('force-stop' in args for args, _ in self.calls))

    def test_idle_reopen_is_fixed_one_dispatch_after_fresh_artifact_verification(self):
        proofs = self.prepared_reopen()
        self.assertTrue(self.backend.reopen_after_idle(proofs, 600))
        commands = [args[3:] for args, _ in self.calls]
        self.assertEqual(1, commands.count(['shell', 'am', 'force-stop', phone.PACKAGE]))
        self.assertEqual(1, commands.count(['shell', 'am', 'start', '-W', '-n', phone.ACTIVITY]))
        self.assertEqual(2, commands.count(['shell', 'getprop', 'ro.product.model']))
        with self.assertRaisesRegex(phone.PhoneError, 'phone_reopen_not_granted'):
            self.backend.reopen_after_idle(proofs, 600)

    def test_reopen_rejects_no_idle_held_reconnect_and_changed_apk_without_mutation(self):
        proofs = self.prepared_reopen()
        wrong = phone.Frame(proofs[-1].started, proofs[-1].ended,
            xml_screen(('Reconnecting',), ('Stop reconnecting',)), proofs[-1].trace_before, proofs[-1].trace_after)
        with self.assertRaisesRegex(phone.PhoneError, 'phone_idle_proof_invalid'):
            self.backend.reopen_after_idle((proofs[0], wrong), 600)
        self.apk.write_bytes(b'changed local APK')
        with self.assertRaisesRegex(phone.PhoneError, 'phone_artifact_changed'):
            self.backend.reopen_after_idle(proofs, 600)
        self.assertFalse(any('force-stop' in args for args, _ in self.calls))

    def test_uncertain_production_force_stop_never_replays(self):
        proofs = self.prepared_reopen()
        original = self.backend.runner
        def uncertain(args, **kwargs):
            if args[3:] == ['shell', 'am', 'force-stop', phone.PACKAGE]:
                self.calls.append((args, kwargs))
                raise subprocess.TimeoutExpired(args, kwargs['timeout'])
            return original(args, **kwargs)
        self.backend.runner = uncertain
        with self.assertRaisesRegex(phone.PhoneError, 'phone_command_failed'):
            self.backend.reopen_after_idle(proofs, 600)
        with self.assertRaisesRegex(phone.PhoneError, 'phone_reopen_not_granted'):
            self.backend.reopen_after_idle(proofs, 600)
        self.assertEqual(1, sum('force-stop' in args for args, _ in self.calls))
        self.assertFalse(any(args[3:6] == ['shell', 'am', 'start'] for args, _ in self.calls))


class TraceTests(unittest.TestCase):
    def test_current_schema4_full_protected_sequence_only(self):
        clock, backend = Clock(), None
        backend = FakeBackend(clock)
        backend._ready_trace()
        trace = phone.Trace.parse(backend.trace())
        self.assertEqual((71, 5, 3), trace.identity())
        for key in ('PROTECTED_PROTOCOL_INFO_ACCEPTED', 'ATT_MTU_NEGOTIATED',
                    'STREAM_SUBSCRIPTION_ESTABLISHED', 'AUTHORIZATION_ACCEPTED', 'SNAPSHOT_ACCEPTED'):
            raw = backend.trace().replace(key.encode(), b'AUTHORIZATION_PENDING')
            with self.subTest(key=key), self.assertRaises(phone.PhoneError):
                phone.Trace.parse(raw).identity()

    def test_noncurrent_history_may_be_trimmed_but_current_trace_gaps_refuse(self):
        raw = b'OTCL\t4\t71\n70\t0\tS\tREADY_REACHED\t1\t20\t1\t0\t0\tNONE\tUNAVAILABLE\n71\t0\tC\tDISCONNECTED\n'
        self.assertEqual(71, phone.Trace.parse(raw).session)
        with self.assertRaisesRegex(phone.PhoneError, 'trace_gap'):
            phone.Trace.parse(raw.replace(b'70\t0\tS', b'71\t0\tS'))

    def test_trace_rollback_session_replacement_and_truncation_refuse(self):
        for raw in (b'OTCL\t3\t71\n71\t0\tC\tREADY\n', b'OTCL\t4\t71\n71\t0\tC\tREADY',
                    b'OTCL\t4\t71\n71\t2\tC\tREADY\n71\t1\tC\tDISCONNECTED\n'):
            with self.subTest(raw=raw), self.assertRaises(phone.PhoneError):
                phone.Trace.parse(raw)
        old = phone.Trace.parse(b'OTCL\t4\t71\n71\t0\tC\tDISCONNECTED\n')
        new = phone.Trace.parse(b'OTCL\t4\t72\n72\t0\tC\tDISCONNECTED\n')
        with self.assertRaisesRegex(phone.PhoneError, 'trace_changed'):
            new.extends(old)


if __name__ == '__main__':
    unittest.main()
