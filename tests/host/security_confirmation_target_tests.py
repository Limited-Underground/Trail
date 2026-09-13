"""OT-215 real target body/SDK-boundary proof; no device or network access.

Optional scalar-object reuse requires the exact accepted OT-206 proof, compiler,
source inventory, generated header and object hashes. Signing/common objects are
built once because their earlier proof did not retain individual object hashes.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import sys

ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / 'firmware/targets/heltec_v4_confirmation_eval/main'
STUBS = ROOT / 'tests/host/security_invitation_target_stubs'
REPORT = ROOT / 'tests/benchmarks/crypto/OT-206-INVITATION-LIFECYCLE-2026-09-12.json'
REPORT_SHA = '32a010e37a9e8019dd8697416ca9e0b489d2a4c4e2302a4c99d01d327b3c07ba'
CC, CXX = Path('C:/msys64/ucrt64/bin/gcc.exe'), Path('C:/msys64/ucrt64/bin/g++.exe')


def need(ok, message):
    if not ok:
        raise RuntimeError(message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pin(path):
    return {'bytes': path.stat().st_size, 'sha256': sha(path)}


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run(output, reuse):
    need(sys.dont_write_bytecode and sys.flags.utf8_mode and not sys.flags.optimize,
         'use_python_-X_utf8_-B_without_optimization')
    need(os.name == 'nt' and CC.is_file() and CXX.is_file(), 'pinned_toolchain_required')
    need(output.is_absolute() and '..' not in output.parts and
         (output.is_relative_to(ROOT / '.private') or output.is_relative_to(ROOT / 'build')),
         'absolute_output_inside_worktree_required')
    for part in (output, *output.parents):
        need(not part.is_symlink() and not getattr(part, 'is_junction', lambda: False)(), 'indirect_output')
    output.mkdir(parents=True, exist_ok=False)
    result = {'schema': 'OT215-CONFIRMATION-TARGET-HOST-1', 'result': 'failed', 'hardware': False}
    try:
        need(sha(REPORT) == REPORT_SHA, 'accepted_report_changed')
        report = json.loads(REPORT.read_bytes())
        for name, expected in report['source_pins'].items():
            need(pin(ROOT / name) == expected, 'frozen_source_changed: ' + name)
        for compiler in (CC, CXX):
            need(sha(compiler) == report['toolchain'][compiler.name]['sha256'], 'compiler_changed')
        frozen = load(ROOT / 'tests/host/security_policy_invitation_lifecycle_tests.py', 'ot215_commands')
        commands = frozen.Commands(output)
        module = load(ROOT / 'tools/noise_xk_independent_interop.py', 'ot215_crypto')
        module.SUCCESSOR = ROOT / 'firmware/targets/heltec_v4_security_eval/main/noise_adapter'
        module.SUCCESSOR_SHA = 'b0be8109d017a851cea3952c4713157847c3bc64fe0eba1367c2b3c27cbcdc8b'
        module.verify_inputs()
        env = dict(os.environ)
        env.pop('PYTHONOPTIMIZE', None)
        env['PATH'] = str(CC.parent) + os.pathsep + env.get('PATH', '')
        if reuse is not None:
            need(reuse.is_absolute() and reuse.is_relative_to(ROOT / '.private'), 'invalid_reuse_root')
            for part in (reuse, *reuse.parents):
                need(not part.is_symlink() and not getattr(part, 'is_junction', lambda: False)(), 'indirect_reuse')
            need(pin(reuse / 'result.json') == report['host_result_binding'], 'reuse_proof_changed')
            proof = json.loads((reuse / 'result.json').read_bytes())
            need(proof['result'] == 'passed', 'reuse_proof_failed')
            control = proof['control']; build = reuse / 'scalar-build'
            need(control['compiler_sha256'] == sha(CC), 'reuse_compiler_changed')
            need(sha(build / 'include/sodium/version.h') == control['generated']['version.h'], 'reuse_header_changed')
            for index in range(1, 17):
                name = f'source-{index}.o'
                need(sha(build / name) == control['objects'][name], 'reuse_object_changed')
            result['scalar_reuse'] = {'proof': pin(reuse / 'result.json'), 'objects': {
                f'source-{index}.o': pin(build / f'source-{index}.o') for index in range(1, 17)}}
        else:
            from types import SimpleNamespace
            module.subprocess = SimpleNamespace(run=commands.run, check_output=commands.check_output)
            build = output / 'scalar-build'
            result['scalar_control'] = module.run(build, True)
        flags = ['-O2', '-Wall', '-Wextra', '-DSODIUM_STATIC', '-DCONFIGURED=1',
                 '-DNATIVE_LITTLE_ENDIAN=1', '-fno-asynchronous-unwind-tables',
                 '-fno-unwind-tables', '-ffunction-sections', '-fdata-sections']
        includes = [STUBS, build / 'include', module.SOURCE / 'src/libsodium/include',
                    module.SOURCE / 'src/libsodium/include/sodium', module.SUCCESSOR, TARGET,
                    ROOT / 'firmware/targets/heltec_v4_security_receipt_sync/main',
                    ROOT / 'firmware/targets/heltec_v4_security_policy_eval/main',
                    ROOT / 'firmware/components/security_evaluation/include',
                    ROOT / 'firmware/components/security_diagnostics/include',
                    ROOT / 'firmware/components/security/include',
                    ROOT / 'firmware/components/persistence/include']
        for path in includes:
            flags += ['-I', str(path)]
        objects = [build / f'source-{index}.o' for index in range(1, 17)]
        sources = [Path(__file__), REPORT, *[path for path in STUBS.rglob('*') if path.is_file()],
                   TARGET / 'app_main.cpp', TARGET / 'nvs_confirmation_backend.hpp', TARGET / 'confirmation_evaluation.hpp',
                   ROOT / 'tests/host/security_confirmation_target_tests.cpp',
                   ROOT / 'firmware/components/security_evaluation/include/opentrail/evaluation_confirmation_owner.hpp']
        common = ('firmware/components/persistence/src/persistent_storage_kv.cpp',
                  'firmware/components/persistence/src/outbound_counter_lease_store.cpp',
                  'firmware/components/security/src/aead_nonce.cpp')
        sources += [ROOT / path for path in common]
        source_before = {str(path): pin(path) for path in sources}
        for index, relative in enumerate(frozen.EXTRA):
            source = module.SOURCE / 'src/libsodium' / relative; obj = output / f'signing-{index}.o'
            commands.run([CC, '-std=c11', *flags, '-MMD', '-MF', output / f'signing-{index}.d',
                          '-c', source, '-o', obj], env=env, check=True)
            objects.append(obj)
        for index, source in enumerate([*[ROOT / path for path in common], TARGET / 'app_main.cpp',
                                       ROOT / 'tests/host/security_confirmation_target_tests.cpp']):
            obj = output / f'target-{index}.o'
            commands.run([CXX, '-std=c++17', '-fno-exceptions', '-fno-rtti', '-Werror', *flags,
                          '-MMD', '-MF', output / f'target-{index}.d', '-c', source, '-o', obj],
                         env=env, check=True)
            objects.append(obj)
        exe = output / 'confirmation-target-tests.exe'
        commands.run([CXX, '-Wl,--gc-sections', '-Wl,--wrap=sodium_init',
                      '-Wl,--wrap=sodium_memzero', '-Wl,--wrap=crypto_aead_chacha20poly1305_ietf_encrypt',
                      '-Wl,--wrap=crypto_aead_chacha20poly1305_ietf_decrypt', *objects, '-o', exe], env=env, check=True)
        completed = commands.run([exe], env=env, check=True, timeout=60)
        need(completed.stdout.startswith('PASS ') and ' actual confirmation target groups' in completed.stdout,
             'test_report_missing')
        need(source_before == {str(path): pin(path) for path in sources}, 'source_changed_during_test')
        result.update(result='passed', output=completed.stdout.strip(), binary=pin(exe),
                      source_pins={Path(path).relative_to(ROOT).as_posix(): value for path, value in source_before.items()},
                      compiled_objects={str(path.relative_to(output)): pin(path) for path in objects if path.is_relative_to(output)})
        print(completed.stdout.strip(), flush=True)
    except Exception as error:
        result['error_type'] = type(error).__name__
        result['error'] = str(error)
        raise
    finally:
        (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8', newline='\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output-root', type=Path, required=True)
    parser.add_argument('--reuse-crypto-root', type=Path)
    args = parser.parse_args()
    try:
        run(args.output_root, args.reuse_crypto_root)
    except Exception as error:
        print(f'target_tests_failed: {type(error).__name__}; see {args.output_root}/result.json and commands.json', file=sys.stderr)
        raise SystemExit(1) from None
