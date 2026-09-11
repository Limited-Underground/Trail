"""Bounded decoder hostile fixtures; SDK generator fixture is synthetic, never device NVS."""
from pathlib import Path
import binascii,hashlib,json,struct,sys,unittest,zlib
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
import security_policy_input_readback as decoder

def stage_record(stage=1,error=0):
    value=bytes([0x98,0xd1,1,1,stage,error])
    return value+binascii.crc_hqx(value,0xffff).to_bytes(2,'little')
def entry(namespace_index,kind,key,data,span=1,chunk=255):
    key=key.encode() if isinstance(key,str) else key
    raw=bytearray(b'\xff'*32);raw[:4]=bytes([namespace_index,kind,span,chunk]);raw[8:24]=key+b'\x00'*(16-len(key));raw[24:32]=data.ljust(8,b'\xff')
    struct.pack_into('<I',raw,4,zlib.crc32(raw[:4]+raw[8:],0xffffffff)&0xffffffff)
    return bytes(raw)
def fixture(entries=None,*,sequence=0,state=0xfffffffe):
    if entries is None:return b'\xff'*12288
    page=bytearray(b'\xff'*4096);struct.pack_into('<II',page,0,state,sequence);page[8]=0xfe
    struct.pack_into('<I',page,28,zlib.crc32(page[4:28],0xffffffff)&0xffffffff)
    bitmap=(1<<256)-1
    for i,value in enumerate(entries):
        status,raw=value if isinstance(value,tuple) else (2,value)
        page[64+32*i:96+32*i]=raw
        bitmap=(bitmap&~(3<<(2*i)))|(status<<(2*i))
    page[32:64]=bitmap.to_bytes(32,'little')
    return bytes(page)+b'\xff'*8192

def diagnostic(stage=1,error=0):
    return fixture([entry(0,1,'ot198diag',b'\x01'),entry(1,8,'stage',stage_record(stage,error))])

