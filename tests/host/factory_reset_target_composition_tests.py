"""Actual Heltec reset ports + durable executor/authority; SDK I/O simulated."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def run():
    compiler = shutil.which('g++')
    if not compiler:
        raise RuntimeError('native g++ is required on PATH')
    (ROOT / 'build').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='reset-composition-', dir=ROOT / 'build') as directory:
        executable = Path(directory) / 'tests.exe'
        command = [compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror']
        includes = ['tests/host/fixtures/identity_nvs', 'firmware/targets/heltec_v4_bench/main']
        includes += [f'firmware/components/{name}/include' for name in
                     ['persistence', 'security_evaluation', 'companion', 'security', 'radio', 'protocol', 'location', 'time']]
        for path in includes:
            command += ['-I', str(ROOT / path)]
        sources = ['tests/host/factory_reset_target_composition_tests.cpp',
                   'firmware/targets/heltec_v4_bench/main/enrollment_identity_nvs_storage.cpp',
                   'firmware/targets/heltec_v4_bench/main/heltec_v4_factory_reset_storage.cpp',
                   'firmware/components/companion/src/device_factory_reset_executor.cpp',
                   'firmware/components/companion/src/companion_factory_reset_authority.cpp']
        command += [str(ROOT / path) for path in sources] + ['-o', str(executable)]
        subprocess.run(command, check=True, timeout=90)
        result = subprocess.run([str(executable)], text=True, capture_output=True, timeout=30)
        if result.returncode:
            raise RuntimeError(result.stderr or str(result.returncode))
        assert result.stdout.strip() == 'PASS 26 actual target reset composition groups', result.stdout
        print(result.stdout.strip())

if __name__ == '__main__':
    run()
