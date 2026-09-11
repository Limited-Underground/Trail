"""Pinned temporary managed dependency and portable native probe build.

The official archive is admitted against its byte pin and the already accepted
731-file checksums plus the tracked 733-file managed-component manifest.
"""
from pathlib import Path,PurePosixPath
import hashlib
import io
import json
import os
import stat
import sys
import time
import subprocess
import urllib.request
import zipfile

ROOT=Path(__file__).resolve().parents[3]
BASE='https://components-file.espressif.com/components/espressif/libsodium/1.0.22/'
ARCHIVE_URL=BASE+'espressif__libsodium-v1.0.22.zip'
ARCHIVE_SHA='865ea3aba354b16be4c7051da48b63247a395f30d1e0537269b253918aa78c5f'
CHECKSUM_SHA='5e3983c5496a3cffba3d013c70991b18cda6c345655fff883fb0e14dfa09e582'
MANIFEST_SHA='988fbb2b95d1b4f3850d153495f8d73482be52cb80808fa48e24a8d1e9bf91ea'
COMPONENT_HASH='39c9dc77d81804d54a539c8f076faed165152be7720ddd0e721acb9daf4aa5af'

def need(ok):
    if not ok:raise RuntimeError('managed_dependency_refused')
def sha(raw):return hashlib.sha256(raw).hexdigest()
def download(url,limit):
    deadline=time.monotonic()+90
    chunks=[];size=0
    with urllib.request.urlopen(url,timeout=45) as response:
        need(response.geturl()==url)
        while True:
            need(time.monotonic()<deadline)
            chunk=response.read1(min(65536,limit+1-size))
            need(time.monotonic()<deadline)
            if not chunk:break
            size+=len(chunk);need(size<=limit);chunks.append(chunk)
    return b''.join(chunks)

def members(archive):
    rows=archive.infolist();need(len(rows)==731)
    seen=set();total=0
    for row in rows:
        name=row.filename;path=PurePosixPath(name)
        need(name and '\\' not in name and ':' not in name and not path.is_absolute()
             and all(x not in ('','..','.') for x in name.split('/')) and not row.is_dir()
             and not row.flag_bits&1 and stat.S_IFMT(row.external_attr>>16) in (0,stat.S_IFREG))
        need(name.casefold() not in seen and row.file_size<=4*1024*1024)
        seen.add(name.casefold());total+=row.file_size
        need(total<=32*1024*1024)
    return rows

def acquire(destination,fetch=download):
    destination=Path(destination).resolve();need(not destination.exists())
    manifest=(ROOT/'tests/benchmarks/crypto/esp_idf/espressif_libsodium_1_0_22/source-manifest.sha256').read_bytes()
    need(sha(manifest)==MANIFEST_SHA)
    expected={}
    for line in manifest.decode('utf-8').splitlines():
        name,size,digest=line.split('\t');need(name not in expected);expected[name]=(int(size),digest)
    need(len(expected)==733)
    archive_raw=fetch(ARCHIVE_URL,3*1024*1024)
    need(len(archive_raw)==2545109 and sha(archive_raw)==ARCHIVE_SHA)
    checksums=fetch(BASE+'CHECKSUMS.json',128*1024)
    need(len(checksums)==107508 and sha(checksums)==CHECKSUM_SHA)
    inventory=json.loads(checksums)['files'];need(len(inventory)==731)
    listed={x['path']:(x['size'],x['hash']) for x in inventory};need(len(listed)==731)
    files={}
    with zipfile.ZipFile(io.BytesIO(archive_raw)) as archive:
        for row in members(archive):
            raw=archive.read(row);need((len(raw),sha(raw))==listed.get(row.filename)==expected.get(row.filename))
            files[row.filename]=raw
    need(set(files)==set(listed))
    files['CHECKSUMS.json']=checksums;files['.component_hash']=COMPONENT_HASH.encode()
    need(set(files)==set(expected) and all((len(raw),sha(raw))==expected[name] for name,raw in files.items()))
    # All paths and bytes are admitted before creating any dependency file.
    destination.mkdir(parents=True)
    for name,raw in files.items():
        path=destination/name;path.parent.mkdir(parents=True,exist_ok=True)
        with path.open('xb') as stream:stream.write(raw)
    return destination

def native_probe(module,build,compiler):
    need(sys.byteorder=='little')
    vectors=module.verify_inputs()
    need(not build.exists());build.mkdir()
    generated=build/'include/sodium';generated.mkdir(parents=True)
    version=(module.SOURCE/'src/libsodium/include/sodium/version.h.in').read_text()
    for key,value in {'@VERSION@':'1.0.22','@SODIUM_LIBRARY_VERSION_MAJOR@':'26',
                      '@SODIUM_LIBRARY_VERSION_MINOR@':'4','@SODIUM_LIBRARY_MINIMAL_DEF@':'#define SODIUM_LIBRARY_MINIMAL 1'}.items():version=version.replace(key,value)
    need('@' not in version)
    (generated/'version.h').write_text(version,encoding='utf-8',newline='\n')
    (build/'independent_vectors.h').write_text(module.header(vectors),encoding='utf-8',newline='\n')
    flags=['-std=c11','-O2','-Wall','-Wextra','-DSODIUM_STATIC','-DCONFIGURED=1','-DNATIVE_LITTLE_ENDIAN=1',
           '-fno-asynchronous-unwind-tables','-fno-unwind-tables','-ffunction-sections','-fdata-sections']
    for include in (build,build/'include',module.SOURCE/'src/libsodium/include',module.SOURCE/'src/libsodium/include/sodium',module.SUCCESSOR):flags+=['-I',str(include)]
    inputs=[ROOT/'tests/host/noise_xk_independent_interop_probe.c',module.SUCCESSOR/'noise_xk_libsodium.c']+[module.SOURCE/'src/libsodium'/name for name in module.SOURCES]
    objects=[]
    for i,source in enumerate(inputs):
        obj=build/f'source-{i}.o';objects.append(str(obj))
        subprocess.run([compiler,*flags,*(['-Werror'] if i<2 else []),'-c',str(source),'-o',str(obj)],check=True,capture_output=True,timeout=120)
    exe=build/'independent-probe.exe'
    subprocess.run([compiler,'-Wl,--gc-sections',*objects,*(['-ladvapi32'] if os.name=='nt' else []),'-o',str(exe)],check=True,capture_output=True,timeout=120)
    result=subprocess.run([str(exe)],check=True,capture_output=True,text=True,timeout=20)
    need('PASS: 26 ' in result.stdout)
