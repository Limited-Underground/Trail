#!/usr/bin/env python3
"""Validate immutable host-only OT-098 candidate inspection evidence."""
import argparse, hashlib, json, re, sys, unicodedata
from pathlib import Path
EXPECTED_SHA256="b7be03e305c6253e10f69f624132a736cce5aea3f559760cde4f948ae79abad6"; MAX_BYTES=131072
PRIVATE=(re.compile(r"[A-Za-z]:\\"),re.compile(r"/(?:home|users)/",re.I),re.compile(r"\b(?:password|private[_ -]?key|secret|latitude|longitude)\s*[:=]",re.I))
class ContractError(ValueError): pass
def _pairs(pairs):
 out={}
 for k,v in pairs:
  if k in out: raise ContractError("duplicate key")
  out[k]=v
 return out
def _walk(v,d=0,n=None):
 n=[0] if n is None else n; n[0]+=1
 if n[0]>4096 or d>16: raise ContractError("bounds")
 if isinstance(v,dict):
  for k,x in v.items():
   if unicodedata.normalize("NFC",k)!=k: raise ContractError("key")
   _walk(x,d+1,n)
 elif isinstance(v,list):
  for x in v:_walk(x,d+1,n)
 elif isinstance(v,str):
  if len(v)>2048 or unicodedata.normalize("NFC",v)!=v or any(p.search(v) for p in PRIVATE): raise ContractError("text")
 elif v is not None and not isinstance(v,(bool,int)): raise ContractError("scalar")
def validate(path):
 with Path(path).open("rb") as stream: raw=stream.read(MAX_BYTES+1)
 if not raw or len(raw)>MAX_BYTES or hashlib.sha256(raw).hexdigest()!=EXPECTED_SHA256: raise ContractError("contract digest mismatch")
 try:data=json.loads(raw.decode("utf-8"),object_pairs_hook=_pairs)
 except Exception as e: raise ContractError(str(e))
 _walk(data)
 if data["schema"]!="OTCAI0" or data["version"]!=0: raise ContractError("schema")
 if [c["name"] for c in data["candidates"]]!=["libsodium","Monocypher"]: raise ContractError("order")
 if [sum(c["operations"].values()) for c in data["candidates"]]!=[7,5]: raise ContractError("operations")
 if len(data["unchanged_blockers"])!=6 or any(data["authority"].values()): raise ContractError("boundary")
 return data
def main(argv=None):
 p=argparse.ArgumentParser();p.add_argument("artifact",type=Path);a=p.parse_args(argv)
 try:d=validate(a.artifact)
 except (OSError,ContractError,KeyError,TypeError,UnicodeError,RecursionError): print("OTCAI0 validation failed",file=sys.stderr);return 2
 print(d["public_result"]);return 0
if __name__=="__main__":raise SystemExit(main())
