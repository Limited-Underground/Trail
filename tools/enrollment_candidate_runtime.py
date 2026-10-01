"""Source-pinned isolated OTCAND1 execution artifact; no device access here.

The frozen ROM Runtime requires a real capsule executable, exact four-path
sys.path and inventory-backed module origins. This additive assembly puts the
reviewed Python closure under actual policy paths; it does not alter the old
capsule, SDK, import paths or loaded module origins. No grant issuer is provided.
"""
from dataclasses import dataclass, field
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time

SOURCE_NAMES = ('enrollment_candidate_runtime.py', 'enrollment_candidate_runner.py',
    'enrollment_candidate_private_view.py', 'enrollment_candidate_operator.py',
    'enrollment_candidate_rom_adapter.py', 'enrollment_candidate_controller.py',
    'enrollment_candidate_custody.py', 'enrollment_candidate_usb_client.py',
    'ble_confirmation_trial.py', 'security_policy_execution.py',
    'security_policy_input_readback.py', 'security_policy_deadline_operator.py')
HEX64 = re.compile('[0-9a-f]{64}')


class RuntimeErrorFixed(RuntimeError):
    """Fixed, non-identifying refusal category."""


def need(value, category='assembly_refused'):
    if not value:
        raise RuntimeErrorFixed(category)


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode('ascii')


def decode(raw):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            need(key not in result, 'record_invalid')
            result[key] = value
        return result
    try:
        return json.loads(raw, object_pairs_hook=unique,
            parse_constant=lambda _: (_ for _ in ()).throw(RuntimeErrorFixed('record_invalid')))
    except RuntimeErrorFixed:
        raise
    except BaseException:
        raise RuntimeErrorFixed('record_invalid') from None


