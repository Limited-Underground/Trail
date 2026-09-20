"""OT240 evaluation bridge for already-owned passive handles only.
Import performs no I/O. No flashing, serial opening, reset or host confirmation.
The operator must supply current handle/identity guards and physical button input.
No keys, invitations, peer identities or ciphertext are logged or persisted.
"""
import math
import secrets
import struct
import time
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
from enrolled_trace_schema import TRACE_FIELDS, valid_target_trace
from pair_bench_bridge import BridgeError, need, decimal, unhex, identity
from enrolled_trace_schema import (valid_host_timing, HOST_TIMING_COMMAND_LIMIT,
                                   valid_transport_timing, TRANSPORT_TIMING_FIELDS)

COMMANDS = frozenset('OPEN SYNC HELLO INIT TIME BEGIN RADIO RFSEND RFPOLL RFSTAT SEND FRAME REVIEW STATUS NEXTCONTROL CONTROL TRAFFIC RFCONTROL RFSTATUS SENDSTATUS STATUSFRAME CLOSE UNKNOWN'.split())
SNAPSHOT_FIELDS = ('fault', 'tick_last_us', 'tick_max_us', 'nvs_reads', 'nvs_read_us',
                   'tx_queue_age_ms', 'tx_age_ms', 'service_gap_ms', 'tx_attempts', 'tx_completed')
def valid_snapshot(value):
    return (type(value) is dict and set(value) == set(SNAPSHOT_FIELDS) and
            all(type(value[k]) is int and 0 <= value[k] < 1 << 64 for k in SNAPSHOT_FIELDS)
            and value['fault'] <= 12 and value['tick_last_us'] <= value['tick_max_us']
            and value['tx_completed'] <= value['tx_attempts'] <= 16)

FAILURE_STAGES = frozenset(
    [prefix + role for prefix in ('open_', 'sync_', 'hello_', 'init_', 'time_', 'begin_',
                                  'radio_', 'review_', 'status_', 'close_') for role in ('A', 'B')]
    + ['handshake_1', 'handshake_2', 'handshake_3', 'wait_local_confirmation',
       'activation_1', 'activation_2', 'activation_3', 'activation_4'])
FAILURE_REASONS = frozenset(('timeout', 'wrong_response_kind', 'invalid_frame_shape',
    'identity_guard', 'partial_write', 'serial_read_failed', 'late_read', 'target_refused', 'unknown'))
_REASON_CODES = {
    'bridge_timeout': 'timeout', 'confirmation_timeout': 'timeout', 'radio_transmit_timeout': 'timeout',
    'unexpected_response': 'wrong_response_kind', 'unexpected_ready': 'wrong_response_kind',
    'diagnostic_invalid': 'invalid_frame_shape', 'diagnostic_missing': 'invalid_frame_shape',
    'response_invalid': 'invalid_frame_shape', 'line_overflow': 'invalid_frame_shape',
    'numeric_invalid': 'invalid_frame_shape', 'hex_invalid': 'invalid_frame_shape',
    'frame_order_invalid': 'invalid_frame_shape', 'frame_size_invalid': 'invalid_frame_shape',
    'control_invalid': 'invalid_frame_shape', 'status_frame_invalid': 'invalid_frame_shape',
    'ready_invalid': 'invalid_frame_shape', 'review_invalid': 'invalid_frame_shape',
    'status_invalid': 'invalid_frame_shape', 'radio_statistics_invalid': 'invalid_frame_shape',
    'passive_lease_invalid': 'identity_guard', 'identity_changed': 'identity_guard',
    'partial_write': 'partial_write', 'partial_startup_sync': 'partial_write',
    'serial_read_failed': 'serial_read_failed', 'serial_read_invalid': 'serial_read_failed',
    'late_read': 'late_read', 'target_refused': 'target_refused',
}
def failure_reason(error):
    # Never stringify arbitrary exceptions or retain their message as evidence.
    if type(error) is BridgeError and len(error.args) == 1 and type(error.args[0]) is str:
        return _REASON_CODES.get(error.args[0], 'unknown')
    return 'unknown'

