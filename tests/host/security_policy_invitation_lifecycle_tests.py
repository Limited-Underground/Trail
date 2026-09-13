"""OT-206 actual-source lifecycle proof with admitted libsodium; no device access.

Build the existing pinned scalar/Noise control once, then the invitation authority, authorized-session, known-answer and unchanged
core regression suites. Retain every command and exit status.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import subprocess
import sys
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
CC = Path('C:/msys64/ucrt64/bin/gcc.exe')
CXX = Path('C:/msys64/ucrt64/bin/g++.exe')
EXTRA = (
    'crypto_sign/crypto_sign.c', 'crypto_sign/ed25519/sign_ed25519.c',
    'crypto_sign/ed25519/ref10/keypair.c', 'crypto_sign/ed25519/ref10/sign.c',
    'crypto_sign/ed25519/ref10/open.c', 'crypto_hash/sha512/hash_sha512.c',
    'crypto_hash/sha512/cp/hash_sha512_cp.c',
)
COMMON = (
    'firmware/components/security/test_support/fake_secure_random.cpp',
    'firmware/components/persistence/test_support/memory_persistent_storage.cpp',
    'firmware/components/persistence/src/outbound_counter_lease_store.cpp',
    'firmware/components/security/src/aead_nonce.cpp',
)
SUITES = ('security_policy_invitation_lifecycle_tests',
          'security_policy_authorized_session_tests',
          'security_candidate_known_answer_tests',
          'security_policy_invitation_tests', 'security_policy_session_tests')


def need(ok, message):
    if not ok:
        raise RuntimeError(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class Commands:
    def __init__(self, output):
        self.output, self.rows = output, []

    def run(self, command, **kwargs):
        command = [str(value) for value in command]
        number = len(self.rows) + 1
        row = {'command': command, 'status': 'running'}
        self.rows.append(row)
        self.save()
        kwargs.setdefault('cwd', ROOT)
        kwargs.setdefault('capture_output', True)
        kwargs.setdefault('text', True)
        kwargs.setdefault('encoding', 'utf-8')
        kwargs.setdefault('errors', 'replace')
        kwargs.setdefault('timeout', 120)
        try:
            result = subprocess.run(command, **kwargs)
            row.update(status='completed', exit_status=result.returncode)
            stdout, stderr = result.stdout or '', result.stderr or ''
        except subprocess.CalledProcessError as error:
            row.update(status='failed', exit_status=error.returncode)
            stdout, stderr = error.stdout or '', error.stderr or ''
            raise
        except Exception as error:
            row.update(status='launch_or_timeout_failure', error_type=type(error).__name__)
            stdout, stderr = '', ''
            raise
        finally:
            for suffix, value in (('stdout', stdout), ('stderr', stderr)):
                path = self.output / f'{number:03d}-{suffix}.log'
                path.write_text(value, encoding='utf-8', newline='\n')
            self.save()
        return result

    def check_output(self, command, **kwargs):
        kwargs['check'] = True
        return self.run(command, **kwargs).stdout

    def save(self):
        (self.output / 'commands.json').write_text(
            json.dumps(self.rows, indent=2) + '\n', encoding='utf-8', newline='\n')


def run(output):
    need(sys.dont_write_bytecode and sys.flags.utf8_mode and not sys.flags.optimize,
         'run_python_with_-X_utf8_-B_and_without_optimization')
    need(os.name == 'nt' and CC.is_file() and CXX.is_file(), 'pinned_ucrt_toolchain_required')
    need(output.is_absolute() and '..' not in output.parts, 'absolute_output_required')
    need(output.is_relative_to(ROOT / '.private') or output.is_relative_to(ROOT / 'build'),
         'output_must_stay_in_active_worktree')
    for part in (output, *output.parents):
        need(not part.is_symlink() and not getattr(part, 'is_junction', lambda: False)(),
             'indirect_output_refused')
    output.mkdir(parents=True, exist_ok=False)
    commands = Commands(output)
    result = {'schema': 'OT206-INVITATION-HOST-1', 'result': 'failed', 'hardware': False}
    try:
        helper = ROOT / 'tools/noise_xk_independent_interop.py'
        spec = importlib.util.spec_from_file_location('ot206_pinned_crypto_build', helper)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.SUCCESSOR = ROOT / 'firmware/targets/heltec_v4_security_eval/main/noise_adapter'
        module.SUCCESSOR_SHA = 'b0be8109d017a851cea3952c4713157847c3bc64fe0eba1367c2b3c27cbcdc8b'
        module.subprocess = SimpleNamespace(run=commands.run, check_output=commands.check_output)
        env = dict(os.environ)
        env.pop('PYTHONOPTIMIZE', None)
        env['PATH'] = str(CC.parent) + os.pathsep + env.get('PATH', '')
        result['compilers'] = {str(path): {'sha256': sha(path),
            'version': commands.check_output([path, '--version'], env=env).splitlines()[0]}
            for path in (CC, CXX)}
        build = output / 'scalar-build'
        result['control'] = module.run(build, True)
        flags = ['-O2', '-Wall', '-Wextra', '-DSODIUM_STATIC', '-DCONFIGURED=1',
                 '-DNATIVE_LITTLE_ENDIAN=1', '-fno-asynchronous-unwind-tables',
                 '-fno-unwind-tables', '-ffunction-sections', '-fdata-sections']
        for path in (build / 'include', module.SOURCE / 'src/libsodium/include',
                     module.SOURCE / 'src/libsodium/include/sodium', module.SUCCESSOR,
                     ROOT / 'firmware/components/security_evaluation/include',
                     ROOT / 'firmware/components/security/include',
                     ROOT / 'firmware/components/security/test_support',
                     ROOT / 'firmware/components/persistence/include',
                     ROOT / 'firmware/components/persistence/test_support'):
            flags += ['-I', str(path)]
        objects = [str(build / f'source-{index}.o') for index in range(1, 17)]
        sources = [helper, Path(__file__), *[ROOT / path for path in COMMON]]
        for index, relative in enumerate(EXTRA):
            source = module.SOURCE / 'src/libsodium' / relative
            obj = output / f'signing-{index}.o'
            commands.run([CC, '-std=c11', *flags, '-MMD', '-MF', output / f'signing-{index}.d',
                          '-c', source, '-o', obj], env=env, check=True)
            objects.append(str(obj)); sources.append(source)
        for index, relative in enumerate(COMMON):
            obj = output / f'common-{index}.o'
            commands.run([CXX, '-std=c++17', '-fno-exceptions', '-fno-rtti', '-Werror',
                          *flags, '-MMD', '-MF', output / f'common-{index}.d',
                          '-c', ROOT / relative, '-o', obj], env=env, check=True)
            objects.append(str(obj))
        result['suites'] = {}
        for name in SUITES:
            source = ROOT / 'tests/host' / (name + '.cpp')
            exe = output / (name + '.exe')
            commands.run([CXX, '-std=c++17', '-fno-exceptions', '-fno-rtti', '-Werror',
                          *flags, '-Wl,--gc-sections', '-MMD', '-MF', output / (name + '.d'),
                          source, *objects, '-o', exe],
                         env=env, check=True)
            completed = commands.run([exe], env=env, check=True, timeout=30)
            need(completed.stdout.startswith('PASS ') and ' groups\n' in completed.stdout,
                 'suite_did_not_report_success')
            result['suites'][name] = {'output': completed.stdout.strip(), 'binary_sha256': sha(exe)}
            sources.append(source)
            print(completed.stdout.strip(), flush=True)
        for name in ('evaluation_invitation_authority.hpp', 'authorized_policy_session.hpp',
                     'evaluation_invitation.hpp', 'evaluation_policy_session.hpp'):
            sources.append(ROOT / 'firmware/components/security_evaluation/include/opentrail' / name)
        sources.append(ROOT / 'tests/host/security_policy_invitation_lifecycle_fixture.hpp')
        result['source_pins'] = {path.relative_to(ROOT).as_posix(): {'bytes': path.stat().st_size,
                                 'sha256': sha(path)} for path in sources}
        result['result'] = 'passed'
    except Exception as error:
        result['error_type'] = type(error).__name__
        raise
    finally:
        (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n',
                                         encoding='utf-8', newline='\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output-root', type=Path, required=True)
    args = parser.parse_args()
    run(args.output_root)
