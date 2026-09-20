"""Host-only controller tests; --node-exe uses two C++ processes, never devices."""
import argparse,sys,unittest,subprocess
from pathlib import Path
from types import SimpleNamespace
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
import enrolled_pair_bridge as bridge
from pair_bench_bridge_tests import ProcessSerial, Serial
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey,Ed25519PublicKey
class Peer:
    def __init__(self,role):
        self.role=role;self.handle=SimpleNamespace(is_open=True);self.closed=False;self.t=1.;self.commands=[]
        self.public=bytes([role])*32;self.boot=bytes([role])*16;self.device=100+role*1000
        self.bad=None;self.hs=0;self.code=0;self.tx=0;self.done=0;self.pending=None;self.inbox=[];self.rx=0;self.stopped=0
    def now(self):self.t+=.001;return self.t
    def startup_sync(self):self.commands.append('SYNC');return True
    def close(self):self.closed=True;self.handle.is_open=False;return True
    def exchange(self,command,expected,deadline,**kwargs):
        self.commands.append(command);p=command.split();name=p[0]
        if name=='HELLO':return ['1',str(self.device)]
        if name in ('INIT','TIME'):
            if name=='INIT':self.signer=bytes.fromhex(p[2]);self.group=int(p[3])
            return [self.public.hex().upper(),self.boot.hex().upper(),str(self.device)]
        if name=='BEGIN':
            self.inv=bytes.fromhex(p[1]);Ed25519PublicKey.from_public_bytes(self.signer).verify(self.inv[188:],self.inv[:188]);return ['BEGIN']
        if name=='SEND':self.hs+=1;return [str(2 if self.role==2 else (1 if self.hs==1 else 3)),'1','AA']
        if name=='FRAME':return ['FRAME']
        if name=='REVIEW':return [('BB' if self.bad=='review' else 'AA')*32,str(self.device+60000),str(self.device)]
        if name=='STATUS':return ['4',str(self.device+1),'0']
        if name=='NEXTCONTROL':return ['AA'*108]
        if name=='CONTROL':return ['CONTROL']
        if name=='TRAFFIC':return ['0' if self.bad=='ready' else '1']
        if name=='SENDSTATUS':return [f'{int(p[1]):02X}'+'00'*107]
        if name=='STATUSFRAME':return [str(int(p[1][:2],16))]
        if name=='RADIO':return ['RADIO']
        if name in ('RFSEND','RFCONTROL','RFSTATUS'):
            self.tx+=1;self.pending=['HANDSHAKE'] if name=='RFSEND' else ['CONTROL'] if name=='RFCONTROL' else ['RECEIVED',p[1]];return [name]
        if name=='RFPOLL':
            if self.pending:self.peer.inbox.append(self.pending);self.pending=None;self.done=self.tx
            if self.inbox:self.rx+=1;return self.inbox.pop(0)
            return ['WAIT']
        if name=='RFSTAT':return list(map(str,(self.tx,self.done,self.rx,0,self.stopped)))
        if name=='CLOSE':self.stopped=1;return ['0' if self.bad=='close' else '1']
        raise AssertionError(name)
def pair():
    a,b=Peer(1),Peer(2);a.peer=b;b.peer=a;return a,b
class ReceiveLifecyclePeer(Peer):
    """Radio seam: finishReceive/consume leaves standby until a full service.

    Unlike an always-delivering inbox, air transmission is lost while the peer
    is not receiving. STATUS/REVIEW model completion-only target maintenance.
    """
    def __init__(self,role,air):
        super().__init__(role);self.air=air;self.receiving=False;self.dropped=[]
    def now(self):self.air['time']+=.01;return self.air['time']
    def exchange(self,command,expected,deadline,**kwargs):
        verb=command.split()[0]
        if verb=='RADIO':self.receiving=True
        if verb=='RFPOLL':
            self.commands.append(command)
            if self.pending:
                self.receiving=False
                event=(self.role,self.pending[:])
                self.air['transmissions'].append(event)
                if self.peer.receiving:self.peer.inbox.append(self.pending)
                else:self.dropped.append(event)
                self.pending=None;self.done=self.tx
                return ['WAIT']
            if self.inbox:
                # service captures RX_DONE in standby, receive consumes it.
                self.receiving=False;self.rx+=1
                return self.inbox.pop(0)
            self.receiving=True
            return ['WAIT']
        return super().exchange(command,expected,deadline,**kwargs)
def receive_lifecycle_pair():
    air={'time':1.,'transmissions':[]}
    a,b=ReceiveLifecyclePeer(1,air),ReceiveLifecyclePeer(2,air)
    a.peer=b;b.peer=a;return a,b
