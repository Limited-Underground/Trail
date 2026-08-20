#!/usr/bin/env python3
"""Strictly validate append-only OT-100 admission of exact OT-099 source evidence."""
import argparse,hashlib,importlib.util,json,re,sys,unicodedata
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
DEFAULT_CONTRACT=ROOT/"tests/benchmarks/crypto/OT-097-OT005-LICENSE-AWARE-SOURCE-LOCK-ADMISSION-V1.json"
DEFAULT_EVIDENCE=ROOT/"tests/benchmarks/crypto/OT-099-OT005-LIBSODIUM-MANAGED-IMPORT-V0.json"
EXPECTED_ADMISSION_SHA256="df595f2d07ba1b5d0a9bdf70237b1f0ea5a01fe8cb5a63ffb3575fe484faede0"
EXPECTED_CONTRACT_RAW_SHA256="d9c77f2cd22200fa18f8f43bffccfa55123f57a4f73979f2c037768fcfb44427"
EXPECTED_CONTRACT_POLICY_SHA256="51639e1b9342dc9e501fb0682d044c0f7c05e691e1a26f463358a753f28a123a"
EXPECTED_EVIDENCE_SHA256="8285fa7308bfc83a5d55503a7a3e1fa4c21895a42b095197b3ec75f634411ec9"
SCHEMA="OTCSLA0";VERSION=0;KIND="append_only_source_lock_acceptance_delta";IDENTITY="OT-100-OT005-LIBSODIUM-SOURCE-LOCK-ADMISSION-DELTA-V0";DATE="2026-08-20";STATUS="libsodium_source_lock_admitted_host_only_readiness_blocked";RESULT="LIBSODIUM-1.0.22-SOURCE-LOCK-ADMITTED-HOST-ONLY; FIVE-OTCBR0-REQUIREMENTS-REMAIN; NO-API-CONFIG-OR-IMPORT-ACCEPTANCE; OTCBR0-READINESS-BLOCKED"
MAX_BYTES=131072;MAX_DEPTH=16;MAX_NODES=4096;MAX_STRING=2048
PRIVATE=(re.compile(r"[A-Za-z]:\\"),re.compile(r"/(?:home|users)/",re.I),re.compile(r"\b(?:password|private[_ -]?key|secret|latitude|longitude)\s*[:=]",re.I))
CANDIDATES=("espressif_libsodium","esp_idf_mbedtls_psa","monocypher")
TOP={"schema","version","artifact_kind","acceptance_id","accepted_date","status","public_result","parents","accepted_candidate","accepted_source_evidence_sha256","accepted_api_config_evidence_sha256","accepted_candidate_import_evidence_sha256","acceptance_counts","historical_six_blockers","current_five_blockers","claims","authority","license_claims"}
CANDIDATE_KEYS={"candidate_id","version","component","component_hash_sha256","upstream_commit","upstream_tree","lock_kind","lock_sha256","source_manifest_sha256","source_manifest_entry_count","license_expression","project_license_choice","license_inventory_complete","spdx_sha256","transitive_inventory_complete_for_managed_lock","patch_manifest_sha256","patch_count"}
CLAIM_KEYS={"libsodium_source_lock_accepted","api_config_eligibility_proven","candidate_import_accepted","final_candidate_configuration_proven","readiness_accepted","benchmark_executed","candidate_selected","suite_selected","packet_v1_wire_selected","hardware_or_device_accessed","physical_evidence_added","score_credit_added"}
AUTH_KEYS={"dependency_acquisition_authorized","candidate_import_authorized","benchmark_build_authorized","benchmark_execution_authorized","device_access_authorized","radio_transmit_authorized","key_or_entropy_operation_authorized","suite_selection_authorized","packet_v1_authorized","score_credit_added"}
class AdmissionError(ValueError):pass
def _module(name,path):
 spec=importlib.util.spec_from_file_location(name,path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
def _pairs(items):
 out={}
 for k,v in items:
  if k in out:raise AdmissionError("duplicate key")
  out[k]=v
 return out
def _walk(v,d=0,n=None):
 n=[0] if n is None else n;n[0]+=1
 if n[0]>MAX_NODES or d>MAX_DEPTH:raise AdmissionError("bounds")
 if type(v) is dict:
  for k,x in v.items():
   if type(k) is not str or not k or len(k)>MAX_STRING or unicodedata.normalize("NFC",k)!=k:raise AdmissionError("key")
   _walk(x,d+1,n)
 elif type(v) is list:
  for x in v:_walk(x,d+1,n)
 elif type(v) is str:
  if not v or len(v)>MAX_STRING or unicodedata.normalize("NFC",v)!=v or any(p.search(v) for p in PRIVATE):raise AdmissionError("text")
 elif v is not None and type(v) not in (bool,int):raise AdmissionError("scalar")
def _read(path,expected=None):
 with Path(path).open("rb") as f:raw=f.read(MAX_BYTES+1)
 if not raw or len(raw)>MAX_BYTES or (expected and hashlib.sha256(raw).hexdigest()!=expected):raise AdmissionError("immutable bytes mismatch")
 return raw
def load_admission(path,enforce_digest=True):
 raw=_read(path,EXPECTED_ADMISSION_SHA256 if enforce_digest else None)
 try:data=json.loads(raw.decode("utf-8"),object_pairs_hook=_pairs)
 except (UnicodeError,json.JSONDecodeError,RecursionError,ValueError) as e:raise AdmissionError("invalid admission") from e
 _walk(data);return data
def _keys(v,keys,name):
 if type(v) is not dict or set(v)!=keys:raise AdmissionError(name+" fields")
def _bools(v,keys,name,only_true=None):
 _keys(v,keys,name)
 for k,x in v.items():
  expected=(k==only_true) if only_true else False
  if type(x) is not bool or x is not expected:raise AdmissionError(name+" booleans")
def validate_semantics(data,evidence):
 _keys(data,TOP,"top")
 exact=(data["schema"],data["version"],data["artifact_kind"],data["acceptance_id"],data["accepted_date"],data["status"],data["public_result"])
 if exact!=(SCHEMA,VERSION,KIND,IDENTITY,DATE,STATUS,RESULT) or type(data["version"]) is not int:raise AdmissionError("identity")
 parents={"otcsl0_v1_raw_sha256":EXPECTED_CONTRACT_RAW_SHA256,"otcsl0_v1_policy_sha256":EXPECTED_CONTRACT_POLICY_SHA256,"otlmi0_v0_raw_sha256":EXPECTED_EVIDENCE_SHA256}
 if type(data["parents"]) is not dict or data["parents"]!=parents:raise AdmissionError("parents")
 for name in ("accepted_source_evidence_sha256","accepted_api_config_evidence_sha256","accepted_candidate_import_evidence_sha256"):_keys(data[name],set(CANDIDATES),name)
 exact_source={"espressif_libsodium":[EXPECTED_EVIDENCE_SHA256],"esp_idf_mbedtls_psa":[],"monocypher":[]};empty={k:[] for k in CANDIDATES}
 if data["accepted_source_evidence_sha256"]!=exact_source or data["accepted_api_config_evidence_sha256"]!=empty or data["accepted_candidate_import_evidence_sha256"]!=empty:raise AdmissionError("anchors")
 counts=data["acceptance_counts"]
 _keys(counts,{"source","api_config","candidate_import"},"counts")
 if any(type(x) is not int for x in counts.values()) or counts!={"source":1,"api_config":0,"candidate_import":0}:raise AdmissionError("counts")
 candidate=data["accepted_candidate"]
 _keys(candidate,CANDIDATE_KEYS,"candidate")
 checks={"candidate_id":"espressif_libsodium","version":evidence["acquisition"]["version"],"component":evidence["acquisition"]["component"],"component_hash_sha256":evidence["acquisition"]["component_hash_sha256"],"upstream_commit":evidence["acquisition"]["upstream_commit"],"upstream_tree":evidence["acquisition"]["upstream_tree"],"lock_kind":evidence["source_lock"]["lock_kind"],"lock_sha256":evidence["source_lock"]["lock_sha256"],"source_manifest_sha256":evidence["source_lock"]["source_manifest_sha256"],"source_manifest_entry_count":evidence["source_lock"]["source_manifest_entry_count"],"license_expression":evidence["licenses"]["project_license_expression"],"project_license_choice":evidence["licenses"]["project_license_choice"],"license_inventory_complete":evidence["licenses"]["complete_observed_package_inventory"],"spdx_sha256":evidence["inventory"]["spdx_sha256"],"transitive_inventory_complete_for_managed_lock":evidence["inventory"]["transitive_inventory_complete_for_managed_lock"],"patch_manifest_sha256":evidence["source_lock"]["patch_manifest_sha256"],"patch_count":evidence["source_lock"]["patch_count"]}
 if candidate!=checks or type(candidate["source_manifest_entry_count"]) is not int or type(candidate["patch_count"]) is not int or type(candidate["license_inventory_complete"]) is not bool or type(candidate["transitive_inventory_complete_for_managed_lock"]) is not bool:raise AdmissionError("candidate facts")
 historical=evidence["remaining_blockers"]
 if type(data["historical_six_blockers"]) is not list or data["historical_six_blockers"]!=historical or len(historical)!=6:raise AdmissionError("history")
 current=[x for x in historical if x!="espressif_libsodium_source_lock_absent"]
 if type(data["current_five_blockers"]) is not list or data["current_five_blockers"]!=current or len(current)!=5:raise AdmissionError("current blockers")
 _bools(data["claims"],CLAIM_KEYS,"claims","libsodium_source_lock_accepted");_bools(data["authority"],AUTH_KEYS,"authority");_bools(data["license_claims"],{"legal_clearance_claimed","license_compatibility_determined"},"license")
 return data
def validate(admission_path,contract_path=DEFAULT_CONTRACT,evidence_path=DEFAULT_EVIDENCE,enforce_admission_digest=True):
 data=load_admission(admission_path,enforce_admission_digest);_read(contract_path,EXPECTED_CONTRACT_RAW_SHA256);_read(evidence_path,EXPECTED_EVIDENCE_SHA256)
 otcsl=_module("ot100_otcsl",ROOT/"tools/crypto_candidate_source_lock.py");otlmi=_module("ot100_otlmi",ROOT/"tools/crypto_libsodium_managed_import.py")
 try:contract=otcsl.load(Path(contract_path));otcsl.validate_contract(contract);evidence=otlmi.validate(Path(evidence_path))
 except Exception as e:raise AdmissionError("parent validation failed") from e
 if otcsl.admission_policy_sha256(contract)!=EXPECTED_CONTRACT_POLICY_SHA256:raise AdmissionError("policy mismatch")
 return validate_semantics(data,evidence)
def main(argv=None):
 p=argparse.ArgumentParser();p.add_argument("admission",type=Path);p.add_argument("--contract",type=Path,default=DEFAULT_CONTRACT);p.add_argument("--evidence",type=Path,default=DEFAULT_EVIDENCE);a=p.parse_args(argv)
 try:d=validate(a.admission,a.contract,a.evidence)
 except (OSError,AdmissionError,KeyError,TypeError,UnicodeError,RecursionError):print("OTCSLA0 validation failed",file=sys.stderr);return 2
 print(d["public_result"]);return 0
if __name__=="__main__":raise SystemExit(main())
