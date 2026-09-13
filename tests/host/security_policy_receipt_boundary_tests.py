"""Actual endpoint/capture regression, synthetic bytes only, no hardware."""
from pathlib import Path
import argparse,json,sys,unittest
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
from security_policy_receipt_boundary import ReceiptBoundaryEndpoint,Boundary,BoundaryError,MAX_PREAMBLE
from security_policy_diagnostics import ObservedEndpoint
from security_policy_endpoint import EndpointError
C='0123456789abcdef0123456789abcdef'
BEGIN=b'SEC_BEGIN1 '+C.encode()+b'\r\n'
RECEIPT=b'SEC_EVAL1 ot187-policy-v0 '+C.encode()+b' pass\r\n'
FIXTURES=None

class Handle:
    def __init__(self,chunks):self.chunks=list(chunks);self.is_open=True;self.now=0.;self.timeout=0.;self.write_timeout=0.;self.requests=[];self.writes=[]
    def write(self,raw):self.writes.append(raw);return len(raw)
    def read(self,size):
        self.requests.append(size);self.now+=min(self.timeout,.125)
        return self.chunks.pop(0) if self.chunks else b''
    def close(self):self.is_open=False

def run(chunks,kind=ReceiptBoundaryEndpoint):
    h=Handle(chunks);e=kind(h,role='A',monotonic=lambda:h.now)
    result=None;error=None
    try:result=e.run_once(C,30.)
    except EndpointError as caught:error=caught.args[0]
    return h,e,result,error

