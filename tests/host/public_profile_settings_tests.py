"""Compile real public profile codec, owner and target NVS using SDK I/O seams."""
from pathlib import Path
import shutil, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
def run():
    compiler=shutil.which('g++')
    if not compiler: raise RuntimeError('native g++ required')
    (ROOT/'build').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='public-profile-',dir=ROOT/'build') as directory:
        exe=Path(directory)/'tests.exe'
        command=[compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror']
        includes=['tests/host/fixtures/identity_nvs','firmware/targets/heltec_v4_bench/main']
        includes += [f'firmware/components/{name}/include' for name in ['persistence','security_evaluation','companion','security','radio','protocol','location','time']]
        for path in includes: command += ['-I',str(ROOT/path)]
        paths=['tests/host/public_profile_settings_tests.cpp','firmware/targets/heltec_v4_bench/main/enrollment_identity_nvs_storage.cpp','firmware/targets/heltec_v4_bench/main/heltec_v4_factory_reset_storage.cpp','firmware/targets/heltec_v4_bench/main/companion_public_profile_storage.cpp']
        paths += [f'firmware/components/companion/src/{name}.cpp' for name in ['companion_device_name_codec','companion_device_name_owner','companion_public_profile_codec','companion_public_profile_owner']]
        subprocess.run(command+[str(ROOT/path) for path in paths]+['-o',str(exe)],check=True,timeout=90)
        subprocess.run([str(exe),str(ROOT/"tests/fixtures/companion_public_profile_v1_vectors.csv")],check=True,timeout=30)
if __name__=='__main__':run()
