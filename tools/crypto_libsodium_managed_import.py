#!/usr/bin/env python3
"""Strict, read-only validator for the immutable OT-099 evidence bundle."""
import argparse, hashlib, json, re, sys, unicodedata
from pathlib import Path
EXPECTED_SHA256="8285fa7308bfc83a5d55503a7a3e1fa4c21895a42b095197b3ec75f634411ec9"; MAX_BYTES=131072
PRIVATE=(re.compile(r"[A-Za-z]:\\"),re.compile(r"/(?:home|users)/",re.I),re.compile(r"\b(?:password|private[_ -]?key|secret|latitude|longitude)\s*[:=]",re.I))
class ContractError(ValueError): pass
def _pairs(pairs):
 out={}
 for k,v in pairs:
  if k in out: raise ContractError("duplicate key")
  out[k]=v
 return out
def _walk(v,d=0,n=None):
 n=[0] if n is None else n;n[0]+=1
 if n[0]>4096 or d>16:raise ContractError("bounds")
 if isinstance(v,dict):
  for k,x in v.items():
   if unicodedata.normalize("NFC",k)!=k:raise ContractError("key")
   _walk(x,d+1,n)
 elif isinstance(v,list):
  for x in v:_walk(x,d+1,n)
 elif isinstance(v,str):
  if len(v)>2048 or unicodedata.normalize("NFC",v)!=v or any(p.search(v) for p in PRIVATE):raise ContractError("text")
 elif v is not None and not isinstance(v,(bool,int)):raise ContractError("scalar")
def validate(path):
 with Path(path).open("rb") as f:raw=f.read(MAX_BYTES+1)
 if not raw or len(raw)>MAX_BYTES or hashlib.sha256(raw).hexdigest()!=EXPECTED_SHA256:raise ContractError("contract digest mismatch")
 try:d=json.loads(raw.decode("utf-8"),object_pairs_hook=_pairs)
 except Exception as e:raise ContractError(str(e))
 _walk(d)
 if d["schema"]!="OTLMI0" or d["version"]!=0:raise ContractError("schema")
 if d["source_lock"]["accepted"] or not d["source_lock"]["evidence_complete"] or d["source_lock"]["patch_count"]!=0:raise ContractError("lock")
 if d["operations"]["present_count"]!=7 or d["operations"]["noise_xk_handshake"]:raise ContractError("operations")
 if d["build"]["result"]!="PASS" or not all((d["build"]["probe_compiled"],d["build"]["candidate_archive_built"],d["build"]["candidate_archive_in_link_graph"],d["build"]["application_elf_linked"])) or d["build"]["probe_symbols_retained_in_final_map"]:raise ContractError("build")
 if any(d["boundaries"].values()) or len(d["remaining_blockers"])!=6:raise ContractError("boundary")
 return d
def main(argv=None):
 p=argparse.ArgumentParser();p.add_argument("artifact",type=Path);a=p.parse_args(argv)
 try:d=validate(a.artifact)
 except (OSError,ContractError,KeyError,TypeError,UnicodeError,RecursionError):print("OTLMI0 validation failed",file=sys.stderr);return 2
 print(d["public_result"]);return 0
if __name__=="__main__":raise SystemExit(main())
