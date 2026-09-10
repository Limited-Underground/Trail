"""Actual composed coordinator and strict 1015-frame runner under mock hardware."""
from pathlib import Path
import importlib.util,json,sys,tempfile,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
import mbedtls_comparison_execution as m

def load(name,path):
    s=importlib.util.spec_from_file_location(name,path);v=importlib.util.module_from_spec(s);sys.modules[name]=v;s.loader.exec_module(v);return v
fixtures=load('mbedtls_frame_fixtures',ROOT/'tests/host/ot149_mbedtls_psa_frame_tests.py')
transport=load('mbedtls_transport_fixtures',ROOT/'tests/host/ot150_mbedtls_psa_protocol_runner_tests.py')

class Tests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
        self.paths=[self.root/n for n in ('candidate.bin','original-a.bin','original-b.bin')]
        self.raw=[b'candidate',b'original-A',b'original-B-distinct']
        for p,raw in zip(self.paths,self.raw):p.write_bytes(raw)
        self.addCleanup(patch.stopall)
        patch.object(m,'BENCHMARK',m.descriptor(self.paths[0])).start();patch.object(m,'ORIGINALS',tuple(m.descriptor(p) for p in self.paths[1:])).start()
        self.package=m.freeze_package(*self.paths);self.session=m.Session(self.package);self.c=self.session.coordinator
        self.c.ROOT=self.root;self.c.PRIVATE_ROOT=self.root/'.private';self.c.PRIVATE_ROOT.mkdir()
        for attr,s in (('JOURNAL_PATH','journal'),('EXECUTION_RECEIPT_PATH','execution'),('RECOVERY_RECEIPT_PATH','recovery')):setattr(self.c,attr,self.c.PRIVATE_ROOT/('mbedtls-comparison-1-'+s+'.json'))
        self.endpoints=(object(),object());self.config=self.session.config(self.endpoints)
        case=self
        class Backend:
            def __init__(self):self.current=dict(zip(case.endpoints,case.raw[1:]));self.events=[];self.fail_write=False;self.fail_restore=False;self.identity=True
            def verify_role(self,e,role,desc):return self.identity and e is case.endpoints[('A','B').index(role)] and desc.sha256==m.digest(case.raw[('A','B').index(role)+1])
            def write_application(self,e,o,image):
                self.events.append(('write',case.endpoints.index(e),image.payload));self.current[e]=image.payload
                if self.fail_write and image.payload==case.raw[0]:raise RuntimeError('partialwrite')
                if self.fail_restore and image.payload!=case.raw[0]:raise RuntimeError('restorefailed')
            def verify_application(self,e,o,image):
                if self.current[e]!=image.payload:raise RuntimeError('mismatch')
            def hard_reset(self,e):self.events.append(('reset',case.endpoints.index(e)))
            def reset(self,e):self.hard_reset(e)
            def is_present(self,e):return True
            def open(self,e):return transport.Endpoint([case.c.protocol.READY+fixtures._encode(fixtures._valid_records())])
        self.backend=Backend()
        class Authority:
            def validate(self,binding,*,recovery):
                if binding!=case.session.binding:raise RuntimeError('wrongbinding')
                return case.c.AuthorityGrant('a'*64,1,False,False)
        self.authority=Authority()
    def test_full_composed_success_and_distinct_restores(self):
        result=self.session.execute(self.config,self.backend,self.authority)
        self.assertTrue(result['restoration_complete']);self.assertTrue(all(n['result']['frame_count']==1015 for n in result['nodes']))
        self.assertEqual(list(self.backend.current.values()),self.raw[1:])
        self.assertEqual(set(result['validated_capture_custody']),{'A','B'})
        for role,d in result['validated_capture_custody'].items():
            raw=(self.c.PRIVATE_ROOT/('mbedtls-comparison-1-'+role+'-validated.frames')).read_bytes()
            self.assertEqual(m.digest(raw),d['sha256']);self.assertEqual(len(raw.splitlines()),1015)
        self.assertEqual([e[1] for e in self.backend.events if e[0]=='write'],[0,0,1,1])
    def test_partial_write_restores_exact_touched_original(self):
        self.backend.fail_write=True
        with self.assertRaises(self.c.CoordinatorError):self.session.execute(self.config,self.backend,self.authority)
        self.assertEqual(list(self.backend.current.values()),self.raw[1:])
    def test_role_identity_rejects_before_mutation(self):
        self.backend.identity=False
        with self.assertRaises(self.c.CoordinatorError):self.session.execute(self.config,self.backend,self.authority)
        self.assertEqual(self.backend.events,[])
    def test_persistence_failure_cleanup_still_resets(self):
        prior=self.c._persist;count=[0]
        def persist(j):
            count[0]+=1
            return False if count[0]>2 else prior(j)
        with patch.object(self.c,'_persist',side_effect=persist):
            with self.assertRaises(self.c.CoordinatorError):self.session.execute(self.config,self.backend,self.authority)
        self.assertEqual(list(self.backend.current.values()),self.raw[1:]);self.assertIn(('reset',0),self.backend.events)
    def test_consumed_namespace_rejected(self):
        self.c.JOURNAL_PATH.write_bytes(b'consumed')
        with self.assertRaises(self.c.CoordinatorError):self.session.execute(self.config,self.backend,self.authority)
        self.assertEqual(self.backend.events,[])
    def test_missing_authority_rejected(self):
        with self.assertRaises(self.c.CoordinatorError):self.session.execute(self.config,self.backend,None)
        self.assertEqual(self.backend.events,[])
    def test_recovery_missing_candidate_restores_original(self):
        self.backend.fail_write=True;self.backend.fail_restore=True
        with self.assertRaises(self.c.CoordinatorError):self.session.execute(self.config,self.backend,self.authority)
        self.paths[0].unlink();self.backend.fail_write=False;self.backend.fail_restore=False
        result=self.session.recover(self.config,self.backend,self.authority)
        self.assertTrue(result['restoration_complete']);self.assertEqual(list(self.backend.current.values()),self.raw[1:])
    def test_package_and_original_tamper(self):
        self.paths[1].write_bytes(b'wrong')
        with self.assertRaises(ValueError):self.session.execute(self.config,self.backend,self.authority)
        self.assertEqual(self.backend.events,[])

    def test_capture_custody_failure_restores(self):
        path=self.c.PRIVATE_ROOT/'mbedtls-comparison-1-A-validated.frames'
        path.write_bytes(b'existing')
        with self.assertRaises(self.c.CoordinatorError):self.session.execute(self.config,self.backend,self.authority)
        self.assertEqual(list(self.backend.current.values()),self.raw[1:])
        self.assertEqual(path.read_bytes(),b'existing')
    def test_wrong_dependency_and_scope_rejected(self):
        from copy import deepcopy
        bad=deepcopy(self.package);bad['scope']['radio_allowed']=True
        with self.assertRaises(ValueError):m.verify_package(bad)
        path=m.ROOT/'tools/ot149_mbedtls_psa_frames.py';original=Path.read_bytes
        def changed(p):return original(p)+b'\n' if p==path else original(p)
        with patch.object(Path,'read_bytes',changed):
            with self.assertRaises(ValueError):m.Session(self.package)
    def test_radio_grant_rejected(self):
        with patch.object(self.authority,'validate',return_value=self.c.AuthorityGrant('a'*64,1,False,True)):
            with self.assertRaises(self.c.CoordinatorError):self.session.execute(self.config,self.backend,self.authority)
        self.assertEqual(self.backend.events,[])
    def test_verified_dependencies_ignore_stale_loader(self):
        import importlib.machinery
        prior=importlib.machinery.SourceFileLoader.get_code
        def stale(loader,name):
            if Path(loader.path).resolve().is_relative_to(m.ROOT/'tools'):
                raise RuntimeError('stale loader used')
            return prior(loader,name)
        with patch.object(importlib.machinery.SourceFileLoader,'get_code',stale):
            session=m.Session(self.package)
        self.assertEqual(session.coordinator.protocol.EXPECTED_FRAME_COUNT if hasattr(session.coordinator.protocol,'EXPECTED_FRAME_COUNT') else session.coordinator.protocol.frame_contract.EXPECTED_FRAME_COUNT,1015)

    def test_concurrent_constructors_restore_import_hook(self):
        from concurrent.futures import ThreadPoolExecutor
        import importlib.machinery
        original=importlib.machinery.SourceFileLoader.get_code
        with ThreadPoolExecutor(max_workers=2) as pool:
            sessions=list(pool.map(lambda _:m.Session(self.package),range(2)))
        self.assertIs(importlib.machinery.SourceFileLoader.get_code,original)
        self.assertIsNot(sessions[0].coordinator,sessions[1].coordinator)
    def test_second_partial_write_after_first_restored(self):
        original=self.backend.write_application
        def write(e,o,image):
            original(e,o,image)
            if e is self.endpoints[1] and image.payload==self.raw[0]:raise RuntimeError('Bpartial')
        with patch.object(self.backend,'write_application',side_effect=write):
            with self.assertRaises(self.c.CoordinatorError):self.session.execute(self.config,self.backend,self.authority)
        self.assertEqual(list(self.backend.current.values()),self.raw[1:])
        self.assertEqual([e[1] for e in self.backend.events if e[0]=='write'],[0,0,1,1])

    def test_coordinator_changed_after_validation_before_compile(self):
        path=m.ROOT/'tools/ot150_mbedtls_psa_coordinator.py'
        original=Path.read_bytes
        reads=[0]
        def changed(p):
            raw=original(p)
            if p==path:
                reads[0]+=1
                if reads[0]>=3:return raw+b'\nraise RuntimeError("unchecked code executed")\n'
            return raw
        with patch.object(Path,'read_bytes',changed):
            with self.assertRaisesRegex(ValueError,'dependency_changed'):
                m.Session(self.package)
        self.assertEqual(reads[0],3)

if __name__=='__main__':unittest.main()
