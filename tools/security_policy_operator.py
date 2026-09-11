"""OT-189 isolated operator entry point. Import is inert; physical modes need authority.

The reviewed PowerShell verifier and its OS/.NET host are the external trust base.
They verify capsule bytes before Python startup. This worker rechecks before imports.
A malicious concurrent local administrator is outside this integrity boundary.
"""
from pathlib import Path
import argparse
import hashlib
import importlib
from importlib.machinery import PathFinder
import json
import os
import re
import subprocess
import sys
import tempfile

HEX = re.compile(r'[0-9a-f]{64}\Z')
PTH = b'Lib\nDLLs\npackages\npolicy\n'
CONFIG = b'[esptool]\n'
POLICY = ('bundle', 'hardware', 'endpoint', 'execution', 'capture')

class OperatorError(RuntimeError):
    pass

def need(value):
    if not value:
        raise OperatorError('operator_refused')

def digest(raw):
    return hashlib.sha256(raw).hexdigest()

def decode(raw):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            need(key not in result)
            result[key] = value
        return result
    try:
        return json.loads(raw, object_pairs_hook=unique,
                          parse_constant=lambda _: (_ for _ in ()).throw(ValueError()))
    except Exception:
        raise OperatorError('operator_refused') from None

def regular(path):
    path = Path(path)
    need(path.is_absolute())
    for part in (path, *path.parents):
        need(not part.is_symlink() and not getattr(part, 'is_junction', lambda: False)())
    need(path.is_file())
    return path

def load_request(path, expected_sha):
    try:
        need(type(expected_sha) is str and HEX.fullmatch(expected_sha))
        raw = regular(path).read_bytes()
        need(len(raw) <= 8 * 1024 * 1024 and digest(raw) == expected_sha)
        result = decode(raw)
        need(type(result) is dict)
        return result
    except Exception:
        raise OperatorError('operator_refused') from None

def descriptor(path, expected):
    need(type(expected) is dict and set(expected) == {'bytes', 'sha256'})
    need(type(expected['bytes']) is int and expected['bytes'] >= 0
         and type(expected['sha256']) is str and HEX.fullmatch(expected['sha256']))
    raw = regular(path).read_bytes()
    need(len(raw) == expected['bytes'] and digest(raw) == expected['sha256'])

def verify_manifest(path, expected_sha):
    try:
        obj = load_request(path, expected_sha)
        need(set(obj) == {'schema', 'root', 'worktree', 'files', 'versions', 'powershell'}
             and obj['schema'] == 'OT189-RUNTIME-1')
        root, worktree = Path(obj['root']), Path(obj['worktree'])
        need(root.is_absolute() and worktree.is_absolute() and root.is_dir()
             and root.resolve().is_relative_to((worktree / '.private').resolve())
             and root != worktree / '.private')
        for part in (root, *root.parents):
            need(not part.is_symlink() and not getattr(part, 'is_junction', lambda: False)())
        files = obj['files']
        need(type(files) is dict and 0 < len(files) <= 50000)
        folded = set()
        for name, value in files.items():
            need(type(name) is str and name and '\\' not in name and ':' not in name
                 and not name.startswith('/') and all(x not in ('', '.', '..') for x in name.split('/'))
                 and not name.lower().endswith(('.pyc', '.pyo', '.pth')) and name.casefold() not in folded)
            folded.add(name.casefold())
            descriptor(root / name, value)
        observed = set()
        for item in root.rglob('*'):
            need(not item.is_symlink() and not getattr(item, 'is_junction', lambda: False)())
            if item.is_file():
                observed.add(item.relative_to(root).as_posix())
        need(observed == set(files) and (root / 'python314._pth').read_bytes() == PTH
             and (root / 'esptool.cfg').read_bytes() == CONFIG)
        need(type(obj['versions']) is dict and obj['versions'].get('python') == '3.14.6'
             and obj['versions'].get('esptool') == '5.3.1' and obj['versions'].get('pyserial') == '3.5')
        host = obj['powershell']
        need(type(host) is dict and set(host) == {'path', 'bytes', 'sha256'})
        descriptor(Path(host['path']), {k: host[k] for k in ('bytes', 'sha256')})
        for name in ('python.exe', 'python314.dll', 'esptool.cfg', 'policy/security_policy_operator.py',
                     'policy/Invoke-SecurityPolicyOperator.ps1', *('policy/security_policy_'+x+'.py' for x in POLICY)):
            need(name in files)
        return obj
    except Exception:
        raise OperatorError('operator_refused') from None

