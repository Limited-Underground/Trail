#!/usr/bin/env python3
import copy,importlib.util,json,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];ART=ROOT/"tests/benchmarks/crypto/OT-098-OT005-EXTERNAL-CANDIDATE-ACQUISITION-V0.json";TOOL=ROOT/"tools/crypto_candidate_acquisition_inspection.py"
s=importlib.util.spec_from_file_location("otcai",TOOL);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class Tests(unittest.TestCase):
 def setUp(self):self.data=json.loads(ART.read_text(encoding="utf-8"))
 def reject(self,f):
  d=copy.deepcopy(self.data);f(d)
  with tempfile.TemporaryDirectory() as td:
   p=Path(td)/"x.json";p.write_text(json.dumps(d),encoding="utf-8")
   with self.assertRaises(m.ContractError):m.validate(p)
 def test_canonical_cli(self):
  data=m.validate(ART);self.assertEqual(data["summary"]["candidate_count"],2);self.assertEqual(data["candidates"][1]["license_expression"],"CC0-1.0 OR BSD-2-Clause");self.assertEqual(data["candidates"][0]["project_license_choice"],"ISC");self.assertIsNone(data["candidates"][1]["project_license_choice"]);self.assertEqual(data["candidates"][1]["parent_commit"],"9a046c3bfbade2e503471a52ea44272297798f98");self.assertEqual(len(data["candidates"][1]["license_files"]),2)
  p=subprocess.run([sys.executable,str(TOOL),str(ART)],capture_output=True,text=True);self.assertEqual(p.returncode,0);self.assertIn("ZERO-SOURCES-IMPORTED",p.stdout)
 def test_checkout_bytes_are_deterministic(self):
  rel=ART.relative_to(ROOT).as_posix();p=subprocess.run(["git","check-attr","-z","text","eol","--",rel],cwd=ROOT,capture_output=True,check=True)
  parts=p.stdout.decode("utf-8").split("\0");attrs={parts[i+1]:parts[i+2] for i in range(0,len(parts)-1,3)}
  self.assertEqual(attrs,{"text":"set","eol":"lf"})
 def test_forgery_rejected(self):
  fs=[lambda d:d["candidates"].reverse(),lambda d:d["candidates"][0].__setitem__("commit","0"*40),lambda d:d["candidates"][0].__setitem__("tree","0"*40),lambda d:d["candidates"][0].__setitem__("manifest_sha256","0"*64),lambda d:d["candidates"][0].__setitem__("license_expression","MIT"),lambda d:d["candidates"][1].__setitem__("project_license_choice","BSD-2-Clause"),lambda d:d["candidates"][1].__setitem__("parent_commit","0"*40),lambda d:d["candidates"][1]["license_files"].pop(),lambda d:d["candidates"][0]["signature"].__setitem__("verified",True),lambda d:d["candidates"][0]["operations"].__setitem__("noise_xk_handshake",True),lambda d:d["candidates"][0].__setitem__("sbom_present",True),lambda d:d["candidates"][0].__setitem__("transitive_dependency_inventory_complete",True),lambda d:d["candidates"][0].__setitem__("project_source_lock_present",True),lambda d:d["summary"].__setitem__("sources_imported_count",1),lambda d:d["authority"].__setitem__("benchmark_build_authorized",True),lambda d:d["claims"].__setitem__("candidate_selected",True),lambda d:d["summary"].__setitem__("score_credit_added",True),lambda d:d["unchanged_blockers"].pop(),lambda d:d["candidates"][0].__setitem__("origin","C:\\private")]
  for f in fs:
   with self.subTest(f=f):self.reject(f)
 def test_duplicate_and_pure(self):
  with tempfile.TemporaryDirectory() as td:
   p=Path(td)/"x";p.write_text('{"a":1,"a":2}',encoding="utf-8")
   with self.assertRaises(m.ContractError):m.validate(p)
  src=TOOL.read_text(encoding="utf-8")
  for x in ("import subprocess","import urllib","import requests","git clone","idf.py","esptool"):self.assertNotIn(x,src)
  p=subprocess.run([sys.executable,str(TOOL),r"Z:\restricted\artifact.json"],capture_output=True,text=True);self.assertEqual(p.returncode,2);self.assertEqual(p.stdout,"");self.assertEqual(p.stderr.strip(),"OTCAI0 validation failed");self.assertNotIn("restricted",p.stderr)
if __name__=="__main__":unittest.main()
