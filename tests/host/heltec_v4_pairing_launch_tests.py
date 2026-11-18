#!/usr/bin/env python3
"""I-073: compile the actual app grant callback with real driver/coordinator."""
from __future__ import annotations
import argparse,hashlib,json,os,re,shutil,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
TARGET=ROOT/'firmware/targets/heltec_v4_bench/main'
def run(output_root: Path) -> dict:
    output_root=Path(output_root);output_root.mkdir(parents=True,exist_ok=False)
    source=(TARGET/'app_main.cpp').read_text(encoding='utf-8')
    match=re.search(r'bool lora_transmit_permitted\(void\*\) \{.*?\n\}',source,re.S)
    if match is None:raise RuntimeError('Exact production launch callback not found')
    (output_root/'app_pairing_permission.inc').write_text(match.group(0)+'\n',encoding='utf-8')
    candidate = shutil.which(os.environ.get("CXX", "g++"))
    if candidate is None and os.name == "nt":
        candidate = r"C:\msys64\ucrt64\bin\g++.exe"
    if candidate is None or not Path(candidate).is_file():
        raise RuntimeError("Existing host C++ compiler unavailable")
    compiler = Path(candidate).resolve()
    exe=output_root/'pairing-launch.exe'
    includes=[ROOT/'tests/host/heltec_v4_lora_stubs',TARGET,ROOT/'firmware/components/radio/include',ROOT/'firmware/components/security/include',ROOT/'firmware/components/protocol/include',output_root]
    argv=[str(compiler),'-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Werror',*[f'-I{x}' for x in includes],str(ROOT/'tests/host/heltec_v4_pairing_launch_tests.cpp'),str(TARGET/'heltec_v4_lora.cpp'),'-o',str(exe)]
    env=os.environ.copy();env['PATH']=str(compiler.parent)+os.pathsep+env.get('PATH','');env['TEMP']=str(output_root);env['TMP']=str(output_root)
    records=[]
    def command(args,log):
        done=subprocess.run(args,cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=90)
        (output_root/log).write_text(done.stdout+done.stderr,encoding='utf-8')
        records.append({'argv':args,'cwd':str(ROOT),'exit':done.returncode,'stdout':done.stdout,'stderr':done.stderr})
        (output_root/'commands.json').write_text(json.dumps(records,indent=2)+'\n',encoding='utf-8')
        return done
    compiled=command(argv,'compile.log')
    tested=command([str(exe)],'run.log') if compiled.returncode==0 else None
    result={'result':'PASS' if tested is not None and tested.returncode==0 else 'FAIL','compile_exit':compiled.returncode,'test_exit':None if tested is None else tested.returncode,'case_lines':0 if tested is None else tested.stdout.count('CASE '),'output_root':str(output_root),'actual_callback':match.group(0),'source_sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [TARGET/'app_main.cpp',TARGET/'heltec_v4_lora.cpp',ROOT/'firmware/components/protocol/include/opentrail/pairing_coordinator.hpp']}}
    (output_root/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    return result
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--output-root',type=Path,default=ROOT/'build'/f'i073-bench-launch-{time.time_ns()}');args=parser.parse_args()
    result=run(args.output_root);print(json.dumps(result,indent=2));raise SystemExit(0 if result['result']=='PASS' else 1)
