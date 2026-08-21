#!/usr/bin/env python3
"""Generate the deterministic, OTCSL0/v1-conformant OT-102 source lock."""
from __future__ import annotations

import argparse
import hashlib
import json
import stat
import subprocess
import unicodedata
from pathlib import Path


COMMIT="ab2b16dd619ad5f6979a4fbe69cfa324a6fcc35f";PARENT="9a046c3bfbade2e503471a52ea44272297798f98";TREE="eccc366491fc98c4149401d580ce41081a7854b1";TAG="4.0.3";ORIGIN="https://github.com/LoupVaillant/Monocypher.git";DATE="2026-08-20"
OT097_RAW="d9c77f2cd22200fa18f8f43bffccfa55123f57a4f73979f2c037768fcfb44427";OT097_POLICY="51639e1b9342dc9e501fb0682d044c0f7c05e691e1a26f463358a753f28a123a";OT098_RAW="b7be03e305c6253e10f69f624132a736cce5aea3f559760cde4f948ae79abad6";OT099_RAW="8285fa7308bfc83a5d55503a7a3e1fa4c21895a42b095197b3ec75f634411ec9";OT100_RAW="df595f2d07ba1b5d0a9bdf70237b1f0ea5a01fe8cb5a63ffb3575fe484faede0"
RESERVED={"CON","PRN","AUX","NUL",*(f"COM{x}" for x in range(1,10)),*(f"LPT{x}" for x in range(1,10))}
HISTORICAL_SIX=["exact_received_target_profile_unresolved","final_candidate_build_configuration_unresolved","espressif_libsodium_source_lock_absent","esp_idf_mbedtls_psa_dependency_lock_and_api_config_unresolved","monocypher_source_lock_absent","direct_radio_mtu_phy_region_unresolved"]
PRIOR_FIVE=[x for x in HISTORICAL_SIX if x!="espressif_libsodium_source_lock_absent"]
CURRENT_FOUR=[x for x in PRIOR_FIVE if x!="monocypher_source_lock_absent"]
AUTHORITY_FIELDS=("dependency_acquisition_authorized","candidate_import_authorized","benchmark_build_authorized","benchmark_execution_authorized","device_access_authorized","radio_transmit_authorized","key_or_entropy_operation_authorized","suite_selection_authorized","packet_v1_authorized","score_credit_added")
CLAIM_FIELDS=("source_acquired","source_lock_accepted","candidate_imported","api_config_eligibility_proven","candidate_benchmark_executed","candidate_selected","suite_selected","packet_v1_wire_selected","hardware_or_device_accessed","physical_evidence_added","score_credit_added")