class Tests(unittest.TestCase):
    def test_actual_cpp_writer_and_fifo_outputs(self):
        self.assertIsNotNone(FIXTURES,'Use Test-SecurityReceiptBoundary.py to generate actual C++ wire fixtures')
        rows=FIXTURES.read_text(encoding='ascii').splitlines()
        self.assertEqual(rows.pop(0),'case\twire_hex');self.assertEqual(len(rows),21)
        seen=set()
        for row in rows:
            name,encoded=row.split('\t');self.assertNotIn(name,seen);seen.add(name)
            wire=bytes.fromhex(encoded)
            if name=='unframed_stale64_pass':
                h,e,result,error=run([b'',wire],ObservedEndpoint)
                self.assertEqual(e.observation.row['read_bytes'],129)
                self.assertEqual(e.observation.row['capture_error'],'read_invalid')
                continue
            marker=wire.index(BEGIN);expected=wire[marker+len(BEGIN):].replace(b'\r\n',b'\n')
            for size in (1,63,64,128,129):
                with self.subTest(case=name,size=size):
                    h,e,result,error=run([b'',*(wire[i:i+size] for i in range(0,len(wire),size))])
                    self.assertIsNone(error);self.assertEqual(result,expected)
                    self.assertEqual(e.boundary.row['preamble_discarded_bytes'],marker)
                    self.assertTrue(e.boundary.row['begin_observed'])

    def test_real_old_adapter_signature_is_reproduced(self):
        self.assertEqual(len(RECEIPT),65)
        h,e,result,error=run([b'',b'o'*64+RECEIPT],ObservedEndpoint)
        row=e.observation.row
        self.assertEqual((row['command_accepted_bytes'],row['read_calls'],row['read_bytes'],row['capture_error']),(63,2,129,'read_invalid'))
        self.assertEqual(error,'endpoint_capture_refused');self.assertIsNone(result)

    def test_all_chunk_splits_through_actual_frozen_capture(self):
        wire=b'o'*64+BEGIN+RECEIPT
        for split in range(len(wire)+1):
            with self.subTest(split=split):
                # Model a stream read bounded by the actual requested129 sentinel.
                segments=[wire[:split],wire[split:]]
                chunks=[v[i:i+129] for v in segments for i in range(0,len(v),129)]
                h,e,result,error=run([b'',*chunks])
                self.assertIsNone(error);self.assertEqual(result,RECEIPT.replace(b'\r\n',b'\n'))
                self.assertEqual(e.boundary_summary()['preamble_discarded_bytes'],64)
                self.assertTrue(all(n==129 for n in h.requests));self.assertEqual(len(h.writes),1)
                self.assertIsNone(e.boundary.expected);self.assertEqual(e.boundary.pending,b'')

    def test_bytewise_and_binary_preamble(self):
        prefix=bytes(range(64));h,e,result,error=run([bytes([x]) for x in prefix+BEGIN+RECEIPT])
        self.assertIsNone(error);self.assertIsNotNone(result);self.assertEqual(e.boundary.row['preamble_discarded_bytes'],64)

    def test_marker_required(self):
        for raw in (RECEIPT,b'',b'noise\r\n'):
            with self.subTest(raw=raw):
                h,e,result,error=run([raw]);self.assertIsNone(result);self.assertIsNotNone(error)
                self.assertFalse(e.boundary.row['begin_observed'])

    def test_wrong_challenge_version_and_malformed_marker_refused(self):
        for marker in (BEGIN.replace(C.encode(),b'a'*32),BEGIN.replace(b'BEGIN1',b'BEGIN2'),BEGIN.replace(b'\r\n',b'\n'),b'SEC_BEGIN1 x\n'):
            h,e,result,error=run([marker,BEGIN,RECEIPT]);self.assertIsNotNone(error);self.assertEqual(e.boundary.row['error'],'begin_invalid')

    def test_preamble_budget_exact_boundary(self):
        for count,passed in ((MAX_PREAMBLE,True),(MAX_PREAMBLE+1,False)):
            raw=b'x'*count+BEGIN+RECEIPT;h,e,result,error=run([raw[i:i+64] for i in range(0,len(raw),64)])
            self.assertEqual(result is not None,passed)
            if not passed:self.assertEqual(e.boundary.row['error'],'preamble_limit')

    def test_receipt_and_trailing_rules_unchanged(self):
        cases=([BEGIN,b'x'*129],[BEGIN,b'x'*100,b'y'*29],[BEGIN,RECEIPT,b'x'],[BEGIN,RECEIPT+RECEIPT[:1]],[BEGIN,RECEIPT.replace(C.encode(),b'a'*32)],[BEGIN,BEGIN,RECEIPT])
        for chunks in cases:
            with self.subTest(chunks=chunks):
                h,e,result,error=run(chunks);self.assertIsNone(result);self.assertIsNotNone(error)
        h,e,_,_=run([BEGIN,b'x'*129]);self.assertEqual(e.observation.row['capture_error'],'read_invalid')

    def test_bad_transport_type_and_size_refused(self):
        for raw in (None,bytearray(b'x'),b'x'*130):
            h,e,result,error=run([raw]);self.assertIsNone(result);self.assertIsNotNone(error)

    def test_one_use_close_and_privacy(self):
        h,e,result,error=run([BEGIN,RECEIPT]);self.assertIsNotNone(result)
        with self.assertRaisesRegex(EndpointError,'endpoint_consumed'):e.run_once(C,30.)
        self.assertTrue(e.close());self.assertFalse(h.is_open)
        projection=json.dumps(e.boundary_summary());self.assertNotIn(C,projection);self.assertNotIn('SEC_EVAL1',projection)

    def test_invalid_challenge_consumes_without_io(self):
        h=Handle([]);e=ReceiptBoundaryEndpoint(h,role='A',monotonic=lambda:h.now)
        with self.assertRaisesRegex(EndpointError,'endpoint_capture_refused'):e.run_once('bad',30.)
        self.assertFalse(h.writes);self.assertFalse(h.requests)
        with self.assertRaisesRegex(EndpointError,'endpoint_consumed'):e.run_once(C,30.)

    def test_marker_split_prefix_accounting(self):
        b=Boundary();b.arm(C)
        self.assertEqual(b.push(b'oldSEC_'),b'')
        self.assertEqual(b.push(b'BEGIN1 '+C.encode()+b'\r'),b'')
        self.assertEqual(b.push(b'\n'+RECEIPT),RECEIPT)
        self.assertEqual(b.row['preamble_discarded_bytes'],3)

if __name__=='__main__':
    parser=argparse.ArgumentParser(add_help=False)
    parser.add_argument('--wire-fixtures',type=Path,required=True)
    args,remaining=parser.parse_known_args();FIXTURES=args.wire_fixtures
    unittest.main(argv=[sys.argv[0],*remaining])
