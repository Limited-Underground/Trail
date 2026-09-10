"""Real libsodium host proof for evaluation invitation and composed policy session."""
from pathlib import Path
import importlib.util,os,subprocess,tempfile,json
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('interop',ROOT/'tools/noise_xk_independent_interop.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
EXTRA=['crypto_sign/crypto_sign.c','crypto_sign/ed25519/sign_ed25519.c','crypto_sign/ed25519/ref10/keypair.c','crypto_sign/ed25519/ref10/sign.c','crypto_sign/ed25519/ref10/open.c','crypto_hash/sha512/hash_sha512.c','crypto_hash/sha512/cp/hash_sha512_cp.c']
def run():
 (ROOT/'build').mkdir(exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='policy-crypto-',dir=ROOT/'build') as d:
  build=Path(d);m.SUCCESSOR=ROOT/'firmware/targets/heltec_v4_security_eval/main/noise_adapter';m.SUCCESSOR_SHA='b0be8109d017a851cea3952c4713157847c3bc64fe0eba1367c2b3c27cbcdc8b'
  evidence=m.run(build,True) # Verifies every731upstreamfile before compiling any extra primitive.
  c='C:/msys64/ucrt64/bin/gcc.exe' if os.name=='nt' else 'cc';cpp='C:/msys64/ucrt64/bin/g++.exe' if os.name=='nt' else 'c++'
  env=dict(os.environ)
  if os.name=='nt':env['PATH']='C:/msys64/ucrt64/bin;'+env['PATH']
  flags=['-O2','-Wall','-Wextra','-DSODIUM_STATIC','-DCONFIGURED=1','-DNATIVE_LITTLE_ENDIAN=1','-fno-asynchronous-unwind-tables','-fno-unwind-tables','-ffunction-sections','-fdata-sections']
  includes=[build/'include',m.SOURCE/'src/libsodium/include',m.SOURCE/'src/libsodium/include/sodium',m.SUCCESSOR,ROOT/'firmware/components/security_evaluation/include',ROOT/'firmware/components/security/include',ROOT/'firmware/components/security/test_support',ROOT/'firmware/components/persistence/include',ROOT/'firmware/components/persistence/test_support']
  for p in includes:flags+=['-I',str(p)]
  objects=[str(build/f'source-{i}.o') for i in range(1,17)]
  for i,path in enumerate(EXTRA):
   obj=build/f'extra-{i}.o';command=[c,'-std=c11',*flags,'-MMD','-MF',str(build/f'extra-{i}.d'),'-c',str(m.SOURCE/'src/libsodium'/path),'-o',str(obj)]
   compiled=subprocess.run(command,capture_output=True,text=True,env=env,timeout=120)
   if compiled.returncode:raise RuntimeError(compiled.stderr)
   evidence.setdefault('extra_primitives',[]).append({'path':path,'sha256':m.sha(m.SOURCE/'src/libsodium'/path),'warnings':compiled.stderr})
   objects.append(str(obj))
  common=['firmware/components/security/test_support/fake_secure_random.cpp','firmware/components/persistence/test_support/memory_persistent_storage.cpp','firmware/components/persistence/src/outbound_counter_lease_store.cpp','firmware/components/security/src/aead_nonce.cpp']
  for name in ['security_policy_invitation_tests','security_policy_session_tests']:
   exe=build/(name+'.exe');command=[cpp,'-std=c++17','-fno-exceptions','-fno-rtti','-Werror',*flags,'-Wl,--gc-sections',str(ROOT/'tests/host'/f'{name}.cpp'),*[str(ROOT/p) for p in common],*objects,'-o',str(exe)]
   subprocess.run(command,check=True,env=env,timeout=120)
   result=subprocess.run([str(exe)],capture_output=True,text=True,check=True,env=env,timeout=20)
   if name=='security_policy_invitation_tests' and result.stdout.strip()!='PASS 40 real signed invitation gate groups':raise RuntimeError(result.stdout)
   if name=='security_policy_session_tests' and result.stdout.strip()!='PASS 31 actual policy session groups':raise RuntimeError(result.stdout)
   print(result.stdout.strip())
if __name__=='__main__':run()