def safe_notify(notify, event):
    try: notify(event)
    except BaseException: pass  # Diagnostics must not mask failure or skip cleanup.


class Endpoint:
    def __init__(self, handle, guard, *, monotonic=time.monotonic,
                 enable_transport_timing=False,transport_clock_ns=time.monotonic_ns):
        self.handle, self.guard, self.clock = handle, guard, monotonic
        self.diagnostic = None
        self.trace = None
        self.trace_seen = False
        self.unconfirmed_trace = None
        self.diagnostic_seen = False
        self.closed = False
        self.last = None
        self.buffer = bytearray()
        self.received = bytearray()
        self.startup_ready = 0
        self.startup_sync_attempted = False
        need(type(enable_transport_timing) is bool,'diagnostic_invalid')
        self.enable_transport_timing=enable_transport_timing
        self.transport_clock_ns=transport_clock_ns
        self.transport_metrics=None
        self._transport_current=None
        self._transport_valid=False

    def transport_begin(self):
        self.transport_metrics=None
        self._transport_current=None
        self._transport_valid=False
        if self.enable_transport_timing:
            try:
                self._transport_current=dict.fromkeys(TRANSPORT_TIMING_FIELDS,0)
                self._transport_valid=True
            except BaseException:pass

    def transport_call(self,kind,action,*args):
        # Clock sampling never invokes the identity guard or touches a port.
        # Diagnostic failures discard this command's metrics, not its outcome.
        start=None
        if self._transport_current is not None and self._transport_valid:
            try:
                start=self.transport_clock_ns()
                if type(start) is not int or start<0:
                    self._transport_valid=False;start=None
            except BaseException:self._transport_valid=False
        try:
            return action(*args)
        finally:
            if start is not None:
                try:
                    end=self.transport_clock_ns()
                    if type(end) is not int or end<start:
                        self._transport_valid=False
                    else:
                        self._transport_current[kind+'_ns']+=end-start
                        self._transport_current[kind+'_calls']+=1
                except BaseException:self._transport_valid=False

    def transport_finish(self):
        try:
            if self._transport_valid and valid_transport_timing(self._transport_current):
                self.transport_metrics=dict(self._transport_current)
        except BaseException:pass
        finally:
            self._transport_current=None
            self._transport_valid=False

    def now(self):
        value = self.clock()
        need(type(value) in (float, int) and math.isfinite(value)
             and (self.last is None or value >= self.last), 'host_clock_invalid')
        self.last = value
        return value

    def admit(self):
        try:
            good = not self.closed and self.handle.is_open is True and self.transport_call('guard',self.guard) is True
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
        self.transport_begin()
        try:
            self.admit()
            self.handle.write_timeout = .5
            need(self.transport_call('write',self.handle.write,b'\n') == 1, 'partial_startup_sync')
            self.admit()
            need(self.now() < deadline, 'late_startup_sync')
            return True
        except BridgeError:
            raise
        except Exception:
            raise BridgeError('startup_sync_failed') from None
        finally:self.transport_finish()

    def exchange(self, command, expected, deadline, *, startup=False):
        self.transport_begin()
        try:
            self.admit()
            need(type(deadline) in (int, float) and math.isfinite(deadline)
                 and 0 < deadline - self.now() <= 125, 'host_deadline_invalid')
            raw = ('OTENROLL1 ' + command + '\n').encode('ascii')
            need(len(raw) <= 701 and b'\r' not in raw and raw.count(b'\n') == 1,
                 'command_invalid')
            self.handle.write_timeout = min(.5, deadline - self.now())
            self.admit()
            need(self.transport_call('write',self.handle.write,raw) == len(raw), 'partial_write')
            self.admit()
            need(self.now() < deadline, 'late_write')
            discarded = 0
            pending = None
            pending_trace = None
            while self.now() < deadline:
                if not self.received:
                    self.admit()
                    try:
                        # Availability sizes this read only; it never substitutes
                        # for the fresh identity guards around actual I/O.
                        available = getattr(self.handle, 'in_waiting', 0)
                        need(type(available) is int and 0 <= available <= 0xffffffff,
                             'serial_read_invalid')
                        count = max(1, min(64, available))
                        remaining = deadline - self.now()
                        need(remaining > 0, 'late_read')
                        self.handle.timeout = min(.1, remaining)
                        try: chunk = self.transport_call('read', self.handle.read, count)
                        except Exception: raise BridgeError('serial_read_failed') from None
                    finally:
                        self.admit()
                    need(type(chunk) is bytes and len(chunk) <= count, 'serial_read_invalid')
                    need(self.now() < deadline, 'late_read')
                    self.received.extend(chunk)
                    if not self.received:
                        continue
                # Already-guarded bytes may contain more than one response line.
                # Keep the bounded tail for the next line or command exchange.
                data = bytes(self.received[:1])
                del self.received[:1]
                if data != b'\n':
                    self.buffer.extend(data)
                    need(len(self.buffer) <= (2048 if startup else 767), 'line_overflow')
                    continue
                wire_line = bytes(self.buffer)
                line = wire_line.rstrip(b'\r')
                self.buffer.clear()
                if pending is not None:
                    if line.startswith(b'OTENROLL1 TRACE '):
                        need(not self.trace_seen and wire_line == line, 'diagnostic_invalid')
                        trace_parts = line.decode('ascii').split(' ')
                        need(len(trace_parts) == 3 + len(TRACE_FIELDS) and trace_parts[2] == '1', 'diagnostic_invalid')
                        pending_trace = dict(zip(TRACE_FIELDS, (decimal(x, (1 << 64)-1) for x in trace_parts[3:])))
                        need(valid_target_trace(pending_trace) and pending_trace['fault'] == pending['fault'], 'diagnostic_invalid')
                        self.trace_seen = True
                        self.unconfirmed_trace = dict(pending_trace)
                        continue
                    terminal = (line in (b'OTENROLL1 REFUSED 0', b'OTENROLL1 REFUSED 1') or
                                (command == 'CLOSE' and expected == 'CLOSED' and
                                 line in (b'OTENROLL1 CLOSED 0', b'OTENROLL1 CLOSED 1')))
                    need(terminal and wire_line == line, 'diagnostic_invalid')
                    self.diagnostic = pending
                    self.trace = pending_trace
                    self.unconfirmed_trace = None
                    pending = None
                    pending_trace = None
                if startup and not line.startswith(b'OTENROLL1 '):
                    discarded += len(line) + 1
                    need(discarded <= 2048, 'startup_noise_exceeded')
                    continue
                text = line.decode('ascii')
                parts = text.split(' ')
                need(len(parts) >= 2 and all(parts) and parts[0] == 'OTENROLL1', 'response_invalid')
                if parts[1] == 'DIAG':
                    need(not self.diagnostic_seen and wire_line == line and len(parts) == 13
                         and parts[2] == '1', 'diagnostic_invalid')
                    self.diagnostic_seen = True
                    pending = dict(zip(SNAPSHOT_FIELDS, (decimal(x, (1 << 64)-1) for x in parts[3:])))
                    need(valid_snapshot(pending), 'diagnostic_invalid')
                    continue
                if parts[1] == 'READY' and expected != 'READY' and self.startup_ready < 2:
                    need(startup, 'unexpected_ready')
                    self.startup_ready += 1
                    continue
                need(parts[1] != 'REFUSED', 'target_refused')
                need(parts[1] == expected, 'unexpected_response')
                return parts[2:]
            raise BridgeError('bridge_timeout')
        except BridgeError:
            raise
        except Exception:
            raise BridgeError('serial_operation_failed') from None
        finally:self.transport_finish()

    def close(self):
        if self.closed:
            return self.handle.is_open is False
        try:
            self.handle.close()
            self.closed = self.handle.is_open is False
        except Exception:
            return False
        return self.closed



