"""OT-198 isolated stage operator entry point. Import is inert; physical modes need authority.

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
POLICY = ('bundle','hardware','endpoint','execution','capture','operator','backup',
          'backup_operator','diagnostics','runtime_bundle','input_bundle','input_execution',
          'input_operator','input_backup_operator','input_runtime_bundle','input_readback','input_observation','input_timing')

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
             and obj['schema'] == 'OT198-INPUT-RUNTIME-1')
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
        for name in ('python.exe', 'python314.dll', 'esptool.cfg', 'policy/security_policy_input_operator.py',
                     'policy/Invoke-SecurityPolicyInputOperator.ps1','policy/Invoke-SecurityPolicyOperator.ps1', *('policy/security_policy_'+x+'.py' for x in POLICY)):
            need(name in files)
        return obj
    except Exception:
        raise OperatorError('operator_refused') from None

def child_args(manifest, manifest_path, manifest_sha, mode, request_path=None, request_sha=None):
    need(mode in ('Probe', 'Version', 'InputOperator', 'InputRom', 'InputBackup', 'InputBackupRom'))
    args = [manifest['powershell']['path'], '-NoProfile', '-NonInteractive', '-File',
            str(Path(manifest['root']) / 'policy' / 'Invoke-SecurityPolicyInputOperator.ps1'),
            '-Manifest', str(manifest_path), '-ManifestSha256', manifest_sha, '-Mode', mode]
    if mode in ('InputOperator', 'InputRom', 'InputBackup', 'InputBackupRom'):
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
         and req['schema'] == 'OT198-INPUT-OPERATOR-1' and req['runtime_sha256'] == ctx['sha']
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
    need(all('policy/security_policy_'+suffix+'.py' in ctx['manifest']['files'] for suffix in POLICY))
    return POLICY


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
            req = {'schema': 'OT198-INPUT-ROM-1', 'runtime_sha256': ctx['sha'], 'operator_request':
                   {'path': str(request_path), 'sha256': request_sha}, 'argv': argv, 'purpose':getattr(self,'observation_purpose',None)}
            raw = json.dumps(req, sort_keys=True, separators=(',', ':')).encode()
            with tempfile.TemporaryDirectory(prefix='ot198-input-rom-', dir=self.private_root) as directory:
                path = Path(directory) / 'request.json'
                path.write_bytes(raw)
                result = subprocess.run(child_args(ctx['manifest'], ctx['path'], ctx['sha'], 'InputRom', path, digest(raw)),
                                        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                        timeout=240, check=False, cwd=ctx['manifest']['root'])
            hardware.require(result.returncode == 0 and len(result.stdout) <= 1048576, 'rom_command_failed')
            return result.stdout
    return VerifiedRomTransport(Path(ctx['manifest']['worktree']) / '.private')

def make_backend(ctx, bindings, transport):
    module=importlib.import_module('security_policy_diagnostics')
    backend=module.make_backend(bindings,transport=transport)
    backend.input_timing=importlib.import_module('security_policy_input_timing').Recorder()
    def read_observation(role,offset,size,intent_descriptor):
        need(offset==53248 and size==12288 and type(intent_descriptor) is dict)
        with backend.lock:
            need(getattr(transport,'observation_purpose',None) is None)
            transport.observation_purpose=intent_descriptor
            try:return backend.read(role,offset,size)
            finally:transport.observation_purpose=None
    backend.read_observation=read_observation
    return backend


def with_diagnostics(ctx, backend, result):
    if 'diagnostics' not in runtime_policy(ctx):
        return result
    report = {'schema': 'OT192-RECEIPT-DIAGNOSTICS-1', 'available': False, 'roles': []}
    try:
        rows = importlib.import_module('security_policy_diagnostics').summary(backend)
        report.update(available=all(row['diagnostics_available'] for row in rows), roles=rows)
    except Exception:
        pass
    return {**result, 'receipt_diagnostics': report, 'input_timing': importlib.import_module('security_policy_input_timing').summary(getattr(backend,'input_timing',None))}

def bind_sources(ctx, package):
    need(package.get('runtime_sha256') == ctx['sha'])
    for suffix in POLICY:
        name='security_policy_'+suffix+'.py'
        need(package['sources']['tools/'+name] == ctx['manifest']['files']['policy/'+name])
    for launcher in ('Invoke-SecurityPolicyInputOperator.ps1','Invoke-SecurityPolicyOperator.ps1'):
        need(package['sources']['tools/'+launcher] == ctx['manifest']['files']['policy/'+launcher])


def run_operator(ctx, request_path, request_sha):
    req = operator_request(ctx, request_path, request_sha)
    bundle = importlib.import_module('security_policy_input_bundle')
    execution = importlib.import_module('security_policy_input_execution')
    hardware = importlib.import_module('security_policy_hardware')
    package = load_request(private_file(ctx, req['package_path']), req['package_sha256'])
    bind_sources(ctx, package)
    need(req['operation']=='recover')  # Execution requires typed fresh-backup handoff.
    root = Path(ctx['manifest']['worktree'])
    bindings = tuple(hardware.RoleBinding(r['role'], r['private_route'], r['private_identity']) for r in package['roles'])
    transport = make_transport(ctx, request_path, request_sha)
    backend = make_backend(ctx, bindings, transport)
    authority = execution.FileAuthority(root, Path(req['grant_path']), req['grant_sha256'], runtime_sha256=ctx['sha'])
    audit_loaded_modules(ctx)
    result = execution.recover(root,package,authority,backend,origin_attempt=req['origin_attempt'])
    return with_diagnostics(ctx, backend, result)

def admit_journal_authority(op,grant,journal):
    rows=journal.events
    need(rows and rows[0]['event']=='created')
    recoveries=[row for row in rows if row['event']=='recovery_authorized']
    if op['operation']=='execute':
        need(not recoveries and rows[0]['attempt']==grant.attempt and rows[0]['grant']==grant.raw_sha256)
    else:
        need(recoveries and recoveries[-1]['attempt']==grant.attempt and recoveries[-1]['grant']==grant.raw_sha256)


def admit_observation_purpose(op,events,argv,purpose):
    if op['operation']=='recover':need(purpose is None)
    nvs_read=argv[11]=='read-flash' and tuple(int(x,0) for x in argv[12:14])==(53248,12288)
    if op['operation']=='execute' and op['_phase']=='restore_intent' and nvs_read and 'candidate_boot_intent' in events and events.count('restore_intent')==1:
        need(purpose is not None)


def run_rom(ctx, path, sha):
    req = load_request(private_file(ctx, path), sha)
    need(set(req) == {'schema', 'runtime_sha256', 'operator_request', 'argv', 'purpose'}
         and req['schema'] == 'OT198-INPUT-ROM-1' and req['runtime_sha256'] == ctx['sha'])
    source = req['operator_request']
    need(type(source) is dict and set(source) == {'path', 'sha256'})
    op = operator_request(ctx, source['path'], source['sha256'])
    execution = importlib.import_module('security_policy_input_execution')
    bundle = importlib.import_module('security_policy_input_bundle')
    root = Path(ctx['manifest']['worktree'])
    package = load_request(private_file(ctx, op['package_path']), op['package_sha256'])
    bind_sources(ctx, package)
    package_sha = bundle.digest(package)
    authority = execution.FileAuthority(root, Path(op['grant_path']), op['grant_sha256'], runtime_sha256=ctx['sha'])
    grant = authority.validate(package_sha, operation=op['operation'], origin_attempt=op['origin_attempt'])
    # Subordinate ROM work requires the already-consumed grant and active journal.
    origin = op['origin_attempt'] or grant.attempt
    journal = execution.Journal(root, origin, package_sha)
    admit_journal_authority(op,grant,journal)
    used = load_request_digestless(execution.used_path(root,grant.attempt))
    need(used == {'package_sha256': package_sha, 'grant': grant.raw_sha256, 'runtime_sha256':ctx['sha']})
    data = bundle.verify(root, package, recovery=op['operation'] == 'recover')
    need(type(req['argv']) is list and len(req['argv']) > 3)
    route = req['argv'][3]
    roles = [r['role'] for r in data['roles'] if r['private_route'] == route]
    need(len(roles) == 1)
    events = [e['event'] for e in journal.events if e.get('role') == roles[0]]
    need(events and not any(e['event'] == 'finished' for e in journal.events))
    op['_phase'] = events[-1]
    argv = validate_rom_argv(ctx, op, data, req['argv'])
    purpose=req['purpose']
    admit_observation_purpose(op,events,argv,purpose)
    if purpose is not None:
        need(op['operation']=='execute' and op['_phase']=='restore_intent')
        need(argv[11] in ('read-mac','flash-id','read-flash'))
        if argv[11]=='read-flash':need(tuple(int(x,0) for x in argv[12:14])==(53248,12288))
        row=next(r for r in data['roles'] if r['role']==roles[0])
        execution.admit_observation_child(root,package_sha,grant,journal,row,purpose,
            runtime_sha256=ctx['sha'],consume=argv[11]=='read-flash')
    # Consume the exact subordinate invocation before reaching esptool. Mutation
    # tokens additionally reject a rewritten request for the same phase/action.
    claim=digest((str(Path(path).resolve())+'|'+sha).encode())
    execution.exclusive(execution.private_path(root,'ot198-input-child-'+claim+'.used'),{'request_sha256':sha})
    if argv[11] in ('write-flash','run'):
        action=argv[11]+(':'+argv[14] if argv[11]=='write-flash' else '')
        marker=digest((grant.attempt+'|'+roles[0]+'|'+op['_phase']+'|'+action).encode())
        execution.exclusive(execution.private_path(root,'ot198-input-mutation-'+marker+'.used'),{'grant':grant.raw_sha256})
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
    parser.add_argument('--mode', choices=('probe','version','inputoperator','inputrom','inputbackup','inputbackuprom'), required=True)
    parser.add_argument('--request')
    parser.add_argument('--request-sha256')
    args = parser.parse_args(argv)
    try:
        ctx = {'manifest': verify_manifest(args.manifest, args.sha256), 'path': args.manifest, 'sha': args.sha256}
        isolated_environment(ctx)
        admit_origins(ctx)
        if args.mode in ('inputbackup','inputbackuprom'):
            need('backup' in runtime_policy(ctx))
            controller = importlib.import_module('security_policy_input_backup_operator')
            audit_loaded_modules(ctx)
            if args.mode == 'inputbackuprom':
                controller.run_rom(ctx, args.request, args.request_sha256)
                return 0
            result = controller.run_operator(ctx, args.request, args.request_sha256)
            print(json.dumps(result, sort_keys=True))
            return 0
        if args.mode == 'inputrom':
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