def descriptor(raw):
    return {'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}


def pin_valid(value):
    return (type(value) is dict and set(value) == {'bytes', 'sha256'}
        and type(value['bytes']) is int and 0 < value['bytes'] <= 67108864
        and type(value['sha256']) is str and HEX64.fullmatch(value['sha256']))


def regular(path, *, directory=False):
    path = Path(path)
    need(path.is_absolute(), 'path_invalid')
    for item in (path, *path.parents):
        need(not item.is_symlink() and not getattr(item, 'is_junction', lambda: False)(), 'path_invalid')
    need(path.is_dir() if directory else path.is_file(), 'path_invalid')
    return path


def read_pin(path, pin, *, private=None, allow_empty=False):
    # The admitted interpreter inventory includes exact empty package markers.
    # Other inputs (sources, private refs and images) must remain nonempty.
    empty = (allow_empty is True and type(pin) is dict and set(pin) == {'bytes', 'sha256'}
        and type(pin['bytes']) is int and pin['bytes'] == 0
        and type(pin['sha256']) is str and HEX64.fullmatch(pin['sha256']))
    need(pin_valid(pin) or empty, 'pin_invalid')
    path = regular(path)
    if private is not None:
        need(path.resolve().is_relative_to(regular(private, directory=True).resolve()), 'path_invalid')
    need(path.stat().st_size == pin['bytes'], 'file_changed')
    raw = path.read_bytes()
    need(descriptor(raw) == pin, 'file_changed')
    return raw


def _verify_manifest(path, sha, verifier):
    if verifier is None:
        from security_policy_deadline_operator import verify_manifest
        return verify_manifest(path, sha)
    return verifier(path, sha)


def _write_once(path, raw):
    need(not path.exists() and not path.is_symlink(), 'output_exists')
    with path.open('xb', buffering=0) as stream:
        need(stream.write(raw) == len(raw), 'artifact_write_failed')
        os.fsync(stream.fileno())
    need(path.read_bytes() == raw, 'artifact_write_failed')


@dataclass(frozen=True)
class Assembly:
    root: Path
    manifest_path: Path
    manifest_sha256: str
    assembly_path: Path
    assembly_sha256: str
    worktree: Path
    _manifest_raw: bytes = field(repr=False)

    @property
    def manifest(self):
        return decode(self._manifest_raw)


def _source_pins(path, sha):
    raw = regular(path).read_bytes()
    need(descriptor(raw)['sha256'] == sha and len(raw) <= 32768, 'source_pins_changed')
    value = decode(raw)
    need(type(value) is dict and set(value) == {'schema', 'files'}
        and value['schema'] == 'OT-CANDIDATE-SOURCE-PINS-1'
        and type(value['files']) is dict and set(value['files']) ==
        {'tools/' + name for name in SOURCE_NAMES}
        and all(pin_valid(pin) for pin in value['files'].values()), 'source_pins_invalid')
    return value['files'], descriptor(raw)


def assemble(base_manifest_path, base_manifest_sha256, source_root, source_pins_path,
             source_pins_sha256, destination, *, manifest_verifier=None):
    """Fresh local packaging only. Copies solely the admitted manifest closure."""
    base_path = regular(base_manifest_path)
    base_raw = base_path.read_bytes()
    need(hashlib.sha256(base_raw).hexdigest() == base_manifest_sha256, 'base_changed')
    base = _verify_manifest(base_path, base_manifest_sha256, manifest_verifier)
    source_root = regular(source_root, directory=True)
    need(source_root.resolve() == Path(base['worktree']).resolve(), 'source_root_invalid')
    private = regular(source_root / '.private', directory=True)
    source_pins_path = regular(source_pins_path)
    need(source_pins_path.resolve().is_relative_to(private.resolve()), 'path_invalid')
    sources, source_pin = _source_pins(source_pins_path, source_pins_sha256)
    destination = Path(destination)
    need(destination.is_absolute() and destination.resolve().is_relative_to(private.resolve())
        and destination != private and not destination.exists(), 'output_exists')
    regular(destination.parent, directory=True)
    need(not destination.resolve().is_relative_to(Path(base['root']).resolve()), 'output_invalid')
    manifest_path = destination.with_name(destination.name + '.manifest.json')
    sidecar_path = destination.with_name(destination.name + '.assembly.json')
    need(not manifest_path.exists() and not sidecar_path.exists(), 'output_exists')
    # All inputs are read and admitted before creating the execution artifact.
    source_bytes = {name: read_pin(source_root / name, pin) for name, pin in sources.items()}
    files = dict(base['files'])
    for name, pin in sources.items():
        target = 'policy/' + Path(name).name
        need(target not in files or files[target] == pin, 'base_policy_conflict')
        files[target] = pin
    destination.mkdir()
    for name, pin in base['files'].items():
        raw = read_pin(Path(base['root']) / name, pin, allow_empty=True)
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        _write_once(target, raw)
    for name, raw in source_bytes.items():
        target = destination / 'policy' / Path(name).name
        if target.exists():
            need(target.read_bytes() == raw, 'base_policy_conflict')
        else:
            _write_once(target, raw)
    manifest = dict(base, root=str(destination), files=files)
    manifest_raw = canonical(manifest) + b'\n'
    _write_once(manifest_path, manifest_raw)
    sidecar = {'schema': 'OT-CANDIDATE-ASSEMBLY-1', 'root': str(destination),
        'source_root': str(source_root), 'base': dict(path=str(base_path), **descriptor(base_raw)),
        'source_pins': dict(path=str(source_pins_path), **source_pin), 'sources': sources,
        'runtime': dict(path=str(manifest_path), **descriptor(manifest_raw))}
    sidecar_raw = canonical(sidecar) + b'\n'
    _write_once(sidecar_path, sidecar_raw)
    # Reverify original and assembled bytes; do not modify or clean failed artifacts.
    return verify_assembly(sidecar_path, descriptor(sidecar_raw)['sha256'],
                           manifest_verifier=manifest_verifier)


def assert_isolated(manifest):
    root = Path(manifest['root']).resolve()
    need(sys.flags.isolated and sys.flags.no_site and sys.dont_write_bytecode
        and Path(sys.executable).resolve() == root / 'python.exe'
        and sys.version_info[:3] == (3, 14, 6)
        and [Path(p).resolve() for p in sys.path] ==
        [root / name for name in ('Lib', 'DLLs', 'packages', 'policy')], 'isolated_caller_required')
    for module in tuple(sys.modules.values()):
        origin = getattr(module, '__file__', None)
        if origin:
            path = Path(origin).resolve()
            need(path.is_relative_to(root) and path.relative_to(root).as_posix()
                in manifest['files'], 'isolated_caller_required')


def verify_assembly(assembly_path, assembly_sha256, *, manifest_verifier=None, require_isolated=False):
    path = regular(assembly_path)
    raw = path.read_bytes()
    need(len(raw) <= 32768 and hashlib.sha256(raw).hexdigest() == assembly_sha256, 'assembly_changed')
    value = decode(raw)
    need(type(value) is dict and set(value) == {'schema', 'root', 'source_root', 'base',
        'source_pins', 'sources', 'runtime'} and value['schema'] == 'OT-CANDIDATE-ASSEMBLY-1', 'assembly_invalid')
    worktree = regular(value['source_root'], directory=True)
    private = regular(worktree / '.private', directory=True)
    need(path.resolve().is_relative_to(private.resolve()), 'path_invalid')
    for name in ('base', 'source_pins', 'runtime'):
        ref = value[name]
        need(type(ref) is dict and set(ref) == {'path', 'bytes', 'sha256'}, 'assembly_invalid')
        read_pin(ref['path'], {k: ref[k] for k in ('bytes', 'sha256')}, private=private)
    base = _verify_manifest(value['base']['path'], value['base']['sha256'], manifest_verifier)
    sources, _ = _source_pins(value['source_pins']['path'], value['source_pins']['sha256'])
    need(value['sources'] == sources and worktree.resolve() == Path(base['worktree']).resolve(), 'assembly_invalid')
    expected = dict(base['files'])
    for name, pin in sources.items():
        read_pin(worktree / name, pin)
        target = 'policy/' + Path(name).name
        need(target not in expected or expected[target] == pin, 'base_policy_conflict')
        expected[target] = pin
    runtime = value['runtime']
    manifest = _verify_manifest(runtime['path'], runtime['sha256'], manifest_verifier)
    need(manifest['files'] == expected and Path(manifest['root']) == Path(value['root'])
        and manifest['worktree'] == base['worktree'] and manifest['versions'] == base['versions']
        and manifest['powershell'] == base['powershell'] and manifest['schema'] == base['schema'], 'assembly_invalid')
    root = regular(value['root'], directory=True)
    need(root.resolve().is_relative_to(private.resolve()) and root != private
        and root.resolve() != Path(base['root']).resolve(), 'assembly_invalid')
    for name, pin in expected.items():
        read_pin(root / name, pin, allow_empty=True)
    need({p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()} == set(expected), 'assembly_invalid')
    if require_isolated:
        assert_isolated(manifest)
    return Assembly(root, Path(runtime['path']), runtime['sha256'], path, assembly_sha256,
                    worktree, canonical(manifest))


def launch(assembly_path, assembly_sha256, mode, package_path=None, package_sha256=None, *,
           subprocess_run=subprocess.run, monotonic=time.monotonic, utc=time.time,
           assembly_verifier=verify_assembly):
    """Launch the actual artifact; this function never accesses devices itself."""
    need(mode in ('help', 'preflight', 'execute', 'recover'), 'mode_invalid')
    from enrollment_candidate_controller import Clock
    clock = Clock(monotonic)
    start = clock.now()
    assembly = assembly_verifier(assembly_path, assembly_sha256)
    args = [str(assembly.root / 'python.exe'), '-I', '-S', '-B',
            str(assembly.root / 'policy/enrollment_candidate_runner.py')]
    timeout = 120.0
    ceiling = start + timeout
    if mode == 'help':
        need(package_path is None and package_sha256 is None, 'package_invalid')
        args += ['--help']
    else:
        args += ['--mode', mode, '--assembly', str(assembly.assembly_path),
                 '--assembly-sha256', assembly.assembly_sha256]
        if package_path is not None:
            from enrollment_candidate_runner import preflight
            prepared = preflight(package_path, package_sha256, utc=utc, monotonic=monotonic,
                                 assembly_verifier=assembly_verifier)
            need(prepared.assembly.assembly_sha256 == assembly.assembly_sha256, 'assembly_invalid')
            args += ['--package', str(prepared.package_path), '--package-sha256', prepared.package_sha256]
            if mode in ('execute', 'recover'):
                need(mode == prepared.operation, 'mode_invalid')
                clock.check(prepared.restore_deadline)
                remaining = prepared.restore_deadline - clock.now()
                need(remaining > 0, 'deadline_expired')
                timeout = remaining
                ceiling = prepared.restore_deadline
                args += ['--execute-deadline', str(prepared.execute_deadline),
                         '--restore-deadline', str(prepared.restore_deadline)]
        else:
            need(mode == 'preflight' and package_sha256 is None, 'package_invalid')
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('PYTHON', 'ESPTOOL_'))}
    env['ESPTOOL_CFGFILE'] = str(assembly.root / 'esptool.cfg')
    try:
        clock.check(ceiling)
        timeout = ceiling - clock.now()
        need(timeout > 0, 'deadline_expired')
        result = subprocess_run(args, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, timeout=timeout, check=False, cwd=str(assembly.root), env=env)
        clock.check(ceiling)
        need(type(result.stdout) is bytes and len(result.stdout) <= 32768
            and type(result.stderr) is bytes and len(result.stderr) <= 32768, 'child_refused')
        if mode == 'help':
            need(result.returncode == 0, 'child_refused')
            return {'schema': 'OT-CANDIDATE-LAUNCH-1', 'mode': 'help', 'outcome': 'available'}
        answer = decode(result.stdout)
        from enrollment_candidate_runner import PUBLIC_CATEGORIES
        need(type(answer) is dict and set(answer) == {'schema', 'mode', 'outcome', 'category'}
            and answer['schema'] == 'OT-CANDIDATE-LAUNCH-1' and answer['mode'] == mode
            and answer['outcome'] in ({'ready', 'failed', 'held'} if mode == 'preflight' else
                {'passed', 'failed', 'held'} if mode == 'execute' else {'recovered', 'failed', 'held'})
            and (answer['category'] is None or (type(answer['category']) is str
                and answer['category'] in PUBLIC_CATEGORIES)), 'child_refused')
        need(answer['outcome'] not in ('ready', 'passed') or answer['category'] is None, 'child_refused')
        need((result.returncode == 0) == (answer['outcome'] in ('ready', 'passed', 'recovered')), 'child_refused')
        return answer
    except RuntimeErrorFixed:
        raise
    except BaseException:
        raise RuntimeErrorFixed('child_refused') from None


