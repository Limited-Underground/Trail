"""Synthetic private fixtures only; package checks never access hardware."""
from pathlib import Path
import copy
import importlib.util
import json
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('bundle',ROOT/'tools/security_policy_bundle.py')
b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)

class BundleTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name).resolve()
        (self.root/'.private').mkdir()
        for name in b.SOURCE_PATHS:
            p=self.root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b'# synthetic isolated code fixture\n')
        pins={}
        for i in range(33):
            name=f'fixture/source{i}.txt';p=self.root/name;p.parent.mkdir(exist_ok=True);p.write_bytes(str(i).encode())
            pins[name]={'bytes':p.stat().st_size,'sha256':b.sha(p.read_bytes())}
        report=self.root/b.REPORT;report.parent.mkdir(parents=True);report.write_text(json.dumps({'source_pins':pins}))
        self.candidate=self.root/'candidate.bin';self.candidate.write_bytes(b'C'*b.CANDIDATE_SIZE)
        self.roles=[]
        for index,role in enumerate('AB'):
            app=self.root/'.private'/f'{role}-original.bin';app.write_bytes(bytes([index+1])*589824)
            nvs=self.root/'.private'/f'{role}-nvs.bin';nvs.write_bytes(bytes([index+3])*12288)
            protected={name:{'bytes':size,'sha256':b.sha(name.encode())} for name,size in b.PROTECTED.items()}
            self.roles.append({'role':role,'private_route':f'COM{91+index}',
                'private_identity':f'01:23:45:67:89:a{index}','application':str(app),'nvs':str(nvs),'protected':protected})
        self.addCleanup(patch.stopall)
        patch.multiple(b,CANDIDATE_SHA=b.sha(self.candidate.read_bytes()),REPORT_SHA=b.sha(report.read_bytes()),
            CAPTURE_SHA=b.sha((self.root/'tools/security_policy_capture.py').read_bytes()),
            PARTITION_SHA=b.sha(b'partition'),OTA_SHA=b.sha(b'ota')).start()
        self.package=b.freeze(self.root,self.candidate,tuple(self.roles))

    def test_roundtrip_exact_bytes_and_digest(self):
        loaded=b.verify(self.root,self.package)
        self.assertEqual(loaded['candidate'],self.candidate.read_bytes())
        for index,role in enumerate(loaded['roles']):
            self.assertEqual(role['application'],bytes([index+1])*589824)
            self.assertEqual(role['nvs'],bytes([index+3])*12288)
        self.assertEqual(b.digest(self.package),b.digest(b.loads(json.dumps(self.package))))
        loaded['package']['roles'][0]['private_route']='other'
        self.assertEqual(self.package['roles'][0]['private_route'],'COM91')

    def test_private_repr_and_summary(self):
        for material in (self.package,self.package['candidate'],self.package['roles'][0],b.verify(self.root,self.package)):
            self.assertNotIn('COM91',repr(material));self.assertNotIn(str(self.root),repr(material))
        summary=json.dumps(b.sanitized_summary(self.package))
        self.assertNotIn('COM91',summary);self.assertNotIn('012345',summary);self.assertNotIn(str(self.root),summary)

    def test_recovery_does_not_require_candidate_report_or_build_sources(self):
        self.candidate.unlink();(self.root/b.REPORT).unlink();(self.root/'fixture/source0.txt').unlink()
        with self.assertRaises(b.BundleError):b.verify(self.root,self.package)
        self.assertIsNone(b.verify(self.root,self.package,True)['candidate'])

    def test_candidate_altered(self):
        self.candidate.write_bytes(b'Z'*b.CANDIDATE_SIZE)
        with self.assertRaisesRegex(b.BundleError,'file_changed'):b.verify(self.root,self.package)
        self.assertIsNone(b.verify(self.root,self.package,True)['candidate'])

    def test_build_source_altered(self):
        (self.root/'fixture/source0.txt').write_bytes(b'X')
        with self.assertRaisesRegex(b.BundleError,'file_changed'):b.verify(self.root,self.package)

    def test_build_report_altered(self):
        (self.root/b.REPORT).write_bytes(b'{}')
        with self.assertRaises(b.BundleError):b.verify(self.root,self.package)

    def test_code_change_blocks_execution_and_recovery(self):
        for name in b.SOURCE_PATHS:
            p=self.root/name;old=p.read_bytes();p.write_bytes(old+b'#change')
            for recovery in (False,True):
                with self.subTest(name=name,recovery=recovery),self.assertRaises(b.BundleError):b.verify(self.root,self.package,recovery)
            p.write_bytes(old)

    def test_every_private_payload_alteration_blocks_both_modes(self):
        for role in self.roles:
            for name in ('application','nvs'):
                p=Path(role[name]);old=p.read_bytes();p.write_bytes(b'X'+old[1:])
                for recovery in (False,True):
                    with self.subTest(role=role['role'],name=name,recovery=recovery),self.assertRaisesRegex(b.BundleError,'file_changed'):b.verify(self.root,self.package,recovery)
                p.write_bytes(old)

    def test_role_order_and_missing_side(self):
        for roles in (tuple(reversed(self.roles)),self.roles[:1]):
            with self.assertRaises(b.BundleError):b.freeze(self.root,self.candidate,roles)

    def test_duplicate_routes_casefold_and_normalized_identity(self):
        for name,value in [('private_route','com91'),('private_identity','01-23-45-67-89-a0')]:
            roles=copy.deepcopy(self.roles);roles[1][name]=value
            with self.assertRaisesRegex(b.BundleError,'roles_not_unique'):b.freeze(self.root,self.candidate,tuple(roles))

    def test_duplicate_custody_rejected(self):
        for name in ('application','nvs'):
            roles=copy.deepcopy(self.roles);roles[1][name]=roles[0][name]
            with self.assertRaisesRegex(b.BundleError,'custody_not_unique'):b.freeze(self.root,self.candidate,tuple(roles))

    def test_wrong_layout_and_boot_selection(self):
        for name in ('partition','ota'):
            package=copy.deepcopy(self.package);package['roles'][0]['protected'][name]['sha256']='0'*64
            for recovery in (False,True):
                with self.assertRaisesRegex(b.BundleError,'boot_selection_invalid'):b.verify(self.root,package,recovery)

    def test_sizes_and_protected_shapes(self):
        for name in ('application','nvs'):
            package=copy.deepcopy(self.package);package['roles'][1][name]['bytes']-=1
            with self.assertRaises(b.BundleError):b.verify(self.root,package,True)
        package=copy.deepcopy(self.package);package['roles'][0]['protected']['extra']={}
        with self.assertRaises(b.BundleError):b.verify(self.root,package,True)

    def test_traversal_outside_and_relative_paths(self):
        paths=[self.root/'.private'/'..'/'candidate.bin',self.root/'candidate.bin',Path('relative.bin')]
        for path in paths:
            roles=copy.deepcopy(self.roles);roles[0]['application']=str(path)
            with self.assertRaises(b.BundleError):b.freeze(self.root,self.candidate,tuple(roles))

    def test_reparse_rejected_before_read(self):
        original=b._nonreparse
        def reject(path):
            if Path(path).name=='A-nvs.bin':raise b.BundleError('reparse_rejected')
            return original(path)
        with patch.object(b,'_nonreparse',side_effect=reject):
            with self.assertRaisesRegex(b.BundleError,'reparse_rejected'):b.verify(self.root,self.package,True)
        # Exercise the actual platform-neutral symlink/reparse-bit branch, not just a caller mock.
        class Info:st_mode=0;st_file_attributes=0x400
        with patch.object(Path,'lstat',return_value=Info()):
            with self.assertRaisesRegex(b.BundleError,'reparse_rejected'):original(self.root)

    def test_strict_json_duplicate_and_nonfinite(self):
        for raw in ('{"a":1,"a":2}','{"a":NaN}','{'):
            with self.assertRaises(b.BundleError):b.loads(raw)

    def test_scope_exact_types_and_modes(self):
        for key,value in [('radio',0),('application_offset',65536.0),('version','other')]:
            package=copy.deepcopy(self.package);package['scope'][key]=value
            with self.assertRaises(b.BundleError):b.verify(self.root,package)
        with self.assertRaises(b.BundleError):b.verify(self.root,self.package,1)

    def test_pin_and_source_manifest_shape(self):
        for mutation in ('candidate','build_report','sources'):
            package=copy.deepcopy(self.package)
            if mutation=='sources':package['sources'].pop(next(iter(package['sources'])))
            else:package[mutation]['sha256']='0'*64
            with self.assertRaises(b.BundleError):b.verify(self.root,package,True)

    def test_errors_do_not_expose_private_values(self):
        Path(self.roles[0]['nvs']).unlink()
        try:b.verify(self.root,self.package)
        except b.BundleError as error:
            self.assertNotIn('COM91',str(error));self.assertNotIn(str(self.root),str(error))
        else:self.fail('missing file accepted')

if __name__=='__main__':unittest.main()
