"""Actual additive stage target lifecycle, real policy crypto; external SDK simulated."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import security_policy_crypto_tests as crypto
from security_policy_lifecycle import dependencies

ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tests/host/security_policy_stage'
PREVIOUS=ROOT/'tests/host/security_policy_lifecycle'
TARGET=ROOT/'firmware/targets/heltec_v4_security_input_diag/main'
POLICY=ROOT/'firmware/targets/heltec_v4_security_policy_eval/main'
SCENARIOS=('success','preexisting','nvs_fail','create_fail','set_fail','readback_fail','readback_corrupt','commit_applied_fail','install_fail','begin_fail','no_input','partial','invalid','input_fault','start_fail','sodium_fail','evaluate_fail','namespace_fail','stop_fail','entropy_fault','send_fail','poll_cap','late_first','last_first','invalid_prefix','invalid_hex','buffer_limit')

def run():
    compiler=shutil.which('g++') or shutil.which('c++')
    ccompiler=shutil.which('gcc') or shutil.which('cc')
    if not compiler or not ccompiler:raise RuntimeError('native C/C++ compilers required')
    (ROOT/'build').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='policy-lifecycle-',dir=ROOT/'build') as d:
        work=Path(d).resolve();build=work/'crypto'
        crypto.m.SUCCESSOR=ROOT/'firmware/targets/heltec_v4_security_eval/main/noise_adapter'
        crypto.m.SUCCESSOR_SHA='b0be8109d017a851cea3952c4713157847c3bc64fe0eba1367c2b3c27cbcdc8b'
        component=dependencies.acquire(work/'managed-component')
        crypto.m.COMPONENT=component
        crypto.m.SOURCE=component/'libsodium'
        dependencies.native_probe(crypto.m,build,ccompiler)
        flags=['-O2','-Wall','-Wextra','-DSODIUM_STATIC','-DCONFIGURED=1','-DNATIVE_LITTLE_ENDIAN=1',
               '-ffunction-sections','-fdata-sections']
        includes=[work,build/'include',crypto.m.SOURCE/'src/libsodium/include',crypto.m.SOURCE/'src/libsodium/include/sodium',
                  crypto.m.SUCCESSOR,TARGET,POLICY,ROOT/'firmware/components/security_diagnostics/include',ROOT/'firmware/components/security_evaluation/include',
                  ROOT/'firmware/components/security/include',ROOT/'firmware/components/persistence/include']
        for p in includes:flags+=['-I',str(p)]
        objects=[str(build/f'source-{i}.o') for i in range(1,17)]
        for i,path in enumerate(crypto.EXTRA):
            obj=work/f'extra-{i}.o'
            subprocess.run([ccompiler,'-std=c11',*flags,'-c',str(crypto.m.SOURCE/'src/libsodium'/path),'-o',str(obj)],check=True,timeout=120)
            objects.append(str(obj))
        stubs={'entropy_runtime.hpp':(PREVIOUS/'entropy_runtime.hpp').read_bytes(),
               'nvs.h':(ROOT/'tests/host/fixtures/security_eval_nvs/nvs.h').read_bytes(),
               'nvs_flash.h':b'#pragma once\n#include "nvs.h"\nint nvs_flash_init();\n',
               'esp_timer.h':b'#pragma once\n#include <cstdint>\nstd::int64_t esp_timer_get_time();\n',
               'freertos/FreeRTOS.h':b'#pragma once\n','freertos/task.h':b'#pragma once\nvoid vTaskDelay(unsigned);\n'}
        for name,raw in stubs.items():
            p=work/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(raw)
        historical=(PREVIOUS/'harness.cpp').read_bytes()
        marker=b'int main(int argc,char** argv)'
        if historical.count(marker)!=1:raise RuntimeError('fixture_boundary_changed')
        (work/'ot194_external_fixture.hpp').write_bytes(historical.split(marker)[0])
        # Compile target path directly: its relative include reuses exact OT-187 evaluation.
        source=TARGET/'app_main.cpp'
        with (work/'nvs.h').open('ab') as out:
            out.write(b'\nconstexpr int NVS_READONLY=0;\nesp_err_t nvs_set_u64(nvs_handle_t,const char*,std::uint64_t);\nesp_err_t nvs_get_u64(nvs_handle_t,const char*,std::uint64_t*);\n')
        common=[ROOT/'firmware/components/persistence/src/persistent_storage_kv.cpp',
                ROOT/'firmware/components/persistence/src/outbound_counter_lease_store.cpp',
                ROOT/'firmware/components/security/src/aead_nonce.cpp']
        exe=work/'lifecycle.exe'
        command=[compiler,'-std=c++17','-Werror',*flags,'-Wl,--gc-sections','-Wl,--wrap=sodium_init',
                 str(source),str(ROOT/'tests/host/security_policy_input_lifecycle_tests.cpp'),*map(str,common),*objects,'-o',str(exe)]
        subprocess.run(command,check=True,timeout=120)
        contract=json.loads((ROOT/'firmware/components/security_diagnostics/input_record_v1.json').read_bytes())
        def check_result(result,scenario):
            lines=result.stdout.decode('ascii').splitlines()
            if lines[0]!='PASS '+scenario:raise RuntimeError('stage_lifecycle_failed')
            for text in lines[1:]:
                value=int(text);raw=value.to_bytes(8,'little')
                if list(raw[:4])!=contract['prefix'] or raw[5] not in contract['allowed_errors'][str(raw[4])]:raise RuntimeError('stage_contract_mismatch')
                crc=contract['crc']['initial']
                for byte in raw[:6]:
                    crc^=byte<<8
                    for _ in range(8):crc=((crc<<1)^contract['crc']['polynomial'] if crc&0x8000 else crc<<1)&65535
                if int.from_bytes(raw[6:],'little')!=crc:raise RuntimeError('stage_crc_mismatch')
        for scenario in SCENARIOS:
            check_result(subprocess.run([str(exe),scenario],check=True,capture_output=True,timeout=20),scenario)
        import security_policy_console_lifecycle_tests as console
        sdk=work/'console_sdk';sdk.mkdir()
        for name,raw in console.console_stubs().items():
            path=sdk/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(raw.encode())
        translation=work/'actual_console.cpp';translation.write_text(console.console_translation_unit()+'\nextern \"C\" void lifecycle_console_send_fault(){fault_write=true;}\n',encoding='utf-8')
        composed=work/'composed.exe'
        command=[compiler,'-std=c++17','-Werror',*flags,'-DACTUAL_CONSOLE','-I',str(sdk),
                 '-I',str(ROOT/'firmware/components/diagnostics/include'),'-Wl,--gc-sections','-Wl,--wrap=sodium_init',
                 str(source),str(ROOT/'tests/host/security_policy_input_lifecycle_tests.cpp'),str(translation),*map(str,common),*objects,'-o',str(composed)]
        subprocess.run(command,check=True,timeout=120)
        for scenario in ('actual_success','actual_fault','actual_input_fault','actual_send_fault'):
            check_result(subprocess.run([str(composed),scenario],check=True,capture_output=True,timeout=20),scenario)
        print(f'PASS {len(SCENARIOS)+4} actual input diagnostic lifecycle groups')

if __name__=='__main__':run()
