#!/usr/bin/env python3
"""Strict read-only validator for OT-102's OTCSL0/v1 Monocypher lock."""
from __future__ import annotations
import argparse,copy,hashlib,importlib.util,json,re,stat,sys,unicodedata
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];BUNDLE=ROOT/'tests/benchmarks/crypto/monocypher/4.0.3';SOURCE=BUNDLE/'source'
OT097=ROOT/'tests/benchmarks/crypto/OT-097-OT005-LICENSE-AWARE-SOURCE-LOCK-ADMISSION-V1.json';OT098=ROOT/'tests/benchmarks/crypto/OT-098-OT005-EXTERNAL-CANDIDATE-ACQUISITION-V0.json';OT099=ROOT/'tests/benchmarks/crypto/OT-099-OT005-LIBSODIUM-MANAGED-IMPORT-V0.json';OT100=ROOT/'tests/benchmarks/crypto/OT-100-OT005-LIBSODIUM-SOURCE-LOCK-ADMISSION-DELTA-V0.json';EVIDENCE=ROOT/'tests/benchmarks/crypto/OT-102-OT005-MONOCYPHER-SOURCE-EVIDENCE-V0.json'
ADMISSION_SHA='6dbeeac0266f9e6dd90265cdd71a721acfd36b4308dcb87180bd9d7c24c77e52';EVIDENCE_SHA='fe037820304103f7ca2253665076e4dc41740598ca9742ba8d45f6ec64ebc06f';RECEIPT_SHA='7273ce62398c07dc68442322941759ee2c76fe52eb479d33cd4c0a5f692e12f3';TREE_MANIFEST_SHA='efb166777889c9ec1e1c08be73c2f8c878bd48300abfcd9f884d81ea5486ad84';TREE_SHA='14005f0fa68b61a1de7cdeb1e238fdbb272c64448e3804d6d9a6b7d038bd8e84';LICENSE_SHA='3de83e152bf130e5ab448760ceba06a727c8db5e92afe27c0c6d679fb426f5b7';SBOM_SHA='cd82634f6512a3bff927111686aa389e8fb9885e8d53e21b90690b4509bf1154';TRANSITIVE_SHA='66a1a45fd529fc6b4a2cdb96fe5db815a284be27b1207ef75eb04f1f8bf7209b';PATCH_SHA='64b13273ec040db3299c858d1fd6c172d7cc30e49b457ee049aa27211e9a1b6f';LOCK_SHA='b78dd459464180518c97f8bd192edab6fb7e886634e30bc498c2f2f5ad0307ca'
OT097_RAW='d9c77f2cd22200fa18f8f43bffccfa55123f57a4f73979f2c037768fcfb44427';OT097_POLICY='51639e1b9342dc9e501fb0682d044c0f7c05e691e1a26f463358a753f28a123a';OT098_RAW='b7be03e305c6253e10f69f624132a736cce5aea3f559760cde4f948ae79abad6';OT099_RAW='8285fa7308bfc83a5d55503a7a3e1fa4c21895a42b095197b3ec75f634411ec9';OT100_RAW='df595f2d07ba1b5d0a9bdf70237b1f0ea5a01fe8cb5a63ffb3575fe484faede0'
COMMIT='ab2b16dd619ad5f6979a4fbe69cfa324a6fcc35f';PARENT='9a046c3bfbade2e503471a52ea44272297798f98';GIT_TREE='eccc366491fc98c4149401d580ce41081a7854b1';ORIGIN='https://github.com/LoupVaillant/Monocypher.git';RESULT='MONOCYPHER-4.0.3-SOURCE-LOCK-ADMITTED-HOST-ONLY-BSD-2-CLAUSE; FOUR-OTCBR0-REQUIREMENTS-REMAIN; NO-FIRMWARE-IMPORT-BUILD-BENCHMARK-OR-SELECTION; OTCBR0-READINESS-BLOCKED'
HISTORICAL_SIX=['exact_received_target_profile_unresolved','final_candidate_build_configuration_unresolved','espressif_libsodium_source_lock_absent','esp_idf_mbedtls_psa_dependency_lock_and_api_config_unresolved','monocypher_source_lock_absent','direct_radio_mtu_phy_region_unresolved'];PRIOR_FIVE=[x for x in HISTORICAL_SIX if x!='espressif_libsodium_source_lock_absent'];CURRENT_FOUR=[x for x in PRIOR_FIVE if x!='monocypher_source_lock_absent']
ADMISSION_AUTHORITY={'benchmark_build_authorized','benchmark_execution_authorized','candidate_import_authorized','device_access_authorized','key_or_entropy_operation_authorized','packet_v1_authorized','radio_transmit_authorized','score_credit_added','suite_selection_authorized'}
ADMISSION_CLAIMS={'api_config_eligibility_proven','benchmark_executed','candidate_import_accepted','candidate_selected','final_candidate_configuration_proven','hardware_or_device_accessed','libsodium_source_lock_remains_accepted','monocypher_source_lock_accepted','packet_v1_wire_selected','physical_evidence_added','readiness_accepted','score_credit_added','suite_selected'}
HEX40=re.compile(r'^[0-9a-f]{40}$');HEX64=re.compile(r'^[0-9a-f]{64}$');PRIVATE=(re.compile(r'[A-Za-z]:\\'),re.compile(r'/(?:home|users)/',re.I),re.compile(r'\b(?:password|private[_ -]?key|secret|latitude|longitude)\s*[:=]',re.I));RESERVED={'CON','PRN','AUX','NUL',*(f'COM{x}' for x in range(1,10)),*(f'LPT{x}' for x in range(1,10))};MAX_BYTES=262144;MAX_DEPTH=20;MAX_NODES=32768;MAX_STRING=2048
class AdmissionError(ValueError):pass
def _module(name,path):
 spec=importlib.util.spec_from_file_location(name,path);mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod);return mod
