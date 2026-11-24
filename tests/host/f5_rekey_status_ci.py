"""Offline pinned-sodium host tests for the F5 path or QR invite contract."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[2]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--suite',choices=('f5-rekey','qr-invite','qr-noise','qr-join'),default='f5-rekey')
    parser.add_argument('--sodium-source',type=Path,required=True,help='Expanded exact espressif component (733 pinned files)')
    args=parser.parse_args()
    out=args.out.resolve()
    if not out.is_relative_to(ROOT/'build'):
        raise ValueError('Output must be within worktree build')
    out.mkdir(parents=True,exist_ok=False)
    source=args.sodium_source.resolve(strict=True)
    inventory=ROOT/'tests/benchmarks/crypto/esp_idf/espressif_libsodium_1_0_22/source-manifest.sha256'
    from security_policy_lifecycle import dependencies
    if hashlib.sha256(inventory.read_bytes()).hexdigest()!=dependencies.MANIFEST_SHA:
        raise ValueError('Admission inventory pin mismatch')
    expected={name:(int(size),sha) for name,size,sha in (line.split('\t') for line in inventory.read_text().splitlines())}
    observed={}
    for path in source.rglob('*'):
        if path.is_symlink() or getattr(path,'is_junction',lambda:False)():
            raise ValueError('Indirect dependency refused')
        if path.is_file():
            raw=path.read_bytes();observed[path.relative_to(source).as_posix()]=(len(raw),hashlib.sha256(raw).hexdigest())
    if len(expected)!=733 or expected!=observed:
        raise ValueError('Expanded dependency does not match complete admission inventory')
    recipe_path=ROOT/'tests/host/fixtures/ot0005i_host_build.json'
    recipe_raw=recipe_path.read_bytes();recipe=json.loads(recipe_raw)
    values={'${ROOT}':str(ROOT),'${SODIUM}':str(source/'libsodium'),'${OUT}':str(out)}
    def expand(value):
        for marker,replacement in values.items():value=value.replace(marker,replacement)
        return value
    for path,content in recipe['generated'].items():
        target=out/path;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(content.encode())
    env=dict(os.environ)
    compiler_dir=Path(env.get('OPENTRAIL_MSYS2_ROOT','C:/msys64'))/'ucrt64/bin'
    env['PATH']=str(compiler_dir)+os.pathsep+env.get('PATH','')
    commands=[]
    def run(command):
        command=list(map(str,command));record={'command':command};commands.append(record)
        result=subprocess.run(command,cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=180)
        record.update(exit=result.returncode,stdout=result.stdout,stderr=result.stderr)
        (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        return result.stdout
    objects=[];template=None;c_template=None
    for raw in recipe['commands']:
        if '-c' not in raw:continue
        if raw[0]=='gcc' and c_template is None:c_template=list(raw)
        destination=raw[raw.index('-o')+1]
        if destination.endswith('common-07.o'):continue
        if destination.endswith('fresh-00.o'):
            template=list(raw);continue
        command=list(map(expand,raw));command[0]=str(compiler_dir/(command[0]+'.exe'))
        target=Path(command[command.index('-o')+1]);objects.append(target)
        run(command)
    if args.suite == 'qr-noise':
        noise_dir = ROOT/'firmware/targets/heltec_v4_security_eval/main/noise_adapter'
        # The pinned recipe already compiles the Noise XK adapter with libsodium.
        command=list(template)
        command[command.index('-c')+1]=str(ROOT/'tests/host/group_qr_noise_binding_tests.cpp')
        command[command.index('-o')+1]=str(out/'qr-noise-tests.o')
        command[command.index('-MF')+1]=str(out/'qr-noise-tests.d')
        command=list(map(expand,command));command[0]=str(compiler_dir/'g++.exe')
        for include in (noise_dir, ROOT/'firmware/components/identity/include',
                        ROOT/'firmware/components/security_evaluation/include'):
            command += ['-I', str(include)]
        run(command);objects.append(out/'qr-noise-tests.o')
        exe=out/'qr-noise.exe'
        run([compiler_dir/'g++.exe','-Wl,--gc-sections',*objects,'-ladvapi32','-o',exe])
        stdout=run([exe])
        if not stdout.startswith('PASS ') or ' QR Noise XK binding groups (pinned libsodium)' not in stdout:
            raise RuntimeError('Missing expected host result')
        record={'result':'PASS','physical':False,'network':False,'stdout':stdout,'dependency_files':733,
            'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'recipe_sha256':hashlib.sha256(recipe_raw).hexdigest()}
        (out/'RESULT.json').write_text(json.dumps(record,indent=2)+'\n')
        print(stdout,end='')
        return
    qr = args.suite in ('qr-invite', 'qr-join')
    join = args.suite == 'qr-join'
    sources = (('firmware/components/identity/src/group_roster.cpp',
                'firmware/components/identity/src/group_qr_invite.cpp',
                'firmware/components/identity/src/group_pair.cpp',
                'firmware/components/identity/src/durable_group_roster.cpp',
                'firmware/components/persistence/src/group_pair_index.cpp',
                'firmware/components/persistence/src/configuration_store.cpp',
                'tests/host/group_qr_join_conversation_tests.cpp' if join else
                'tests/host/group_qr_invite_tests.cpp') if qr else
               ('tests/host/f5_rekey_status_tests.cpp','firmware/components/protocol/src/quick_status_codec.cpp'))
    if join:
        sources = sources + ('firmware/components/persistence/src/group_qr_receipts.cpp',)
    if qr:
        command = list(c_template)
        command[command.index('-c')+1] = str(source/'libsodium/src/libsodium/crypto_core/ed25519/core_ed25519.c')
        command[command.index('-o')+1] = str(out/'qr-ed25519-points.o')
        command[command.index('-MF')+1] = str(out/'qr-ed25519-points.d')
        command=list(map(expand,command));command[0]=str(compiler_dir/'gcc.exe')
        run(command);objects.append(out/'qr-ed25519-points.o')
    for index,path in enumerate(sources):
        command=list(template)
        command[command.index('-c')+1]=str(ROOT/path)
        command[command.index('-o')+1]=str(out/f'f5-{index}.o')
        command[command.index('-MF')+1]=str(out/f'f5-{index}.d')
        command=list(map(expand,command));command[0]=str(compiler_dir/'g++.exe')
        if qr:
            command += ['-DOPENTRAIL_QR_REAL_CRYPTO=1','-I',str(ROOT/'firmware/components/identity/include')]
        if join:
            for include in (ROOT/'firmware/targets/heltec_v4_security_eval/main/noise_adapter',
                            ROOT/'firmware/components/security_evaluation/include',
                            ROOT/'firmware/components/security/include'):
                command += ['-I', str(include)]
        run(command);objects.append(out/f'f5-{index}.o')
    exe=out/('qr-join.exe' if join else 'qr-invite.exe' if qr else 'f5-rekey-status.exe')
    run([compiler_dir/'g++.exe','-Wl,--gc-sections',*objects,'-ladvapi32','-o',exe])
    stdout=run([exe])
    marker = (' QR join conversation groups (pinned libsodium)' if join else
              ' QR invitation contract groups (pinned Ed25519/SHA-256)' if qr else
              ' real retained F5 rekey/status groups')
    if not stdout.startswith('PASS ') or marker not in stdout:
        raise RuntimeError('Missing expected host result')
    record={'result':'PASS','physical':False,'network':False,'stdout':stdout,'dependency_files':733,
        'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'recipe_sha256':hashlib.sha256(recipe_raw).hexdigest()}
    (out/'RESULT.json').write_text(json.dumps(record,indent=2)+'\n')
    print(stdout,end='')

if __name__=='__main__':main()
