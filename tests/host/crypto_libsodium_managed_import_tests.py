#!/usr/bin/env python3
import copy,hashlib,importlib.util,json,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];ART=ROOT/"tests/benchmarks/crypto/OT-099-OT005-LIBSODIUM-MANAGED-IMPORT-V0.json";TOOL=ROOT/"tools/crypto_libsodium_managed_import.py";FIX=ROOT/"tests/benchmarks/crypto/esp_idf/espressif_libsodium_1_0_22"
s=importlib.util.spec_from_file_location("otlmi",TOOL);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class Tests(unittest.TestCase):
 def setUp(self):self.data=json.loads(ART.read_text(encoding="utf-8"))
 def reject(self,f):
  d=copy.deepcopy(self.data);f(d)
  with tempfile.TemporaryDirectory() as td:
   p=Path(td)/"x.json";p.write_text(json.dumps(d),encoding="utf-8")
   with self.assertRaises(m.ContractError):m.validate(p)
 def test_canonical_bundle(self):
  d=m.validate(ART);self.assertEqual(d["acquisition"]["component_hash_sha256"],"39c9dc77d81804d54a539c8f076faed165152be7720ddd0e721acb9daf4aa5af");self.assertEqual(d["source_lock"]["source_manifest_entry_count"],733);self.assertEqual(len(d["licenses"]["files"]),2);self.assertFalse(d["inventory"]["supplied_component_sbom_is_complete_spdx"])
  for key in ("device_accessed","flashed","radio_used","crypto_executed","keys_or_entropy_used","benchmark_executed","candidate_selected","suite_selected","packet_v1_authorized","score_credit_added"):self.assertFalse(d["boundaries"][key])
  self.assertFalse(d["source_lock"]["accepted"]);self.assertTrue(d["source_lock"]["evidence_complete"]);self.assertFalse(d["build"]["probe_symbols_retained_in_final_map"]);self.assertEqual(len(d["remaining_blockers"]),6)
  p=subprocess.run([sys.executable,str(TOOL),str(ART)],capture_output=True,text=True);self.assertEqual(p.returncode,0);self.assertIn("SOURCE-LOCK-ADMISSION-PENDING",p.stdout)
 def test_retained_files_and_digests(self):
  d=self.data
  for k in ("lock","source_manifest","patch_manifest"):
   path=ROOT/d["source_lock"][k+"_path"] if k!="lock" else ROOT/d["source_lock"]["lock_path"]
   self.assertTrue(path.is_file())
  manifest=FIX/"source-manifest.sha256";self.assertEqual(hashlib.sha256(manifest.read_bytes()).hexdigest(),d["source_lock"]["source_manifest_sha256"]);self.assertEqual(len(manifest.read_text(encoding="utf-8").splitlines()),733)
  lock=FIX/"dependencies.lock";self.assertEqual(hashlib.sha256(lock.read_bytes()).hexdigest(),d["source_lock"]["lock_sha256"]);self.assertIn("component_hash: 39c9dc77",lock.read_text(encoding="utf-8"))
  sbom_path=FIX/"sbom.spdx.json";self.assertEqual(hashlib.sha256(sbom_path.read_bytes()).hexdigest(),d["inventory"]["spdx_sha256"]);sbom=json.loads(sbom_path.read_text(encoding="utf-8"));self.assertEqual(sbom["spdxVersion"],"SPDX-2.3");self.assertEqual(len(sbom["packages"]),2)
  patch_path=FIX/"patch-manifest.json";self.assertEqual(hashlib.sha256(patch_path.read_bytes()).hexdigest(),d["source_lock"]["patch_manifest_sha256"]);self.assertEqual(json.loads(patch_path.read_text(encoding="utf-8"))["patches"],[])
 def test_isolation_and_ignore(self):
  self.assertIn("set(COMPONENTS main)",(FIX/"CMakeLists.txt").read_text(encoding="utf-8"));self.assertIn('==1.0.22',(FIX/"main/idf_component.yml").read_text(encoding="utf-8"));self.assertIn("crypto_sign_detached",(FIX/"main/candidate_compile_probe.c").read_text(encoding="utf-8"))
  for rel in ("managed_components/espressif__libsodium/LICENSE","build/candidate.bin","sdkconfig"):
   p=subprocess.run(["git","check-ignore","-q",str(FIX/rel)],cwd=ROOT);self.assertEqual(p.returncode,0,rel)
  p=subprocess.run(["git","ls-files","--error-unmatch",str(FIX/"managed_components/espressif__libsodium/LICENSE")],cwd=ROOT,capture_output=True);self.assertNotEqual(p.returncode,0)
 def test_forgery_and_privacy(self):
  fs=[lambda d:d["acquisition"].__setitem__("version","1.0.21"),lambda d:d["source_lock"].__setitem__("accepted",True),lambda d:d["source_lock"].__setitem__("evidence_complete",False),lambda d:d["source_lock"].__setitem__("source_manifest_sha256","0"*64),lambda d:d["licenses"].__setitem__("legal_clearance_claimed",True),lambda d:d["operations"].__setitem__("noise_xk_handshake",True),lambda d:d["build"].__setitem__("probe_symbols_retained_in_final_map",True),lambda d:d["boundaries"].__setitem__("flashed",True),lambda d:d["remaining_blockers"].pop(),lambda d:d["acquisition"].__setitem__("wrapper_path","C:\\private")]
  for f in fs:
   with self.subTest(f=f):self.reject(f)
  src=TOOL.read_text(encoding="utf-8")
  for x in ("import subprocess","import urllib","import requests","idf.py","esptool","git clone"):self.assertNotIn(x,src)
if __name__=="__main__":unittest.main()