def _pairs(items):
 out={}
 for k,v in items:
  if k in out:raise AdmissionError('duplicate key')
  out[k]=v
 return out
def _scan(v,d=0,n=None):
 n=[0] if n is None else n;n[0]+=1
 if d>MAX_DEPTH or n[0]>MAX_NODES:raise AdmissionError('bounds')
 if type(v) is dict:
  for k,x in v.items():
   if type(k) is not str or not k or len(k)>MAX_STRING or unicodedata.normalize('NFC',k)!=k:raise AdmissionError('key')
   _scan(x,d+1,n)
 elif type(v) is list:
  for x in v:_scan(x,d+1,n)
 elif type(v) is str:
  if not v or len(v)>MAX_STRING or unicodedata.normalize('NFC',v)!=v or any(p.search(v) for p in PRIVATE):raise AdmissionError('text')
 elif v is not None and type(v) not in (bool,int):raise AdmissionError('scalar')
def _raw(path,expected,limit=MAX_BYTES):
 with Path(path).open('rb') as f:raw=f.read(limit+1)
 if not raw or len(raw)>limit or hashlib.sha256(raw).hexdigest()!=expected:raise AdmissionError('immutable bytes')
 return raw
def _canonical(v):return json.dumps(v,ensure_ascii=False,allow_nan=False,sort_keys=True,separators=(',',':')).encode('utf-8')
def _json(path,expected):
 raw=_raw(path,expected)
 try:v=json.loads(raw.decode('utf-8'),object_pairs_hook=_pairs)
 except Exception as e:raise AdmissionError('json') from e
 _scan(v)
 if raw!=_canonical(v):raise AdmissionError('noncanonical json')
 return v
