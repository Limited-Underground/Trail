"""Actual private view and Win32 dispatch/cleanup with synthetic native APIs.

No native window is opened. Invented identity rows and explicit fake clicks
exercise host ownership only, not physical observation or authentication.
"""
from dataclasses import replace
import builtins
import importlib
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import enrollment_candidate_controller as core
import enrollment_candidate_operator as operator
import enrollment_candidate_private_view as private
import enrollment_candidate_operator_tests as prior


class Clock:
    def __init__(self): self.value = 10.0
    def __call__(self): return self.value


def prompt(kind='transcript', token='1' * 32, deadline=100):
    roles = ('A',) if kind in ('fingerprint', 'reset_gesture') else ('A', 'B')
    refs = prior.references()
    if kind == 'fingerprint_local': rows = refs.local()
    elif kind == 'fingerprint':
        refs.confirmed('fingerprint_local', ('A', 'B'))
        rows = refs.peer('A')
    else: rows = ()
    return operator.PrivatePrompt(kind, roles, token, deadline,
        ('Compare complete displayed values and acknowledge only your observation.',), 17, rows)


class Backend:
    def __init__(self, clock):
        self.clock, self.text = clock, None
        self.events, self.calls = [], []
        self.open_fault, self.poll_fault, self.clear_fault = None, None, None
        self.advance, self.reenter = None, None
    def open(self, text):
        self.calls.append('open'); self.text = text
        if self.reenter: self.reenter()
        if self.open_fault == 'raise': raise OSError('synthetic PRIVATE native text')
        return self.open_fault != 'false'
    def poll(self):
        self.calls.append('poll')
        if self.advance is not None: self.clock.value = self.advance
        if self.poll_fault: raise OSError('synthetic PRIVATE poll detail')
        return self.events.pop(0) if self.events else None
    def clear(self):
        self.calls.append('clear')
        if self.clear_fault == 'raise': raise OSError('synthetic PRIVATE clear detail')
        if self.clear_fault == 'false': return False
        self.text = None
        return True


class NativeAPI:
    """Only API effects are doubled; actual Win32 backend owns sequencing."""
    def __init__(self):
        self.trace, self.windows, self.messages = [], {}, []
        self.next_handle, self.last_error, self.affinity = 100, 0, 0
        self.callback_value, self.fail, self.create_count = None, None, 0
        self.c = SimpleNamespace(byref=lambda value: value,
            set_last_error=lambda value: setattr(self, 'last_error', value),
            get_last_error=lambda: self.last_error,
            create_unicode_buffer=lambda size: SimpleNamespace(value=''))
        self.w = SimpleNamespace(DWORD=lambda: SimpleNamespace(value=0))
        self.WNDCLASS = lambda *args: SimpleNamespace(procedure=args[1])
        self.RECT = lambda: SimpleNamespace(left=0, top=0, right=0, bottom=0)
        self.MSG = lambda: SimpleNamespace()
        self.callback = lambda value: value
        self.kernel = SimpleNamespace(GetModuleHandleW=lambda unused: 42)
        self.gdi = SimpleNamespace(CreateFontW=self.font, DeleteObject=self.delete_font)
        self.user = SimpleNamespace(RegisterClassW=self.register, UnregisterClassW=self.unregister,
            SystemParametersInfoW=self.area, CreateWindowExW=self.create,
            SetWindowDisplayAffinity=self.set_affinity, GetWindowDisplayAffinity=self.get_affinity,
            SetWindowTextW=self.set_text, GetWindowTextLengthW=self.text_length,
            GetWindowTextW=self.get_text, DestroyWindow=self.destroy,
            IsWindow=lambda h: h in self.windows and self.windows[h]['alive'],
            IsWindowVisible=lambda h: self.windows[h]['visible'],
            ShowWindow=self.show, SendMessageW=lambda *args: 0,
            DefWindowProcW=lambda *args: 77, PeekMessageW=self.peek,
            TranslateMessage=lambda message: True, DispatchMessageW=self.dispatch)
    def register(self, klass):
        self.callback_value = klass.procedure; self.trace.append('register'); return 1
    def unregister(self, *args):
        self.trace.append('unregister'); return self.fail != 'unregister'
    def area(self, action, ignored, area, flags):
        area.right, area.bottom = (800, 600) if self.fail == 'small-display' else (1200, 900)
        return True
    def create(self, exstyle, name, text, style, x, y, width, height, parent, ident, instance, unused):
        self.create_count += 1
        if self.fail == 'create-' + str(self.create_count): return 0
        self.next_handle += 1; handle = self.next_handle
        self.windows[handle] = dict(parent=parent, text=text, alive=True, visible=False)
        self.trace.append(('create', handle)); return handle
    def set_affinity(self, handle, value):
        self.trace.append('set-affinity'); self.affinity = value
        return self.fail != 'set-affinity'
    def get_affinity(self, handle, value):
        self.trace.append('get-affinity'); value.value = 0 if self.fail == 'affinity-readback' else self.affinity
        return True
    def font(self, *args): self.trace.append('font'); return 55
    def delete_font(self, handle): self.trace.append('delete-font'); return self.fail != 'font-delete'
    def set_text(self, handle, value):
        self.trace.append(('set-text', handle, value == ''))
        if value and self.fail == 'paint': return False
        if not value and self.fail == 'clear-text': return False
        self.windows[handle]['text'] = value; return True
    def text_length(self, handle):
        if self.fail == 'clear-read-error': self.last_error = 5
        return len(self.windows[handle]['text'])
    def get_text(self, handle, buffer, size):
        buffer.value = self.windows[handle]['text'][:size - 1]
        return len(buffer.value)
    def show(self, handle, mode):
        self.trace.append('show'); self.windows[handle]['visible'] = True; return True
    def destroy(self, handle):
        self.trace.append('destroy')
        if self.fail == 'destroy': return False
        for value, state in self.windows.items():
            if value == handle or state['parent'] == handle: state['alive'] = False
        self.callback_value(handle, 0x0082, 0, 0)
        return True
    def peek(self, message, handle, low, high, flags):
        if not self.messages: return False
        message.hwnd, message.message, message.wparam, message.lparam = self.messages.pop(0)
        return True
    def dispatch(self, message):
        return self.callback_value(message.hwnd, message.message, message.wparam, message.lparam)


