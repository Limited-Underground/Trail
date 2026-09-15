"""OT230 bounded nonradio bridge. Import performs no device I/O.

The authorized custody coordinator supplies already admitted passive serial
handles and a live identity/lease guard. This module cannot flash, reset or issue
a local confirmation. Provisioning keys exist only in process memory.
"""
import math
import secrets
import struct
import time
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat


class BridgeError(RuntimeError):
    """Fixed public reason codes; never include serial bytes or identities."""


def need(value, code):
    if not value:
        raise BridgeError(code)


def decimal(value, maximum=(1 << 64) - 1):
    need(isinstance(value, str) and value.isascii() and value.isdecimal()
         and (value == '0' or not value.startswith('0')) and len(value) <= 20,
         'numeric_invalid')
    result = int(value)
    need(result <= maximum, 'numeric_invalid')
    return result


def unhex(value, size):
    need(isinstance(value, str) and len(value) == size * 2
         and all(c in '0123456789ABCDEF' for c in value), 'hex_invalid')
    return bytes.fromhex(value)


class Endpoint:
    def __init__(self, handle, guard, *, monotonic=time.monotonic):
        self.handle, self.guard, self.clock = handle, guard, monotonic
        self.closed = False
        self.last = None
        self.buffer = bytearray()
        self.startup_ready = 0
        self.startup_sync_attempted = False

    def now(self):
        value = self.clock()
        need(type(value) in (float, int) and math.isfinite(value)
             and (self.last is None or value >= self.last), 'host_clock_invalid')
        self.last = value
        return value

    def admit(self):
        try:
            good = not self.closed and self.handle.is_open is True and self.guard() is True
        except Exception:
            good = False
        need(good, 'passive_lease_invalid')

    def startup_sync(self):
        # Exactly one line boundary on a fresh lease, before DIAG or HELLO.
        # Never send this preamble after command admission.
        need(not self.startup_sync_attempted and not self.buffer and self.startup_ready == 0,
             'startup_sync_consumed')
        self.startup_sync_attempted = True
        deadline = self.now() + 2
        try:
            self.admit()
            self.handle.write_timeout = .5
            need(self.handle.write(b'\n') == 1, 'partial_startup_sync')
            self.admit()
            need(self.now() < deadline, 'late_startup_sync')
            return True
        except BridgeError:
            raise
        except Exception:
            raise BridgeError('startup_sync_failed') from None

    def exchange(self, command, expected, deadline, *, startup=False):
        try:
            self.admit()
            need(type(deadline) in (int, float) and math.isfinite(deadline)
                 and 0 < deadline - self.now() <= 125, 'host_deadline_invalid')
            raw = ('OTPAIR1 ' + command + '\n').encode('ascii')
            need(len(raw) <= 701 and b'\r' not in raw and raw.count(b'\n') == 1,
                 'command_invalid')
            self.handle.write_timeout = min(.5, deadline - self.now())
            self.admit()
            need(self.handle.write(raw) == len(raw), 'partial_write')
            self.admit()
            need(self.now() < deadline, 'late_write')
            discarded = 0
            startup_refusals = 0
            while self.now() < deadline:
                self.admit()
                self.handle.timeout = min(.1, deadline - self.now())
                data = self.handle.read(1)
                need(type(data) is bytes and len(data) <= 1, 'serial_read_invalid')
                self.admit()
                need(self.now() < deadline, 'late_read')
                if not data:
                    continue
                if data != b'\n':
                    self.buffer.extend(data)
                    need(len(self.buffer) <= (2048 if startup else 767), 'line_overflow')
                    continue
                line = bytes(self.buffer).rstrip(b'\r')
                self.buffer.clear()
                if startup and not line.startswith(b'OTPAIR1 '):
                    discarded += len(line) + 1
                    need(discarded <= 2048, 'startup_noise_exceeded')
                    continue
                text = line.decode('ascii')
                parts = text.split(' ')
                need(all(parts) and parts[0] == 'OTPAIR1', 'response_invalid')
                if startup and expected == 'DIAG' and parts == ['OTPAIR1', 'REFUSED']:
                    startup_refusals += 1
                    need(startup_refusals <= 2, 'diagnostic_refusal_overflow')
                    continue
                if parts[1] == 'READY' and expected != 'READY' and self.startup_ready < 2:
                    need(startup, 'unexpected_ready')
                    self.startup_ready += 1
                    continue
                need(parts[1] == expected, 'unexpected_response')
                return parts[2:]
            raise BridgeError('bridge_timeout')
        except BridgeError:
            raise
        except Exception:
            raise BridgeError('serial_operation_failed') from None

    def close(self):
        if self.closed:
            return self.handle.is_open is False
        try:
            self.handle.close()
            self.closed = self.handle.is_open is False
        except Exception:
            return False
        return self.closed


