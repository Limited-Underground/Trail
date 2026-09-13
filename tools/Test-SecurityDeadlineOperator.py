"""Complete affected OT-212 host matrix; synthetic devices and local files only."""
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import security_policy_deadline_bundle as bundle

SUITES = [
    'security_policy_deadline_endpoint_tests.py',
    'security_policy_deadline_bundle_tests.py',
    'security_policy_deadline_restore_observation_tests.py',
    'security_policy_deadline_execution_tests.py',
    'security_policy_deadline_runtime_bundle_tests.py',
    'security_policy_deadline_operator_tests.py',
    'security_policy_deadline_operator_integration_tests.py',
    'security_policy_deadline_isolated_tests.py',
    'security_policy_invitation_readback_tests.py',
    'security_policy_hardware_tests.py',
    'security_policy_endpoint_tests.py',
    'security_policy_execution_tests.py',
    'security_policy_backup_tests.py',
    'security_policy_backup_operator_tests.py',
    'security_policy_capture_tests.py',
    'security_policy_input_readback_tests.py',
    'security_policy_input_observation_tests.py',
    'security_policy_input_timing_tests.py',
]

def pin(path):
    raw = path.read_bytes()
    return {'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--candidate', type=Path, help='Optional admitted real image for synthetic-device composed tests')
    args = parser.parse_args()
    stamp = dt.datetime.now(dt.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    output = ROOT/'.private/ot212-deadline/matrix'/stamp
    output.mkdir(parents=True, exist_ok=False)
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', TEMP=str(output), TMP=str(output), TMPDIR=str(output))
    env.pop('PYTHONOPTIMIZE', None)
    bundled = Path('C:/Users/bjela/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe')
    power = bundled if bundled.is_file() else Path(shutil.which('pwsh') or 'missing-pwsh')
    env['PATH'] = str(power.parent) + os.pathsep + env.get('PATH', '')
    result = {'status': 'running', 'started_utc': dt.datetime.now(dt.timezone.utc).isoformat(),
              'root': str(ROOT), 'output': str(output), 'hardware': False, 'network': False,
              'steps': [], 'source_pins': {}}
    def save():
        (output/'result.json').write_bytes((json.dumps(result, indent=2)+'\n').encode())
    try:
        if not power.is_file():
            raise RuntimeError('PowerShell required for actual isolated dispatch')
        sources = [*bundle.SOURCE_PATHS, *('tests/host/'+name for name in SUITES),
                   'tools/Test-SecurityDeadlineOperator.py', bundle.REPORT]
        result['source_pins'] = {name: pin(ROOT/name) for name in sources}
        bundle._sources(ROOT)
        if args.candidate is not None:
            args.candidate = args.candidate.resolve()
            if not args.candidate.is_relative_to(ROOT) or pin(args.candidate) != {'bytes': bundle.CANDIDATE_SIZE, 'sha256': bundle.CANDIDATE_SHA}:
                raise RuntimeError('Candidate does not match the admitted image')
            result['candidate'] = {'path': str(args.candidate), **pin(args.candidate)}
        print('Output: '+str(output), flush=True)
        commands = [('python', [sys.executable, '-B', '--version']),
                    ('powershell', [str(power), '-NoProfile', '-NonInteractive', '-Command', '$PSVersionTable.PSVersion.ToString()'])]
        commands.extend((name.removesuffix('.py'), [sys.executable, '-B', str(ROOT/'tests/host'/name)]) for name in SUITES)
        if args.candidate is not None:
            for label, command in commands:
                if label == 'security_policy_deadline_operator_integration_tests':
                    command.extend(['--candidate', str(args.candidate)])
        for label, command in commands:
            log = output/(label+'.log')
            step = {'name': label, 'command': command, 'cwd': str(ROOT), 'log': str(log),
                    'started_utc': dt.datetime.now(dt.timezone.utc).isoformat()}
            result['steps'].append(step)
            save()
            with log.open('wb') as handle:
                process = subprocess.run(command, cwd=ROOT, env=env, stdout=handle,
                                         stderr=subprocess.STDOUT, timeout=600, check=False)
            step['exit_code'] = process.returncode
            step['finished_utc'] = dt.datetime.now(dt.timezone.utc).isoformat()
            step['log_pin'] = pin(log)
            text = log.read_text(encoding='utf-8', errors='strict')
            step['result_lines'] = [line for line in text.splitlines() if line.startswith(('Ran ', 'OK', 'PASS ', 'FAILED'))]
            if process.returncode or re.search(r'OK \(skipped=\d+\)', text):
                raise RuntimeError(label+' failed or skipped; see '+str(log))
            print('PASS '+label, flush=True)
        for name, expected in result['source_pins'].items():
            if pin(ROOT/name) != expected:
                raise RuntimeError('Source changed during matrix: '+name)
        result['status'] = 'passed'
        print('RESULT: passed', flush=True)
        return 0
    except Exception as error:
        result['status'] = 'failed'
        result['failure'] = str(error)
        print('RESULT: failed: '+str(error), file=sys.stderr, flush=True)
        return 1
    finally:
        result['finished_utc'] = dt.datetime.now(dt.timezone.utc).isoformat()
        save()

if __name__ == '__main__':
    sys.exit(main())
