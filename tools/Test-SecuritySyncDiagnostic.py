"""Complete affected OT-200 host matrix; local dependencies only, no devices.

Every run retains fresh command, output and source manifests under the repository.
Compilation failures cannot dispatch a stale executable. Existing physical
execution/authority packages are neither imported nor modified by this driver.
"""
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / '.private/ot200-integration'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--skip-lifecycle', action='store_true', help='focused work-in-progress check only')
    args = parser.parse_args()
    stamp = dt.datetime.now(dt.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    output = EVIDENCE / 'matrix' / stamp
    output.mkdir(parents=True, exist_ok=False)
    compiler = Path('C:/msys64/ucrt64/bin/g++.exe')
    if not compiler.is_file():
        resolved = shutil.which('g++')
        if not resolved:
            raise RuntimeError('Native g++ required')
        compiler = Path(resolved)
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')
    env.pop('PYTHONOPTIMIZE', None)
    env['PATH'] = str(compiler.parent) + os.pathsep + env.get('PATH', '')
    # Existing pure test wrappers use temporary directories; keep them in the
    # active worktree as required by workspace governance.
    env['TEMP'] = env['TMP'] = env['TMPDIR'] = str(output)
    result = {'started_utc': dt.datetime.now(dt.timezone.utc).isoformat(),
              'status': 'running', 'root': str(ROOT), 'output': str(output),
              'hardware': False, 'network': False, 'steps': [], 'sources': {}}
    inputs = [*ROOT.glob('tools/security_policy_sync*.py'),
              *ROOT.glob('tests/host/security_policy_sync*.*'),
              *ROOT.glob('firmware/targets/heltec_v4_security_sync_diag/**/*'),
              ROOT/'firmware/components/security_evaluation/include/opentrail/evaluation_control_synchronizing.hpp',
              ROOT/'firmware/components/security_diagnostics/include/opentrail/security_sync_record.hpp',
              ROOT/'firmware/components/security_diagnostics/sync_record_v1.json', Path(__file__)]
    for path in inputs:
        if path.is_file():
            raw = path.read_bytes()
            result['sources'][path.relative_to(ROOT).as_posix()] = {'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}

    def save():
        (output/'result.json').write_text(json.dumps(result, indent=2)+'\n')

    def run(label, command, timeout=300):
        row = {'label': label, 'command': list(map(str, command)), 'cwd': str(ROOT),
               'started_utc': dt.datetime.now(dt.timezone.utc).isoformat(), 'log':str(output/(label+'.log'))}
        result['steps'].append(row)
        save()
        try:
            with Path(row['log']).open('wb') as log:
                process = subprocess.run(row['command'], cwd=ROOT, env=env, stdout=log,
                                         stderr=subprocess.STDOUT, timeout=timeout, check=False)
            row['exit_code'] = process.returncode
            if process.returncode:
                raise RuntimeError(f'{label} failed; see {row["log"]}')
            print('PASS '+label, flush=True)
        except OSError as error:
            row['execution_environment_error'] = type(error).__name__
            raise
        finally:
            row['finished_utc'] = dt.datetime.now(dt.timezone.utc).isoformat()
            save()

    try:
        print('Output: '+str(output), flush=True)
        run('compiler', [compiler,'--version'])
        run('python', [sys.executable,'-B','--version'])
        includes = [ROOT/'firmware/components/security_evaluation/include',
                    ROOT/'firmware/components/security_diagnostics/include',
                    ROOT/'firmware/targets/heltec_v4_security_sync_diag/main',ROOT]
        flags = ['-std=c++17','-O2','-Wall','-Wextra','-Werror']
        for include in includes:
            flags += ['-I',str(include)]
        vectors = output/'wire-vectors.tsv'
        for name in ['control','record']:
            exe = output/('sync-'+name+'.exe')
            run(name+'-build', [compiler,*flags,ROOT/f'tests/host/security_policy_sync_{name}_tests.cpp','-o',exe])
            run(name, [exe,*([vectors] if name=='record' else [])])
        for name in ['readback','observation']:
            command = [sys.executable,'-B',ROOT/f'tests/host/security_policy_sync_{name}_tests.py']
            if name=='readback':
                command += ['--wire-vectors',vectors]
            run('sync-'+name, command)
        # Retain the inherited parser, decoder, strict receipt and restoration
        # observation regressions without downloading dependencies or importing
        # a physical runtime into this driver.
        for name in ['security_policy_input_control_tests.py',
                     'security_policy_input_readback_tests.py',
                     'security_policy_input_observation_tests.py',
                     'security_policy_input_timing_tests.py',
                     'security_policy_capture_tests.py']:
            run(name.removesuffix('.py'),[sys.executable,'-B',ROOT/'tests/host'/name])
        if not args.skip_lifecycle:
            lifecycle_output = EVIDENCE/'lifecycle'/('matrix-'+stamp)
            run('actual-sync-lifecycle',[sys.executable,'-B',ROOT/'tests/host/security_policy_sync_lifecycle_tests.py',
                                       '--output-root',lifecycle_output],timeout=600)
            result['lifecycle_result'] = str(lifecycle_output/'result.json')
        result['status'] = 'focused_pass' if args.skip_lifecycle else 'passed'
        print('RESULT: '+result['status'],flush=True)
        return 0
    except Exception as error:
        result['status'] = 'failed'
        result['failure'] = str(error)
        print('RESULT: FAIL '+str(error),file=sys.stderr,flush=True)
        return 1
    finally:
        result['finished_utc'] = dt.datetime.now(dt.timezone.utc).isoformat()
        save()


if __name__ == '__main__':
    sys.exit(main())
