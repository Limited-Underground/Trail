"""Actual app_main/control/session lifecycle; external SDK and entropy are simulated."""
from pathlib import Path
import hashlib
import os
import shutil
import subprocess
import tempfile
import security_policy_crypto_tests as crypto

ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tests/host/security_policy_lifecycle'
TARGET=ROOT/'firmware/targets/heltec_v4_security_policy_eval/main'
SCENARIOS=('success','install_fail','begin_fail','input_fault','no_input','partial','invalid',
           'late_first','last_first','poll_cap','nvs_fail','start_fail','sodium_fail',
           'evaluate_fail','namespace_fail','stop_fail','entropy_fault','send_fail','delayed_stop')

def run():
    compiler=shutil.which('g++') or shutil.which('c++')
    ccompiler=shutil.which('gcc') or shutil.which('cc')
    if not compiler or not ccompiler:raise RuntimeError('native C/C++ compilers required')
    (ROOT/'build').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='policy-lifecycle-',dir=ROOT/'build') as d:
        work=Path(d).resolve();build=work/'crypto'
        crypto.m.SUCCESSOR=ROOT/'firmware/targets/heltec_v4_security_eval/main/noise_adapter'
        crypto.m.SUCCESSOR_SHA='b0be8109d017a851cea3952c4713157847c3bc64fe0eba1367c2b3c27cbcdc8b'
        crypto.m.run(build,True)
        flags=['-O2','-Wall','-Wextra','-DSODIUM_STATIC','-DCONFIGURED=1','-DNATIVE_LITTLE_ENDIAN=1',
               '-ffunction-sections','-fdata-sections']
        includes=[work,build/'include',crypto.m.SOURCE/'src/libsodium/include',crypto.m.SOURCE/'src/libsodium/include/sodium',
                  crypto.m.SUCCESSOR,TARGET,ROOT/'firmware/components/security_evaluation/include',
                  ROOT/'firmware/components/security/include',ROOT/'firmware/components/persistence/include']
        for p in includes:flags+=['-I',str(p)]
        objects=[str(build/f'source-{i}.o') for i in range(1,17)]
        for i,path in enumerate(crypto.EXTRA):
            obj=work/f'extra-{i}.o'
            subprocess.run([ccompiler,'-std=c11',*flags,'-c',str(crypto.m.SOURCE/'src/libsodium'/path),'-o',str(obj)],check=True,timeout=120)
            objects.append(str(obj))
        stubs={'entropy_runtime.hpp':(FIXTURE/'entropy_runtime.hpp').read_bytes(),
               'nvs.h':(ROOT/'tests/host/fixtures/security_eval_nvs/nvs.h').read_bytes(),
               'nvs_flash.h':b'#pragma once\n#include "nvs.h"\nint nvs_flash_init();\n',
               'esp_timer.h':b'#pragma once\n#include <cstdint>\nstd::int64_t esp_timer_get_time();\n',
               'freertos/FreeRTOS.h':b'#pragma once\n','freertos/task.h':b'#pragma once\nvoid vTaskDelay(unsigned);\n'}
        for name,raw in stubs.items():
            p=work/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(raw)
        source=work/'actual_app_main.cpp';source.write_bytes((TARGET/'app_main.cpp').read_bytes())
        if source.read_bytes()!=(TARGET/'app_main.cpp').read_bytes():raise RuntimeError('source_copy_changed')
        common=[ROOT/'firmware/components/persistence/src/persistent_storage_kv.cpp',
                ROOT/'firmware/components/persistence/src/outbound_counter_lease_store.cpp',
                ROOT/'firmware/components/security/src/aead_nonce.cpp']
        exe=work/'lifecycle.exe'
        command=[compiler,'-std=c++17','-Werror',*flags,'-Wl,--gc-sections','-Wl,--wrap=sodium_init',
                 str(source),str(FIXTURE/'harness.cpp'),*map(str,common),*objects,'-o',str(exe)]
        subprocess.run(command,check=True,timeout=120)
        import sys
        sys.path.insert(0,str(ROOT/'tools'))
        from security_policy_capture import capture, CaptureError
        def crosscheck(wire, *, chunks=None, late=False, reject=False):
            pending=list(chunks) if chunks is not None else [wire]
            clock=[0.]
            def read(size,remaining):
                clock[0]+=.2 if late else .01
                return pending.pop(0) if pending else b''
            try:
                result=capture(read,lambda:clock[0],'0123456789abcdef0123456789abcdef',.1)
            except CaptureError:
                if reject:return
                raise
            if reject or not result.startswith(b'SEC_EVAL1 '):raise RuntimeError('capture_crosscheck_failed')
        for scenario in SCENARIOS:
            result=subprocess.run([str(exe),scenario],check=True,capture_output=True,timeout=20)
            first,_,wire=result.stdout.partition(b'\n')
            if first.rstrip(b'\r')!=('PASS '+scenario).encode():raise RuntimeError('unexpected lifecycle result')
            if wire:
                # The fixture asserts a >=31s stop return; this receipt cannot
                # satisfy the unchanged 30s host horizon. No SDK timing claim.
                crosscheck(wire,late=scenario=='delayed_stop',reject=scenario=='delayed_stop')
        import security_policy_console_lifecycle_tests as console
        sdk=work/'console_sdk';sdk.mkdir()
        for name,raw in console.console_stubs().items():
            path=sdk/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(raw.encode())
        translation=work/'actual_console.cpp';translation.write_text(console.console_translation_unit(),encoding='utf-8')
        composed=work/'composed.exe'
        command=[compiler,'-std=c++17','-Werror',*flags,'-DACTUAL_CONSOLE','-I',str(sdk),
                 '-I',str(ROOT/'firmware/components/diagnostics/include'),'-Wl,--gc-sections','-Wl,--wrap=sodium_init',
                 str(source),str(FIXTURE/'harness.cpp'),str(translation),*map(str,common),*objects,'-o',str(composed)]
        subprocess.run(command,check=True,timeout=120)
        for scenario in ('actual_success','actual_fault','actual_input_fault'):
            result=subprocess.run([str(composed),scenario],check=True,capture_output=True,timeout=20)
            if not result.stdout.startswith(('PASS '+scenario+'\n').encode()) and not result.stdout.startswith(('PASS '+scenario+'\r\n').encode()):
                raise RuntimeError('actual console composition failed')
            wire=result.stdout.partition(b'\n')[2]
            if scenario=='actual_success':
                crosscheck(wire)
                crosscheck(wire,chunks=[wire,wire],reject=True)
                crosscheck(wire,chunks=[wire,b'extra'],reject=True)
                crosscheck(wire.replace(b'0123456789abcdef0123456789abcdef',b'0'*32),reject=True)
                crosscheck(wire,late=True,reject=True)
        print(f'PASS {len(SCENARIOS)+3} actual app lifecycle groups and 4 strict receipt rejection crosschecks')

if __name__=='__main__':run()