def identity(parts):
    need(len(parts) == 3, 'identity_invalid')
    key, boot, now = unhex(parts[0], 32), unhex(parts[1], 16), decimal(parts[2])
    need(any(key) and any(boot), 'identity_invalid')
    return key, boot, now


def signed_invitation(key, a, b, *, window=60000):
    need(a[0] != b[0] and 1 <= window <= 60000, 'provisioning_invalid')
    need(all(0 <= item[2] <= (1 << 64) - 1 - window for item in (a, b)),
         'provisioning_time_invalid')
    public = key.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)
    group = secrets.randbelow((1 << 64) - 1) + 1
    nonce = secrets.token_bytes(16)
    need(any(nonce), 'provisioning_entropy_invalid')
    payload = (b'OTEINV\x00\x02' + struct.pack('>QI', group, 1) + public + a[0] + b[0]
               + nonce + a[1] + struct.pack('>QI', a[2], window)
               + b[1] + struct.pack('>QI', b[2], window))
    need(len(payload) == 188, 'provisioning_size_invalid')
    return payload + key.sign(payload)


class PairBridge:
    """One use. Both handles must close before custody may enter ROM again."""
    def __init__(self, a, b, *, notify=lambda event: None, sleep=time.sleep, diagnostics=False):
        need(a is not b and a.handle is not b.handle, 'independent_handles_required')
        self.ends = (a, b)
        self.notify, self.sleep = notify, sleep
        self.used = False
        self.result = 'unavailable'
        self.diagnostics = diagnostics

    def diagnose(self):
        healthy = True
        for role, endpoint in zip(('A', 'B'), self.ends):
            try:
                parts = endpoint.exchange('DIAG', 'DIAG', endpoint.now()+2, startup=True)
                need(len(parts) == 4 and parts[0] == '1', 'diagnostic_invalid')
                stage, error, cleanup = decimal(parts[1], 21), decimal(parts[2], 17), decimal(parts[3], 2)
                need((stage == 0) == (error == 0), 'diagnostic_invalid')
                need(stage != 0 or cleanup == 0, 'diagnostic_invalid')
                self.notify(f'diagnostic_{role}_1_{stage}_{error}_{cleanup}')
                healthy = stage == 0 and healthy
            except Exception:
                try:
                    self.notify('diagnostic_'+role+'_unavailable')
                except Exception:
                    pass  # Reporting must never bypass CLOSE or handle release.
                healthy = False
        return healthy

    def exchange_frames(self, deadline):
        a, b = self.ends
        for source, destination, step in ((a, b, 1), (b, a, 2), (a, b, 3)):
            frame = source.exchange('SEND', 'FRAME', min(deadline, source.now()+5))
            need(len(frame) == 3 and decimal(frame[0], 3) == step, 'frame_order_invalid')
            count = decimal(frame[1], 128)
            need(count > 0, 'frame_size_invalid')
            unhex(frame[2], count)
            reply = destination.exchange('FRAME '+' '.join(frame), 'OK',
                                         min(deadline, destination.now()+5))
            need(reply == ['FRAME'], 'frame_not_accepted')

    def __call__(self):
        need(not self.used, 'bridge_consumed')
        self.used = True
        a, b = self.ends
        deadline = a.now() + 120
        try:
            if self.diagnostics:
                synchronized = True
                for endpoint in self.ends:
                    try:
                        synchronized = endpoint.startup_sync() is True and synchronized
                    except Exception:
                        synchronized = False
                need(synchronized, 'startup_sync_failed')
                need(self.diagnose(), 'target_startup_refused')
            key = Ed25519PrivateKey.generate()
            signer = key.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw).hex().upper()
            ids = []
            for role, endpoint in enumerate(self.ends, 1):
                ready = endpoint.exchange('HELLO', 'READY', min(deadline, endpoint.now()+5), startup=True)
                need(len(ready) == 2 and ready[0] == '1', 'ready_invalid')
                decimal(ready[1])
                ids.append(identity(endpoint.exchange(f'INIT {role} {signer}', 'ID',
                                    min(deadline, endpoint.now()+5), startup=True)))
            # Obtain current device-local times after both identities exist.
            for i, endpoint in enumerate(self.ends):
                fresh = identity(endpoint.exchange('TIME', 'ID', min(deadline, endpoint.now()+5)))
                need(fresh[:2] == ids[i][:2] and fresh[2] >= ids[i][2], 'identity_changed')
                ids[i] = fresh
            invite = signed_invitation(key, ids[0], ids[1])
            key = None  # No raw private-key export or persistence.
            signed = invite.hex().upper()
            for i, endpoint in enumerate(self.ends):
                reply = endpoint.exchange('INV '+ids[1-i][0].hex().upper()+' '+signed,
                                          'OK', min(deadline, endpoint.now()+5))
                need(reply == ['INV'], 'invitation_not_accepted')
            self.exchange_frames(deadline)
            reviews = []
            for i, endpoint in enumerate(self.ends):
                review = endpoint.exchange('REVIEW', 'REVIEW', min(deadline, endpoint.now()+5))
                need(len(review) == 3, 'review_invalid')
                transcript = unhex(review[0], 32)
                expiry, now = decimal(review[1]), decimal(review[2])
                need(any(transcript) and expiry == ids[i][2]+60000 and ids[i][2] <= now < expiry,
                     'review_time_invalid')
                deadline = min(deadline, endpoint.now() + (expiry-now)/1000)
                reviews.append((transcript, expiry, now))
            need(reviews[0][0] == reviews[1][0], 'transcript_mismatch')
            self.notify('compare_device_codes_then_hold_and_release_each_local_button')
            confirmed = [False, False]
            while not all(confirmed):
                need(a.now() < deadline, 'confirmation_timeout')
                for i, endpoint in enumerate(self.ends):
                    status = endpoint.exchange('STATUS', 'STATUS', min(deadline, endpoint.now()+2))
                    need(len(status) == 3, 'status_invalid')
                    state, now, wiped = decimal(status[0], 6), decimal(status[1]), decimal(status[2], 1)
                    need(state in (3, 4) and wiped == 0 and reviews[i][2] <= now < reviews[i][1],
                         'local_confirmation_refused')
                    need(not confirmed[i] or state == 4, 'confirmation_regressed')
                    if state == 4 and not confirmed[i]:
                        self.notify('local_confirmation_'+('A' if i == 0 else 'B'))
                    confirmed[i] = state == 4
                    reviews[i] = (reviews[i][0], reviews[i][1], now)
                if not all(confirmed):
                    self.sleep(.1)
            self.result = 'local_confirmed'
            return self.result
        except BridgeError:
            self.result = 'refused'
            raise
        finally:
            # No remote confirmation command exists. CLOSE only retires/wipes.
            if self.diagnostics and self.result != 'local_confirmed':
                self.diagnose()
            clean = True
            for endpoint in self.ends:
                try:
                    reply = endpoint.exchange('CLOSE', 'CLOSED', endpoint.now()+2)
                    clean = reply == ['1'] and clean
                except Exception:
                    clean = False
            if not clean:
                self.result = 'refused'
                raise BridgeError('device_cleanup_unconfirmed') from None

    def close(self):
        # Do not short-circuit: attempt closure of both independently.
        results = [endpoint.close() for endpoint in self.ends]
        return all(results)

    def assert_idle(self):
        return all(endpoint.closed and endpoint.handle.is_open is False for endpoint in self.ends)