def _jsonl(path,expected):
 raw=_raw(path,expected)
 if not raw.endswith(b'\n') or b'\r' in raw or raw.startswith(b'\xef\xbb\xbf'):raise AdmissionError('jsonl encoding')
 out=[]
 for line in raw.splitlines():
  try:v=json.loads(line.decode('utf-8'),object_pairs_hook=_pairs)
  except Exception as e:raise AdmissionError('jsonl') from e
  _scan(v)
  if line!=_canonical(v):raise AdmissionError('noncanonical jsonl')
  out.append(v)
 return out
def _keys(v,expected,name):
 if type(v) is not dict or set(v)!=set(expected):raise AdmissionError(name+' fields')
def _safe_path(path):
 if type(path) is not str or not path or path!=unicodedata.normalize('NFC',path) or '\\' in path or path.startswith('/') or ':' in path or '//' in path:raise AdmissionError('unsafe path')
 for part in path.split('/'):
  if part in ('','.','..') or part!=part.strip() or part.endswith(('.', ' ')) or part.split('.',1)[0].upper() in RESERVED or any(unicodedata.category(c).startswith('C') for c in part):raise AdmissionError('unsafe path')
 return path
def _false(v,name):
 if type(v) is not dict or not v or any(type(x) is not bool or x for x in v.values()):raise AdmissionError(name)
def _blob_sha(raw):return hashlib.sha1(b'blob '+str(len(raw)).encode('ascii')+b'\0'+raw).hexdigest()
def _validate_tree():
 rows=_jsonl(BUNDLE/'source-tree.jsonl',TREE_MANIFEST_SHA)
 if len(rows)!=175 or [x.get('path') for x in rows]!=sorted((x.get('path') for x in rows),key=lambda x:x.encode('utf-8')):raise AdmissionError('tree order/count')
 manifest={};folds=set();dirs=set();files={};total=0
 for row in rows:
  kind=row.get('kind');path=_safe_path(row.get('path'))
  fold=unicodedata.normalize('NFC',path).casefold()
  if path in manifest or fold in folds:raise AdmissionError('tree collision')
  folds.add(fold);manifest[path]=row
  if kind=='directory':
   _keys(row,{'kind','path'},'directory');dirs.add(path)
  elif kind=='regular_file':
   _keys(row,{'bytes','git_blob','kind','mode','path','sha256'},'file')
   if type(row['bytes']) is not int or row['bytes']<0 or row['mode'] not in ('100644','100755') or not HEX40.fullmatch(row['git_blob']) or not HEX64.fullmatch(row['sha256']):raise AdmissionError('file facts')
   files[path]=row;total+=row['bytes']
  else:raise AdmissionError('entry kind')
 actual_dirs=set();actual_files=set()
 for item in SOURCE.rglob('*'):
  rel=item.relative_to(SOURCE).as_posix();_safe_path(rel);st=item.lstat()
  if item.is_symlink() or (getattr(st,'st_file_attributes',0)&0x400):raise AdmissionError('symlink/reparse')
  if stat.S_ISDIR(st.st_mode):actual_dirs.add(rel)
  elif stat.S_ISREG(st.st_mode):
   actual_files.add(rel);raw=item.read_bytes();row=files.get(rel)
   if row is None or len(raw)!=row['bytes'] or hashlib.sha256(raw).hexdigest()!=row['sha256'] or _blob_sha(raw)!=row['git_blob']:raise AdmissionError('retained source mismatch')
  else:raise AdmissionError('nonregular source')
 if actual_dirs!=dirs or actual_files!=set(files) or len(files)!=161 or len(dirs)!=14 or total!=1413389:raise AdmissionError('retained inventory')
 raw=_raw(BUNDLE/'source-tree.jsonl',TREE_MANIFEST_SHA)
 if hashlib.sha256(b'OTCSL0/v1\0candidate-full-source-tree\0'+raw).hexdigest()!=TREE_SHA:raise AdmissionError('tree digest')
 return files
