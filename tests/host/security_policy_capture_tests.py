import unittest,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/"tools"))
from security_policy_capture import capture,command,CaptureError
C="0123456789abcdef0123456789abcdef"
R=f"SEC_EVAL1 ot187-policy-v0 {C} pass\n".encode()
class Stream:
    def __init__(self,chunks):self.chunks=list(chunks);self.now=0.0
    def clock(self):return self.now
    def read(self,size,remaining):
        self.now+=min(0.01,remaining)
        return self.chunks.pop(0) if self.chunks else b""
class Tests(unittest.TestCase):
    def run_capture(self,chunks):
        s=Stream(chunks);return capture(s.read,s.clock,C,1.0)
    def test_all_splits(self):
        for i in range(len(R)+1):self.assertEqual(R,self.run_capture([R[:i],R[i:]]))
    def test_crlf_canonicalization(self):
        raw=R.replace(b"\n",b"\r\n")
        for i in range(len(raw)+1):self.assertEqual(R,self.run_capture([raw[:i],raw[i:]]))
    def test_invalid_frames(self):
        for raw in [R+R,R+b"x",R.replace(b"pass",b"secret"),R.replace(C.encode(),b"0"*32),b"x"*129]:
            with self.assertRaises(CaptureError):self.run_capture([raw])
    def test_trailing(self):
        with self.assertRaises(CaptureError):self.run_capture([R,b"x"])
    def test_timeout(self):
        with self.assertRaises(CaptureError):self.run_capture([R[:-1]])
    def test_stuck_clock(self):
        with self.assertRaisesRegex(CaptureError,"read_budget"):capture(lambda *_:b"",lambda:0,C,1)
    def test_rollback(self):
        clock=iter([0,0, -1])
        with self.assertRaisesRegex(CaptureError,"clock_invalid"):capture(lambda *_:R,lambda:next(clock),C,1)
    def test_late_output(self):
        s=Stream([])
        def read(*_):s.now=1;return R
        with self.assertRaisesRegex(CaptureError,"late_output"):capture(read,s.clock,C,1)
    def test_command_and_deadline(self):
        self.assertEqual(b"RUN "+R.rsplit(b" ",1)[0]+b"\n",command(C))
        for c in [C.upper(),"0"*31,None]:
            with self.assertRaises(CaptureError):command(c)
        with self.assertRaises(CaptureError):capture(lambda *_:b"",lambda:0,C,float("inf"))
if __name__=="__main__":unittest.main()