def invitation(key, a, b, group, epoch, window=60000):
    need(a[0] != b[0] and 0 < group < 1 << 64 and 0 < epoch < 1 << 32 and 0 < window <= 60000,
         'provisioning_invalid')
    need(all(0 <= item[2] <= (1 << 64)-1-window for item in (a,b)), 'provisioning_time_invalid')
    public=key.public_key().public_bytes(Encoding.Raw,PublicFormat.Raw)
    nonce=secrets.token_bytes(16);need(any(nonce),'provisioning_entropy_invalid')
    payload=(b'OTEINV\x00\x02'+struct.pack('>QI',group,epoch)+public+a[0]+b[0]+nonce+
             a[1]+struct.pack('>QI',a[2],window)+b[1]+struct.pack('>QI',b[2],window))
    need(len(payload)==188,'provisioning_size_invalid')
    return payload+key.sign(payload)

class EnrolledBridge:
    """One bounded exchange; supplied signer/group permit explicitly authorized rekey.
    Caller owns private signer lifetime; this class never serializes its private bytes.
    """
    def __init__(self,a,b,*,signer,group,epoch,radio=False,notify=lambda event:None,sleep=time.sleep,require_diagnostics=False,require_trace=False,
                 enable_host_timing=False,timing_clock_ns=time.monotonic_ns):
        need(a is not b and a.handle is not b.handle,'independent_handles_required')
        self.ends=(a,b);self.signer=signer;self.group=group;self.epoch=epoch
        self.radio=radio;self.notify=notify;self.sleep=sleep;self.used=False
        self.result='unavailable';self.statistics=[]
        need(type(require_diagnostics) is bool,'diagnostic_invalid')
        self.require_diagnostics=require_diagnostics;self.first_failure_point=None;self.point=('A','SYNC')
        need(type(require_trace) is bool,'diagnostic_invalid')
        self.require_trace=require_trace
        self.stage='sync_A';self.first_failure=None;self.cleanup_outcome=None
        need(type(enable_host_timing) is bool,'diagnostic_invalid')
        self.enable_host_timing=enable_host_timing;self.timing_clock_ns=timing_clock_ns
        self.timing_origin=None;self.timing_last=None;self.timing_commands=0
        self.timing_truncated=False;self.timing_status4=set()
        self.timing_confirmation_commands=0;self.last_command_timing=None
    def timing_stamp(self):
        # Instrumentation has its own monotonic clock. Never resample Endpoint
        # clocks or let failed diagnostics alter deadlines, custody or cleanup.
        if not self.enable_host_timing:return None
        try:
            value=self.timing_clock_ns()
            if type(value) is not int or value<0 or (self.timing_last is not None and value<self.timing_last):return None
            if self.timing_origin is None:self.timing_origin=value
            self.timing_last=value
            elapsed=(value-self.timing_origin)//1000
            return elapsed if elapsed<1<<64 else None
        except BaseException:return None
    def timing_event(self,kind,*,at=None,**fields):
        if not self.enable_host_timing:return
        try:
            elapsed=self.timing_stamp() if at is None else at
            if elapsed is not None:
                event=dict(operator='enrolled_host_timing',version=1,kind=kind,elapsed_us=elapsed,**fields)
                if valid_host_timing(event):safe_notify(self.notify,event)
        except BaseException:pass
    def timed_exchange(self,e,command,expected,deadline,*,startup=False):
        start=self.timing_stamp();ok=False
        try:
            result=e.exchange(command,expected,deadline,startup=startup);ok=True;return result
        finally:
            self.command_timing(start,ok,e)
    def command_timing(self,start,ok,e=None):
        self.last_command_timing=None
        if start is None:return
        try:
            end=self.timing_stamp()
            if end is None or end<start:return
            stage=self.stage if self.stage in FAILURE_STAGES else 'sync_A'
            command=dict(stage=stage,role=self.point[0],command=self.point[1],
                         start_us=start,elapsed_us=end,duration_us=end-start,ok=ok)
            metrics=getattr(e,'transport_metrics',None)
            if valid_transport_timing(metrics):command['transport']=dict(metrics)
            if not valid_host_timing(dict(operator='enrolled_host_timing',version=1,kind='command',**command)):return
            # Retain the latest command independently of the emitted timeline,
            # so a capped stream cannot hide the failed command's transport cost.
            self.last_command_timing=command
            confirmation=stage=='wait_local_confirmation' and self.point[1]=='STATUS'
            scope=None
            if confirmation and self.timing_confirmation_commands>=128:
                scope='confirmation_status';limit=128
            elif self.timing_commands>=HOST_TIMING_COMMAND_LIMIT:
                scope='all_commands';limit=HOST_TIMING_COMMAND_LIMIT
            if scope is not None:
                if not self.timing_truncated:
                    self.timing_truncated=True
                    self.timing_event('truncated',at=end,command_limit=limit,scope=scope,
                                      stage=stage,role=self.point[0],command=self.point[1])
                return
            self.timing_commands+=1
            if confirmation:self.timing_confirmation_commands+=1
            fields=dict(command);fields.pop('elapsed_us')
            self.timing_event('command',at=end,**fields)
        except BaseException:pass
    def __call__(self):
        need(not self.used,'bridge_consumed');self.used=True
        self.timing_stamp()
        a,b=self.ends;deadline=None;started=False;body_failed=False
        def exchange(e,command,expected,startup=False):
            self.set_point(e,command)
            return self.timed_exchange(e,command,expected,min(deadline,e.now()+5),startup=startup)
        def radio_send(e,command):
            need(exchange(e,command,'OK')==[command.split(' ')[0]],'radio_send_refused')
            while e.now()<deadline:
                need(exchange(e,'RFPOLL','RF')==['WAIT'],'radio_unexpected_frame')
                raw=exchange(e,'RFSTAT','RFSTAT')
                need(len(raw)==5,'radio_statistics_invalid')
                values=[decimal(x,0xffffffff) for x in raw]
                need(values[3]==0 and values[4]==0 and 0<values[0]<=16 and values[1]<=values[0],
                     'radio_statistics_invalid')
                if values[0]==values[1]:
                    # Completion-only target maintenance deliberately leaves RX off.
                    # This fully guarded poll rearms it before the peer may transmit.
                    need(exchange(e,'RFPOLL','RF')==['WAIT'],'radio_unexpected_frame')
                    return
                self.sleep(.02)
            raise BridgeError('radio_transmit_timeout')
        def radio_poll(e,expected):
            while e.now()<deadline:
                reply=exchange(e,'RFPOLL','RF')
                if reply==['WAIT']:self.sleep(.02);continue
                need(reply==expected,'radio_stage_invalid')
                # Consuming RX_DONE leaves the target in standby. Rearm through
                # the same guarded command before any following peer transmit.
                need(exchange(e,'RFPOLL','RF')==['WAIT'],'radio_unexpected_frame')
                return
            raise BridgeError('bridge_timeout')
        try:
            deadline=a.now()+120
            for role,e in zip(('A','B'),self.ends):
                self.point=(role,'SYNC');self.stage='sync_'+role
                stamp=self.timing_stamp();ok=False
                try:need(e.startup_sync(),'startup_sync_failed');ok=True
                finally:self.command_timing(stamp,ok,e)
            started=True
            self.point=('A','UNKNOWN')
            public=self.signer.public_key().public_bytes(Encoding.Raw,PublicFormat.Raw).hex().upper()
            ids=[]
            for role,e in enumerate(self.ends,1):
                self.stage='hello_'+('A' if role==1 else 'B')
                ready=exchange(e,'HELLO','READY',True);need(len(ready)==2 and ready[0]=='1','ready_invalid')
                decimal(ready[1]);self.stage='init_'+('A' if role==1 else 'B');ids.append(identity(exchange(e,f'INIT {role} {public} {self.group}','ID',True)))
            for i,e in enumerate(self.ends):
                self.stage='time_'+('A' if i==0 else 'B')
                current=identity(exchange(e,'TIME','ID'))
                need(current[:2]==ids[i][:2] and current[2]>=ids[i][2],'identity_changed');ids[i]=current
            self.point=('A','UNKNOWN')
            signed=invitation(self.signer,*ids,self.group,self.epoch).hex().upper()
            for role,e in zip(('A','B'),self.ends):
                self.stage='begin_'+role;need(exchange(e,'BEGIN '+signed,'OK')==['BEGIN'],'invitation_not_accepted')
            if self.radio:
                for role,e in zip(('A','B'),self.ends):
                    self.stage='radio_'+role;need(exchange(e,'RADIO','OK')==['RADIO'],'radio_not_armed')
            for src,dst,step in ((a,b,1),(b,a,2),(a,b,3)):
                self.stage='handshake_'+str(step)
                if self.radio:
                    radio_send(src,'RFSEND');radio_poll(dst,['HANDSHAKE'])
                else:
                    frame=exchange(src,'SEND','FRAME');need(len(frame)==3 and decimal(frame[0],3)==step,'frame_order_invalid')
                    count=decimal(frame[1],128);need(count>0,'frame_size_invalid');unhex(frame[2],count)
                    need(exchange(dst,'FRAME '+' '.join(frame),'OK')==['FRAME'],'frame_not_accepted')
            reviews=[];review_stamps={}
            for role,e in zip(('A','B'),self.ends):
                self.stage='review_'+role;r=exchange(e,'REVIEW','REVIEW');need(len(r)==3,'review_invalid');reviews.append(r)
                review_stamps[role]=self.timing_stamp()
            need(unhex(reviews[0][0],32)==unhex(reviews[1][0],32),'review_mismatch')
            for r in reviews:need(decimal(r[1])>decimal(r[2]),'review_expired')
            self.stage='wait_local_confirmation'
            self.point=('A','UNKNOWN')
            comparison_stamp=self.timing_stamp()
            if comparison_stamp is not None and all(stamp is not None for stamp in review_stamps.values()):
                self.timing_event('comparison',at=comparison_stamp,
                    review_remaining_ms={role:decimal(r[1])-decimal(r[2]) for role,r in zip(('A','B'),reviews)},
                    review_age_us={role:comparison_stamp-stamp for role,stamp in review_stamps.items()})
            safe_notify(self.notify,'physical_comparison_and_buttons_required')
            while a.now()<deadline:
                states=[]
                for role,e in zip(('A','B'),self.ends):
                    state=exchange(e,'STATUS','STATUS');states.append(state)
                    if len(state)==3 and state[0]=='4' and role not in self.timing_status4:
                        self.timing_status4.add(role);self.timing_event('status4',role=role)
                need(all(len(s)==3 for s in states),'status_invalid')
                if all(s[0]=='4' for s in states):break
                need(all(s[0]=='3' or s[0]=='4' for s in states),'confirmation_refused')
                self.sleep(.05)
            else:raise BridgeError('confirmation_timeout')
            for activation,(src,dst) in enumerate(((a,b),(b,a),(a,b),(b,a)),1):
                self.stage='activation_'+str(activation)
                if self.radio:
                    radio_send(src,'RFCONTROL');radio_poll(dst,['CONTROL'])
                else:
                    frame=exchange(src,'NEXTCONTROL','CONTROL');need(len(frame)==1,'control_invalid');unhex(frame[0],108)
                    need(exchange(dst,'CONTROL '+frame[0],'OK')==['CONTROL'],'control_not_accepted')
            for e in self.ends:need(exchange(e,'TRAFFIC','TRAFFIC')==['1'],'traffic_not_ready')
            for code in range(1,5):
                for src,dst in ((a,b),(b,a)):
                    self.stage='status_'+('A' if src is a else 'B')
                    if self.radio:
                        radio_send(src,f'RFSTATUS {code}');radio_poll(dst,['RECEIVED',str(code)])
                    else:
                        frame=exchange(src,f'SENDSTATUS {code}','STATUSFRAME');need(len(frame)==1,'status_frame_invalid');unhex(frame[0],108)
                        need(exchange(dst,'STATUSFRAME '+frame[0],'RECEIVED')==[str(code)],'status_not_received')
            self.result='passed';return True
        except BaseException as error:
            body_failed=True
            self.record_failure(error)
            raise
        finally:
            cleaned=True
            for role,e in zip(('A','B'),self.ends) if started else ():
                self.stage='close_'+role
                try:
                    self.set_point(e,'CLOSE')
                    need(self.timed_exchange(e,'CLOSE','CLOSED',e.now()+2)==['1'],'cleanup_unverified')
                    if self.radio:
                        self.set_point(e,'RFSTAT')
                        raw=self.timed_exchange(e,'RFSTAT','RFSTAT',e.now()+2)
                        need(len(raw)==5,'radio_statistics_invalid')
                        values=[decimal(x,0xffffffff) for x in raw]
                        self.statistics.append(values)
                        need(values[3]==0 and values[4]==1 and values[0]==values[1],'cleanup_unverified')
                except BaseException as error:
                    cleaned=False
                    self.record_failure(error)
            for role,e in zip(('A','B'),self.ends):
                snapshot=getattr(e,'diagnostic',None)
                if valid_snapshot(snapshot):
                    safe_notify(self.notify,{'operator':'enrolled_target_diagnostic','role':role,'snapshot':dict(snapshot)})
                    if self.require_diagnostics and not body_failed and snapshot['fault'] != 0:
                        cleaned=False;self.stage='close_'+role;self.point=(role,'CLOSE')
                        self.record_failure(BridgeError('diagnostic_invalid'))
                elif self.require_diagnostics:
                    cleaned=False;self.stage='close_'+role;self.point=(role,'CLOSE')
                    self.record_failure(BridgeError('diagnostic_missing'))
                trace=getattr(e,'trace',None)
                if valid_target_trace(trace):
                    safe_notify(self.notify,{'operator':'enrolled_target_trace','role':role,'trace':dict(trace)})
                else:
                    partial=getattr(e,'unconfirmed_trace',None)
                    if valid_target_trace(partial):
                        safe_notify(self.notify,{'operator':'enrolled_target_trace_partial','role':role,'trace':dict(partial)})
                    if self.require_trace:
                        cleaned=False;self.stage='close_'+role;self.point=(role,'CLOSE')
                        self.record_failure(BridgeError('diagnostic_missing'))
            released=self.close()
            self.cleanup_outcome=('handles_unverified' if not released else
                                  'protocol_unverified_handles_closed' if not cleaned else 'verified')
            self.timing_event('cleanup',outcome=self.cleanup_outcome)
            if self.first_failure is not None:
                safe_notify(self.notify,'enrolled_cleanup_'+self.cleanup_outcome)
            if not cleaned or not released:
                self.result='cleanup_unverified'
                # An active exception from the body keeps its original cause.
                # A cleanup-only failure still refuses the attempted success.
                if not body_failed:
                    self.record_failure(BridgeError('cleanup_unverified'))
                    raise BridgeError('cleanup_unverified')
    def set_point(self,e,command):
        verb=command.split(' ',1)[0]
        self.point=('A' if e is self.ends[0] else 'B',verb if verb in COMMANDS else 'UNKNOWN')
    def record_failure(self,error):
        if self.first_failure is None:
            stage=self.stage if self.stage in FAILURE_STAGES else 'sync_A'
            self.first_failure=(stage,failure_reason(error))
            self.first_failure_point=self.point
            extra={}
            if self.last_command_timing is not None:
                extra['last_command']=dict(self.last_command_timing)
            self.timing_event('failure',stage=stage,role=self.point[0],command=self.point[1],reason=self.first_failure[1],**extra)
            safe_notify(self.notify,'enrolled_failure_'+stage+'_'+self.first_failure[1])
            safe_notify(self.notify,{'operator':'enrolled_failure_point','role':self.point[0],'command':self.point[1]})
    def close(self):
        ok=True
        for role,e in zip(('A','B'),self.ends):
            self.stage='close_'+role;self.point=(role,'CLOSE')
            try:
                released=e.close() is True
                if not released:self.record_failure(BridgeError('cleanup_unverified'))
                ok=released and ok
            except BaseException as error:
                self.record_failure(error);ok=False
        return ok
    def assert_idle(self):
        need(all(e.closed and e.handle.is_open is False for e in self.ends),'bridge_not_idle')
