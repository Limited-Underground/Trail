"""Inert OTCAND1 public-record client; not a physical trial controller.

The owner supplies an already-open passive handle, its fresh identity guard and
the exact deadline. This module never opens/enumerates devices, issues grants,
restarts/flashes hardware or creates a signer. Replies are untrusted public
values, not proof of enrollment or physical confirmation. POLL/CONFIRM OK means
only that the target accepted a sample. No operation or failed write is retried.
"""
from dataclasses import dataclass, field
import math
import re
import threading
import time

MAX_VALUE_BYTES = 768
MAX_LINE_BYTES = 2048
PREFIX = b'OTCAND1 '
# Canonical field encodings in candidate_usb_codec.hpp, never sizeof(struct).
VALUE_BYTES = {'CANDIDATE': 40, 'OFFER': 109, 'MARK': 44,
               'RETAINCHALLENGE': 64, 'RETAINRESPONSE': 192,
               'RECOVERCHALLENGE': 278, 'RECOVERRESPONSE': 620,
               'ARCHIVE': 380, 'INVITATION': 252, 'FRAME': 132,
               'CONTROL': 108, 'STATUS': 108, 'POSSESSION': 64,
               'SIGNATURE': 64}
HEX_ARGUMENTS = {'PEER': 'CANDIDATE', 'POSSESS': 'OFFER',
                 'RETAINSIGN': 'RETAINCHALLENGE', 'RETAINFINISH': 'RETAINRESPONSE',
                 'RECOVERSIGN': 'RECOVERCHALLENGE', 'RECOVERFINISH': 'RECOVERRESPONSE',
                 'ACCEPTARCHIVE': 'ARCHIVE', 'ACCEPTPOSSESS': 'POSSESSION',
                 'PEERMARK': 'MARK', 'SIGN': 'INVITATION', 'BIND': 'SIGNATURE',
                 'FRAME': 'FRAME', 'CONTROL': 'CONTROL', 'STATUS': 'STATUS'}
VALUE_REPLIES = {'EXPORT': 'CANDIDATE', 'FINISH': 'OFFER',
                 'RETAINBEGIN': 'RETAINCHALLENGE', 'RETAINSIGN': 'RETAINRESPONSE',
                 'RETAINFINISH': 'OFFER', 'RECOVERBEGIN': 'RECOVERCHALLENGE',
                 'RECOVERSIGN': 'RECOVERRESPONSE', 'ARCHIVE': 'ARCHIVE',
                 'MARK': 'MARK', 'INVITE': 'INVITATION', 'NEXTFRAME': 'FRAME',
                 'NEXTCONTROL': 'CONTROL', 'SENDSTATUS': 'STATUS',
                 'POSSESS': 'POSSESSION', 'SIGN': 'SIGNATURE'}
NO_ARGUMENTS = frozenset(('HELLO RESETSTATUS BOOTSTATUS CLOSE CANCEL REVOKE EXPORT FINISH '
    'RETAINBEGIN RECOVERBEGIN ARCHIVE MARK INVITE NEXTFRAME NEXTCONTROL '
    'SHOWPEER SHOWLOCAL POLL CONFIRM COMMIT READY').split())


class ClientError(RuntimeError):
    """Fixed category only; never includes command values or exception text."""


def require(value, category):
    if not value:
        raise ClientError(category)


def decimal(value, maximum, minimum=0):
    require(type(value) is str and re.fullmatch(r'0|[1-9][0-9]{0,19}', value)
            is not None, 'invalid_decimal')
    require(minimum <= int(value) <= maximum, 'invalid_decimal')
    return int(value)


def public_hex(value, kind):
    require(type(value) is str and len(value) == 2 * VALUE_BYTES[kind]
            and len(value) <= 2 * MAX_VALUE_BYTES
            and re.fullmatch(r'[0-9a-f]+', value) is not None, 'invalid_public_value')
    return value


