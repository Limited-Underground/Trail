"""Default-inert host launcher for a separately authorized future diagnostic.

The hash-pinned controller is executed as a namespace inside the unchanged
isolated fourteen-source capsule. No new module origin or SDK file is invented.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def wait_without_kill(child, timeout):
    """Keep the single cleanup owner attached; parent ceilings never kill it."""
    try:
        child.communicate(timeout=timeout)
        return False
    except subprocess.TimeoutExpired:
        # The child retains its original absolute authorization/worker ceilings.
        # Waiting for its terminal result grants no new hardware authority.
        child.communicate()
        return True


def verify_capsule_files(prepared, namespace):
    """Trusted host hash verification before executing the capsule interpreter."""
    private = prepared['private']
    assembly = namespace['decode'](prepared['data']['assembly'])
    manifest = namespace['decode'](namespace['read_ref'](assembly['runtime'], private))
    root = prepared['source_root'].parent
    files = manifest.get('files')
    if not isinstance(files, dict) or 'python.exe' not in files or len(files) <= 14:
        raise RuntimeError('diagnostic_capsule_inventory_invalid')
    for name, pin in files.items():
        relative = Path(name)
        if relative.is_absolute() or '..' in relative.parts or relative.as_posix() != name:
            raise RuntimeError('diagnostic_capsule_inventory_invalid')
        namespace['read_ref'](dict(pin, path=str(root / relative)), private)
    actual = {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()}
    if actual != set(files): raise RuntimeError('diagnostic_capsule_inventory_invalid')


def main(argv=None):
    parser = argparse.ArgumentParser(description='Future diagnostic launcher; inert without exact fresh authority.')
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--controller-sha256')
    parser.add_argument('--package')
    parser.add_argument('--package-sha256')
    parser.add_argument('--authorization')
    parser.add_argument('--authorization-sha256')
    args = parser.parse_args(argv)
    try:
        if not (args.execute and args.controller_sha256 and args.package and args.package_sha256
                and args.authorization and args.authorization_sha256):
            raise RuntimeError('diagnostic_execution_not_authorized')
        controller = Path(__file__).resolve().with_name('enrollment_candidate_recovery_diagnostic.py')
        for part in (controller, *controller.parents):
            if part.is_symlink() or part.is_junction(): raise RuntimeError('diagnostic_path_invalid')
        raw = controller.read_bytes()
        if hashlib.sha256(raw).hexdigest() != args.controller_sha256:
            raise RuntimeError('diagnostic_controller_changed')
        namespace = {'__name__': '_host_diagnostic_admission', '__file__': str(controller)}
        exec(compile(raw, str(controller), 'exec'), namespace)
        prepared = namespace['_admit'](args.package, args.package_sha256, args.authorization,
            args.authorization_sha256, True, spend_authorization=False)
        verify_capsule_files(prepared, namespace)
        root = prepared['source_root'].parent
        program = (
            'import hashlib\n'
            f'p={str(controller)!r}\n'
            'r=open(p,"rb").read()\n'
            f'assert hashlib.sha256(r).hexdigest()=={args.controller_sha256!r}\n'
            'n={"__name__":"_isolated_authorized_recovery_diagnostic","__file__":p}\n'
            'exec(compile(r,p,"exec"),n)\n'
            f'n["execute"]({args.package!r},{args.package_sha256!r},{args.authorization!r},'
            f'{args.authorization_sha256!r},approved_device_execution=True)\n')
        timeout = prepared['cleanup_deadline'] - time.monotonic()
        if timeout <= 0: raise RuntimeError('diagnostic_authorization_expired')
        env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('PYTHON', 'ESPTOOL_'))}
        env['ESPTOOL_CFGFILE'] = str(root / 'esptool.cfg')
        child = subprocess.Popen([str(root / 'python.exe'), '-I', '-S', '-B', '-c', program],
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            cwd=str(root), env=env)
        ceiling_observed = wait_without_kill(child, timeout)
        if ceiling_observed or child.returncode != 0: raise RuntimeError('diagnostic_child_unresolved')
        answer, code = {'outcome': 'diagnostic_receipt_recorded', 'candidate_eligible': False}, 0
    except BaseException:
        answer, code = {'outcome': 'refused_or_unresolved', 'candidate_eligible': False}, 1
    print(json.dumps(answer, separators=(',', ':')))
    return code


if __name__ == '__main__':
    sys.exit(main())
