"""Compile the actual dormant identity adapter and target reset port with SDK seams."""
from pathlib import Path
import shutil, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
def run():
    compiler=shutil.which('g++')
    if not compiler: raise RuntimeError('native g++ is required on PATH')
    (ROOT/'build').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='identity-nvs-',dir=ROOT/'build') as directory:
        exe=Path(directory)/'tests.exe'
        command=[compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror']
        for path in ['tests/host/fixtures/identity_nvs','firmware/targets/heltec_v4_bench/main','firmware/components/persistence/include','firmware/components/security_evaluation/include','firmware/components/companion/include','firmware/components/security/include','firmware/components/radio/include','firmware/components/protocol/include','firmware/components/location/include','firmware/components/time/include']:
            command += ['-I',str(ROOT/path)]
        command += [str(ROOT/path) for path in ['tests/host/enrollment_identity_nvs_tests.cpp','firmware/targets/heltec_v4_bench/main/enrollment_identity_nvs_storage.cpp','firmware/targets/heltec_v4_bench/main/heltec_v4_factory_reset_storage.cpp']]
        command += ['-o',str(exe)]
        subprocess.run(command,check=True,timeout=90)
        result=subprocess.run([str(exe)],text=True,capture_output=True,timeout=30)
        if result.returncode: raise RuntimeError(result.stderr or str(result.returncode))
        assert result.stdout.strip()=='PASS 14 actual identity NVS and factory-reset groups',result.stdout
        print(result.stdout.strip())
if __name__=='__main__':run()