def run(a,b,**kw):return bridge.EnrolledBridge(a,b,signer=Ed25519PrivateKey.generate(),group=17,epoch=1,sleep=lambda _:None,**kw)
class Tests(unittest.TestCase):
    def test_receive_lifecycle_complete_radio_exchange(self):
        a,b=receive_lifecycle_pair();r=run(a,b,radio=True)
        try:self.assertTrue(r())
        finally:self.assertEqual(a.dropped+b.dropped,[])
        self.assertEqual(r.statistics,[[8,8,7,0,1],[7,7,8,0,1]])
        kinds=[frame[0] for _,frame in a.air['transmissions']]
        self.assertEqual(kinds,['HANDSHAKE']*3+['CONTROL']*4+['RECEIVED']*8)
        self.assertIn('REVIEW',b.commands);self.assertIn('STATUS',b.commands)
    def test_receive_rearm_failure_stops_before_next_stage(self):
        for reply in (['HANDSHAKE'],['CONTROL'],[],['WAIT','extra'],None):
            with self.subTest(reply=reply):
                a,b=receive_lifecycle_pair();original=b.exchange;consumed=False
                def exchange(command,*args,**kwargs):
                    nonlocal consumed
                    if command=='RFPOLL' and consumed:
                        if reply is None:raise bridge.BridgeError('device_changed')
                        return reply
                    result=original(command,*args,**kwargs)
                    if result==['HANDSHAKE']:consumed=True
                    return result
                b.exchange=exchange;r=run(a,b,radio=True)
                with self.assertRaises(bridge.BridgeError):r()
                self.assertEqual(r.first_failure_point,('B','RFPOLL'))
                self.assertNotIn('RFSEND',b.commands)
                self.assertTrue(a.closed and b.closed)
    def test_rearm_endpoint_identity_guard_prevents_write(self):
        s=Serial(b'');e=bridge.Endpoint(s,lambda:False)
        with self.assertRaises(bridge.BridgeError):e.exchange('RFPOLL','RF',e.now()+1)
        self.assertEqual(s.writes,[])
    def test_usb_exchange(self):
        a,b=pair();r=run(a,b);self.assertTrue(r());r.assert_idle();self.assertEqual(a.inv,b.inv);self.assertEqual(a.commands[0],'SYNC')
        self.assertFalse(any(c.startswith('CONFIRM') for c in a.commands+b.commands))
        with self.assertRaises(bridge.BridgeError):r()
    def test_command_driven_radio_and_statistics(self):
        a,b=pair();r=run(a,b,radio=True);self.assertTrue(r());self.assertEqual(r.statistics,[[8,8,7,0,1],[7,7,8,0,1]])
    def test_completed_transmit_rearms_before_peer_advances(self):
        a,b=pair();timeline=[]
        for role,e in (('A',a),('B',b)):
            original=e.exchange;e.needs_rearm=False
            def exchange(command,*args,e=e,original=original,role=role,**kwargs):
                verb=command.split()[0];timeline.append((role,verb))
                if verb in ('RFSEND','RFCONTROL','RFSTATUS'):
                    self.assertFalse(e.peer.needs_rearm,'peer RX must be rearmed before next transmission')
                if verb=='RFPOLL':
                    e.needs_rearm=False
                result=original(command,*args,**kwargs)
                if verb=='RFSTAT' and e.tx and e.tx==e.done and not e.stopped:e.needs_rearm=True
                return result
            e.exchange=exchange
        r=run(a,b,radio=True);self.assertTrue(r())
        for index,(role,verb) in enumerate(timeline):
            if verb=='RFSTAT' and index+1<len(timeline) and timeline[index+1][1]!='CLOSE':
                if timeline[index-1][1]=='RFPOLL':self.assertEqual(timeline[index+1],(role,'RFPOLL'))
    def test_rearm_poll_refusal_prevents_peer_advance(self):
        a,b=pair();original=a.exchange;completed_seen=False;events=[]
        def exchange(command,*args,**kwargs):
            nonlocal completed_seen
            if command=='RFPOLL' and completed_seen:raise bridge.BridgeError('target_refused')
            result=original(command,*args,**kwargs)
            if command=='RFSTAT' and a.tx==a.done and a.tx:completed_seen=True
            return result
        a.exchange=exchange;r=run(a,b,radio=True,notify=events.append)
        with self.assertRaisesRegex(bridge.BridgeError,'target_refused'):r()
        self.assertEqual(r.first_failure,('handshake_1','target_refused'))
        self.assertEqual(r.first_failure_point,('A','RFPOLL'))
        self.assertNotIn('RFPOLL',b.commands);self.assertNotIn('RFSEND',b.commands)
        self.assertTrue(a.closed and b.closed)
    def test_refusal_cleanup(self):
        for failure in ('review','ready','close'):
            with self.subTest(failure=failure):
                a,b=pair();b.bad=failure;r=run(a,b)
                with self.assertRaises(bridge.BridgeError):r()
                self.assertTrue(a.closed and b.closed)
    def test_initial_clock_failure_closes_both(self):
        a,b=pair();a.now=lambda: (_ for _ in ()).throw(bridge.BridgeError('clock'));r=run(a,b)
        with self.assertRaises(bridge.BridgeError):r()
        self.assertTrue(a.closed and b.closed)
    def test_duplicate_startup_ready(self):
        s=Serial(b'OTENROLL1 READY 1 100\nOTENROLL1 READY 1 101\nOTENROLL1 ID '+b'AA'*32+b' '+b'BB'*16+b' 101\n')
        e=bridge.Endpoint(s,lambda:True);self.assertTrue(e.startup_sync())
        self.assertEqual(e.exchange('HELLO','READY',e.now()+1,startup=True),['1','100'])
        self.assertEqual(len(e.exchange('INIT 1 '+('AA'*32)+' 17','ID',e.now()+1,startup=True)),3)
    def test_guard_prevents_write(self):
        s=Serial(b'');e=bridge.Endpoint(s,lambda:False)
        with self.assertRaises(bridge.BridgeError):e.startup_sync()
        self.assertEqual(s.writes,[])
    def test_invitation_bounds(self):
        a,b=pair();key=Ed25519PrivateKey.generate();ids=((a.public,a.boot,100),(b.public,b.boot,200))
        for group,epoch in ((0,1),(17,0),(1<<64,1),(17,1<<32)):
            with self.assertRaises(bridge.BridgeError):bridge.invitation(key,*ids,group,epoch)
