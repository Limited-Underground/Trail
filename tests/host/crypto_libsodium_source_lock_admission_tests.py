#!/usr/bin/env python3
import copy,hashlib,importlib.util,json,subprocess,sys,tempfile,unicodedata,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];ART=ROOT/"tests/benchmarks/crypto/OT-100-OT005-LIBSODIUM-SOURCE-LOCK-ADMISSION-DELTA-V0.json";TOOL=ROOT/"tools/crypto_libsodium_source_lock_admission.py";OT097=ROOT/"tests/benchmarks/crypto/OT-097-OT005-LICENSE-AWARE-SOURCE-LOCK-ADMISSION-V1.json";OT099=ROOT/"tests/benchmarks/crypto/OT-099-OT005-LIBSODIUM-MANAGED-IMPORT-V0.json"
s=importlib.util.spec_from_file_location("otcsla",TOOL);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.data=json.loads(ART.read_text(encoding="utf-8"));cls.evidence=json.loads(OT099.read_text(encoding="utf-8"))
 def reject(self,f):
  d=copy.deepcopy(self.data);f(d)
  with self.assertRaises(m.AdmissionError):m.validate_semantics(d,self.evidence)
 def test_exact_admission_cli_and_parents(self):
  d=m.validate(ART);self.assertEqual(d["acceptance_counts"],{"source":1,"api_config":0,"candidate_import":0});self.assertEqual((len(d["historical_six_blockers"]),len(d["current_five_blockers"])),(6,5));self.assertTrue(d["claims"]["libsodium_source_lock_accepted"])
  self.assertEqual(hashlib.sha256(OT097.read_bytes()).hexdigest(),m.EXPECTED_CONTRACT_RAW_SHA256);self.assertEqual(hashlib.sha256(OT099.read_bytes()).hexdigest(),m.EXPECTED_EVIDENCE_SHA256)
  p=subprocess.run([sys.executable,str(TOOL),str(ART)],capture_output=True,text=True);self.assertEqual(p.returncode,0);self.assertIn("FIVE-OTCBR0-REQUIREMENTS-REMAIN",p.stdout)
 def test_checkout_bytes_are_deterministic(self):
  for path in (ART,OT097,OT099):
   with self.subTest(path=path.name):
    rel=path.relative_to(ROOT).as_posix();p=subprocess.run(["git","check-attr","-z","text","eol","--",rel],cwd=ROOT,capture_output=True,check=True)
    parts=p.stdout.decode("utf-8").split("\0");attrs={parts[i+1]:parts[i+2] for i in range(0,len(parts)-1,3)}
    self.assertEqual(attrs,{"text":"set","eol":"lf"})
 def test_identity_schema_and_exact_types(self):
  for key in ("schema","artifact_kind","acceptance_id","accepted_date","status","public_result"):
   with self.subTest(key=key):self.reject(lambda d,key=key:d.__setitem__(key,"changed"))
  for action in (lambda d:d.__setitem__("unknown",False),lambda d:d.pop("status"),lambda d:d["accepted_candidate"].__setitem__("unknown",False),lambda d:d["accepted_candidate"].pop("lock_sha256"),lambda d:d["acceptance_counts"].__setitem__("source",True),lambda d:d["authority"].__setitem__("device_access_authorized",0),lambda d:d["claims"].__setitem__("readiness_accepted",0),lambda d:d["license_claims"].__setitem__("legal_clearance_claimed",0)):
   with self.subTest(action=action):self.reject(action)
 def test_anchors_claims_facts_and_blockers_fail_closed(self):
  actions=[lambda d:d["parents"].__setitem__("otlmi0_v0_raw_sha256","0"*64),lambda d:d["accepted_source_evidence_sha256"]["espressif_libsodium"].append("0"*64),lambda d:d["accepted_source_evidence_sha256"]["monocypher"].append("0"*64),lambda d:d["accepted_api_config_evidence_sha256"]["espressif_libsodium"].append("0"*64),lambda d:d["accepted_candidate_import_evidence_sha256"]["espressif_libsodium"].append("0"*64),lambda d:d["acceptance_counts"].__setitem__("source",2),lambda d:d["accepted_candidate"].__setitem__("version","1.0.21"),lambda d:d["accepted_candidate"].__setitem__("lock_sha256",d["accepted_candidate"]["spdx_sha256"]),lambda d:d["accepted_candidate"].__setitem__("component","C:\\private"),lambda d:d["historical_six_blockers"].pop(),lambda d:d["current_five_blockers"].append("espressif_libsodium_source_lock_absent")]
  for field in m.CLAIM_KEYS-{"libsodium_source_lock_accepted"}:actions.append(lambda d,field=field:d["claims"].__setitem__(field,True))
  actions.append(lambda d:d["claims"].__setitem__("libsodium_source_lock_accepted",False))
  for field in m.AUTH_KEYS:actions.append(lambda d,field=field:d["authority"].__setitem__(field,True))
  for field in ("legal_clearance_claimed","license_compatibility_determined"):actions.append(lambda d,field=field:d["license_claims"].__setitem__(field,True))
  for action in actions:
   with self.subTest(action=action):self.reject(action)
 def test_loader_bounds_unicode_duplicates_and_malformed(self):
  with tempfile.TemporaryDirectory() as td:
   td=Path(td)
   cases={"duplicate":b'{"schema":"OTCSLA0","schema":"OTCSLA0"}',"malformed":b'{',"oversized":b'x'*(m.MAX_BYTES+1)}
   for name,raw in cases.items():
    p=td/(name+".json");p.write_bytes(raw)
    with self.subTest(name=name),self.assertRaises(m.AdmissionError):m.load_admission(p,False)
  for value in ("x"*(m.MAX_STRING+1),unicodedata.normalize("NFD","caf\u00e9"),"secret=value"):
   with self.subTest(value=value[:8]),self.assertRaises(m.AdmissionError):m._walk(value)
  deep={};cursor=deep
  for _ in range(m.MAX_DEPTH+2):cursor["next"]={};cursor=cursor["next"]
  with self.assertRaises(m.AdmissionError):m._walk(deep)
  with self.assertRaises(m.AdmissionError):m._walk([False]*m.MAX_NODES)
 def test_parent_mutation_pure_surface_and_sanitized_cli(self):
  with tempfile.TemporaryDirectory() as td:
   td=Path(td);bad97=td/"97.json";bad97.write_bytes(OT097.read_bytes()+b" ");bad99=td/"99.json";bad99.write_bytes(OT099.read_bytes()+b" ")
   with self.assertRaises(m.AdmissionError):m.validate(ART,bad97,OT099)
   with self.assertRaises(m.AdmissionError):m.validate(ART,OT097,bad99)
  src=TOOL.read_text(encoding="utf-8")
  for token in ("import socket","import requests","import urllib","import subprocess","os.system","idf.py","esptool","git clone"):self.assertNotIn(token,src)
  p=subprocess.run([sys.executable,str(TOOL),str(ART),"--evidence",r"Z:\restricted\private.json"],capture_output=True,text=True);self.assertEqual((p.returncode,p.stdout,p.stderr.strip()),(2,"","OTCSLA0 validation failed"));self.assertNotIn("restricted",p.stderr)
if __name__=="__main__":unittest.main()
