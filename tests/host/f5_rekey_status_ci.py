"""Offline real-R2 -> fresh F5 rekey -> Packet-v1 status host composition."""
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
    objects=[];template=None
    for raw in recipe['commands']:
        if '-c' not in raw:continue
        destination=raw[raw.index('-o')+1]
        if destination.endswith('common-07.o'):continue
        if destination.endswith('fresh-00.o'):
            template=list(raw);continue
        command=list(map(expand,raw));command[0]=str(compiler_dir/(command[0]+'.exe'))
        target=Path(command[command.index('-o')+1]);objects.append(target)
        run(command)
    for index,path in enumerate(('tests/host/f5_rekey_status_tests.cpp','firmware/components/protocol/src/quick_status_codec.cpp')):
        command=list(template)
        command[command.index('-c')+1]=str(ROOT/path)
        command[command.index('-o')+1]=str(out/f'f5-{index}.o')
        command[command.index('-MF')+1]=str(out/f'f5-{index}.d')
        command=list(map(expand,command));command[0]=str(compiler_dir/'g++.exe')
        run(command);objects.append(out/f'f5-{index}.o')
    exe=out/'f5-rekey-status.exe'
    run([compiler_dir/'g++.exe','-Wl,--gc-sections',*objects,'-ladvapi32','-o',exe])
    stdout=run([exe])
    if not stdout.startswith('PASS ') or ' real retained F5 rekey/status groups' not in stdout:
        raise RuntimeError('Missing expected F5 result')
    record={'result':'PASS','physical':False,'network':False,'stdout':stdout,'dependency_files':733,
        'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'recipe_sha256':hashlib.sha256(recipe_raw).hexdigest()}
    (out/'RESULT.json').write_text(json.dumps(record,indent=2)+'\n')
    print(stdout,end='')

if __name__=='__main__':main()
