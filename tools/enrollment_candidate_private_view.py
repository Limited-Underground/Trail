"""Transient Windows checkpoint view; import and construction are inert.

The admitted operator owns the prompt, target sampling and durable records.
Only an explicit local button click can acknowledge this view. Private rows
are never printed, copied to the clipboard, written to disk or sent elsewhere.
Capture exclusion is an OS setting, not protection against every capture.
Python and Windows do not promise physical erasure of copied display strings.
"""
import math
import os
import re
import textwrap
import threading
import time
import uuid

from enrollment_candidate_controller import CheckpointAck, Clock, ROLES
from enrollment_candidate_operator import PrivatePrompt


# Retain native callbacks until every window and registered class is retired.
# An uncertain cleanup must not allow garbage collection of an OS callback.
_LIVE_NATIVE = set()


class ViewError(RuntimeError):
    """Fixed categories only, without prompt or native error details."""


def _need(value, category):
    if not value:
        raise ViewError(category)


def _prompt_text(prompt):
    _need(type(prompt) is PrivatePrompt, 'private_prompt_invalid')
    kinds = {'fingerprint_local': ROLES, 'transcript': ROLES,
             'reset_gesture': ('A',), 'usual_screen': ROLES}
    _need(prompt.kind == 'fingerprint' and prompt.roles in (('A',), ('B',))
          or prompt.kind in kinds and prompt.roles == kinds[prompt.kind],
          'private_prompt_invalid')
    _need(type(prompt.token) is str and re.fullmatch('[0-9a-f]{32}', prompt.token)
          and type(prompt.group) is int and 0 < prompt.group < 1 << 64
          and type(prompt.deadline) in (int, float) and math.isfinite(prompt.deadline),
          'private_prompt_invalid')
    _need(type(prompt.instructions) is tuple and 1 <= len(prompt.instructions) <= 4
          and all(type(value) is str and 1 <= len(value) <= 256
                  and all(32 <= ord(char) <= 126 for char in value)
                  for value in prompt.instructions), 'private_prompt_invalid')
    expected = ROLES if prompt.kind == 'fingerprint_local' else (
        ('B' if prompt.roles == ('A',) else 'A',) if prompt.kind == 'fingerprint' else ())
    _need(type(prompt.references) is tuple and len(prompt.references) == len(expected),
          'private_prompt_invalid')
    for role, entry in zip(expected, prompt.references):
        _need(type(entry) is tuple and len(entry) == 2 and entry[0] == role
              and type(entry[1]) is tuple and len(entry[1]) == 4
              and all(type(row) is str and re.fullmatch('[0-9A-F]{16}', row)
                      for row in entry[1]), 'private_prompt_invalid')
    lines = ['PRIVATE ENROLLMENT CHECKPOINT', '',
             'Checkpoint: ' + prompt.kind.upper(),
             'Roles: ' + ', '.join(prompt.roles) + '    Device group G:' + format(prompt.group, '016X'), '']
    for instruction in prompt.instructions:
        lines.extend(textwrap.wrap(instruction, width=76))
        lines.append('')
    for role, rows in prompt.references:
        lines.append('Device ' + role + ' saved OWN reference:')
        lines.extend(rows)
        lines.append('')
    lines.append('Confirm only your actual observation. Cancel refuses the checkpoint.')
    text = '\r\n'.join(lines)
    _need(len(text) <= 4096 and len(lines) <= 30, 'private_prompt_invalid')
    return text


