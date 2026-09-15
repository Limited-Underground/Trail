"""Pure host tests. Optional --node-exe connects two C++ fixture processes only."""
from pathlib import Path
import argparse
import importlib.util
import queue
import subprocess
import sys
import threading
import unittest
from types import SimpleNamespace
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
import pair_bench_bridge as bridge
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey


class Peer:
    def __init__(self, role):
        self.role=role;self.handle=SimpleNamespace(is_open=True)
        self.closed=False;self.clock=1.;self.commands=[];self.polls=0
        self.key=bytes([role])*32;self.boot=bytes([role])*16;self.device=100 if role==1 else 70000
        self.bad=None;self.wire=None
    def startup_sync(self):self.commands.append('SYNC');return True
    def now(self):
        self.clock+=.001;return self.clock
    def exchange(self, command, expected, deadline, **kwargs):
        self.commands.append(command.split(' ')[0]);parts=command.split(' ')
        if parts[0]=='HELLO':return ['1',str(self.device)]
        if parts[0]=='INIT':
            self.signer=bytes.fromhex(parts[2]);return [self.key.hex().upper(),self.boot.hex().upper(),str(self.device)]
        if parts[0]=='TIME':return [self.key.hex().upper(),self.boot.hex().upper(),str(self.device)]
        if parts[0]=='INV':
            self.wire=bytes.fromhex(parts[2]);Ed25519PublicKey.from_public_bytes(self.signer).verify(self.wire[188:],self.wire[:188]);return ['INV']
        if parts[0]=='SEND':
            step=2 if self.role==2 else (3 if self.commands.count('SEND')==2 else 1)
            return [str(step),'1','A0']
        if parts[0]=='FRAME':return ['FRAME']
        if parts[0]=='REVIEW':return [('02' if self.bad=='transcript' else '01')*32,str(self.device+60000),str(self.device)]
        if parts[0]=='STATUS':
            self.polls+=1
            return ['4' if self.polls>=self.role else '3',str(self.device+1),'0']
        if parts[0]=='CLOSE':return ['0' if self.bad=='cleanup' else '1']
        raise AssertionError(command)
    def close(self):self.closed=True;self.handle.is_open=False;return True


class Serial:
    def __init__(self, raw):self.raw=bytearray(raw);self.is_open=True;self.writes=[]
    def write(self, data):self.writes.append(data);return len(data)
    def read(self, size):data=bytes(self.raw[:size]);del self.raw[:size];return data
    def close(self):self.is_open=False


