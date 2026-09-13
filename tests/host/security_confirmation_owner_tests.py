"""OT215 actual confirmation owner/crypto proof, with optional pinned scalar reuse.

No dependency acquisition or device access. Default builds the same admitted
local scalar sources; --reuse-crypto-root accepts only the exact OT206 proof.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import sys
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
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


def safe_output(path):
    need(path.is_absolute() and '..' not in path.parts and
         (path.is_relative_to(ROOT / '.private') or path.is_relative_to(ROOT / 'build')),
         'absolute_output_inside_worktree_required')
    for part in (path, *path.parents):
        need(not part.is_symlink() and not getattr(part, 'is_junction', lambda: False)(), 'indirect_output')


def run(output, reuse=None):
    need(sys.dont_write_bytecode and sys.flags.utf8_mode and not sys.flags.optimize,
         'use_python_-X_utf8_-B_without_optimization')
    need(os.name == 'nt' and CC.is_file() and CXX.is_file(), 'pinned_ucrt_required')
    safe_output(output)
    output.mkdir(parents=True, exist_ok=False)
    result = {'schema': 'OT215-CONFIRMATION-OWNER-HOST-1', 'result': 'failed', 'hardware': False}
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
            safe_output(reuse)
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
            module.subprocess = SimpleNamespace(run=commands.run, check_output=commands.check_output)
            build = output / 'scalar-build'
            result['scalar_control'] = module.run(build, True)
        flags = ['-O2', '-Wall', '-Wextra', '-DSODIUM_STATIC', '-DCONFIGURED=1',
                 '-DNATIVE_LITTLE_ENDIAN=1', '-fno-asynchronous-unwind-tables',
                 '-fno-unwind-tables', '-ffunction-sections', '-fdata-sections']
        for path in (build / 'include', module.SOURCE / 'src/libsodium/include',
                     module.SOURCE / 'src/libsodium/include/sodium', module.SUCCESSOR,
                     ROOT / 'firmware/components/security_evaluation/include',
                     ROOT / 'firmware/components/security/include', ROOT / 'firmware/components/security/test_support',
                     ROOT / 'firmware/components/persistence/include', ROOT / 'firmware/components/persistence/test_support'):
            flags += ['-I', str(path)]
        objects = [build / f'source-{index}.o' for index in range(1, 17)]
        cpp = ROOT / 'tests/host/security_confirmation_owner_tests.cpp'
        header = ROOT / 'firmware/components/security_evaluation/include/opentrail/evaluation_confirmation_owner.hpp'
        sources = [Path(__file__), REPORT, cpp, header,
                   ROOT / 'tests/host/security_policy_invitation_lifecycle_fixture.hpp',
                   *[ROOT / path for path in frozen.COMMON]]
        before = {str(path): pin(path) for path in sources}
        for index, relative in enumerate(frozen.EXTRA):
            source = module.SOURCE / 'src/libsodium' / relative; obj = output / f'signing-{index}.o'
            commands.run([CC, '-std=c11', *flags, '-MMD', '-MF', output / f'signing-{index}.d',
                          '-c', source, '-o', obj], env=env, check=True)
            objects.append(obj)
        for index, relative in enumerate(frozen.COMMON):
            obj = output / f'common-{index}.o'
            commands.run([CXX, '-std=c++17', '-fno-exceptions', '-fno-rtti', '-Werror', *flags,
                          '-MMD', '-MF', output / f'common-{index}.d', '-c', ROOT / relative, '-o', obj],
                         env=env, check=True)
            objects.append(obj)
        exe = output / 'confirmation-owner-tests.exe'
        commands.run([CXX, '-std=c++17', '-fno-exceptions', '-fno-rtti', '-Werror', *flags,
                      '-Wl,--gc-sections', '-MMD', '-MF', output / 'confirmation-owner-tests.d',
                      cpp, *objects, '-o', exe], env=env, check=True)
        completed = commands.run([exe], env=env, check=True, timeout=60)
        need(completed.stdout.startswith('PASS ') and ' actual confirmation owner groups' in completed.stdout,
             'test_report_missing')
        need(before == {str(path): pin(path) for path in sources}, 'source_changed_during_test')
        result.update(result='passed', output=completed.stdout.strip(), binary=pin(exe),
                      source_pins={Path(path).relative_to(ROOT).as_posix(): value for path, value in before.items()},
                      compiled_objects={str(path.relative_to(output)): pin(path) for path in objects if path.is_relative_to(output)})
        print(completed.stdout.strip(), flush=True)
    except Exception as error:
        result.update(error_type=type(error).__name__, error=str(error))
        raise
    finally:
        (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8', newline='\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output-root', type=Path, required=True)
    parser.add_argument('--reuse-crypto-root', type=Path)
    args = parser.parse_args()
    try:
        run(args.output_root, args.reuse_crypto_root)
    except Exception as error:
        print(f'confirmation_tests_failed: {type(error).__name__}; see {args.output_root}/result.json and commands.json', file=sys.stderr)
        raise SystemExit(1) from None