def child_args(manifest, manifest_path, manifest_sha, mode, request_path=None, request_sha=None):
    need(mode in ('Probe', 'Version', 'Operator', 'Rom', 'Backup', 'BackupRom'))
    args = [manifest['powershell']['path'], '-NoProfile', '-NonInteractive', '-File',
            str(Path(manifest['root']) / 'policy' / 'Invoke-SecurityPolicyOperator.ps1'),
            '-Manifest', str(manifest_path), '-ManifestSha256', manifest_sha, '-Mode', mode]
    if mode in ('Operator', 'Rom', 'Backup', 'BackupRom'):
        need(request_path is not None and type(request_sha) is str and HEX.fullmatch(request_sha))
        args += ['-Request', str(request_path), '-RequestSha256', request_sha]
    else:
        need(request_path is None and request_sha is None)
    return args

def private_file(ctx, path):
    p = regular(path)
    need(p.resolve().is_relative_to((Path(ctx['manifest']['worktree']) / '.private').resolve()))
    return p

def operator_request(ctx, path, sha):
    req = load_request(private_file(ctx, path), sha)
    need(set(req) == {'schema', 'runtime_sha256', 'operation', 'package_path', 'package_sha256',
                     'grant_path', 'grant_sha256', 'origin_attempt'}
         and req['schema'] == 'OT189-OPERATOR-1' and req['runtime_sha256'] == ctx['sha']
         and req['operation'] in ('execute', 'recover'))
    need((req['operation'] == 'execute' and req['origin_attempt'] is None) or
         (req['operation'] == 'recover' and type(req['origin_attempt']) is str
          and re.fullmatch(r'[0-9a-f]{32}', req['origin_attempt'])))
    for prefix in ('package', 'grant'):
        load_request(private_file(ctx, req[prefix+'_path']), req[prefix+'_sha256'])
    return req

def audit_loaded_modules(ctx):
    root = Path(ctx['manifest']['root']).resolve()
    files = ctx['manifest']['files']
    for module in tuple(sys.modules.values()):
        origin = getattr(module, '__file__', None)
        if origin is None:
            continue
        p = Path(origin).resolve()
        need(p.is_relative_to(root) and p.relative_to(root).as_posix() in files)
    return True

def runtime_policy(ctx):
    extra = ('backup', 'backup_operator')
    present = [('policy/security_policy_'+s+'.py') in ctx['manifest']['files'] for s in extra]
    need(not any(present) or all(present))
    diagnostics = 'policy/security_policy_diagnostics.py' in ctx['manifest']['files']
    need(not diagnostics or all(present))
    return POLICY + (extra if all(present) else ()) + (('diagnostics',) if diagnostics else ())

def admit_origins(ctx):
    root = Path(ctx['manifest']['root']).resolve()
    expected = {'esptool': root / 'packages/esptool/__init__.py',
                'serial': root / 'packages/serial/__init__.py'}
    expected.update({'security_policy_'+s: root / ('policy/security_policy_'+s+'.py') for s in runtime_policy(ctx)})
    for name, path in expected.items():
        spec = PathFinder.find_spec(name, sys.path)
        need(spec is not None and spec.origin is not None and Path(spec.origin).resolve() == path)
        if name in sys.modules:
            need(Path(getattr(sys.modules[name], '__file__', '')).resolve() == path)
    return True