class Tests(unittest.TestCase):
    def test_pair_and_signature(self):
        a,b=Peer(1),Peer(2);events=[];run=bridge.PairBridge(a,b,notify=events.append,sleep=lambda _:None)
        self.assertEqual(run(),'local_confirmed');self.assertEqual(a.wire,b.wire)
        self.assertEqual(len(a.wire),252);self.assertEqual(a.wire[:8],b'OTEINV\0\2')
        self.assertIn('local_confirmation_A',events);self.assertIn('local_confirmation_B',events)
        self.assertNotIn('CONFIRM',a.commands+b.commands)
        self.assertTrue(run.close() and run.assert_idle())
        with self.assertRaises(bridge.BridgeError):run()
    def test_transcript_mismatch(self):
        a,b=Peer(1),Peer(2);b.bad='transcript';run=bridge.PairBridge(a,b,sleep=lambda _:None)
        with self.assertRaisesRegex(bridge.BridgeError,'transcript_mismatch'):run()
        self.assertIn('CLOSE',a.commands);self.assertIn('CLOSE',b.commands);self.assertTrue(run.close())
    def test_cleanup_failure(self):
        a,b=Peer(1),Peer(2);a.bad='cleanup';run=bridge.PairBridge(a,b,sleep=lambda _:None)
        with self.assertRaisesRegex(bridge.BridgeError,'cleanup_unconfirmed'):run()
        self.assertTrue(run.close())
    def test_numeric_and_hex(self):
        for value in ('01','-1','1.0','ï¼‘ï¼’','18446744073709551616'):
            with self.assertRaises(bridge.BridgeError):bridge.decimal(value)
        for value in ('aa','A','GG'):
            with self.assertRaises(bridge.BridgeError):bridge.unhex(value,1)
    def test_fragmented_serial(self):
        serial=Serial(b'OTPAIR1 READY 1 100\n')
        end=bridge.Endpoint(serial,lambda:True)
        self.assertEqual(end.exchange('HELLO','READY',end.now()+1),['1','100'])
        self.assertTrue(end.close())
    def test_startup_sync_is_single_guarded_lf(self):
        serial=Serial(b'');end=bridge.Endpoint(serial,lambda:True)
        self.assertTrue(end.startup_sync());self.assertEqual(serial.writes,[b'\n'])
        with self.assertRaisesRegex(bridge.BridgeError,'startup_sync_consumed'):end.startup_sync()
        serial=Serial(b'');end=bridge.Endpoint(serial,lambda:False)
        with self.assertRaises(bridge.BridgeError):end.startup_sync()
        self.assertFalse(serial.writes)

    def test_partial_startup_sync_cannot_retry(self):
        serial=Serial(b'');serial.write=lambda data:0
        end=bridge.Endpoint(serial,lambda:True)
        with self.assertRaisesRegex(bridge.BridgeError,'partial_startup_sync'):end.startup_sync()
        with self.assertRaisesRegex(bridge.BridgeError,'startup_sync_consumed'):end.startup_sync()

    def test_guard_refuses_before_write(self):
        serial=Serial(b'');end=bridge.Endpoint(serial,lambda:False)
        with self.assertRaises(bridge.BridgeError):end.exchange('HELLO','READY',end.now()+1)
        self.assertFalse(serial.writes)
    def test_response_overflow(self):
        serial=Serial(b'X'*800+b'\n');end=bridge.Endpoint(serial,lambda:True)
        with self.assertRaisesRegex(bridge.BridgeError,'line_overflow'):end.exchange('HELLO','READY',end.now()+1)
    def test_partial_write(self):
        serial=Serial(b'');serial.write=lambda data:len(data)-1
        end=bridge.Endpoint(serial,lambda:True)
        with self.assertRaisesRegex(bridge.BridgeError,'partial_write'):end.exchange('HELLO','READY',end.now()+1)
    def test_independent_handles(self):
        a=Peer(1)
        with self.assertRaises(bridge.BridgeError):bridge.PairBridge(a,a)

    def test_diagnostic_refusal_queries_both_before_provisioning(self):
        a,b=Peer(1),Peer(2);events=[]
        for peer in (a,b):
            original=peer.exchange
            def exchange(command, expected, deadline, original=original, peer=peer, **kwargs):
                if command=='DIAG':
                    peer.commands.append('DIAG');return ['1','7','1','1']
                return original(command,expected,deadline,**kwargs)
            peer.exchange=exchange
        run=bridge.PairBridge(a,b,notify=events.append,diagnostics=True)
        with self.assertRaisesRegex(bridge.BridgeError,'target_startup_refused'):run()
        for peer in (a,b):
            self.assertNotIn('INIT',peer.commands);self.assertIn('CLOSE',peer.commands)
        self.assertIn('diagnostic_A_1_7_1_1',events)
        self.assertIn('diagnostic_B_1_7_1_1',events)

    def test_diagnostic_discards_bounded_startup_refusal(self):
        wire=Serial(b'OTPAIR1 REFUSED\nOTPAIR1 DIAG 1 7 1 1\n')
        endpoint=bridge.Endpoint(wire,lambda:True)
        self.assertEqual(endpoint.exchange('DIAG','DIAG',endpoint.now()+1,startup=True),['1','7','1','1'])
        wire=Serial(b'OTPAIR1 REFUSED\n'*3)
        endpoint=bridge.Endpoint(wire,lambda:True)
        with self.assertRaisesRegex(bridge.BridgeError,'diagnostic_refusal_overflow'):
            endpoint.exchange('DIAG','DIAG',endpoint.now()+1,startup=True)

    def test_diagnostic_rejects_unbounded_error(self):
        a,b=Peer(1),Peer(2);events=[]
        a.exchange=lambda *args,**kwargs:['1','7','999','1']
        b.exchange=lambda *args,**kwargs:['1','0','0','0']
        run=bridge.PairBridge(a,b,notify=events.append,diagnostics=True)
        self.assertFalse(run.diagnose())
        self.assertEqual(events,['diagnostic_A_unavailable','diagnostic_B_1_0_0_0'])

    def test_diagnostic_report_failure_still_closes_both(self):
        a,b=Peer(1),Peer(2)
        def refuse_report(_):raise RuntimeError('sink failed')
        run=bridge.PairBridge(a,b,notify=refuse_report,diagnostics=True)
        with self.assertRaises(bridge.BridgeError):run()
        self.assertIn('CLOSE',a.commands);self.assertIn('CLOSE',b.commands)

    def test_healthy_diagnostic_requires_no_cleanup(self):
        for cleanup in ('1','2'):
            a,b=Peer(1),Peer(2)
            a.exchange=lambda *args,**kwargs:['1','0','0',cleanup]
            b.exchange=lambda *args,**kwargs:['1','0','0','0']
            self.assertFalse(bridge.PairBridge(a,b,diagnostics=True).diagnose())


    def radio_peers(self, mutate=None):
        peers = [Peer(1), Peer(2)]
        armed = []
        for index, peer in enumerate(peers):
            original = peer.exchange
            def exchange(command, expected, deadline, index=index, peer=peer, original=original, **kwargs):
                if command in ('RADIOINFO', 'RADIO', 'RADIOSTAT'):
                    peer.commands.append(command)
                    if command == 'RADIOINFO': result = list(bridge.RadioPairBridge.RADIO_INFO)
                    elif command == 'RADIO': armed.append(index); result = ['RADIO']
                    else: result = ['3', '2', '2', '1', '0', '10', '1'] if index == 0 else ['3', '1', '1', '2', '0', '10', '1']
                    return mutate(index, command, result) if mutate else result
                return original(command, expected, deadline, **kwargs)
            peer.exchange = exchange
        return peers, armed

    def test_radio_success_uses_no_usb_frames_and_preserves_confirmation(self):
        peers, armed = self.radio_peers(); events = []
        run = bridge.RadioPairBridge(*peers, notify=events.append, sleep=lambda _: None)
        self.assertEqual(run(), 'local_confirmed'); self.assertEqual(armed, [1, 0])
        for peer in peers:
            self.assertNotIn('SEND', peer.commands); self.assertNotIn('FRAME', peer.commands)
            self.assertIn('REVIEW', peer.commands); self.assertIn('CLOSE', peer.commands)
        self.assertIn('radio_A_verified', events); self.assertIn('radio_B_verified', events)
        self.assertIn('local_confirmation_A', events); self.assertIn('local_confirmation_B', events)

    def test_radio_retains_bounded_counts_and_local_duration(self):
        peers,_ = self.radio_peers(); events = []
        self.assertEqual(bridge.RadioPairBridge(*peers, notify=events.append,
                         sleep=lambda _: None)(), 'local_confirmed')
        stats = [event for event in events if '_stats_' in event]
        self.assertEqual(stats, ['radio_A_stats_2_2_1_0_10_1', 'radio_B_stats_1_1_2_0_10_1'])
        self.assertLess(events.index(stats[0]), events.index('radio_A_verified'))
        self.assertLess(events.index(stats[1]), events.index('radio_B_verified'))

    def test_radio_bad_profile_refuses_before_arm(self):
        peers, armed = self.radio_peers(lambda i,c,r: ['bad'] if c == 'RADIOINFO' else r)
        with self.assertRaises(bridge.BridgeError): bridge.RadioPairBridge(*peers)()
        self.assertFalse(armed)
        self.assertTrue(all('CLOSE' in p.commands for p in peers))

    def test_radio_malformed_refused_stopped_wrong_counts_and_expiry(self):
        for row in (['3'], ['6','0','0','0','0','1','1'], ['3','2','2','1','0','1','0'],
                    ['3','1','1','1','0','1','1'], ['3','2','2','1','1','1','1'],
                    ['3','2','2','1','0','60001','1'], ['2','0','0','0','0','1','1']):
            with self.subTest(row=row):
                peers,_ = self.radio_peers(lambda i,c,r: row if i == 0 and c == 'RADIOSTAT' else r)
                with self.assertRaises(bridge.BridgeError): bridge.RadioPairBridge(*peers)()
                self.assertTrue(all('CLOSE' in p.commands for p in peers))

    def test_radio_regression_refuses(self):
        count = [0]
        def mutate(i,c,r):
            if i == 0 and c == 'RADIOSTAT':
                count[0] += 1
                return ['2', '1' if count[0] == 1 else '0', '0','0','0','1','0']
            return r
        peers,_ = self.radio_peers(mutate)
        with self.assertRaisesRegex(bridge.BridgeError, 'regressed'):
            bridge.RadioPairBridge(*peers, sleep=lambda _: None)()

    def test_radio_state_and_elapsed_cannot_regress(self):
        for kind in ('state', 'elapsed'):
            calls = [0]
            def mutate(i, command, row):
                if command != 'RADIOSTAT': return row
                if i == 1: return ['2','0','0','0','0','1','0']
                calls[0] += 1
                if kind == 'state':
                    return ['3','2','2','1','0','10','1'] if calls[0] == 1 else ['2','2','2','1','0','11','1']
                return ['2','1','0','0','0','10' if calls[0] == 1 else '9','0']
            peers,_ = self.radio_peers(mutate)
            with self.subTest(kind=kind), self.assertRaisesRegex(bridge.BridgeError, 'regressed'):
                bridge.RadioPairBridge(*peers, sleep=lambda _: None)()

    def test_radio_poll_global_deadline_is_bounded(self):
        peers,_ = self.radio_peers(lambda i,c,r: ['2','0','0','0','0','0','0'] if c == 'RADIOSTAT' else r)
        def advance(_):
            for peer in peers: peer.clock += 61
        with self.assertRaisesRegex(bridge.BridgeError, 'radio_timeout'):
            bridge.RadioPairBridge(*peers, sleep=advance)()

    def test_radio_arm_refusal_still_closes_both(self):
        peers,_ = self.radio_peers(lambda i,c,r: ['REFUSED'] if c == 'RADIO' else r)
        with self.assertRaisesRegex(bridge.BridgeError, 'radio_arm_refused'):
            bridge.RadioPairBridge(*peers)()
        self.assertTrue(all('CLOSE' in p.commands for p in peers))