def command_bytes(command):
    require(type(command) is str and command and len(command) < MAX_LINE_BYTES
            and all(32 <= ord(c) <= 126 for c in command), 'invalid_command')
    parts = command.split(' ')
    require(all(parts), 'invalid_command')
    verb, args = parts[0], parts[1:]
    if verb in NO_ARGUMENTS:
        require(not args, 'invalid_command')
    elif verb == 'BEGIN':
        require(len(args) == 3, 'invalid_command')
        decimal(args[0], 2)
        decimal(args[1], 2, 1)
        decimal(args[2], (1 << 64) - 1, 1)
    elif verb == 'SENDSTATUS':
        require(len(args) == 1, 'invalid_command')
        decimal(args[0], 8, 1)
    elif verb in HEX_ARGUMENTS:
        require(len(args) == 1, 'invalid_command')
        public_hex(args[0], HEX_ARGUMENTS[verb])
    else:
        raise ClientError('invalid_command')
    raw = PREFIX + command.encode('ascii') + b'\n'
    require(len(raw) <= MAX_LINE_BYTES, 'invalid_command')
    return verb, raw


@dataclass(frozen=True)
class Reply:
    kind: str
    values: tuple = field(repr=False)


def parse_reply(verb, line):
    require(type(line) is bytes and 0 < len(line) <= MAX_LINE_BYTES
            and all(32 <= c <= 126 for c in line), 'invalid_response')
    require(line.startswith(PREFIX), 'invalid_response')
    parts = line.decode('ascii').split(' ')
    require(all(parts) and len(parts) >= 2, 'invalid_response')
    kind, values = parts[1], tuple(parts[2:])
    if kind == 'REFUSED':
        require(not values, 'invalid_response')
        raise ClientError('target_refused')
    expected = ('READY' if verb == 'HELLO' else verb if verb in ('RESETSTATUS', 'BOOTSTATUS')
                else 'CLOSED' if verb in ('CLOSE', 'CANCEL') else 'REVOKED'
                if verb == 'REVOKE' else 'VALUE' if verb == 'STATUS'
                else VALUE_REPLIES.get(verb, 'OK'))
    require(kind == expected, 'unexpected_response')
    if kind in VALUE_BYTES:
        require(len(values) == 1, 'invalid_response')
        if kind != 'ARCHIVE' or values[0] != 'NONE':
            public_hex(values[0], kind)
    elif kind in ('READY', 'CLOSED', 'REVOKED'):
        require(values == ('1',), 'invalid_response')
    elif kind == 'RESETSTATUS':
        require(len(values) == 2, 'invalid_response')
        decimal(values[0], 6)
        decimal(values[1], 1)
    elif kind == 'BOOTSTATUS':
        require(len(values) == 1, 'invalid_response')
        decimal(values[0], 9)
    elif kind == 'VALUE':
        require(len(values) == 1, 'invalid_response')
        decimal(values[0], 8, 1)
    else:
        require(values == (verb,), 'invalid_response')
    return Reply(kind, values)