class DiagnosticTests(unittest.TestCase):
    def test_begin_timeout_survives_protocol_cleanup_failure(self):
        a,b=pair();original=a.exchange;events=[]
        def exchange(command,*args,**kwargs):
            if command.startswith('BEGIN '):raise bridge.BridgeError('bridge_timeout')
            return original(command,*args,**kwargs)
        a.exchange=exchange;b.bad='close';r=run(a,b,notify=events.append)
        with self.assertRaisesRegex(bridge.BridgeError,'^bridge_timeout$'):r()
        self.assertEqual(r.first_failure,('begin_A','timeout'))
        self.assertEqual(r.cleanup_outcome,'protocol_unverified_handles_closed')
        self.assertEqual([e for e in events if type(e) is str],['enrolled_failure_begin_A_timeout','enrolled_cleanup_protocol_unverified_handles_closed'])
        self.assertTrue(a.closed and b.closed)

    def test_first_cause_survives_handle_cleanup_failure(self):
        a,b=pair();original=a.exchange;events=[]
        def exchange(command,*args,**kwargs):
            if command.startswith('BEGIN '):raise bridge.BridgeError('bridge_timeout')
            return original(command,*args,**kwargs)
        a.exchange=exchange;a.close=lambda:False;r=run(a,b,notify=events.append)
        with self.assertRaisesRegex(bridge.BridgeError,'^bridge_timeout$'):r()
        self.assertEqual(r.first_failure,('begin_A','timeout'));self.assertEqual(r.cleanup_outcome,'handles_unverified')
        self.assertEqual([e for e in events if type(e) is str],['enrolled_failure_begin_A_timeout','enrolled_cleanup_handles_unverified'])
        self.assertTrue(a.handle.is_open and b.closed)

    def test_diagnostics_callback_cannot_mask_original_or_skip_cleanup(self):
        a,b=pair();original=a.exchange
        def exchange(command,*args,**kwargs):
            if command=='HELLO':raise bridge.BridgeError('bridge_timeout')
            return original(command,*args,**kwargs)
        def notify(event):raise RuntimeError('secret callback text')
        a.exchange=exchange;r=run(a,b,notify=notify)
        with self.assertRaisesRegex(bridge.BridgeError,'^bridge_timeout$'):r()
        self.assertEqual(r.first_failure,('hello_A','timeout'));self.assertTrue(a.closed and b.closed)

    def test_unknown_exception_text_is_never_emitted(self):
        a,b=pair();events=[];a.startup_sync=lambda:(_ for _ in ()).throw(RuntimeError('PRIVATE_ID_KEY_PAYLOAD'))
        r=run(a,b,notify=events.append)
        with self.assertRaises(RuntimeError):r()
        self.assertEqual([e for e in events if type(e) is str],['enrolled_failure_sync_A_unknown','enrolled_cleanup_verified'])
        self.assertNotIn('PRIVATE',repr(r.first_failure)+repr(events));self.assertTrue(a.closed and b.closed)
        self.assertEqual(bridge.failure_reason(bridge.BridgeError('PRIVATE_ID_KEY_PAYLOAD')),'unknown')

    def test_target_refusal_and_read_failure_have_fixed_categories(self):
        e=bridge.Endpoint(Serial(b'OTENROLL1 REFUSED 1\n'),lambda:True)
        with self.assertRaises(bridge.BridgeError) as caught:e.exchange('HELLO','READY',e.now()+1,startup=True)
        self.assertEqual(bridge.failure_reason(caught.exception),'target_refused')
        s=Serial(b'');s.read=lambda _:(_ for _ in ()).throw(RuntimeError('PRIVATE_READ_TEXT'))
        e=bridge.Endpoint(s,lambda:True)
        with self.assertRaises(bridge.BridgeError) as caught:e.exchange('HELLO','READY',e.now()+1,startup=True)
        self.assertEqual(bridge.failure_reason(caught.exception),'serial_read_failed')

    def test_later_stage_failure_records_first_activation(self):
        a,b=pair();original=b.exchange;events=[]
        def exchange(command,*args,**kwargs):
            if command.startswith('CONTROL '):raise bridge.BridgeError('target_refused')
            return original(command,*args,**kwargs)
        b.exchange=exchange;r=run(a,b,notify=events.append)
        with self.assertRaises(bridge.BridgeError):r()
        self.assertEqual(r.first_failure,('activation_1','target_refused'))
        self.assertIn('enrolled_failure_activation_1_target_refused',events)

