"""Compile the real board NVS adapter against deterministic host-only ESP seams."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[2]

def run() -> int:
    parser=argparse.ArgumentParser()
    parser.add_argument('--output-root',type=Path,required=True)
    args=parser.parse_args()
    output=args.output_root.resolve()
    output.mkdir(parents=True,exist_ok=False)
    compiler=shutil.which('g++')
    if compiler is None:
        raise RuntimeError('native g++ required')
    component=ROOT/'tests/benchmarks/crypto/esp_idf/espressif_libsodium_1_0_22/managed_components/espressif__libsodium'
    includes=[ROOT/'tests/host/f5_pairing_nvs_stubs',ROOT/'firmware/targets/heltec_v4_bench/main',
        ROOT/'firmware/components/security_evaluation/include',ROOT/'firmware/components/security/include',
        ROOT/'firmware/components/persistence/include',ROOT/'firmware/components/identity/include',
        component/'libsodium/src/libsodium/include',component/'port_include']
    command=[compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-fno-rtti','-fno-exceptions','-DSODIUM_STATIC']
    for include in includes:
        command.extend(['-I',str(include)])
    exe=output/'f5-pairing-nvs.exe'
    command.extend([str(ROOT/'tests/host/f5_pairing_nvs_tests.cpp'),
        str(ROOT/'firmware/targets/heltec_v4_bench/main/pairing_nvs_storage.cpp'),'-o',str(exe)])
    commands=[]
    for index,cmd in enumerate([command,[str(exe)],[str(exe),'--reset-before-register']]):
        result=subprocess.run(cmd,text=True,capture_output=True,timeout=120)
        (output/f'{index}.stdout.txt').write_text(result.stdout,encoding='utf-8')
        (output/f'{index}.stderr.txt').write_text(result.stderr,encoding='utf-8')
        commands.append({'command':cmd,'returncode':result.returncode,'stdout':result.stdout,'stderr':result.stderr})
        (output/'commands.json').write_text(json.dumps(commands,indent=2)+'\n',encoding='utf-8')
        if result.stdout:
            print(result.stdout,end='')
        if result.stderr:
            print(result.stderr,end='')
        if result.returncode:
            return result.returncode
    print('binary SHA-256 '+hashlib.sha256(exe.read_bytes()).hexdigest())
    return 0

if __name__=='__main__':
    raise SystemExit(run())