def _validate_layers(files):
 receipt=_json(BUNDLE/'acquisition-receipt.json',RECEIPT_SHA)
 expected_receipt={'acquired_date':'2026-08-20','artifact_id':'candidate-acquisition-receipt','candidate_id':'monocypher','clean_checkout':True,'commit':COMMIT,'origin':ORIGIN,'parent_commit':PARENT,'requested_ref':'4.0.3','schema':'OTAR0','signature_present':False,'signature_reason':'lightweight_tag_and_unsigned_commit','signature_trust_resolved':False,'signature_verified':False,'tag':'4.0.3','tag_kind':'lightweight','tag_object':None,'tree':GIT_TREE,'version':1}
 if receipt!=expected_receipt:raise AdmissionError('receipt')
 licenses=_jsonl(BUNDLE/'license-inventory.jsonl',LICENSE_SHA);expected_paths=['LICENCE.md','AUTHORS.md','tests/externals/c25519/AUTHORS.md','tests/externals/ed25519-donna/AUTHORS.md','tests/externals/tinyssh/LICENCE','tests/externals/tweetnacl/AUTHORS.md']
 if [x.get('path') for x in licenses]!=expected_paths:raise AdmissionError('licenses')
 for row in licenses:
  _keys(row,{'concluded_expression','declared_expression','git_blob','path','scope','sha256'},'license');source=files[row['path']]
  if row['git_blob']!=source['git_blob'] or row['sha256']!=source['sha256']:raise AdmissionError('license binding')
 if licenses[0]['declared_expression']!='CC0-1.0 OR BSD-2-Clause' or licenses[0]['concluded_expression']!='BSD-2-Clause' or licenses[3]['concluded_expression']!='NOASSERTION':raise AdmissionError('license boundary')
 deps=_jsonl(BUNDLE/'transitive-dependencies.jsonl',TRANSITIVE_SHA);names=['c25519','ed25519-donna','tinyssh','tweetnacl']
 if len(deps)!=4 or [x.get('name') for x in deps]!=names:raise AdmissionError('dependencies')
 for row in deps:
  if row!={'name':row['name'],'path':'tests/externals/'+row['name'],'runtime_or_link_dependency':False,'scope':'bundled-test-only'}:raise AdmissionError('dependency boundary')
 patches=_jsonl(BUNDLE/'patches.jsonl',PATCH_SHA)
 if patches!=[{'artifact_id':'candidate-patch-set','patch_count':0,'post_patch_tree_sha256':TREE_SHA}]:raise AdmissionError('patches')
 sbom=_json(BUNDLE/'sbom.spdx.json',SBOM_SHA);_keys(sbom,{'SPDXID','comment','creationInfo','dataLicense','documentDescribes','documentNamespace','files','hasExtractedLicensingInfos','name','packages','relationships','spdxVersion'},'sbom')
 if sbom['spdxVersion']!='SPDX-2.3' or len(sbom['packages'])!=5 or len(sbom['files'])!=161 or len(sbom['relationships'])!=166:raise AdmissionError('sbom counts')
 if [x.get('SPDXID') for x in sbom['packages']]!=['SPDXRef-Package-Monocypher','SPDXRef-Package-c25519','SPDXRef-Package-ed25519-donna','SPDXRef-Package-tinyssh','SPDXRef-Package-TweetNaCl'] or sbom['packages'][0].get('licenseConcluded')!='BSD-2-Clause' or sbom['packages'][2].get('licenseConcluded')!='NOASSERTION':raise AdmissionError('sbom packages')
 seen=set()
 for row in sbom['files']:
  rel=row.get('fileName','')[2:];source=files.get(rel);checks={x.get('algorithm'):x.get('checksumValue') for x in row.get('checksums',[])}
  if source is None or rel in seen or set(checks)!={'SHA1','SHA256'} or checks['SHA256']!=source['sha256'] or checks['SHA1']!=hashlib.sha1((SOURCE/rel).read_bytes()).hexdigest() or row.get('licenseConcluded')!='NOASSERTION':raise AdmissionError('sbom file')
  seen.add(rel)
 if seen!=set(files):raise AdmissionError('sbom coverage')
 return receipt,licenses,deps,patches,sbom