class HostTimingTests(unittest.TestCase):
    def events(self,**options):
        a,b=pair();events=[];clock=[1000000]
        for e in (a,b):
            original=e.exchange
            def exchange(*args,original=original,**kwargs):
                clock[0]+=10000
                return original(*args,**kwargs)
            e.exchange=exchange
        r=run(a,b,notify=events.append,enable_host_timing=True,
              timing_clock_ns=lambda:clock[0],**options)
        return a,b,r,events
    def timing(self,events):
        return [event for event in events if type(event) is dict and event.get('operator')=='enrolled_host_timing']
    def test_opt_in_preserves_commands_and_measures_responses_without_private_data(self):
        a,b,r,events=self.events(radio=True);self.assertTrue(r())
        aa,bb=pair();baseline_events=[];baseline=run(aa,bb,radio=True,notify=baseline_events.append);self.assertTrue(baseline())
        self.assertEqual([x.split()[0] for x in a.commands],[x.split()[0] for x in aa.commands])
        self.assertEqual([x.split()[0] for x in b.commands],[x.split()[0] for x in bb.commands])
        self.assertFalse(self.timing(baseline_events))
        timing=self.timing(events);self.assertTrue(timing)
        self.assertTrue(all(bridge.valid_host_timing(event) for event in timing))
        self.assertEqual([e['elapsed_us'] for e in timing],sorted(e['elapsed_us'] for e in timing))
        commands=[e for e in timing if e['kind']=='command']
        self.assertTrue(all(e['duration_us']==10 for e in commands if e['command']!='SYNC'))
        comparison=next(e for e in timing if e['kind']=='comparison')
        self.assertEqual(comparison['review_remaining_ms'],{'A':60000,'B':60000})
        self.assertEqual(comparison['review_age_us'],{'A':10,'B':0})
        confirmed=[e for e in timing if e['kind']=='status4']
        self.assertEqual([e['role'] for e in confirmed],['A','B'])
        first_activation=next(e for e in commands if e['stage']=='activation_1')
        self.assertLessEqual(max(e['elapsed_us'] for e in confirmed),first_activation['start_us'])
        self.assertEqual(timing[-1]['kind'],'cleanup');self.assertEqual(timing[-1]['outcome'],'verified')
        for private in (a.public.hex(),a.boot.hex(),a.inv.hex(),'AA'*32):
            self.assertNotIn(private.lower(),repr(timing).lower())
    def test_notification_failure_preserves_failure_cause_and_cleanup_timing(self):
        a,b,r,events=self.events(radio=True);original=b.exchange
        def exchange(command,*args,**kwargs):
            if command=='RFPOLL' and a.tx==3:raise bridge.BridgeError('target_refused')
            return original(command,*args,**kwargs)
        b.exchange=exchange
        def broken_notify(event):events.append(event);raise RuntimeError('PRIVATE_CALLBACK')
        r.notify=broken_notify
        with self.assertRaisesRegex(bridge.BridgeError,'target_refused'):r()
        self.assertEqual(r.first_failure,('activation_1','target_refused'))
        timing=self.timing(events)
        self.assertEqual(len([e for e in timing if e['kind']=='failure']),1)
        self.assertEqual(timing[-1]['kind'],'cleanup')
        self.assertTrue(a.closed and b.closed)
        self.assertNotIn('PRIVATE_CALLBACK',repr(events))
    def test_confirmation_cap_preserves_activation_transport_and_milestones(self):
        a,b,r,events=self.events(radio=True)
        metric={'guard_ns':9000,'read_ns':2000,'write_ns':1000,
                'guard_calls':9,'read_calls':2,'write_calls':1}
        for e in (a,b):
            original=e.exchange;e.polls=0;e.transport_metrics=dict(metric)
            def exchange(command,*args,e=e,original=original,**kwargs):
                if command=='STATUS':
                    e.polls+=1
                    if e.polls<=150:return ['3','0','0']
                result=original(command,*args,**kwargs)
                if command=='RFCONTROL':raise bridge.BridgeError('target_refused')
                return result
            e.exchange=exchange
        with self.assertRaisesRegex(bridge.BridgeError,'target_refused'):r()
        timing=self.timing(events)
        self.assertLessEqual(len(timing),262)
        statuses=[e for e in timing if e['kind']=='command' and e['stage']=='wait_local_confirmation']
        self.assertEqual(len(statuses),128)
        marker=next(e for e in timing if e['kind']=='truncated')
        self.assertEqual(marker['scope'],'confirmation_status');self.assertEqual(marker['command_limit'],128)
        self.assertEqual(sum(e['kind']=='truncated' for e in timing),1)
        self.assertEqual(sum(e['kind']=='status4' for e in timing),2)
        activation=next(e for e in timing if e['kind']=='command' and e['stage']=='activation_1')
        self.assertEqual(activation['transport'],metric);self.assertFalse(activation['ok'])
        failure=next(e for e in timing if e['kind']=='failure')
        self.assertEqual(failure['last_command']['transport'],metric)
        self.assertEqual(failure['last_command']['command'],'RFCONTROL')
        self.assertEqual(timing[-1]['kind'],'cleanup')
        self.assertTrue(all(bridge.valid_host_timing(e) for e in timing))
    def test_global_cap_retains_failed_command_cost_without_extra_command_event(self):
        a,b,r,events=self.events(radio=True);r.timing_commands=256
        metric={'guard_ns':9000,'read_ns':2000,'write_ns':1000,
                'guard_calls':9,'read_calls':2,'write_calls':1}
        a.transport_metrics=dict(metric);original=a.exchange
        def exchange(command,*args,**kwargs):
            result=original(command,*args,**kwargs)
            if command=='RFCONTROL':raise bridge.BridgeError('target_refused')
            return result
        a.exchange=exchange
        with self.assertRaisesRegex(bridge.BridgeError,'target_refused'):r()
        timing=self.timing(events)
        self.assertFalse(any(e['kind']=='command' for e in timing))
        self.assertLessEqual(len(timing),6) # 256 already emitted plus six milestones
        marker=next(e for e in timing if e['kind']=='truncated')
        self.assertEqual(marker['scope'],'all_commands');self.assertEqual(marker['command_limit'],256)
        failure=next(e for e in timing if e['kind']=='failure');last=failure['last_command']
        self.assertEqual((last['stage'],last['role'],last['command']),('activation_1','A','RFCONTROL'))
        self.assertEqual(last['transport'],metric);self.assertEqual(last['duration_us'],10)
        self.assertFalse(last['ok']);self.assertEqual(timing[-1]['kind'],'cleanup')
        self.assertTrue(all(bridge.valid_host_timing(e) for e in timing))
        for key,value in (('payload','PRIVATE'),('duration_us',True),('kind','failure')):
            altered=dict(failure,last_command=dict(last));altered['last_command'][key]=value
            self.assertFalse(bridge.valid_host_timing(altered))
    def test_broken_diagnostic_clock_does_not_change_exchange(self):
        a,b=pair()
        def broken_clock():raise RuntimeError('PRIVATE_CLOCK')
        r=run(a,b,enable_host_timing=True,timing_clock_ns=broken_clock)
        self.assertTrue(r());self.assertTrue(a.closed and b.closed)
    def test_timing_validator_rejects_extra_fields_types_ranges_and_payloads(self):
        a,b,r,events=self.events();self.assertTrue(r())
        event=next(e for e in self.timing(events) if e['kind']=='command')
        for key,value in (('payload','PRIVATE'),('command','BEGIN PRIVATE'),('role','PRIVATE'),
                          ('elapsed_us',True),('duration_us',-1),('version',True),('kind',[]),
                          ('stage','PRIVATE'),('start_us',1<<64),('ok',1)):
            changed=dict(event);changed[key]=value
            self.assertFalse(bridge.valid_host_timing(changed),(key,value))
        comparison=next(e for e in self.timing(events) if e['kind']=='comparison')
        changed=dict(comparison);changed['review_remaining_ms']={'A':60001,'B':60000}
        self.assertFalse(bridge.valid_host_timing(changed))
        self.assertFalse(bridge.valid_host_timing(None))

