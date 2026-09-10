import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/"tools"))
from security_policy_endpoint import Endpoint,EndpointError
C="0123456789abcdef0123456789abcdef"
R=f"SEC_EVAL1 ot187-policy-v0 {C} pass\n".encode()
class Handle:
    def __init__(self,chunks=None):
        self.is_open=True;self.now=0.0;self.chunks=list(chunks if chunks is not None else [R]);self.writes=[];self.timeout=0;self.write_timeout=0;self.close_calls=0
    def clock(self):return self.now
    def write(self,raw):self.writes.append(raw);return len(raw)
    def read(self,size):
        assert size==129 and 0<self.timeout<=0.25
        self.now+=min(self.timeout,0.01)
        return self.chunks.pop(0) if self.chunks else b""
    def close(self):self.close_calls+=1;self.is_open=False
class Tests(unittest.TestCase):
    def endpoint(self,h,guard=lambda:True):return Endpoint(h,guard=guard,monotonic=h.clock)
    def test_success_and_close(self):
        h=Handle();e=self.endpoint(h);self.assertEqual(R,e.run_once(C,1));self.assertEqual(1,len(h.writes));self.assertEqual(.5,h.write_timeout);self.assertTrue(e.close());self.assertTrue(e.close());self.assertEqual(1,h.close_calls)
    def test_fragmentation(self):
        wire=R.replace(b"\n",b"\r\n")
        for i in range(len(wire)+1):
            h=Handle([wire[:i],wire[i:]]);self.assertEqual(R,self.endpoint(h).run_once(C,1))
    def test_invalid_consumes(self):
        h=Handle();e=self.endpoint(h)
        with self.assertRaises(EndpointError):e.run_once("bad",1)
        with self.assertRaisesRegex(EndpointError,"consumed"):e.run_once(C,1)
        self.assertEqual([],h.writes)
    def test_partial_write_no_retry(self):
        h=Handle();h.write=lambda raw:len(raw)-1;e=self.endpoint(h)
        with self.assertRaisesRegex(EndpointError,"partial_write"):e.run_once(C,1)
        with self.assertRaisesRegex(EndpointError,"consumed"):e.run_once(C,1)
    def test_late_write(self):
        h=Handle()
        def write(raw):h.now=1;return len(raw)
        h.write=write
        with self.assertRaisesRegex(EndpointError,"write_late"):self.endpoint(h).run_once(C,1)
    def test_guard_changes_during_write(self):
        h=Handle();valid=[True]
        def write(raw):valid[0]=False;return len(raw)
        h.write=write;e=self.endpoint(h,lambda:valid[0])
        with self.assertRaisesRegex(EndpointError,"admission_failed"):e.run_once(C,1)
        self.assertTrue(e.close())
    def test_guard_changes_during_read(self):
        h=Handle();valid=[True];original=h.read
        def read(n):out=original(n);valid[0]=False;return out
        h.read=read
        with self.assertRaises(EndpointError):self.endpoint(h,lambda:valid[0]).run_once(C,1)
    def test_strict_guard_and_open(self):
        for guard in [lambda:1,lambda:False]:
            h=Handle()
            with self.assertRaises(EndpointError):self.endpoint(h,guard).run_once(C,1)
            self.assertEqual([],h.writes)
        h=Handle();h.is_open=1
        with self.assertRaises(EndpointError):self.endpoint(h).run_once(C,1)
    def test_rejected_frames(self):
        for chunks in [[R+R],[R,b"x"],[R.replace(C.encode(),b"0"*32)],[R[:-1]],[b"private junk\n"]]:
            with self.assertRaises(EndpointError):self.endpoint(Handle(chunks)).run_once(C,1)
    def test_close_uncertainty_retriable(self):
        h=Handle();h.close=lambda:None;e=self.endpoint(h)
        with self.assertRaisesRegex(EndpointError,"close_unconfirmed"):e.close()
        with self.assertRaisesRegex(EndpointError,"consumed"):e.run_once(C,1)
        h.close=lambda:setattr(h,"is_open",False);self.assertTrue(e.close())
    def test_exception_privacy(self):
        h=Handle()
        def fail(*args):raise RuntimeError("PRIVATE_DEVICE_IDENTIFIER")
        h.write=fail
        with self.assertRaises(EndpointError) as cm:self.endpoint(h).run_once(C,1)
        self.assertNotIn("PRIVATE",str(cm.exception));self.assertTrue(cm.exception.__suppress_context__)
    def test_admission_time_consumes_budget(self):
        h=Handle()
        def guard():h.now+=0.6;return True
        with self.assertRaisesRegex(EndpointError,"write_late"):self.endpoint(h,guard).run_once(C,1)
        self.assertEqual([],h.writes)
    def test_clock_invalid(self):
        for value in [float("nan"),float("inf"),True]:
            h=Handle();h.clock=lambda:value
            with self.assertRaises(EndpointError):self.endpoint(h).run_once(C,1)
        h=Handle();h.read=lambda size:setattr(h,"now",-1) or b""
        with self.assertRaises(EndpointError):self.endpoint(h).run_once(C,1)
if __name__=="__main__":unittest.main()