def isolated_environment(ctx):
    root = Path(ctx['manifest']['root']).resolve()
    need(sys.flags.isolated and sys.flags.no_site and sys.dont_write_bytecode
         and sys.version_info[:3] == (3, 14, 6)
         and Path(sys.executable).resolve() == root / 'python.exe')
    expected = [root / x for x in ('Lib', 'DLLs', 'packages', 'policy')]
    need([Path(x).resolve() for x in sys.path] == expected)
    # Never add site directories or execute .pth files; the verified _pth is exact.
    need(os.environ.get('ESPTOOL_CFGFILE') == str(root / 'esptool.cfg'))
    need(not any(k.upper().startswith('ESPTOOL_') and k.upper() != 'ESPTOOL_CFGFILE' for k in os.environ))
    audit_loaded_modules(ctx)

def version():
    tool = importlib.import_module('esptool')
    serial = importlib.import_module('serial')
    need(tool.__version__ == '5.3.1' and serial.__version__ == '3.5')
    return {'esptool': tool.__version__, 'pyserial': serial.__version__}

def probe(ctx):
    versions = version()
    for suffix in runtime_policy(ctx):
        importlib.import_module('security_policy_' + suffix)
    audit_loaded_modules(ctx)
    result = subprocess.run(child_args(ctx['manifest'], ctx['path'], ctx['sha'], 'Version'),
                            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            timeout=120, check=False, cwd=ctx['manifest']['root'])
    need(result.returncode == 0 and len(result.stdout) < 4096 and decode(result.stdout) == versions)
    return {'status': 'pass', 'files_verified': len(ctx['manifest']['files']),
            'runtime_sha256': ctx['sha'], 'versions': versions, 'parent_import_origins': 'verified',
            'isolated_child': 'verified', 'hardware_access': False}

def make_transport(ctx, request_path, request_sha):
    hardware = importlib.import_module('security_policy_hardware')
    class VerifiedRomTransport(hardware.RomTransport):
        @hardware.safe
        def command(self, route, operation, *, reset=False):
            # The child cannot use ordinary `python -m esptool` startup.
            argv = ['--chip', 'esp32s3', '--port', route, '--baud', '115200', '--before',
                    'default-reset', '--after', 'hard-reset' if reset else 'no-reset', '--no-stub'] + operation
            req = {'schema': 'OT189-ROM-1', 'runtime_sha256': ctx['sha'], 'operator_request':
                   {'path': str(request_path), 'sha256': request_sha}, 'argv': argv}
            raw = json.dumps(req, sort_keys=True, separators=(',', ':')).encode()
            with tempfile.TemporaryDirectory(prefix='ot189-rom-', dir=self.private_root) as directory:
                path = Path(directory) / 'request.json'
                path.write_bytes(raw)
                result = subprocess.run(child_args(ctx['manifest'], ctx['path'], ctx['sha'], 'Rom', path, digest(raw)),
                                        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                        timeout=240, check=False, cwd=ctx['manifest']['root'])
            hardware.require(result.returncode == 0 and len(result.stdout) <= 1048576, 'rom_command_failed')
            return result.stdout
    return VerifiedRomTransport(Path(ctx['manifest']['worktree']) / '.private')

def make_backend(ctx, bindings, transport):
    if 'diagnostics' in runtime_policy(ctx):
        module = importlib.import_module('security_policy_diagnostics')
        return module.make_backend(bindings, transport=transport)
    return importlib.import_module('security_policy_hardware').Backend(bindings, transport=transport)

def with_diagnostics(ctx, backend, result):
    if 'diagnostics' not in runtime_policy(ctx):
        return result
    report = {'schema': 'OT192-RECEIPT-DIAGNOSTICS-1', 'available': False, 'roles': []}
    try:
        rows = importlib.import_module('security_policy_diagnostics').summary(backend)
        report.update(available=all(row['diagnostics_available'] for row in rows), roles=rows)
    except Exception:
        pass
    return {**result, 'receipt_diagnostics': report}