class TransportTimingTests(unittest.TestCase):
    def endpoint(self,enabled=True):
        clock=[1000];calls={'guard':0,'read':0,'write':0}
        class DelayedSerial(Serial):
            def read(self,size):
                clock[0]+=700;calls['read']+=1
                return super().read(size)
            def write(self,raw):
                clock[0]+=1100;calls['write']+=1
                return super().write(raw)
        def guard():clock[0]+=200;calls['guard']+=1;return True
        endpoint=bridge.Endpoint(DelayedSerial(b'OTENROLL1 OK RADIO\n'),guard,monotonic=lambda:1.,
                                 enable_transport_timing=enabled,transport_clock_ns=lambda:clock[0])
        return endpoint,clock,calls
    def test_guard_serial_read_write_are_separate_and_do_not_add_calls(self):
        endpoint,clock,calls=self.endpoint()
        self.assertEqual(endpoint.exchange('RADIO','OK',2.),['RADIO'])
        count=len(b'OTENROLL1 OK RADIO\n')
        self.assertEqual(calls,{'guard':3+2*count,'read':count,'write':1})
        metrics=endpoint.transport_metrics
        self.assertEqual(metrics,{'guard_ns':200*(3+2*count),'read_ns':700*count,'write_ns':1100,
                                  'guard_calls':3+2*count,'read_calls':count,'write_calls':1})
        baseline,_,baseline_calls=self.endpoint(False)
        self.assertEqual(baseline.exchange('RADIO','OK',2.),['RADIO'])
        self.assertEqual(baseline_calls,calls);self.assertIsNone(baseline.transport_metrics)
        endpoint.handle.raw=bytearray(b'OTENROLL1 OK RADIO\n')
        self.assertEqual(endpoint.exchange('RADIO','OK',2.),['RADIO'])
        self.assertEqual(endpoint.transport_metrics,metrics) # reset, not cumulative
    def test_failed_calls_are_measured_without_masking_original(self):
        endpoint,clock,calls=self.endpoint()
        def failed_read(size):clock[0]+=1700;raise OSError('PRIVATE_READ_FAILURE')
        endpoint.handle.read=failed_read
        with self.assertRaisesRegex(bridge.BridgeError,'^serial_read_failed$'):
            endpoint.exchange('RADIO','OK',2.)
        self.assertEqual(endpoint.transport_metrics['read_ns'],1700)
        self.assertEqual(endpoint.transport_metrics['read_calls'],1)
        # Even a failed read is followed by fresh identity admission.
        self.assertEqual(endpoint.transport_metrics['guard_calls'],5)
        self.assertNotIn('PRIVATE',repr(endpoint.transport_metrics))
        endpoint,clock,calls=self.endpoint()
        def refused_guard():clock[0]+=900;return False
        endpoint.guard=refused_guard
        with self.assertRaisesRegex(bridge.BridgeError,'^passive_lease_invalid$'):
            endpoint.exchange('RADIO','OK',2.)
        self.assertEqual(endpoint.transport_metrics['guard_ns'],900)
        self.assertEqual(endpoint.transport_metrics['write_calls'],0)
    def test_invalid_diagnostic_clock_discards_metrics_and_preserves_result(self):
        for clock in (lambda:True,lambda:-1,iter([10,9]).__next__):
            endpoint,_,_=self.endpoint();endpoint.transport_clock_ns=clock
            self.assertEqual(endpoint.exchange('RADIO','OK',2.),['RADIO'])
            self.assertIsNone(endpoint.transport_metrics)
        endpoint,_,_=self.endpoint()
        def broken_clock():raise RuntimeError('PRIVATE_CLOCK')
        endpoint.transport_clock_ns=broken_clock
        self.assertEqual(endpoint.exchange('RADIO','OK',2.),['RADIO'])
        self.assertIsNone(endpoint.transport_metrics)
    def test_startup_sync_measured_and_command_event_accepts_optional_metrics(self):
        endpoint,_,_=self.endpoint();self.assertTrue(endpoint.startup_sync())
        self.assertEqual(endpoint.transport_metrics,{'guard_ns':400,'read_ns':0,'write_ns':1100,
                                                   'guard_calls':2,'read_calls':0,'write_calls':1})
        events=[];r=run(endpoint,Peer(2),notify=events.append,enable_host_timing=True)
        r.timing_stamp();r.set_point(endpoint,'RADIO')
        self.assertEqual(r.timed_exchange(endpoint,'RADIO','OK',2.),['RADIO'])
        self.assertEqual(len(events),1)
        event=events[0];self.assertTrue(bridge.valid_host_timing(event))
        self.assertEqual(event['transport'],endpoint.transport_metrics)
        for key,value in (('guard_ns',True),('read_ns',-1),('write_calls',1<<64),('payload','PRIVATE')):
            altered=dict(event,transport=dict(event['transport']));altered['transport'][key]=value
            self.assertFalse(bridge.valid_host_timing(altered))
        altered=dict(event,transport=dict(event['transport']));del altered['transport']['guard_calls']
        self.assertFalse(bridge.valid_host_timing(altered))