def git(source:Path,*args:str)->str:
 r=subprocess.run(["git","-C",str(source),*args],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 return r.stdout.decode("utf-8").rstrip("\n")
def git_bytes(source:Path,*args:str)->bytes:
 r=subprocess.run(["git","-C",str(source),*args],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 return r.stdout
def sha(raw:bytes)->str:return hashlib.sha256(raw).hexdigest()
def canonical(value:object)->bytes:return json.dumps(value,ensure_ascii=False,allow_nan=False,sort_keys=True,separators=(",",":")).encode("utf-8")
def write_canonical(path:Path,value:object)->str:
 raw=canonical(value);path.write_bytes(raw);return sha(raw)
def write_jsonl(path:Path,values:list[dict])->str:
 raw=b"".join(canonical(v)+b"\n" for v in values);path.write_bytes(raw);return sha(raw)
def safe_path(path:str)->None:
 if path!=unicodedata.normalize("NFC",path) or "\\" in path or path.startswith("/") or ":" in path or "//" in path:raise SystemExit("unsafe path")
 for part in path.split("/"):
  if part in ("",".","..") or part!=part.strip() or part.endswith(("."," ")) or part.split(".",1)[0].upper() in RESERVED or any(unicodedata.category(c).startswith("C") for c in part):raise SystemExit("unsafe path")
def package_for(path:str)->str:
 if not path.startswith("tests/externals/"):return "monocypher"
 return path.split("/",3)[2]
def verification_code(files:list[dict])->str:
 values=sorted(next(x["checksumValue"] for x in f["checksums"] if x["algorithm"]=="SHA1") for f in files)
 return hashlib.sha1("".join(values).encode("ascii")).hexdigest()


def main()->int:
 p=argparse.ArgumentParser();p.add_argument("source",type=Path);p.add_argument("bundle",type=Path);p.add_argument("source_evidence",type=Path);p.add_argument("admission",type=Path);a=p.parse_args()
 source=a.source.resolve();bundle=a.bundle.resolve();vendor=bundle/"source"
 checks=((git(source,"remote","get-url","origin"),ORIGIN),(git(source,"rev-parse","HEAD"),COMMIT),(git(source,"rev-parse","HEAD^"),PARENT),(git(source,"rev-parse","HEAD^{tree}"),TREE),(git(source,"rev-parse",f"refs/tags/{TAG}"),COMMIT),(git(source,"cat-file","-t",f"refs/tags/{TAG}"),"commit"))
 if any(x!=y for x,y in checks) or git(source,"status","--porcelain=v1","--untracked-files=all") or git(source,"submodule","status"):raise SystemExit("unexpected or dirty source")
 if bundle.exists() or a.source_evidence.exists() or a.admission.exists():raise SystemExit("output already exists")
 index={}
 for line in git(source,"ls-files","-s").splitlines():
  left,path=line.split("\t",1);mode,blob,stage=left.split(" ")
  if stage!="0" or mode not in ("100644","100755"):raise SystemExit("non-regular source entry")
  index[path]=(mode,blob)
 paths=git(source,"ls-files").splitlines()
 if paths!=sorted(paths) or len(paths)!=161 or set(paths)!=set(index):raise SystemExit("unexpected source inventory")
 casefold=set();directories=set();file_rows=[];spdx_files=[];package_files={x:[] for x in ("monocypher","c25519","ed25519-donna","tinyssh","tweetnacl")};total_bytes=0
 bundle.mkdir(parents=True);vendor.mkdir()
 for number,rel in enumerate(paths,1):
  safe_path(rel);fold=unicodedata.normalize("NFC",rel).casefold()
  if fold in casefold:raise SystemExit("casefold collision")
  casefold.add(fold)
  parent=Path(rel).parent
  while parent!=Path("."):directories.add(parent.as_posix());parent=parent.parent
  src=source/rel
  if src.is_symlink() or (getattr(src.stat(follow_symlinks=False),"st_file_attributes",0)&0x400):raise SystemExit("symlink or reparse point")
  mode,blob=index[rel];raw=git_bytes(source,"cat-file","blob",blob);total_bytes+=len(raw);dst=vendor/Path(rel);dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(raw)
  row={"bytes":len(raw),"git_blob":blob,"kind":"regular_file","mode":mode,"path":rel,"sha256":sha(raw)};file_rows.append(row)
  entry={"SPDXID":f"SPDXRef-File-{number:04d}","checksums":[{"algorithm":"SHA1","checksumValue":hashlib.sha1(raw).hexdigest()},{"algorithm":"SHA256","checksumValue":sha(raw)}],"comment":f"Exact Git blob {blob}; mode {mode}.","copyrightText":"NOASSERTION","fileName":"./"+rel,"licenseConcluded":"NOASSERTION","licenseInfoInFiles":["NOASSERTION"]};spdx_files.append(entry);package_files[package_for(rel)].append(entry)
 dir_rows=[{"kind":"directory","path":x} for x in sorted(directories)]
 tree_rows=sorted(dir_rows+file_rows,key=lambda x:x["path"].encode("utf-8"));tree_manifest_sha=write_jsonl(bundle/"source-tree.jsonl",tree_rows);tree_sha=sha(b"OTCSL0/v1\0candidate-full-source-tree\0"+(bundle/"source-tree.jsonl").read_bytes())
 receipt={"acquired_date":DATE,"artifact_id":"candidate-acquisition-receipt","candidate_id":"monocypher","clean_checkout":True,"commit":COMMIT,"origin":ORIGIN,"parent_commit":PARENT,"requested_ref":TAG,"schema":"OTAR0","signature_present":False,"signature_reason":"lightweight_tag_and_unsigned_commit","signature_trust_resolved":False,"signature_verified":False,"tag":TAG,"tag_kind":"lightweight","tag_object":None,"tree":TREE,"version":1};receipt_sha=write_canonical(bundle/"acquisition-receipt.json",receipt)
 license_specs=[("LICENCE.md","project","CC0-1.0 OR BSD-2-Clause","BSD-2-Clause"),("AUTHORS.md","project-attribution","NOASSERTION","NOASSERTION"),("tests/externals/c25519/AUTHORS.md","test-only-external","LicenseRef-Public-Domain-Dedication","LicenseRef-Public-Domain-Dedication"),("tests/externals/ed25519-donna/AUTHORS.md","test-only-external","LicenseRef-Upstream-Public-Domain-Intent","NOASSERTION"),("tests/externals/tinyssh/LICENCE","test-only-external","CC0-1.0","CC0-1.0"),("tests/externals/tweetnacl/AUTHORS.md","test-only-external","LicenseRef-Public-Domain-Dedication","LicenseRef-Public-Domain-Dedication")]
 license_rows=[]
 for rel,scope,declared,concluded in license_specs:
  row=next(x for x in file_rows if x["path"]==rel);license_rows.append({"concluded_expression":concluded,"declared_expression":declared,"git_blob":row["git_blob"],"path":rel,"scope":scope,"sha256":row["sha256"]})
 license_sha=write_jsonl(bundle/"license-inventory.jsonl",license_rows)
 transitive_rows=[{"name":name,"path":"tests/externals/"+name,"runtime_or_link_dependency":False,"scope":"bundled-test-only"} for name in ("c25519","ed25519-donna","tinyssh","tweetnacl")];transitive_sha=write_jsonl(bundle/"transitive-dependencies.jsonl",transitive_rows)
 patch_rows=[{"artifact_id":"candidate-patch-set","patch_count":0,"post_patch_tree_sha256":tree_sha}];patch_sha=write_jsonl(bundle/"patches.jsonl",patch_rows)
 package_specs=[("monocypher","Monocypher","SPDXRef-Package-Monocypher","CC0-1.0 OR BSD-2-Clause","BSD-2-Clause",ORIGIN),("c25519","c25519-test-only","SPDXRef-Package-c25519","LicenseRef-Public-Domain-Dedication","LicenseRef-Public-Domain-Dedication","https://www.dlbeer.co.nz/oss/c25519.html"),("ed25519-donna","ed25519-donna-test-only","SPDXRef-Package-ed25519-donna","LicenseRef-Upstream-Public-Domain-Intent","NOASSERTION","https://github.com/floodyberry/ed25519-donna"),("tinyssh","tinyssh-test-only","SPDXRef-Package-tinyssh","CC0-1.0","CC0-1.0","NOASSERTION"),("tweetnacl","TweetNaCl-test-only","SPDXRef-Package-TweetNaCl","LicenseRef-Public-Domain-Dedication","LicenseRef-Public-Domain-Dedication","https://tweetnacl.cr.yp.to/")]
 packages=[];package_ids={}
 for key,name,identifier,declared,concluded,location in package_specs:
  package_ids[key]=identifier;packages.append({"SPDXID":identifier,"copyrightText":"NOASSERTION","downloadLocation":location,"filesAnalyzed":True,"licenseConcluded":concluded,"licenseDeclared":declared,"name":name,"packageVerificationCode":{"packageVerificationCodeValue":verification_code(package_files[key])},"versionInfo":TAG if key=="monocypher" else "NOASSERTION"})
 relationships=[{"relatedSpdxElement":package_ids["monocypher"],"relationshipType":"DESCRIBES","spdxElementId":"SPDXRef-DOCUMENT"}]
 for key in ("c25519","ed25519-donna","tinyssh","tweetnacl"):relationships.append({"relatedSpdxElement":package_ids[key],"relationshipType":"CONTAINS","spdxElementId":package_ids["monocypher"]})
 for key,entries in package_files.items():
  for entry in entries:relationships.append({"relatedSpdxElement":entry["SPDXID"],"relationshipType":"CONTAINS","spdxElementId":package_ids[key]})
 sbom={"SPDXID":"SPDXRef-DOCUMENT","comment":"Complete exact-tree package/file inventory; not legal clearance or compatibility determination.","creationInfo":{"created":"2026-08-20T00:00:00Z","creators":["Tool: OpenTrail OT-102 deterministic source-lock generator"]},"dataLicense":"CC0-1.0","documentDescribes":[package_ids["monocypher"]],"documentNamespace":f"https://opentrail.invalid/spdx/ot-102/monocypher/{COMMIT}","files":spdx_files,"hasExtractedLicensingInfos":[{"extractedText":"Upstream states that the identified test-only code was dedicated to the public domain.","licenseId":"LicenseRef-Public-Domain-Dedication"},{"extractedText":"Upstream records public-domain intent for the identified test-only code while noting incomplete per-file notices; no legal conclusion is made.","licenseId":"LicenseRef-Upstream-Public-Domain-Intent"}],"name":"OpenTrail-OT-102-Monocypher-4.0.3-exact-source","packages":packages,"relationships":relationships,"spdxVersion":"SPDX-2.3"};sbom_sha=write_canonical(bundle/"sbom.spdx.json",sbom)
 root="tests/benchmarks/crypto/monocypher/4.0.3/";project_lock={"boundaries":{"benchmark_executed":False,"build_executed":False,"candidate_selected":False,"crypto_executed":False,"device_accessed":False,"firmware_changed":False,"flashed":False,"keys_or_entropy_used":False,"physical_evidence_added":False,"radio_used":False,"score_credit_added":False,"source_linked_or_imported_by_firmware":False},"candidate_id":"monocypher","evidence":{"acquisition_receipt":{"kind":"sha256-canonical-json-acquisition-receipt-v1","path":root+"acquisition-receipt.json","sha256":receipt_sha},"full_tree":{"directory_count":len(dir_rows),"entry_count":len(tree_rows),"file_count":161,"kind":"sha256-utf8-jsonl-posix-tree-v1","path":root+"source-tree.jsonl","sha256":tree_manifest_sha,"total_bytes":total_bytes,"tree_sha256":tree_sha},"license":{"file_count":6,"inventory_complete":True,"kind":"sha256-utf8-jsonl-license-inventory-v1","path":root+"license-inventory.jsonl","sha256":license_sha},"patches":{"kind":"sha256-utf8-jsonl-ordered-patches-v1","patch_count":0,"path":root+"patches.jsonl","sha256":patch_sha},"sbom":{"component_count":5,"file_count":161,"kind":"sha256-canonical-spdx-json-v1","path":root+"sbom.spdx.json","sha256":sbom_sha},"transitive":{"dependency_count":4,"kind":"sha256-utf8-jsonl-transitive-dependencies-v1","path":root+"transitive-dependencies.jsonl","sha256":transitive_sha}},"lock_id":"OT-102-OT005-MONOCYPHER-4.0.3-VENDORED-SOURCE-TREE-LOCK-V0","lock_kind":"vendored_source_tree_lock","locked_date":DATE,"manifest_policy":{"allowed_entry_kinds":["directory","regular_file"],"case_policy":"unicode-15.1-nfc-casefold-unique","forbidden_entry_kinds":["absolute_path","backslash_path","dot_segment","drive_path","fifo","reparse_point","socket","symlink"],"path_encoding":"utf8_relative_posix","path_order":"ordinal_bytewise_ascending"},"project_license_choice":"BSD-2-Clause","schema":"OTMSL0","source":{"commit":COMMIT,"origin":ORIGIN,"parent_commit":PARENT,"retained_tree_path":root+"source","tag":TAG,"tag_kind":"lightweight","tree":TREE,"version":TAG},"upstream_license_expression":"CC0-1.0 OR BSD-2-Clause","version":0};project_lock_sha=write_canonical(bundle/"project-lock.json",project_lock)
 source_evidence={"acquisition_receipt":{"receipt_kind":"sha256-canonical-json-acquisition-receipt-v1","receipt_sha256":receipt_sha,"required":True},"artifact_kind":"candidate_source_evidence","authority":{x:False for x in AUTHORITY_FIELDS},"candidate_id":"monocypher","claims":{x:False for x in CLAIM_FIELDS},"contract_policy_sha256":OT097_POLICY,"evidence_id":"OT-102-OT005-MONOCYPHER-SOURCE-EVIDENCE-V0","full_tree_manifest":{"artifact_id":"candidate-full-source-tree","casefold_collision_count":0,"directory_count":len(dir_rows),"entry_count":len(tree_rows),"manifest_kind":"sha256-utf8-jsonl-posix-tree-v1","manifest_sha256":tree_manifest_sha,"regular_file_count":161,"reparse_point_count":0,"symlink_count":0,"total_bytes":total_bytes,"tree_sha256":tree_sha},"legal_clearance_claimed":False,"license_compatibility_determined":False,"license_manifest":{"artifact_id":"candidate-license-inventory","file_count":6,"inventory_complete":True,"manifest_kind":"sha256-utf8-jsonl-license-inventory-v1","manifest_sha256":license_sha,"project_license_choice":"BSD-2-Clause","upstream_license_expression":"CC0-1.0 OR BSD-2-Clause"},"lock_kind":"vendored_source_tree_lock","parent_idf_binding":{"component_glue_manifest_kind":None,"component_glue_manifest_sha256":None,"gitlink_commit":None,"gitlink_path":None,"parent_source_commit":None,"required":False},"patch_manifest":{"artifact_id":"candidate-patch-set","manifest_kind":"sha256-utf8-jsonl-ordered-patches-v1","manifest_sha256":patch_sha,"patch_count":0,"post_patch_tree_sha256":tree_sha},"project_dependency_lock":{"digest_kind":"sha256-raw-project-lock-bytes-v1","lock_kind":"vendored_source_tree_lock","lock_sha256":project_lock_sha,"logical_path":root+"project-lock.json"},"project_license_choice":"BSD-2-Clause","recorded_date":DATE,"role":"comparison","sbom_manifest":{"artifact_id":"candidate-sbom","component_count":5,"manifest_kind":"sha256-canonical-spdx-json-v1","manifest_sha256":sbom_sha},"schema":"OTCSLE0","source_commit":COMMIT,"source_kind":"external_vendored_source","transitive_manifest":{"artifact_id":"candidate-transitive-dependencies","dependency_count":4,"manifest_kind":"sha256-utf8-jsonl-transitive-dependencies-v1","manifest_sha256":transitive_sha},"upstream_license_expression":"CC0-1.0 OR BSD-2-Clause","version":1,"version_string":TAG};source_evidence_sha=write_canonical(a.source_evidence,source_evidence)
 admission_record={"acceptance_counts":{"api_config":0,"candidate_import":0,"source":2},"accepted_api_config_evidence_sha256":{"esp_idf_mbedtls_psa":[],"espressif_libsodium":[],"monocypher":[]},"accepted_candidate":{"candidate_id":"monocypher","license_inventory_sha256":license_sha,"lock_kind":"vendored_source_tree_lock","project_dependency_lock_sha256":project_lock_sha,"project_license_choice":"BSD-2-Clause","source_commit":COMMIT,"source_evidence_sha256":source_evidence_sha,"tree_sha256":tree_sha,"upstream_license_expression":"CC0-1.0 OR BSD-2-Clause","version":"4.0.3"},"accepted_candidate_import_evidence_sha256":{"esp_idf_mbedtls_psa":[],"espressif_libsodium":[],"monocypher":[]},"accepted_date":DATE,"accepted_source_evidence_sha256":{"esp_idf_mbedtls_psa":[],"espressif_libsodium":[OT099_RAW],"monocypher":[source_evidence_sha]},"admission_id":"OT-102-OT005-MONOCYPHER-SOURCE-LOCK-ADMISSION-DELTA-V0","artifact_kind":"append_only_monocypher_source_lock_acceptance_delta","authority":{"benchmark_build_authorized":False,"benchmark_execution_authorized":False,"candidate_import_authorized":False,"device_access_authorized":False,"key_or_entropy_operation_authorized":False,"packet_v1_authorized":False,"radio_transmit_authorized":False,"score_credit_added":False,"suite_selection_authorized":False},"claims":{"api_config_eligibility_proven":False,"benchmark_executed":False,"candidate_import_accepted":False,"candidate_selected":False,"final_candidate_configuration_proven":False,"hardware_or_device_accessed":False,"libsodium_source_lock_remains_accepted":True,"monocypher_source_lock_accepted":True,"packet_v1_wire_selected":False,"physical_evidence_added":False,"readiness_accepted":False,"score_credit_added":False,"suite_selected":False},"current_four_blockers":CURRENT_FOUR,"historical_six_blockers":HISTORICAL_SIX,"license_claims":{"legal_clearance_claimed":False,"license_compatibility_determined":False},"owner_authorization":{"project_license_choice":"BSD-2-Clause","project_license_choice_authorized":True,"source_reacquisition_authorized":True},"parents":{"otcai0_v0_raw_sha256":OT098_RAW,"otcsl0_v1_policy_sha256":OT097_POLICY,"otcsl0_v1_raw_sha256":OT097_RAW,"otcsla0_v0_raw_sha256":OT100_RAW,"otlmi0_v0_raw_sha256":OT099_RAW},"prior_current_five_blockers":PRIOR_FIVE,"public_result":"MONOCYPHER-4.0.3-SOURCE-LOCK-ADMITTED-HOST-ONLY-BSD-2-CLAUSE; FOUR-OTCBR0-REQUIREMENTS-REMAIN; NO-FIRMWARE-IMPORT-BUILD-BENCHMARK-OR-SELECTION; OTCBR0-READINESS-BLOCKED","schema":"OTMSLA0","source_evidence":{"path":"tests/benchmarks/crypto/OT-102-OT005-MONOCYPHER-SOURCE-EVIDENCE-V0.json","sha256":source_evidence_sha},"status":"monocypher_source_lock_admitted_host_only_readiness_blocked","version":0};admission_sha=write_canonical(a.admission,admission_record)
 for name,value in (("receipt",receipt_sha),("tree_manifest",tree_manifest_sha),("tree",tree_sha),("license",license_sha),("sbom",sbom_sha),("transitive",transitive_sha),("patches",patch_sha),("project_lock",project_lock_sha),("source_evidence",source_evidence_sha),("admission",admission_sha)):print(f"{name}_sha256={value}")
 return 0
if __name__=="__main__":raise SystemExit(main())
