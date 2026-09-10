"""Build and test frozen adapter with admitted real libsodium, without device access."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
FIX=ROOT/'tests/benchmarks/crypto/independent_noise_xk'
COMPONENT=ROOT/'tests/benchmarks/crypto/esp_idf/espressif_libsodium_1_0_22/managed_components/espressif__libsodium'
SOURCE=COMPONENT/'libsodium'
CHECKSUM_SHA='5e3983c5496a3cffba3d013c70991b18cda6c345655fff883fb0e14dfa09e582'
VECTOR_SHA='b7053c022656471c81f91860753ecb404633f7815bf8348de9fbce4ec9a99ca3'
ADAPTER_SHA={'noise_xk_libsodium.c':'8534fe1a6a4b68cd37e491ebd0f564dd38fd3935fb21d8f2d45aa8333ae442b8','noise_xk_libsodium.h':'b7c649434cdffe648e467bb117849ae0296a73fa041d614d3d4ba32578e40c45'}
ADAPTER=ROOT/'tests/benchmarks/crypto/adapters/libsodium_noise_xk_v0'
SUCCESSOR=FIX/'nonnull_adapter'
SUCCESSOR_SHA='4f3852e9822eebfed1df8946f6d0ca8ae756a7790ee15b48f1748aa662ba66c0'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def require(ok,msg):
 if not ok:raise ValueError(msg)
def verify_inputs():
 require(sha(COMPONENT/'CHECKSUMS.json')==CHECKSUM_SHA,'source_inventory_changed')
 for item in json.loads((COMPONENT/'CHECKSUMS.json').read_text())['files']:
  p=COMPONENT/item['path'];require(p.stat().st_size==item['size'] and sha(p)==item['hash'],'libsodium_source_changed')
 for name,digest in ADAPTER_SHA.items():require(sha(ADAPTER/name)==digest,'adapter_changed')
 require(sha(FIX/'vectors.json')==VECTOR_SHA,'vectors_changed')
 require(sha(SUCCESSOR/'noise_xk_libsodium.c')==SUCCESSOR_SHA,'successor_changed')
 require(sha(SUCCESSOR/'noise_xk_libsodium.h')==ADAPTER_SHA['noise_xk_libsodium.h'],'successor_header_changed')
 return json.loads((FIX/'vectors.json').read_text())
SOURCES = """crypto_hash/sha256/hash_sha256.c crypto_hash/sha256/cp/hash_sha256_cp.c crypto_auth/hmacsha256/auth_hmacsha256.c crypto_kdf/hkdf/kdf_hkdf_sha256.c crypto_aead/chacha20poly1305/aead_chacha20poly1305.c crypto_onetimeauth/poly1305/onetimeauth_poly1305.c crypto_onetimeauth/poly1305/donna/poly1305_donna.c crypto_stream/chacha20/stream_chacha20.c crypto_stream/chacha20/ref/chacha20_ref.c crypto_verify/verify.c crypto_scalarmult/curve25519/scalarmult_curve25519.c crypto_scalarmult/curve25519/ref10/x25519_ref10.c crypto_core/ed25519/ref10/ed25519_ref10.c sodium/utils.c sodium/core.c""".split()

def header(vectors):
 result='struct Vector {const char *prologue;const char *private_keys[4];const char *public_keys[4];const char *messages[3];const char *hash,*initiator_tx,*responder_tx;};\nstatic const struct Vector vectors[]={\n'
 for v in vectors['cases']:
  q=lambda x:json.dumps(x)
  arr=lambda xs:'{'+','.join(q(x) for x in xs)+'}'
  result+='{'+','.join([q(v['prologue']),arr(v['private_keys']),arr(v['public_keys']),arr(v['messages']),q(v['handshake_hash']),q(v['initiator_tx']),q(v['responder_tx'])])+'},\n'
 return result+'};\n'
def run(build,successor=False):
 require(sys.byteorder=='little','little_endian_host_required')
 vectors=verify_inputs()
 require(not build.exists() or not any(build.iterdir()),'build_directory_must_be_empty')
 build.mkdir(parents=True,exist_ok=True)
 generated=build/'include/sodium';generated.mkdir(parents=True,exist_ok=True)
 version=(SOURCE/'src/libsodium/include/sodium/version.h.in').read_text()
 for key,value in {'@VERSION@':'1.0.22','@SODIUM_LIBRARY_VERSION_MAJOR@':'26','@SODIUM_LIBRARY_VERSION_MINOR@':'4','@SODIUM_LIBRARY_MINIMAL_DEF@':'#define SODIUM_LIBRARY_MINIMAL 1'}.items():version=version.replace(key,value)
 require('@' not in version,'version_template_changed')
 (generated/'version.h').write_text(version,encoding='utf-8',newline='\n')
 (build/'independent_vectors.h').write_text(header(vectors),encoding='utf-8',newline='\n')
 adapter=SUCCESSOR if successor else ADAPTER
 compiler=Path('C:/msys64/ucrt64/bin/gcc.exe') if os.name=='nt' else Path(shutil.which('cc'))
 flags=['-std=c11','-O2','-Wall','-Wextra','-DSODIUM_STATIC','-DCONFIGURED=1','-DNATIVE_LITTLE_ENDIAN=1','-fno-asynchronous-unwind-tables','-fno-unwind-tables','-ffunction-sections','-fdata-sections']
 for inc in [build,build/'include',SOURCE/'src/libsodium/include',SOURCE/'src/libsodium/include/sodium',adapter]:flags+=['-I',str(inc)]
 env=dict(os.environ)
 if os.name=='nt':env['PATH']='C:/msys64/ucrt64/bin;'+env['PATH']
 commands=[];objects=[];warnings=[]
 inputs=[ROOT/'tests/host/noise_xk_independent_interop_probe.c',adapter/'noise_xk_libsodium.c']+[SOURCE/'src/libsodium'/p for p in SOURCES]
 for i,path in enumerate(inputs):
  obj=build/f'source-{i}.o';objects.append(str(obj))
  command=[str(compiler),*flags,*(['-Werror'] if i==0 or (i==1 and successor) else []),'-MMD','-MF',str(build/f'source-{i}.d'),'-c',str(path),'-o',str(obj)]
  result=subprocess.run(command,capture_output=True,text=True,env=env,timeout=120)
  require(result.returncode==0,result.stderr);commands.append(command)
  if result.stderr:warnings.append(result.stderr)
 exe=build/('independent-probe.exe' if os.name=='nt' else 'independent-probe')
 command=[str(compiler),'-Wl,--gc-sections',*objects,*(['-ladvapi32'] if os.name=='nt' else []),'-o',str(exe)]
 result=subprocess.run(command,capture_output=True,text=True,env=env,timeout=120);require(result.returncode==0,result.stderr);commands.append(command)
 result=subprocess.run([str(exe)],capture_output=True,text=True,env=env,timeout=20)
 require(result.returncode==0,result.stderr);require('PASS: 26 ' in result.stdout,'scenario_count_changed')
 # All admitted library files (including every private header) are verified above.
 # Retain compiler dependency outputs, generated inputs and object hashes as build evidence.
 evidence={'result':'passed','groups':26,'source_files':731,'successor':successor,'compiler_sha256':sha(compiler),'compiler_version':subprocess.check_output([str(compiler),'--version'],text=True,env=env).splitlines()[0],'primitive_sources':{p:sha(SOURCE/'src/libsodium'/p) for p in SOURCES},'objects':{Path(p).name:sha(Path(p)) for p in objects},'dependencies':{p.name:p.read_text() for p in sorted(build.glob('source-*.d'))},'generated':{p.name:sha(p) for p in [generated/'version.h',build/'independent_vectors.h']},'commands':commands,'warnings':warnings,'output':result.stdout.strip(),'hardware':False}
 (build/'result.json').write_text(json.dumps(evidence,indent=2)+'\n',encoding='utf-8',newline='\n')
 return evidence
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--build-dir',type=Path,default=ROOT/'build/independent-noise-scalar');p.add_argument('--successor',action='store_true');a=p.parse_args()
 result=run(a.build_dir.resolve(),a.successor)
 print(json.dumps({k:result[k] for k in ['result','groups','successor','compiler_version','output','hardware']},indent=2))
