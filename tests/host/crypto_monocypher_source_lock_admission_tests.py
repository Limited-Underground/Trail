#!/usr/bin/env python3
import copy,hashlib,importlib.util,json,shutil,subprocess,sys,tempfile,unicodedata,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];ART=ROOT/'tests/benchmarks/crypto/OT-102-OT005-MONOCYPHER-SOURCE-LOCK-ADMISSION-DELTA-V0.json';EVIDENCE=ROOT/'tests/benchmarks/crypto/OT-102-OT005-MONOCYPHER-SOURCE-EVIDENCE-V0.json';TOOL=ROOT/'tools/crypto_monocypher_source_lock_admission.py';BUNDLE=ROOT/'tests/benchmarks/crypto/monocypher/4.0.3';OT097=ROOT/'tests/benchmarks/crypto/OT-097-OT005-LICENSE-AWARE-SOURCE-LOCK-ADMISSION-V1.json';OT098=ROOT/'tests/benchmarks/crypto/OT-098-OT005-EXTERNAL-CANDIDATE-ACQUISITION-V0.json';OT099=ROOT/'tests/benchmarks/crypto/OT-099-OT005-LIBSODIUM-MANAGED-IMPORT-V0.json';OT100=ROOT/'tests/benchmarks/crypto/OT-100-OT005-LIBSODIUM-SOURCE-LOCK-ADMISSION-DELTA-V0.json'
s=importlib.util.spec_from_file_location('otmsla',TOOL);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.data=json.loads(ART.read_text(encoding='utf-8'))
 def mutated(self,action):
  value=copy.deepcopy(self.data);action(value)
  with tempfile.TemporaryDirectory() as td:
   path=Path(td)/'admission.json';path.write_text(json.dumps(value),encoding='utf-8')
   with self.assertRaises(m.AdmissionError):m.validate(path,enforce_digest=False)
 def test_exact_admission_cli_parents_and_four_blockers(self):
  data=m.validate(ART);self.assertEqual(data['acceptance_counts'],{'api_config':0,'candidate_import':0,'source':2});self.assertEqual((len(data['historical_six_blockers']),len(data['prior_current_five_blockers']),len(data['current_four_blockers'])),(6,5,4));self.assertEqual(data['accepted_source_evidence_sha256']['monocypher'],[m.EVIDENCE_SHA]);self.assertTrue(data['claims']['monocypher_source_lock_accepted'])
  for path,expected in ((OT097,m.OT097_RAW),(OT098,m.OT098_RAW),(OT099,m.OT099_RAW),(OT100,m.OT100_RAW),(EVIDENCE,m.EVIDENCE_SHA),(ART,m.ADMISSION_SHA)):self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),expected)
  result=subprocess.run([sys.executable,str(TOOL),str(ART)],capture_output=True,text=True);self.assertEqual(result.returncode,0);self.assertIn('FOUR-OTCBR0-REQUIREMENTS-REMAIN',result.stdout)
 def test_exact_git_blob_tree_and_complete_policy_layers(self):
  files=m._validate_tree();receipt,licenses,deps,patches,sbom=m._validate_layers(files);lock=m._validate_lock();self.assertEqual(len(files),161);self.assertEqual(receipt['commit'],m.COMMIT);self.assertEqual(len(licenses),6);self.assertEqual(len(deps),4);self.assertEqual(patches[0]['patch_count'],0);self.assertEqual((len(sbom['packages']),len(sbom['files'])),(5,161));self.assertEqual(lock['source']['retained_tree_path'],'tests/benchmarks/crypto/monocypher/4.0.3/source')
  expected={'acquisition-receipt.json':m.RECEIPT_SHA,'source-tree.jsonl':m.TREE_MANIFEST_SHA,'license-inventory.jsonl':m.LICENSE_SHA,'sbom.spdx.json':m.SBOM_SHA,'transitive-dependencies.jsonl':m.TRANSITIVE_SHA,'patches.jsonl':m.PATCH_SHA,'project-lock.json':m.LOCK_SHA}
  for name,digest in expected.items():self.assertEqual(hashlib.sha256((BUNDLE/name).read_bytes()).hexdigest(),digest)
  for rel,row in files.items():self.assertEqual(m._blob_sha((BUNDLE/'source'/rel).read_bytes()),row['git_blob'])
 def test_genuine_otcsle_v1_chain_and_license_choice(self):
  evidence,facts=m._validated_otcsle();self.assertEqual((evidence['schema'],evidence['version']),('OTCSLE0',1));self.assertEqual(evidence['contract_policy_sha256'],m.OT097_POLICY);self.assertEqual(evidence['project_license_choice'],'BSD-2-Clause');self.assertTrue(evidence['license_manifest']['inventory_complete']);self.assertEqual(evidence['project_dependency_lock']['lock_kind'],'vendored_source_tree_lock');self.assertTrue(facts['source_lock_accepted']);self.assertFalse(facts['import_authorized']);self.assertFalse(facts['execution_authorized'])
 def test_git_attributes_preserve_exact_source_without_weakening_project_files(self):
  source_path='tests/benchmarks/crypto/monocypher/4.0.3/source/README.md';project_path='tests/host/crypto_monocypher_source_lock_admission_tests.py'
  result=subprocess.run(['git','check-attr','text','whitespace','--',source_path,project_path],cwd=ROOT,capture_output=True,text=True,check=True).stdout
  self.assertIn(f'{source_path}: text: unset',result);self.assertIn(f'{source_path}: whitespace: -trailing-space,-space-before-tab',result);self.assertIn(f'{project_path}: whitespace: unspecified',result)
 def test_admission_anchors_claims_authority_and_blockers_fail_closed(self):
  actions=[lambda d:d.__setitem__('schema','changed'),lambda d:d.__setitem__('version',False),lambda d:d.__setitem__('unknown',False),lambda d:d['parents'].__setitem__('otcsla0_v0_raw_sha256','0'*64),lambda d:d['owner_authorization'].__setitem__('project_license_choice','CC0-1.0'),lambda d:d['source_evidence'].__setitem__('sha256','0'*64),lambda d:d['accepted_candidate'].__setitem__('lock_kind','git_submodule_dependency_lock'),lambda d:d['accepted_candidate'].__setitem__('project_license_choice','CC0-1.0'),lambda d:d['accepted_candidate'].__setitem__('unknown',False),lambda d:d['accepted_source_evidence_sha256']['monocypher'].append('0'*64),lambda d:d['accepted_api_config_evidence_sha256']['monocypher'].append('0'*64),lambda d:d['accepted_candidate_import_evidence_sha256']['monocypher'].append('0'*64),lambda d:d['acceptance_counts'].__setitem__('source',True),lambda d:d['acceptance_counts'].pop('api_config'),lambda d:d['historical_six_blockers'].pop(),lambda d:d['prior_current_five_blockers'].pop(),lambda d:d['current_four_blockers'].append('monocypher_source_lock_absent'),lambda d:d['claims'].__setitem__('benchmark_executed',True),lambda d:d['claims'].__setitem__('monocypher_source_lock_accepted',False),lambda d:d['claims'].pop('readiness_accepted'),lambda d:d['claims'].__setitem__('invented_claim',False),lambda d:d['authority'].__setitem__('candidate_import_authorized',True),lambda d:d['authority'].pop('device_access_authorized'),lambda d:d['authority'].__setitem__('invented_authority',False),lambda d:d['license_claims'].__setitem__('legal_clearance_claimed',True)]
  for action in actions:
   with self.subTest(action=action):self.mutated(action)
 def test_retained_source_manifest_and_path_mutations_fail_closed(self):
  with tempfile.TemporaryDirectory() as td:
   copied=Path(td)/'source';shutil.copytree(m.SOURCE,copied);target=copied/'src/monocypher.c';target.write_bytes(target.read_bytes()+b' ');old=m.SOURCE
   try:
    m.SOURCE=copied
    with self.assertRaises(m.AdmissionError):m._validate_tree()
   finally:m.SOURCE=old
   changed=Path(td)/'changed.jsonl';changed.write_bytes((BUNDLE/'source-tree.jsonl').read_bytes()+b' ')
   with self.assertRaises(m.AdmissionError):m._raw(changed,m.TREE_MANIFEST_SHA)
  for path in ('../escape','./relative','/absolute','a//b','a\\b','CON','folder/NUL.txt','trail.',' lead'):
   with self.subTest(path=path),self.assertRaises(m.AdmissionError):m._safe_path(path)
  for value in ('x'*(m.MAX_STRING+1),unicodedata.normalize('NFD','caf\u00e9'),'secret=value','C:\\private'):
   with self.subTest(value=value[:12]),self.assertRaises(m.AdmissionError):m._scan(value)
 def test_parent_mutation_pure_surface_and_sanitized_cli(self):
  with tempfile.TemporaryDirectory() as td:
   td=Path(td);bad97=td/'97.json';bad97.write_bytes(OT097.read_bytes()+b' ');bad98=td/'98.json';bad98.write_bytes(OT098.read_bytes()+b' ');bad99=td/'99.json';bad99.write_bytes(OT099.read_bytes()+b' ');bad100=td/'100.json';bad100.write_bytes(OT100.read_bytes()+b' ');bad_evidence=td/'evidence.json';bad_evidence.write_bytes(EVIDENCE.read_bytes()+b' ')
   for paths in ((bad97,OT098,OT099,OT100,EVIDENCE),(OT097,bad98,OT099,OT100,EVIDENCE),(OT097,OT098,bad99,OT100,EVIDENCE),(OT097,OT098,OT099,bad100,EVIDENCE),(OT097,OT098,OT099,OT100,bad_evidence)):
    with self.subTest(paths=paths),self.assertRaises(m.AdmissionError):m.validate(ART,*paths)
  source=TOOL.read_text(encoding='utf-8')
  for token in ('import socket','import requests','import urllib','import subprocess','os.system','idf.py','esptool','git clone'):self.assertNotIn(token,source)
  result=subprocess.run([sys.executable,str(TOOL),str(ART),'--evidence',r'Z:\restricted\private.json'],capture_output=True,text=True);self.assertEqual((result.returncode,result.stdout,result.stderr.strip()),(2,'','OTMSLA0 validation failed'));self.assertNotIn('restricted',result.stderr)
if __name__=='__main__':unittest.main()