class WindowsPrivateView:
    """Frozen PrivateView ABI with lazy, injectable native presentation.

    backend_factory produces open(text), poll() and clear() methods. poll()
    returns None, 'confirm' or 'cancel'; only positively verified clear() ==
    True permits acknowledgement. The default backend binds Win32 on open.
    All operations belong to the construction thread. Cleanup is attempted
    once per presentation; uncertain clearing is sticky and cannot be retried.
    """
    def __init__(self, *, backend_factory=None, monotonic=time.monotonic):
        self._factory = _Win32Backend if backend_factory is None else backend_factory
        self._clock, self._thread = Clock(monotonic), threading.get_ident()
        self._backend, self._point = None, None
        self._busy, self._failed = False, False
        self._clear_attempted, self._clear_verified = False, True

    def _enter(self):
        if self._busy or threading.get_ident() != self._thread:
            self._failed = True
            raise ViewError('private_view_thread_or_reentry')
        self._busy = True

    def _clear_once(self):
        self._point = None
        if self._clear_attempted:
            return self._clear_verified
        self._clear_attempted, self._clear_verified = True, False
        try:
            self._clear_verified = self._backend is None or self._backend.clear() is True
        except BaseException:
            self._clear_verified = False
        if self._clear_verified:
            self._backend = None
        else:
            self._failed = True
        return self._clear_verified

    def show(self, prompt):
        self._enter()
        try:
            _need(not self._failed and self._point is None and self._clear_verified,
                  'private_view_terminal')
            text = _prompt_text(prompt)
            self._clock.check(prompt.deadline)
            self._clear_attempted, self._clear_verified = False, False
            self._point = prompt
            self._backend = self._factory()
            _need(self._backend.open(text) is True, 'private_view_open_failed')
            text = None
            self._clock.check(prompt.deadline)
            _need(not self._failed, 'private_view_thread_or_reentry')
            return True
        except BaseException:
            self._failed = True
            self._clear_once()
            raise ViewError('private_view_open_failed') from None
        finally:
            self._busy = False

    def poll(self):
        self._enter()
        try:
            _need(not self._failed, 'private_view_terminal')
            if self._point is None:
                return None
            point = self._point
            self._clock.check(point.deadline)
            event = self._backend.poll()
            self._clock.check(point.deadline)
            _need(not self._failed, 'private_view_thread_or_reentry')
            if event is None:
                return None
            _need(event == 'confirm', 'private_view_cancelled')
            ack = CheckpointAck('OT-CANDIDATE-ACK-1', point.kind, point.roles, point.token)
            _need(self._clear_once(), 'private_view_not_cleared')
            self._clock.check(point.deadline)
            _need(not self._failed, 'private_view_thread_or_reentry')
            return ack
        except BaseException:
            self._failed = True
            self._clear_once()
            raise ViewError('private_view_refused') from None
        finally:
            self._busy = False

    def close(self):
        try:
            self._enter()
        except ViewError:
            return False
        try:
            return self._clear_once()
        finally:
            self._busy = False


class _Win32Api:
    """64-bit-safe native signatures, loaded only for an admitted show()."""
    def __init__(self):
        _need(os.name == 'nt', 'native_view_unavailable')
        import ctypes
        from ctypes import wintypes as w
        self.c, self.w = ctypes, w
        self.user = ctypes.WinDLL('user32', use_last_error=True)
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        self.gdi = ctypes.WinDLL('gdi32', use_last_error=True)
        self.callback = ctypes.WINFUNCTYPE(ctypes.c_ssize_t, w.HWND, w.UINT,
                                          ctypes.c_size_t, ctypes.c_ssize_t)
        callback = self.callback
        class WNDCLASS(ctypes.Structure):
            _fields_ = [('style', w.UINT), ('procedure', callback), ('class_extra', ctypes.c_int),
                        ('window_extra', ctypes.c_int), ('instance', w.HINSTANCE),
                        ('icon', w.HICON), ('cursor', w.HANDLE), ('background', w.HBRUSH),
                        ('menu', w.LPCWSTR), ('name', w.LPCWSTR)]
        self.WNDCLASS, self.MSG, self.RECT = WNDCLASS, w.MSG, w.RECT
        def bind(library, name, result, *args):
            value = getattr(library, name)
            value.restype, value.argtypes = result, list(args)
        bind(self.kernel, 'GetModuleHandleW', w.HMODULE, w.LPCWSTR)
        bind(self.user, 'RegisterClassW', w.WORD, ctypes.POINTER(WNDCLASS))
        bind(self.user, 'UnregisterClassW', w.BOOL, w.LPCWSTR, w.HINSTANCE)
        bind(self.user, 'CreateWindowExW', w.HWND, w.DWORD, w.LPCWSTR, w.LPCWSTR,
             w.DWORD, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
             w.HWND, w.HMENU, w.HINSTANCE, w.LPVOID)
        bind(self.user, 'DefWindowProcW', ctypes.c_ssize_t, w.HWND, w.UINT,
             ctypes.c_size_t, ctypes.c_ssize_t)
        bind(self.user, 'SetWindowDisplayAffinity', w.BOOL, w.HWND, w.DWORD)
        bind(self.user, 'GetWindowDisplayAffinity', w.BOOL, w.HWND, ctypes.POINTER(w.DWORD))
        bind(self.user, 'SetWindowTextW', w.BOOL, w.HWND, w.LPCWSTR)
        bind(self.user, 'GetWindowTextLengthW', ctypes.c_int, w.HWND)
        bind(self.user, 'GetWindowTextW', ctypes.c_int, w.HWND, w.LPWSTR, ctypes.c_int)
        bind(self.user, 'DestroyWindow', w.BOOL, w.HWND)
        bind(self.user, 'IsWindow', w.BOOL, w.HWND)
        bind(self.user, 'IsWindowVisible', w.BOOL, w.HWND)
        bind(self.user, 'ShowWindow', w.BOOL, w.HWND, ctypes.c_int)
        bind(self.user, 'SendMessageW', ctypes.c_ssize_t, w.HWND, w.UINT,
             ctypes.c_size_t, ctypes.c_ssize_t)
        bind(self.user, 'PeekMessageW', w.BOOL, ctypes.POINTER(w.MSG), w.HWND,
             w.UINT, w.UINT, w.UINT)
        bind(self.user, 'TranslateMessage', w.BOOL, ctypes.POINTER(w.MSG))
        bind(self.user, 'DispatchMessageW', ctypes.c_ssize_t, ctypes.POINTER(w.MSG))
        bind(self.user, 'SystemParametersInfoW', w.BOOL, w.UINT, w.UINT, w.LPVOID, w.UINT)
        bind(self.gdi, 'CreateFontW', w.HANDLE, ctypes.c_int, ctypes.c_int, ctypes.c_int,
             ctypes.c_int, ctypes.c_int, w.DWORD, w.DWORD, w.DWORD, w.DWORD,
             w.DWORD, w.DWORD, w.DWORD, w.DWORD, w.LPCWSTR)
        bind(self.gdi, 'DeleteObject', w.BOOL, w.HANDLE)