def main(argv=None):
    """Host-only artifact assembly; no imports of serial, SDK or native view."""
    class Parser(argparse.ArgumentParser):
        def error(self, message):
            raise RuntimeErrorFixed('assembly_refused')
    parser = Parser(description='Assemble the exact admitted OTCAND1 runtime closure; no device access.')
    parser.add_argument('--base-manifest', required=True)
    parser.add_argument('--base-manifest-sha256', required=True)
    parser.add_argument('--source-root', required=True)
    parser.add_argument('--source-pins', required=True)
    parser.add_argument('--source-pins-sha256', required=True)
    parser.add_argument('--destination', required=True)
    try:
        args = parser.parse_args(argv)
        result = assemble(args.base_manifest, args.base_manifest_sha256, args.source_root,
            args.source_pins, args.source_pins_sha256, args.destination)
        answer = {'schema': 'OT-CANDIDATE-PACKAGING-1', 'outcome': 'assembled', 'category': None,
                  'manifest_sha256': result.manifest_sha256, 'assembly_sha256': result.assembly_sha256}
    except SystemExit:
        raise
    except BaseException:
        answer = {'schema': 'OT-CANDIDATE-PACKAGING-1', 'outcome': 'failed', 'category': 'assembly_refused',
                  'manifest_sha256': None, 'assembly_sha256': None}
    print(json.dumps(answer, separators=(',', ':')))
    return 0 if answer['outcome'] == 'assembled' else 1


if __name__ == '__main__':
    sys.exit(main())