class SnapshotTests(unittest.TestCase):
    line=b'OTENROLL1 DIAG 1 0 4 9 12 30 0 1 2 8 8\n'
    snapshot=dict(zip(bridge.SNAPSHOT_FIELDS,(0,4,9,12,30,0,1,2,8,8)))
    def endpoint(self,raw):
        ticks=iter(x/10000 for x in range(100000))
        return bridge.Endpoint(Serial(raw),lambda:True,monotonic=lambda:next(ticks))
    def test_terminal_close_and_refusal_commit(self):
        for command,expected,terminal in [('CLOSE','CLOSED',b'CLOSED 1'),('RFPOLL','RF',b'REFUSED 1')]:
            e=self.endpoint(self.line+b'OTENROLL1 '+terminal+b'\n')
            if command=='CLOSE':self.assertEqual(e.exchange(command,expected,e.now()+1),['1'])
            else:
                with self.assertRaisesRegex(bridge.BridgeError,'target_refused'):e.exchange(command,expected,e.now()+1)
            self.assertEqual(e.diagnostic,self.snapshot)
    def test_reject_malformed_out_of_context_duplicate_and_partial(self):
        malformed=[self.line.replace(b'DIAG 1',b'DIAG 2'),self.line.replace(b'1 0 4',b'1 13 4'),
            self.line.replace(b'1 0 4',b'1 00 4'),self.line.replace(b' 4 9 ',b' 10 9 '),
            self.line.replace(b' 8 8',b' 8 9'),self.line.replace(b' 8 8',b' 17 17'),
            self.line.replace(b' 30 ',b' 18446744073709551616 '),self.line.replace(b' 30 ',b' -1 '),
            self.line.replace(b' 30 ',b' +1 '),self.line.replace(b'\n',b'\r\n'),self.line.replace(b' 8 8',b' 8'),
            self.line+b'OTENROLL1 OK RFSEND\n',self.line+b'noise\n',self.line+self.line,
            self.line+b'OTENROLL1 REFUSED\n',self.line+b'OTENROLL1 CLOSED 1\n',self.line[:-1],self.line]
        for raw in malformed:
            with self.subTest(raw=raw):
                e=self.endpoint(raw)
                with self.assertRaises(bridge.BridgeError):e.exchange('RFSEND','OK',e.now()+.1,startup=True)
                self.assertIsNone(e.diagnostic)
    def test_second_exchange_snapshot_rejected_preserving_first(self):
        e=self.endpoint(self.line+b'OTENROLL1 CLOSED 1\n'+self.line+b'OTENROLL1 CLOSED 1\n')
        self.assertEqual(e.exchange('CLOSE','CLOSED',e.now()+1),['1'])
        with self.assertRaises(bridge.BridgeError):e.exchange('CLOSE','CLOSED',e.now()+1)
        self.assertEqual(e.diagnostic,self.snapshot)
    def test_required_snapshots_success_and_missing_failure(self):
        for present in (0,1,2):
            a,b=pair();events=[]
            for e in (a,b)[:present]:e.diagnostic=dict(self.snapshot)
            r=run(a,b,require_diagnostics=True,notify=events.append)
            if present==2:self.assertTrue(r())
            else:
                with self.assertRaises(bridge.BridgeError):r()
                self.assertEqual(r.first_failure_point,('A' if present==0 else 'B','CLOSE'))
            self.assertTrue(a.closed and b.closed)
            snapshots=[e for e in events if type(e) is dict and e['operator']=='enrolled_target_diagnostic']
            self.assertEqual(len(snapshots),present)
    def test_third_handshake_each_radio_command_role_is_precise(self):
        for role,verb in [('A','RFSEND'),('A','RFPOLL'),('A','RFSTAT'),('B','RFPOLL')]:
            a,b=pair();r=run(a,b,radio=True);endpoint=a if role=='A' else b;original=endpoint.exchange
            def exchange(command,*args,**kwargs):
                if r.stage=='handshake_3' and command==verb:raise bridge.BridgeError('target_refused')
                return original(command,*args,**kwargs)
            endpoint.exchange=exchange
            with self.assertRaisesRegex(bridge.BridgeError,'target_refused'):r()
            self.assertEqual(r.first_failure_point,(role,verb))
            self.assertEqual(r.first_failure,('handshake_3','target_refused'))
            self.assertTrue(a.closed and b.closed)
    def test_nonzero_fault_refuses_strict_success_but_preserves_snapshot(self):
        a,b=pair();events=[]
        a.diagnostic=dict(self.snapshot);a.diagnostic['fault']=1;b.diagnostic=dict(self.snapshot)
        r=run(a,b,require_diagnostics=True,notify=events.append)
        with self.assertRaises(bridge.BridgeError):r()
        self.assertEqual(r.first_failure,('close_A','invalid_frame_shape'))
        self.assertEqual(r.first_failure_point,('A','CLOSE'))
        self.assertTrue(a.closed and b.closed)
        self.assertEqual(len([e for e in events if type(e) is dict and e['operator']=='enrolled_target_diagnostic']),2)
    def test_host_signer_failure_is_unknown_command(self):
        a,b=pair();r=run(a,b)
        r.signer=SimpleNamespace(public_key=lambda:(_ for _ in ()).throw(RuntimeError('PRIVATE')))
        with self.assertRaises(RuntimeError):r()
        self.assertEqual(r.first_failure_point,('A','UNKNOWN'))
        self.assertTrue(a.closed and b.closed)
    def test_snapshot_callback_exception_does_not_skip_handle_release(self):
        a,b=pair();a.diagnostic=dict(self.snapshot);b.diagnostic=dict(self.snapshot)
        def notify(event):
            if type(event) is dict:raise RuntimeError('PRIVATE')
        r=run(a,b,require_diagnostics=True,notify=notify)
        self.assertTrue(r());self.assertTrue(a.closed and b.closed)
    def test_handshake_third_failure_point_and_snapshot_survive_cleanup_failure(self):
        a,b=pair();events=[];original=a.exchange
        def exchange(command,*args,**kwargs):
            if command=='RFSEND' and a.tx==1:
                a.diagnostic=dict(self.snapshot);raise bridge.BridgeError('target_refused')
            return original(command,*args,**kwargs)
        a.exchange=exchange;b.bad='close';r=run(a,b,radio=True,require_diagnostics=True,notify=events.append)
        with self.assertRaisesRegex(bridge.BridgeError,'^target_refused$'):r()
        self.assertEqual(r.first_failure,('handshake_3','target_refused'))
        self.assertEqual(r.first_failure_point,('A','RFSEND'))
        self.assertIn({'operator':'enrolled_failure_point','role':'A','command':'RFSEND'},events)
        self.assertIn({'operator':'enrolled_target_diagnostic','role':'A','snapshot':self.snapshot},events)
        self.assertTrue(a.closed and b.closed)

def diagnostic_interop(exe):
    raw=subprocess.run([str(exe),'--emit-fixture'],check=True,capture_output=True).stdout
    assert raw.startswith(b'OTENROLL1 DIAG 1 ') and raw.count(b'\n')==1
    e=bridge.Endpoint(Serial(raw+b'OTENROLL1 CLOSED 1\n'),lambda:True)
    assert e.exchange('CLOSE','CLOSED',e.now()+1)==['1']
    assert bridge.valid_snapshot(e.diagnostic)
    print('PASS actual C++ diagnostic serializer / Python terminal parser interop')

def interop(exe):
    handles=[ProcessSerial(exe,1),ProcessSerial(exe,2)]
    try:
        ends=[bridge.Endpoint(h,lambda h=h:h.is_open) for h in handles]
        r=run(*ends);assert r();r.assert_idle()
        print('PASS actual enrolled two-process C++ / Python status bridge interop')
    finally:
        for h in handles:
            if h.is_open:h.close()
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--node-exe');parser.add_argument('--diagnostic-exe');args,other=parser.parse_known_args()
    if args.diagnostic_exe:diagnostic_interop(args.diagnostic_exe)
    if args.node_exe:interop(args.node_exe)
    unittest.main(argv=[sys.argv[0],*other])