def run_operator(ctx, request_path, request_sha):
    req = operator_request(ctx, request_path, request_sha)
    bundle = importlib.import_module('security_policy_bundle')
    execution = importlib.import_module('security_policy_execution')
    hardware = importlib.import_module('security_policy_hardware')
    package = load_request(private_file(ctx, req['package_path']), req['package_sha256'])
    # Frozen package verification compares its live source pins with admitted copied code.
    for suffix in POLICY:
        name = 'tools/security_policy_' + suffix + '.py'
        need(package['sources'][name] == ctx['manifest']['files']['policy/security_policy_'+suffix+'.py'])
    root = Path(ctx['manifest']['worktree'])
    bindings = tuple(hardware.RoleBinding(r['role'], r['private_route'], r['private_identity']) for r in package['roles'])
    transport = make_transport(ctx, request_path, request_sha)
    backend = make_backend(ctx, bindings, transport)
    authority = execution.FileAuthority(root, Path(req['grant_path']), req['grant_sha256'])
    audit_loaded_modules(ctx)
    if 'backup' in runtime_policy(ctx):
        def dispatch(locked_root):
            if req['operation'] == 'execute':
                # A held backup requires the typed single-use handoff wrapper.
                need(not (locked_root / '.private' / 'security-policy-backup-active.lock').exists())
                return execution.execute.__wrapped__(locked_root, package, authority, backend)
            return execution.recover.__wrapped__(locked_root, package, authority, backend,
                                                  origin_attempt=req['origin_attempt'])
        result = execution.single_process(dispatch)(root)
    elif req['operation'] == 'execute':
        result = execution.execute(root, package, authority, backend)
    else:
        result = execution.recover(root, package, authority, backend, origin_attempt=req['origin_attempt'])
    return with_diagnostics(ctx, backend, result)

def run_rom(ctx, path, sha):
    req = load_request(private_file(ctx, path), sha)
    need(set(req) == {'schema', 'runtime_sha256', 'operator_request', 'argv'}
         and req['schema'] == 'OT189-ROM-1' and req['runtime_sha256'] == ctx['sha'])
    source = req['operator_request']
    need(type(source) is dict and set(source) == {'path', 'sha256'})
    op = operator_request(ctx, source['path'], source['sha256'])
    execution = importlib.import_module('security_policy_execution')
    bundle = importlib.import_module('security_policy_bundle')
    root = Path(ctx['manifest']['worktree'])
    package = load_request(private_file(ctx, op['package_path']), op['package_sha256'])
    package_sha = bundle.digest(package)
    authority = execution.FileAuthority(root, Path(op['grant_path']), op['grant_sha256'])
    grant = authority.validate(package_sha, operation=op['operation'], origin_attempt=op['origin_attempt'])
    # Subordinate ROM work requires the already-consumed grant and active journal.
    origin = op['origin_attempt'] or grant.attempt
    journal = execution.Journal(root, origin, package_sha)
    used = load_request_digestless(root / '.private' / ('security-policy-grant-'+grant.attempt+'.used'))
    need(used == {'package_sha256': package_sha, 'grant': grant.raw_sha256})
    data = bundle.verify(root, package, recovery=op['operation'] == 'recover')
    need(type(req['argv']) is list and len(req['argv']) > 3)
    route = req['argv'][3]
    roles = [r['role'] for r in data['roles'] if r['private_route'] == route]
    need(len(roles) == 1)
    events = [e['event'] for e in journal.events if e.get('role') == roles[0]]
    need(events and not any(e['event'] == 'finished' for e in journal.events))
    op['_phase'] = events[-1]
    argv = validate_rom_argv(ctx, op, data, req['argv'])
    tool = importlib.import_module('esptool')
    audit_loaded_modules(ctx)
    tool.main(argv)