class Tests(unittest.TestCase):
    def refuse(self,raw,*,fresh=False):
        with self.assertRaisesRegex(decoder.ReadbackError,'^input_readback_refused$'):
            (decoder.assert_fresh if fresh else decoder.decode)(raw)
    def test_erased_fresh(self):self.assertTrue(decoder.assert_fresh(fixture()));self.refuse(fixture())
    def test_size_and_types(self):
        for raw in (b'',b'\xff'*12287,b'\xff'*12289,bytearray(fixture()),'private'):
            self.refuse(raw)
    def test_all_record_pairs(self):
        for stage,errors in decoder.ALLOWED.items():
            for error in errors:
                self.assertEqual(decoder.decode(diagnostic(stage,error))['error'],decoder.ERRORS[error])
    def test_current_is_not_fresh(self):self.refuse(diagnostic(),fresh=True)
    def test_erased_namespace_not_fresh(self):self.refuse(fixture([(0,entry(0,1,'ot198diag',b'\x01'))]),fresh=True)
    def test_purged_erased_cannot_be_selected(self):
        raw=fixture([(0,b'\x00'*32)])
        self.assertTrue(decoder.assert_fresh(raw));self.refuse(raw)
    def test_erased_prior_stage_and_current(self):
        rows=[entry(0,1,'ot198diag',b'\x01'),(0,entry(1,8,'stage',stage_record())),entry(1,8,'stage',stage_record(8,0))]
        self.assertEqual(decoder.decode(fixture(rows))['stage'],'send_return')
        rows[1]=(0,b'\x00'*32);self.assertEqual(decoder.decode(fixture(rows))['stage'],'send_return')
    def test_duplicate_current(self):
        self.refuse(fixture([entry(0,1,'ot198diag',b'\x01'),entry(1,8,'stage',stage_record()),entry(1,8,'stage',stage_record(8))]))
    def test_duplicate_namespace(self):
        for row in (entry(0,1,'other',b'\x01'),entry(0,1,'ot198diag',b'\x02')):
            self.refuse(fixture([entry(0,1,'ot198diag',b'\x01'),row]))
    def test_orphan_namespace_refused(self):self.refuse(fixture([entry(1,8,'stage',stage_record())]),fresh=True)
    def test_unknown_diag_key_type(self):
        for kind,key in ((4,'stage'),(8,'private')):
            self.refuse(fixture([entry(0,1,'ot198diag',b'\x01'),entry(1,kind,key,stage_record())]))
    def test_record_corruption_and_wrong_schema(self):
        for n in range(8):
            raw=bytearray(stage_record());raw[n]^=1
            self.refuse(fixture([entry(0,1,'ot198diag',b'\x01'),entry(1,8,'stage',bytes(raw))]))
        for stage,error in ((9,0),(1,8),(8,7),(0,0)):
            self.refuse(fixture([entry(0,1,'ot198diag',b'\x01'),entry(1,8,'stage',stage_record(stage,error))]))
    def test_page_and_item_crc(self):
        for index in (4,8,9,28,68,72,88):
            raw=bytearray(diagnostic());raw[index]^=1;self.refuse(bytes(raw))
    def test_bitmap_illegal_and_dirty_empty(self):
        self.refuse(fixture([(1,entry(0,1,'ordinary',b'\x01'))]),fresh=True)
        self.refuse(fixture([(3,entry(0,1,'ordinary',b'\x01'))]),fresh=True)
        raw=bytearray(diagnostic());raw[63]&=0x0f;self.refuse(bytes(raw))
    def test_unsupported_pages(self):
        for state in (0,0xfffffff8,0xfffffff0,0xffffffff):self.refuse(fixture([],state=state))
    def test_duplicate_sequence_and_active(self):
        page=fixture([])[:4096];self.refuse(page+page+b'\xff'*4096)
    def test_variable_payload_crc(self):
        payload=b'synthetic\x00';meta=struct.pack('<HHI',len(payload),65535,zlib.crc32(payload,0xffffffff)&0xffffffff)
        rows=[entry(0,1,'normal',b'\x01'),entry(1,0x21,'text',meta,span=2),payload.ljust(32,b'\xff')]
        raw=fixture(rows);self.assertTrue(decoder.assert_fresh(raw))
        bad=bytearray(raw);bad[128]^=1;self.refuse(bytes(bad),fresh=True)
    def test_blob_closure(self):
        payload=b'abc';meta=struct.pack('<HHI',3,65535,zlib.crc32(payload,0xffffffff)&0xffffffff)
        rows=[entry(0,1,'normal',b'\x01'),entry(1,0x42,'blob',meta,span=2,chunk=128),payload.ljust(32,b'\xff'),entry(1,0x48,'blob',struct.pack('<IBBH',3,1,128,65535))]
        self.assertTrue(decoder.assert_fresh(fixture(rows)));self.refuse(fixture(rows[:-1]),fresh=True)
    def test_projection_privacy(self):
        value=decoder.decode(diagnostic());self.assertEqual(set(value),{'schema','image','stage','error'})
        self.assertNotIn('ot198diag',json.dumps(value))
    def test_contract_agreement(self):
        c=json.loads((ROOT/'firmware/components/security_diagnostics/input_record_v1.json').read_text())
        self.assertEqual(c['stages'],{str(k):v for k,v in decoder.STAGES.items()})
        self.assertEqual(c['errors'],{str(k):v for k,v in decoder.ERRORS.items()})
        self.assertEqual(c['allowed_errors'],{str(k):sorted(v) for k,v in decoder.ALLOWED.items()})
        self.assertEqual((c['namespace'],c['key'],c['prefix']),('ot198diag','stage',[152,209,1,1]))
    def test_sdk_generated_fixture(self):
        folder=ROOT/'tests/host/fixtures/security_policy_stage'
        raw=(folder/'sdk-stage.bin').read_bytes();meta=json.loads((folder/'provenance.json').read_text())
        self.assertEqual(hashlib.sha256(raw).hexdigest(),meta['fixture_sha256'])
        self.assertEqual(hashlib.sha256((folder/'sdk-stage.csv').read_bytes()).hexdigest(),meta['csv_sha256'])
        self.refuse(raw);self.assertTrue(decoder.assert_fresh(raw))
    def test_input_reasons_and_generic_refusal_rejected(self):
        expected={9:'idle_timeout',10:'assembly_timeout',11:'clock_regression',12:'buffer_limit',13:'invalid_length',14:'invalid_prefix',15:'invalid_hex',16:'loop_limit',17:'console_fault',18:'unexpected_state'}
        for code,name in expected.items():
            with self.subTest(code=code):
                self.assertEqual(decoder.decode(diagnostic(4,code)),{'schema':'OT198-INPUT-READBACK-1','image':1,'stage':'input_result','error':name})
                self.refuse(diagnostic(3,code));self.refuse(diagnostic(8,code))
        self.refuse(diagnostic(4,2));self.refuse(diagnostic(4,19))
    def test_parser_body_preserved(self):
        import inspect
        import security_policy_stage_readback as old
        self.assertEqual(inspect.getsource(decoder._parse),inspect.getsource(old._parse))
    def test_old_decoder_and_new_decoder_do_not_cross_accept(self):
        import security_policy_stage_readback as old
        import security_policy_stage_readback_tests as previous
        self.refuse(previous.diagnostic())
        with self.assertRaises(old.ReadbackError):old.decode(diagnostic())
        wrong=bytearray(stage_record(4,9));wrong[0]=0x95;wrong[6:]=binascii.crc_hqx(wrong[:6],0xffff).to_bytes(2,'little')
        self.refuse(fixture([entry(0,1,'ot198diag',b'\x01'),entry(1,8,'stage',bytes(wrong))]))
    def test_wrong_version_or_image_with_valid_record_crc(self):
        for index,value in ((0,0x95),(1,0xd2),(2,2),(3,2)):
            record=bytearray(stage_record(4,9));record[index]=value
            record[6:]=binascii.crc_hqx(record[:6],0xffff).to_bytes(2,'little')
            self.refuse(fixture([entry(0,1,'ot198diag',b'\x01'),entry(1,8,'stage',bytes(record))]))
    def test_new_history_refused_old_namespace_permitted(self):
        import security_policy_stage_readback_tests as previous
        self.assertTrue(decoder.assert_fresh(previous.diagnostic()))
        self.refuse(diagnostic(),fresh=True)
        raw=fixture([entry(0,1,'ot198diag',b'\x01'),(0,entry(1,8,'stage',stage_record(4,9)))])
        self.refuse(raw,fresh=True);self.refuse(raw)
if __name__=='__main__':unittest.main()