def _validate_lock():
 lock=_json(BUNDLE/'project-lock.json',LOCK_SHA);_keys(lock,{'boundaries','candidate_id','evidence','lock_id','lock_kind','locked_date','manifest_policy','project_license_choice','schema','source','upstream_license_expression','version'},'lock')
 if type(lock['version']) is not int or (lock['schema'],lock['version'],lock['candidate_id'],lock['lock_kind'],lock['project_license_choice'],lock['upstream_license_expression'])!=('OTMSL0',0,'monocypher','vendored_source_tree_lock','BSD-2-Clause','CC0-1.0 OR BSD-2-Clause'):raise AdmissionError('lock identity')
 _keys(lock['source'],{'commit','origin','parent_commit','retained_tree_path','tag','tag_kind','tree','version'},'lock source')
 if lock['source']!={'commit':COMMIT,'origin':ORIGIN,'parent_commit':PARENT,'retained_tree_path':'tests/benchmarks/crypto/monocypher/4.0.3/source','tag':'4.0.3','tag_kind':'lightweight','tree':GIT_TREE,'version':'4.0.3'}:raise AdmissionError('lock source')
 expected_policy={'allowed_entry_kinds':['directory','regular_file'],'case_policy':'unicode-15.1-nfc-casefold-unique','forbidden_entry_kinds':['absolute_path','backslash_path','dot_segment','drive_path','fifo','reparse_point','socket','symlink'],'path_encoding':'utf8_relative_posix','path_order':'ordinal_bytewise_ascending'}
 _safe_path(lock['source']['retained_tree_path'])
 if lock['manifest_policy']!=expected_policy:raise AdmissionError('path policy')
 _keys(lock['boundaries'],{'benchmark_executed','build_executed','candidate_selected','crypto_executed','device_accessed','firmware_changed','flashed','keys_or_entropy_used','physical_evidence_added','radio_used','score_credit_added','source_linked_or_imported_by_firmware'},'lock boundaries');_false(lock['boundaries'],'lock boundaries')
 _keys(lock['evidence'],{'acquisition_receipt','full_tree','license','patches','sbom','transitive'},'lock evidence')
 facts={'acquisition_receipt':('sha256-canonical-json-acquisition-receipt-v1',RECEIPT_SHA),'full_tree':('sha256-utf8-jsonl-posix-tree-v1',TREE_MANIFEST_SHA),'license':('sha256-utf8-jsonl-license-inventory-v1',LICENSE_SHA),'sbom':('sha256-canonical-spdx-json-v1',SBOM_SHA),'transitive':('sha256-utf8-jsonl-transitive-dependencies-v1',TRANSITIVE_SHA),'patches':('sha256-utf8-jsonl-ordered-patches-v1',PATCH_SHA)}
 for name,(kind,digest) in facts.items():
  value=lock['evidence'][name]
  expected={'kind','path','sha256'}
  if name=='full_tree':expected|={'directory_count','entry_count','file_count','total_bytes','tree_sha256'}
  elif name=='license':expected|={'file_count','inventory_complete'}
  elif name=='sbom':expected|={'component_count','file_count'}
  elif name=='transitive':expected|={'dependency_count'}
  elif name=='patches':expected|={'patch_count'}
  _keys(value,expected,'lock '+name)
  _safe_path(value.get('path'))
  if value.get('kind')!=kind or value.get('sha256')!=digest:raise AdmissionError('lock layer')
 if lock['evidence']['full_tree'].get('tree_sha256')!=TREE_SHA or lock['evidence']['full_tree'].get('entry_count')!=175 or lock['evidence']['full_tree'].get('total_bytes')!=1413389:raise AdmissionError('lock tree facts')
 return lock
