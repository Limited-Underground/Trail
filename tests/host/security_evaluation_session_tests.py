"""Compile evaluation helper against real admitted scalar crypto and actual counter store."""
from pathlib import Path
import importlib.util,subprocess,os,tempfile
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('interop',ROOT/'tools/noise_xk_independent_interop.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
def run():
 (ROOT/'build').mkdir(exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='security-composition-',dir=ROOT/'build') as d:
  build=Path(d)
  m.SUCCESSOR=ROOT/'firmware/targets/heltec_v4_security_eval/main/noise_adapter'
  m.SUCCESSOR_SHA='b0be8109d017a851cea3952c4713157847c3bc64fe0eba1367c2b3c27cbcdc8b'
  m.run(build,True)
  compiler='C:/msys64/ucrt64/bin/g++.exe' if os.name=='nt' else 'c++'
  flags=['-std=c++17','-fno-exceptions','-fno-rtti','-O2','-Wall','-Wextra','-Werror','-DSODIUM_STATIC','-fno-asynchronous-unwind-tables','-fno-unwind-tables','-ffunction-sections','-fdata-sections','-Wl,--gc-sections']
  for p in [build/'include',m.SOURCE/'src/libsodium/include',m.SOURCE/'src/libsodium/include/sodium',m.SUCCESSOR,ROOT/'firmware/targets/heltec_v4_security_eval/main',ROOT/'firmware/components/security/include',ROOT/'firmware/components/security/test_support',ROOT/'firmware/components/persistence/include',ROOT/'firmware/components/persistence/test_support']:flags+=['-I',str(p)]
  sources=['tests/host/security_evaluation_session_tests.cpp','firmware/components/security/test_support/fake_secure_random.cpp','firmware/components/persistence/test_support/memory_persistent_storage.cpp','firmware/components/persistence/src/outbound_counter_lease_store.cpp']
  exe=build/'composition.exe';cmd=[compiler,*flags,*[str(ROOT/p) for p in sources],*[str(build/f'source-{i}.o') for i in range(1,17)],'-o',str(exe)]
  env=dict(os.environ)
  if os.name=='nt':env['PATH']='C:/msys64/ucrt64/bin;'+env['PATH']
  subprocess.run(cmd,check=True,env=env,timeout=120)
  result=subprocess.run([str(exe)],check=True,env=env,timeout=20,text=True,capture_output=True)
  if result.stdout.strip()!='PASS 19 actual security evaluation composition groups':raise RuntimeError(result.stdout)
  print(result.stdout.strip())
  size_exe=build/'size-test.exe'
  ccompiler='C:/msys64/ucrt64/bin/gcc.exe' if os.name=='nt' else 'cc'
  cflags=['-std=c11','-O2','-Wall','-Wextra','-Werror','-DSODIUM_STATIC','-fno-asynchronous-unwind-tables','-fno-unwind-tables','-ffunction-sections','-fdata-sections','-Wl,--gc-sections']
  for inc in [build/'include',m.SOURCE/'src/libsodium/include',m.SOURCE/'src/libsodium/include/sodium',m.SUCCESSOR]:cflags+=['-I',str(inc)]
  command=[ccompiler,*cflags,str(ROOT/'tests/host/security_eval_noise_size_tests.c'),*[str(build/f'source-{i}.o') for i in range(2,17)],'-o',str(size_exe)]
  subprocess.run(command,check=True,env=env,timeout=90)
  output=subprocess.check_output([str(size_exe)],text=True,env=env,timeout=20).strip()
  if output!='PASS 5 target adapter size-bound refusal groups':raise RuntimeError(output)
  print(output)
if __name__=='__main__':run()