class PrivateViewTests(unittest.TestCase):
    def fixture(self):
        clock, made = Clock(), []
        def factory():
            backend = Backend(clock); made.append(backend); return backend
        return private.WindowsPrivateView(backend_factory=factory, monotonic=clock), clock, made

    def test_import_and_construction_are_lazy(self):
        original_import = builtins.__import__
        def imported(name, *args, **kwargs):
            if name.split('.')[0] in ('ctypes', 'serial', 'esptool'):
                self.fail('native or hardware dependency imported eagerly')
            return original_import(name, *args, **kwargs)
        with patch('builtins.__import__', side_effect=imported):
            importlib.reload(private)
        with patch.object(private, '_Win32Api', side_effect=AssertionError('native API during construction')):
            private.WindowsPrivateView()
            private._Win32Backend()
        view, clock, made = self.fixture()
        self.assertEqual(made, [])
        self.assertTrue(view.close()); self.assertEqual(made, [])

    def test_saved_own_rows_render_privately_and_ack_follows_verified_clear(self):
        view, clock, made = self.fixture()
        p = prompt('fingerprint_local')
        self.assertTrue(view.show(p)); b = made[0]
        self.assertIn('G:0000000000000011', b.text)
        for role, rows in p.references:
            self.assertIn('Device ' + role + ' saved OWN reference:', b.text)
            self.assertTrue(all(row in b.text for row in rows))
        self.assertNotIn(p.token, b.text)
        self.assertIsNone(view.poll())
        b.events.append('confirm'); ack = view.poll()
        self.assertEqual(ack, core.CheckpointAck('OT-CANDIDATE-ACK-1', p.kind, p.roles, p.token))
        self.assertIs(type(ack), core.CheckpointAck)
        self.assertIsNone(b.text); self.assertEqual(b.calls.count('clear'), 1)
        self.assertTrue(view.close()); self.assertEqual(b.calls.count('clear'), 1)
        self.assertIsNone(view.poll())
        next_point = prompt('fingerprint', token='2' * 32)
        self.assertTrue(view.show(next_point)); made[1].events.append('confirm')
        next_ack = view.poll()
        self.assertEqual((next_ack.kind, next_ack.roles, next_ack.token), ('fingerprint', ('A',), '2' * 32))
        self.assertNotEqual(next_ack, ack)

    def test_prompt_shape_and_reference_topology_are_rejected_before_native_open(self):
        p = prompt('fingerprint_local')
        changes = [dict(roles=('B', 'A')), dict(token='A' * 32), dict(group=True),
            dict(deadline=float('nan')), dict(instructions=('bad\nline',)),
            dict(references=p.references[::-1]), dict(references=(('A', ('a' * 16,) * 4), p.references[1])),
            dict(kind='unknown'), dict(references=())]
        for change in changes:
            view, clock, made = self.fixture()
            with self.subTest(change=tuple(change)), self.assertRaises(private.ViewError): view.show(replace(p, **change))
            self.assertEqual(made, [])
            self.assertTrue(view.close())

    def test_cancel_bad_event_deadline_and_clock_rollback_never_ack(self):
        for fault in ('cancel', 'bad-event', 'poll-error', 'expired', 'late-confirm', 'rollback'):
            view, clock, made = self.fixture(); view.show(prompt()); b = made[0]
            b.events.append('confirm')
            if fault == 'cancel': b.events[0] = 'cancel'
            if fault == 'bad-event': b.events[0] = True
            if fault == 'poll-error': b.poll_fault = True
            if fault == 'expired': clock.value = 100
            if fault == 'late-confirm': b.advance = 100
            if fault == 'rollback': clock.value = 9
            with self.subTest(fault=fault), self.assertRaisesRegex(private.ViewError, '^private_view_refused$'): view.poll()
            self.assertEqual(b.calls.count('clear'), 1); self.assertIsNone(b.text)
            with self.assertRaises(private.ViewError): view.show(prompt())
            self.assertEqual(len(made), 1)

    def test_partial_open_failed_clear_and_reentry_are_sticky_without_retry(self):
        for fault in ('open-false', 'open-raise', 'clear-false', 'clear-raise', 'reentry'):
            clock, b = Clock(), None
            backend = Backend(clock)
            view = private.WindowsPrivateView(backend_factory=lambda: backend, monotonic=clock)
            if fault.startswith('open-'): backend.open_fault = fault.split('-')[1]
            if fault.startswith('clear-'): backend.clear_fault = fault.split('-')[1]
            if fault == 'reentry': backend.reenter = view.poll
            with self.subTest(fault=fault):
                if fault.startswith('clear-'):
                    view.show(prompt()); backend.events.append('confirm')
                    with self.assertRaises(private.ViewError): view.poll()
                    self.assertFalse(view.close())
                else:
                    with self.assertRaisesRegex(private.ViewError, '^private_view_open_failed$'): view.show(prompt())
                    self.assertTrue(view.close())
                self.assertEqual(backend.calls.count('clear'), 1)
                with self.assertRaises(private.ViewError): view.show(prompt())
                self.assertEqual(backend.calls.count('clear'), 1)

    def test_actual_controller_records_exact_ack_after_native_clear(self):
        f = prior.Fixture(); self.addCleanup(f.cleanup)
        f.activate(); self.assertEqual(f.run().outcome, 'passed')
        view, clock, made = self.fixture(); events = []
        original_factory = view._factory
        def factory():
            b = original_factory(); b.events.append('confirm'); return b
        view._factory = factory
        ui = operator.CheckpointUI(operator.CapturedReferences(), view, group=17, monotonic=clock)
        def record(event):
            if event.get('schema') == 'OT-CANDIDATE-ACK-1':
                self.assertIsNone(made[0].text)
            events.append(event); return True
        controller = f.instance.controller
        controller.human, controller.record = ui, record
        controller.tokens = lambda: 'a' * 32
        self.assertNotIn('a' * 32, controller.seen_tokens)
        commands = list(f.world.commands())
        ack = controller.confirm_original(100)
        self.assertEqual(ack, core.CheckpointAck('OT-CANDIDATE-ACK-1', 'usual_screen', ('A', 'B'), 'a' * 32))
        self.assertEqual([e['schema'] for e in events], ['OT-CANDIDATE-CHECKPOINT-1', 'OT-CANDIDATE-ACK-1'])
        self.assertFalse(any('references' in e for e in events))
        self.assertEqual(f.world.commands(), commands)

    def test_actual_native_paint_is_after_capture_setting_and_cleanup_reads_all_text(self):
        api = NativeAPI(); backend = private._Win32Backend(api_factory=lambda: api)
        self.assertTrue(backend.open('SYNTHETIC PRIVATE ROWS'))
        paint = next(i for i, event in enumerate(api.trace) if type(event) is tuple and event[0] == 'set-text' and event[2] is False)
        self.assertLess(api.trace.index('get-affinity'), paint)
        self.assertLess(paint, api.trace.index('show'))
        handles = list(api.windows)
        self.assertTrue(backend.clear())
        self.assertTrue(all(api.windows[h]['text'] == '' and not api.windows[h]['alive'] for h in handles))
        self.assertEqual(sum(event == 'destroy' for event in api.trace), 1)
        trace = list(api.trace); self.assertTrue(backend.clear()); self.assertEqual(api.trace, trace)
        self.assertIn('delete-font', trace); self.assertIn('unregister', trace)
        self.assertNotIn(backend, private._LIVE_NATIVE)

    def test_native_exact_click_close_and_window_loss_dispatch(self):
        for event in ('confirm', 'cancel', 'close', 'destroyed', 'wrong-click', 'wrong-thread'):
            api = NativeAPI(); thread = [1]
            backend = private._Win32Backend(api_factory=lambda: api, thread_id=lambda: thread[0])
            backend.open('SYNTHETIC PRIVATE ROWS'); root = backend._window
            if event == 'confirm': args = (root, 0x111, 1001, backend._confirm)
            elif event == 'cancel': args = (root, 0x111, 1002, backend._cancel)
            elif event == 'close': args = (root, 0x10, 0, 0)
            elif event == 'destroyed': args = (root, 0x82, 0, 0)
            elif event == 'wrong-click': args = (root, 0x111, 1001 | (1 << 16), backend._confirm)
            else:
                thread[0] = 2; args = (root, 0x111, 1001, backend._confirm)
            api.messages.append(args)
            with self.subTest(event=event):
                if event == 'wrong-thread':
                    self.assertEqual(backend._dispatch(*args), 0); self.assertEqual(backend._event, 'error')
                    thread[0] = 1
                else:
                    observed = backend.poll()
                    self.assertEqual(observed, 'confirm' if event == 'confirm' else None if event == 'wrong-click' else 'cancel')
                self.assertTrue(backend.clear())

    def test_partial_native_open_and_clear_faults_never_promote_ack(self):
        for fault in ('small-display', 'create-2', 'set-affinity', 'affinity-readback', 'paint',
                      'clear-text', 'clear-read-error', 'destroy', 'font-delete', 'unregister'):
            api = NativeAPI(); api.fail = fault
            native = private._Win32Backend(api_factory=lambda: api)
            self.addCleanup(private._LIVE_NATIVE.discard, native)
            clock = Clock()
            view = private.WindowsPrivateView(backend_factory=lambda: native, monotonic=clock)
            with self.subTest(fault=fault):
                if fault in ('clear-text', 'clear-read-error', 'destroy', 'font-delete', 'unregister'):
                    view.show(prompt()); api.messages.append((native._window, 0x111, 1001, native._confirm))
                    with self.assertRaises(private.ViewError): view.poll()
                    self.assertFalse(view.close())
                else:
                    with self.assertRaises(private.ViewError): view.show(prompt())
                    self.assertTrue(view.close())
                trace = list(api.trace)
                view.close(); self.assertEqual(api.trace, trace)
                self.assertLessEqual(sum(event == 'destroy' for event in api.trace), 1)
                if fault == 'destroy':
                    self.assertIn(native, private._LIVE_NATIVE)
                    self.assertIsNotNone(native._callback)
                if fault == 'unregister':
                    self.assertIn(native, private._LIVE_NATIVE)
                    self.assertTrue(native._registered)
                    self.assertIsNotNone(native._callback)
                if fault == 'font-delete':
                    self.assertIn(native, private._LIVE_NATIVE)
                    self.assertIsNotNone(native._font)


if __name__ == '__main__': unittest.main(verbosity=2)