def _validated_otcsle(contract_path=OT097,evidence_path=EVIDENCE):
 contract_raw=_raw(contract_path,OT097_RAW);evidence=_json(evidence_path,EVIDENCE_SHA);sl=_module('ot102_otcsl',ROOT/'tools/crypto_candidate_source_lock.py')
 try:contract=json.loads(contract_raw.decode('utf-8'),object_pairs_hook=_pairs);base=sl.validate_contract(contract)
 except Exception as e:raise AdmissionError('policy validation') from e
 if base['admission_policy_sha256']!=OT097_POLICY or sl.canonical_sha256(evidence)!=EVIDENCE_SHA:raise AdmissionError('policy/evidence digest')
 future=copy.deepcopy(contract);future['accepted_source_evidence_sha256']['monocypher']=[EVIDENCE_SHA];old_digest=sl.EXPECTED_V1_CONTRACT_SHA256;old_anchor=sl.ACCEPTED_SOURCE_EVIDENCE_SHA256['monocypher']
 try:
  sl.EXPECTED_V1_CONTRACT_SHA256=sl.canonical_sha256(future);sl.ACCEPTED_SOURCE_EVIDENCE_SHA256['monocypher']=frozenset({EVIDENCE_SHA});facts=sl.validate_source_evidence(evidence,future)
 except Exception as e:raise AdmissionError('OTCSLE0/v1 validation') from e
 finally:sl.EXPECTED_V1_CONTRACT_SHA256=old_digest;sl.ACCEPTED_SOURCE_EVIDENCE_SHA256['monocypher']=old_anchor
 return evidence,facts