class Endpoint:
    """One serialized passive lease; reconstruction needs a NEW endpoint.

    After a verified target refusal only explicit RESETSTATUS/BOOTSTATUS inspection and
    passive-handle close are permitted. Other failures permit close only. No
    automatic cleanup command is sent. Physical custody remains the caller's
    responsibility; close returning False forbids subsequent ROM access.
    """
    def __init__(self, handle, guard, *, monotonic=time.monotonic):
        self.handle, self.guard, self.clock = handle, guard, monotonic
        self.ready = False
        self.failed = False
        self.closed = False
        self._refused = False
        self._last = None
        self._lock = threading.Lock()

    def _remaining(self, deadline):
        now = self.clock()
        require(type(now) in (int, float) and math.isfinite(now)
                and (self._last is None or now >= self._last), 'host_clock_invalid')
        self._last = now
        require(type(deadline) in (int, float) and math.isfinite(deadline)
                and now < deadline, 'deadline_expired')
        return deadline - now

    def _admit(self, deadline, inspect_refusal=False):
        self._remaining(deadline)
        require(not self.closed and (not self.failed or (inspect_refusal and self._refused))
                and self.handle.is_open is True
                and self.guard() is True, 'identity_guard')
        self._remaining(deadline)
        # The injected guard/clock are callbacks, not a lease-state snapshot.
        # Reentry during either must be visible before publishing acceptance.
        require(not self.closed and (not self.failed or (inspect_refusal and self._refused))
                and self.handle.is_open is True, 'identity_guard')

    def _available(self, deadline, inspect_refusal=False):
        self._admit(deadline, inspect_refusal)
        count = self.handle.in_waiting
        self._admit(deadline, inspect_refusal)
        require(type(count) is int and 0 <= count <= 0xffffffff, 'invalid_read')
        return count

    def exchange(self, command, deadline):
        if not self._lock.acquire(blocking=False):
            self.failed = True
            self._refused = False
            raise ClientError('client_busy')
        try:
            verb, raw = command_bytes(command)
            inspect_refusal = verb in ('RESETSTATUS', 'BOOTSTATUS')
            require(not self.closed and (not self.failed or
                    (inspect_refusal and self._refused)), 'lease_terminal')
            require(self.ready or verb in ('HELLO', 'RESETSTATUS', 'BOOTSTATUS'), 'readiness_required')
            self._admit(deadline, inspect_refusal)
            # Even the first HELLO must not consume a queued old READY as its
            # reply. The owner supplies a fresh quiet passive lease.
            require(self._available(deadline, inspect_refusal) == 0,
                    'unsolicited_response')
            self.handle.write_timeout = min(.5, self._remaining(deadline))
            self._admit(deadline, inspect_refusal)
            require(self.handle.write(raw) == len(raw), 'partial_write')
            self._admit(deadline, inspect_refusal)
            line, noise = bytearray(), 0
            while True:
                self._admit(deadline, inspect_refusal)
                self.handle.timeout = min(.1, self._remaining(deadline))
                available = self._available(deadline, inspect_refusal)
                count = max(1, min(64, available))
                self._admit(deadline, inspect_refusal)
                chunk = self.handle.read(count)
                self._admit(deadline, inspect_refusal)
                require(type(chunk) is bytes and len(chunk) <= count, 'invalid_read')
                if not chunk:
                    continue
                for index, byte in enumerate(chunk):
                    if byte != 10:
                        line.append(byte)
                        require(len(line) < MAX_LINE_BYTES, 'line_overflow')
                        continue
                    complete = bytes(line)
                    line.clear()
                    if not self.ready and verb == 'HELLO' and not complete.startswith(PREFIX):
                        noise += len(complete) + 1
                        require(noise <= MAX_LINE_BYTES, 'startup_noise_exceeded')
                        continue
                    # A quiet, nonpipelined target has exactly one response.
                    # Never carry queued reply bytes into the NEXT command.
                    require(index == len(chunk) - 1, 'unexpected_response_tail')
                    result = parse_reply(verb, complete)
                    self._admit(deadline, inspect_refusal)
                    require(self._available(deadline, inspect_refusal) == 0,
                            'unexpected_response_tail')
                    if verb == 'HELLO':
                        self.ready = True
                    return result
        except ClientError as error:
            self.failed = True
            if str(error) != 'lease_terminal':
                self._refused = str(error) == 'target_refused'
            raise
        except Exception:
            self.failed = True
            self._refused = False
            raise ClientError('serial_operation_failed') from None
        finally:
            self._lock.release()

    def close(self):
        if not self._lock.acquire(blocking=False):
            self.failed = True
            self._refused = False
            raise ClientError('client_busy')
        try:
            self.failed = True
            try:
                self.handle.close()
                self.closed = self.handle.is_open is False
            except Exception:
                self.closed = False
            return self.closed
        finally:
            self._lock.release()