def validate_rom_argv(ctx, op, data, argv):
    need(type(argv) is list and all(type(x) is str for x in argv) and 12 <= len(argv) <= 16)
    need(argv[:3] == ['--chip', 'esp32s3', '--port']
         and argv[4:9] == ['--baud', '115200', '--before', 'default-reset', '--after']
         and argv[9] in ('hard-reset', 'no-reset') and argv[10] == '--no-stub')
    matches = [r for r in data['roles'] if r['private_route'] == argv[3]]
    need(len(matches) == 1)
    row = matches[0]
    operation = argv[11:]
    name = operation[0]
    need(name in ('read-mac', 'flash-id', 'read-flash', 'write-flash', 'run'))
    need(argv[9] == ('hard-reset' if name == 'run' else 'no-reset'))
    phase = op.get('_phase')
    need(phase in ('preflight_intent', 'candidate_write_intent', 'candidate_boot_intent',
                   'restore_intent', 'original_boot_intent'))
    if name in ('read-mac', 'flash-id', 'run'):
        need(len(operation) == 1)
        if name == 'run':
            need(phase in ('candidate_boot_intent', 'original_boot_intent'))
        return argv
    private = Path(ctx['manifest']['worktree']) / '.private'
    def region_file(value, prefix, existing):
        path = Path(value)
        need(path.is_absolute() and path.name == 'region.bin' and path.parent.parent == private
             and path.parent.name.startswith(prefix) and path.parent.is_dir())
        for part in (path, *path.parents):
            need(not part.is_symlink() and not getattr(part, 'is_junction', lambda: False)())
        need(path.is_file() if existing else not path.exists())
        return path
    if name == 'read-flash':
        need(len(operation) == 4 and phase in ('preflight_intent', 'candidate_write_intent', 'restore_intent'))
        offset, size = int(operation[1], 0), int(operation[2])
        need((offset, size) in {(0, 32768), (32768, 4096), (36864, 8192), (53248, 12288), (65536, 589824)})
        region_file(operation[3], 'ot188-read-', False)
    else:
        need(len(operation) == 5 and operation[1:3] == ['--flash-size', '16MB'])
        offset = int(operation[3], 0)
        raw = region_file(operation[4], 'ot188-write-', True).read_bytes()
        need(offset in (53248, 65536))
        need(phase in ('candidate_write_intent', 'restore_intent'))
        admitted = ([row['nvs']] if offset == 53248 else [row['application']]) if phase == 'restore_intent' else []
        if phase == 'candidate_write_intent' and offset == 65536 and op['operation'] == 'execute':
            need(type(data['candidate']) is bytes and len(data['candidate']) <= 589824)
            admitted.append(data['candidate'].ljust(589824, b'\xff'))
        need(len(raw) == (12288 if offset == 53248 else 589824) and raw in admitted)
    return argv

def load_request_digestless(path):
    return decode(regular(path).read_bytes())

def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument('--manifest', required=True)
    parser.add_argument('--sha256', required=True)
    parser.add_argument('--mode', choices=('probe', 'version', 'operator', 'rom', 'backup', 'backuprom'), required=True)
    parser.add_argument('--request')
    parser.add_argument('--request-sha256')
    args = parser.parse_args(argv)
    try:
        ctx = {'manifest': verify_manifest(args.manifest, args.sha256), 'path': args.manifest, 'sha': args.sha256}
        isolated_environment(ctx)
        admit_origins(ctx)
        if args.mode in ('backup', 'backuprom'):
            need('backup' in runtime_policy(ctx))
            controller = importlib.import_module('security_policy_backup_operator')
            audit_loaded_modules(ctx)
            if args.mode == 'backuprom':
                controller.run_rom(ctx, args.request, args.request_sha256)
                return 0
            result = controller.run_operator(ctx, args.request, args.request_sha256)
            print(json.dumps(result, sort_keys=True))
            return 0
        if args.mode == 'rom':
            run_rom(ctx, args.request, args.request_sha256)
            return 0
        if args.mode == 'probe':
            result = probe(ctx)
        elif args.mode == 'version':
            result = version()
            audit_loaded_modules(ctx)
        else:
            result = run_operator(ctx, args.request, args.request_sha256)
        print(json.dumps(result, sort_keys=True))
        return 0
    except Exception:
        print('operator_refused', file=sys.stderr)
        return 1

if __name__ == '__main__':
    raise SystemExit(main())