def validate(admission_path,ot097_path=OT097,ot098_path=OT098,ot099_path=OT099,ot100_path=OT100,evidence_path=EVIDENCE,enforce_digest=True):
 admission=_json(admission_path,ADMISSION_SHA) if enforce_digest else json.loads(Path(admission_path).read_text(encoding='utf-8'),object_pairs_hook=_pairs)
 _raw(ot097_path,OT097_RAW);_raw(ot098_path,OT098_RAW);_raw(ot099_path,OT099_RAW);_raw(ot100_path,OT100_RAW)
 ot100m=_module('ot102_ot100',ROOT/'tools/crypto_libsodium_source_lock_admission.py');ot098m=_module('ot102_ot098',ROOT/'tools/crypto_candidate_acquisition_inspection.py')
 try:prior=ot100m.validate(Path(ot100_path),Path(ot097_path),Path(ot099_path));inspection=ot098m.validate(Path(ot098_path))
 except Exception as e:raise AdmissionError('parent validation') from e
 candidate=[x for x in inspection['candidates'] if x['name']=='Monocypher']
 if len(candidate)!=1 or candidate[0]['commit']!=COMMIT or candidate[0]['tree']!=GIT_TREE:raise AdmissionError('inspection binding')
 files=_validate_tree();_validate_layers(files);lock=_validate_lock();evidence,facts=_validated_otcsle(Path(ot097_path),Path(evidence_path))
 _keys(admission,{'acceptance_counts','accepted_api_config_evidence_sha256','accepted_candidate','accepted_candidate_import_evidence_sha256','accepted_date','accepted_source_evidence_sha256','admission_id','artifact_kind','authority','claims','current_four_blockers','historical_six_blockers','license_claims','owner_authorization','parents','prior_current_five_blockers','public_result','schema','source_evidence','status','version'},'admission')
 if type(admission['version']) is not int or (admission['schema'],admission['version'],admission['artifact_kind'],admission['admission_id'],admission['accepted_date'],admission['status'],admission['public_result'])!=('OTMSLA0',0,'append_only_monocypher_source_lock_acceptance_delta','OT-102-OT005-MONOCYPHER-SOURCE-LOCK-ADMISSION-DELTA-V0','2026-08-20','monocypher_source_lock_admitted_host_only_readiness_blocked',RESULT):raise AdmissionError('admission identity')
 parents={'otcai0_v0_raw_sha256':OT098_RAW,'otcsl0_v1_policy_sha256':OT097_POLICY,'otcsl0_v1_raw_sha256':OT097_RAW,'otcsla0_v0_raw_sha256':OT100_RAW,'otlmi0_v0_raw_sha256':OT099_RAW}
 if admission['parents']!=parents or admission['owner_authorization']!={'project_license_choice':'BSD-2-Clause','project_license_choice_authorized':True,'source_reacquisition_authorized':True} or admission['source_evidence']!={'path':'tests/benchmarks/crypto/OT-102-OT005-MONOCYPHER-SOURCE-EVIDENCE-V0.json','sha256':EVIDENCE_SHA}:raise AdmissionError('admission binding')
 expected_candidate={'candidate_id':'monocypher','license_inventory_sha256':LICENSE_SHA,'lock_kind':'vendored_source_tree_lock','project_dependency_lock_sha256':LOCK_SHA,'project_license_choice':'BSD-2-Clause','source_commit':COMMIT,'source_evidence_sha256':EVIDENCE_SHA,'tree_sha256':TREE_SHA,'upstream_license_expression':'CC0-1.0 OR BSD-2-Clause','version':'4.0.3'}
 _keys(admission['accepted_candidate'],set(expected_candidate),'accepted candidate')
 if admission['accepted_candidate']!=expected_candidate or facts['source_commit']!=COMMIT or facts['project_dependency_lock_sha256']!=LOCK_SHA or facts['source_lock_accepted'] is not True:raise AdmissionError('accepted candidate')
 empty={'esp_idf_mbedtls_psa':[],'espressif_libsodium':[],'monocypher':[]};sources={'esp_idf_mbedtls_psa':[],'espressif_libsodium':[OT099_RAW],'monocypher':[EVIDENCE_SHA]}
 _keys(admission['acceptance_counts'],{'api_config','candidate_import','source'},'counts')
 if any(type(v) is not int for v in admission['acceptance_counts'].values()) or admission['accepted_source_evidence_sha256']!=sources or admission['accepted_api_config_evidence_sha256']!=empty or admission['accepted_candidate_import_evidence_sha256']!=empty or admission['acceptance_counts']!={'api_config':0,'candidate_import':0,'source':2}:raise AdmissionError('anchors/counts')
 if admission['historical_six_blockers']!=HISTORICAL_SIX or admission['prior_current_five_blockers']!=prior['current_five_blockers'] or admission['prior_current_five_blockers']!=PRIOR_FIVE or admission['current_four_blockers']!=CURRENT_FOUR:raise AdmissionError('blockers')
 true={'libsodium_source_lock_remains_accepted','monocypher_source_lock_accepted'}
 _keys(admission['claims'],ADMISSION_CLAIMS,'claims')
 if any(type(v) is not bool or v is not (k in true) for k,v in admission['claims'].items()):raise AdmissionError('claims')
 _keys(admission['authority'],ADMISSION_AUTHORITY,'authority');_false(admission['authority'],'authority')
 _keys(admission['license_claims'],{'legal_clearance_claimed','license_compatibility_determined'},'license claims')
 if admission['license_claims']!={'legal_clearance_claimed':False,'license_compatibility_determined':False}:raise AdmissionError('license claims')
 return admission
def main(argv=None):
 p=argparse.ArgumentParser();p.add_argument('admission',type=Path);p.add_argument('--ot097',type=Path,default=OT097);p.add_argument('--ot098',type=Path,default=OT098);p.add_argument('--ot099',type=Path,default=OT099);p.add_argument('--ot100',type=Path,default=OT100);p.add_argument('--evidence',type=Path,default=EVIDENCE);a=p.parse_args(argv)
 try:d=validate(a.admission,a.ot097,a.ot098,a.ot099,a.ot100,a.evidence)
 except (OSError,AdmissionError,KeyError,TypeError,UnicodeError,RecursionError,json.JSONDecodeError):print('OTMSLA0 validation failed',file=sys.stderr);return 2
 print(d['public_result']);return 0
if __name__=='__main__':raise SystemExit(main())