class RadioPairBridge(PairBridge):
    """Exact evaluation radio profile; provisioning and confirmation stay shared.

    The host 60-second bound supplements the target's signed local deadlines.
    It cannot replace or extend target authority. No USB frame forwarding occurs.
    """
    RADIO_INFO = ['1', '915000000', '125000', '7', '5', '2', '154', '2']

    def exchange_frames(self, deadline):
        a, b = self.ends
        deadline = min(deadline, a.now() + 60)
        for endpoint in self.ends:
            need(endpoint.exchange('RADIOINFO', 'RADIOINFO', min(deadline, endpoint.now()+2))
                 == self.RADIO_INFO, 'radio_profile_invalid')
        for endpoint in (b, a):
            need(endpoint.exchange('RADIO', 'OK', min(deadline, endpoint.now()+2))
                 == ['RADIO'], 'radio_arm_refused')
        previous = [None, None]
        finished = [False, False]
        expected = ((2, 2, 1), (1, 1, 2))
        while not all(finished):
            need(a.now() < deadline, 'radio_timeout')
            for index, endpoint in enumerate(self.ends):
                parts = endpoint.exchange('RADIOSTAT', 'RADIOSTAT', min(deadline, endpoint.now()+2))
                need(len(parts) == 7, 'radio_status_invalid')
                state, attempts, completed, received, errors, elapsed, stopped = (
                    decimal(value, limit) for value, limit in zip(parts, (6, 2, 2, 2, 0, 60000, 1)))
                need(state in (2, 3) and completed <= attempts, 'radio_state_invalid')
                need(attempts <= expected[index][0] and received <= expected[index][2], 'radio_counts_invalid')
                row = (state, attempts, completed, received, errors, elapsed, stopped)
                old = previous[index]
                need(old is None or all(current >= prior for current, prior in zip(row, old)),
                     'radio_status_regressed')
                need(state != 2 or stopped == 0, 'radio_stopped_early')
                if state == 3:
                    need(stopped == 1 and (attempts, completed, received) == expected[index],
                         'radio_completion_invalid')
                    if not finished[index]:
                        role = 'A' if index == 0 else 'B'
                        self.notify(f'radio_{role}_stats_{attempts}_{completed}_{received}_{errors}_{elapsed}_{stopped}')
                        self.notify('radio_' + role + '_verified')
                    finished[index] = True
                previous[index] = row
                need(endpoint.now() < deadline, 'radio_timeout')
            if not all(finished):
                self.sleep(.1)