class _Win32Backend:
    """One UI-thread-owned native window; injectable API for focused tests."""
    def __init__(self, *, api_factory=None, thread_id=threading.get_ident):
        self._api_factory = _Win32Api if api_factory is None else api_factory
        self._thread_id, self._thread = thread_id, thread_id()
        self._api, self._window, self._font = None, None, None
        self._children, self._callback = [], None
        self._confirm, self._cancel = None, None
        self._event, self._ready, self._destroyed = None, False, False
        self._registered, self._clear_attempted, self._cleared = False, False, False
        self._name, self._instance = None, None

    def _dispatch(self, hwnd, message, wparam, lparam):
        try:
            _need(self._thread_id() == self._thread, 'native_view_thread')
            if message == 0x0010:  # WM_CLOSE: X never acknowledges.
                self._event = 'cancel'
                return 0
            if message == 0x0082:  # WM_NCDESTROY
                self._destroyed = True
                if not self._clear_attempted:
                    self._event = 'cancel'
            if message == 0x0111 and self._ready:  # WM_COMMAND / BN_CLICKED
                if (wparam >> 16) == 0 and lparam == self._confirm and (wparam & 0xffff) == 1001:
                    self._event = 'confirm'
                    return 0
                if (wparam >> 16) == 0 and lparam == self._cancel and (wparam & 0xffff) == 1002:
                    self._event = 'cancel'
                    return 0
            return self._api.user.DefWindowProcW(hwnd, message, wparam, lparam)
        except BaseException:
            self._event = 'error'
            return 0

    def open(self, text):
        _need(self._thread_id() == self._thread and self._api is None, 'native_view_used')
        self._api = a = self._api_factory()
        c, u = a.c, a.user
        self._instance = a.kernel.GetModuleHandleW(None)
        _need(self._instance, 'native_view_open_failed')
        self._name = 'OTCandidatePrivateView_' + uuid.uuid4().hex
        self._callback = a.callback(self._dispatch)
        klass = a.WNDCLASS(0, self._callback, 0, 0, self._instance, None, None, 6, None, self._name)
        _need(u.RegisterClassW(c.byref(klass)), 'native_view_open_failed')
        self._registered = True
        _LIVE_NATIVE.add(self)
        area = a.RECT()
        _need(u.SystemParametersInfoW(0x0030, 0, c.byref(area), 0), 'native_view_open_failed')
        width, height = 900, 700
        _need(area.right - area.left >= width and area.bottom - area.top >= height,
              'native_view_display_too_small')
        x, y = area.left + (area.right - area.left - width) // 2, area.top + (area.bottom - area.top - height) // 2
        self._window = u.CreateWindowExW(8, self._name, 'Private enrollment checkpoint',
            0x00c80000, x, y, width, height, None, None, self._instance, None)
        _need(self._window, 'native_view_open_failed')
        # The root is still hidden and contains no private rows at this point.
        _need(u.SetWindowDisplayAffinity(self._window, 0x11), 'native_capture_exclusion_failed')
        affinity = a.w.DWORD()
        _need(u.GetWindowDisplayAffinity(self._window, c.byref(affinity)) and affinity.value == 0x11,
              'native_capture_exclusion_failed')
        self._font = a.gdi.CreateFontW(-18, 0, 0, 0, 400, 0, 0, 0, 1, 0, 0, 0, 0, 'Consolas')
        _need(self._font, 'native_view_open_failed')
        for name, ident, caption, bx, by, bw, bh in (
                ('STATIC', 1000, '', 24, 20, 850, 565),
                ('BUTTON', 1001, 'I checked this checkpoint', 220, 610, 280, 34),
                ('BUTTON', 1002, 'Cancel', 530, 610, 140, 34)):
            handle = u.CreateWindowExW(0, name, caption, 0x50000000,
                bx, by, bw, bh, self._window, ident, self._instance, None)
            _need(handle, 'native_view_open_failed')
            self._children.append(handle)
            u.SendMessageW(handle, 0x0030, self._font, 0)  # WM_SETFONT
        self._confirm, self._cancel = self._children[1:]
        _need(u.SetWindowTextW(self._children[0], text), 'native_view_open_failed')
        text = None
        self._ready = True
        u.ShowWindow(self._window, 5)
        _need(u.IsWindowVisible(self._window) and not self._destroyed and self._event is None,
              'native_view_open_failed')
        return True

    def poll(self):
        _need(self._thread_id() == self._thread and not self._clear_attempted,
              'native_view_thread_or_closed')
        a = self._api
        _need(a is not None and a.user.IsWindow(self._window), 'native_view_lost')
        message = a.MSG()
        for _ in range(32):
            if not a.user.PeekMessageW(a.c.byref(message), self._window, 0, 0, 1):
                break
            a.user.TranslateMessage(a.c.byref(message))
            a.user.DispatchMessageW(a.c.byref(message))
            if self._event is not None:
                break
        return self._event

    def clear(self):
        if self._clear_attempted:
            return self._cleared
        self._clear_attempted, self._ready = True, False
        if self._thread_id() != self._thread:
            return False
        a = self._api
        if a is None:
            self._cleared = True
            return True
        okay = True
        handles = self._children + ([self._window] if self._window else [])
        for handle in handles:
            try:
                if a.user.IsWindow(handle):
                    a.c.set_last_error(0)
                    emptied = a.user.SetWindowTextW(handle, '')
                    a.c.set_last_error(0)
                    length = a.user.GetWindowTextLengthW(handle)
                    readable = a.c.get_last_error() == 0
                    buffer = a.c.create_unicode_buffer(2)
                    a.c.set_last_error(0)
                    count = a.user.GetWindowTextW(handle, buffer, 2)
                    okay = bool(emptied and readable and length == 0 and count == 0
                                and a.c.get_last_error() == 0 and buffer.value == '') and okay
            except BaseException:
                okay = False
        try:
            if self._window and a.user.IsWindow(self._window):
                okay = bool(a.user.DestroyWindow(self._window)) and okay
            okay = all(not a.user.IsWindow(handle) for handle in handles) and okay
        except BaseException:
            okay = False
        # Keep callback/font/class alive if any window may still use them.
        if any(a.user.IsWindow(handle) for handle in handles):
            return False
        try:
            if self._font:
                deleted = bool(a.gdi.DeleteObject(self._font))
                okay = deleted and okay
                if deleted:
                    self._font = None
            if self._registered:
                unregistered = bool(a.user.UnregisterClassW(self._name, self._instance))
                okay = unregistered and okay
                if unregistered:
                    self._registered = False
        except BaseException:
            okay = False
        self._event, self._children = None, []
        self._window, self._confirm, self._cancel = None, None, None
        if not self._registered:
            self._callback, self._name = None, None
        if not self._registered and self._font is None:
            _LIVE_NATIVE.discard(self)
        self._cleared = bool(okay)
        return self._cleared