class ProcessSerial:
    """Test-only byte pipe: never a serial port and never runs on a device."""
    def __init__(self, exe, role):
        self.proc=subprocess.Popen([str(exe),'node',str(role)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        self.is_open=True;self.timeout=.1;self.write_timeout=.5;self.queue=queue.Queue()
        def pump():
            while True:
                data=self.proc.stdout.read(1)
                self.queue.put(data)
                if not data:break
        self.thread=threading.Thread(target=pump,daemon=True);self.thread.start()
    def write(self,data):
        self.proc.stdin.write(data);self.proc.stdin.flush();return len(data)
    def read(self,size):
        try:return self.queue.get(timeout=self.timeout)
        except queue.Empty:return b''
    def close(self):
        if not self.is_open:return
        self.proc.stdin.close()
        try:self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:self.proc.kill();self.proc.wait();raise RuntimeError('fixture_did_not_close')
        self.thread.join(timeout=1);self.proc.stdout.close();self.proc.stderr.close();self.is_open=False
        if self.proc.returncode:raise RuntimeError('fixture_failed')


def interop(exe):
    handles=[]
    try:
        handles=[ProcessSerial(exe,1),ProcessSerial(exe,2)]
        ends=[bridge.Endpoint(h,lambda h=h:h.is_open) for h in handles]
        events=[];run=bridge.PairBridge(*ends,notify=events.append,sleep=lambda _:None)
        if run()!='local_confirmed':raise RuntimeError('interop_failed')
        if not run.close() or not run.assert_idle():raise RuntimeError('interop_close_failed')
        print('PASS actual two-process C++ endpoint / Python provisioning bridge interop')
    finally:
        for handle in handles:
            if handle.is_open:handle.close()


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--node-exe',type=Path);args=parser.parse_args()
    result=unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(Tests))
    if not result.wasSuccessful():raise SystemExit(1)
    if args.node_exe:interop(args.node_exe)
