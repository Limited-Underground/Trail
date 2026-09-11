"""OT-198 stage backup custody operator. Import is inert; every device path needs external authority.

Backup capture/reset and candidate handoff have separate typed requests. ROM reads
use the same externally verified interpreter boundary as the evaluation runner.
"""
from pathlib import Path
import importlib
import json
import re
import subprocess
import tempfile

import security_policy_input_operator as operator


def request(ctx, path, sha):
    req = operator.load_request(operator.private_file(ctx, path), sha)
    operator.need(set(req) == {'schema','runtime_sha256','operation','request_path',
        'request_sha256','grant_path','grant_sha256','origin_attempt','execution_request'}
        and req['schema'] == 'OT198-INPUT-BACKUP-OPERATOR-1' and req['runtime_sha256'] == ctx['sha']
        and req['operation'] in ('capture','reset','handoff'))
    core = importlib.import_module('security_policy_backup')
    data = core.validate_request(operator.load_request(
        operator.private_file(ctx, req['request_path']), req['request_sha256']))
    operator.need(data['runtime_sha256'] == ctx['sha'])
    if req['operation'] == 'capture':
        operator.need(req['origin_attempt'] is None)
    else:
        operator.need(type(req['origin_attempt']) is str and re.fullmatch('[0-9a-f]{32}',req['origin_attempt']))
    if req['operation'] == 'handoff':
        source = req['execution_request']
        operator.need(req['grant_path'] is None and req['grant_sha256'] is None
                      and type(source) is dict and set(source) == {'path','sha256'})
        op = operator.operator_request(ctx, source['path'], source['sha256'])
        operator.need(op['operation'] == 'execute')
    else:
        operator.need(req['execution_request'] is None)
        operator.load_request(operator.private_file(ctx, req['grant_path']), req['grant_sha256'])
    return req, data


def make_transport(ctx, path, sha):
    hardware = importlib.import_module('security_policy_hardware')
    class BackupRomTransport(hardware.RomTransport):
        @hardware.safe
        def command(self, route, operation, *, reset=False):
            argv = ['--chip','esp32s3','--port',route,'--baud','115200','--before',
                    'default-reset','--after','hard-reset' if reset else 'no-reset','--no-stub'] + operation
            child = {'schema':'OT198-INPUT-BACKUP-ROM-1','runtime_sha256':ctx['sha'],
                     'operator_request':{'path':str(path),'sha256':sha},'argv':argv}
            raw = json.dumps(child,sort_keys=True,separators=(',',':')).encode()
            with tempfile.TemporaryDirectory(prefix='ot198-input-backup-rom-',dir=self.private_root) as directory:
                child_path = Path(directory)/'request.json'
                child_path.write_bytes(raw)
                result = subprocess.run(operator.child_args(ctx['manifest'],ctx['path'],ctx['sha'],
                    'InputBackupRom',child_path,operator.digest(raw)),stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=240,check=False,
                    cwd=ctx['manifest']['root'])
            hardware.require(result.returncode == 0 and len(result.stdout) <= 1048576,'rom_command_failed')
            return result.stdout
    return BackupRomTransport(Path(ctx['manifest']['worktree'])/'.private')


def run_operator(ctx, path, sha):
    req, data = request(ctx,path,sha)
    core = importlib.import_module('security_policy_backup')
    hardware = importlib.import_module('security_policy_hardware')
    root = Path(ctx['manifest']['worktree'])
    if req['operation'] == 'handoff':
        return run_handoff(root,ctx,req,data)
    bindings = tuple(hardware.RoleBinding(r['role'],r['private_route'],r['private_identity']) for r in data['roles'])
    backend = hardware.Backend(bindings,transport=make_transport(ctx,path,sha))
    authority = core.FileAuthority(root,Path(req['grant_path']),req['grant_sha256'])
    operator.audit_loaded_modules(ctx)
    if req['operation'] == 'capture':
        result = core.backup(root,data,authority,backend)
    else:
        result = core.reset_backup(root,data,authority,backend,origin_attempt=req['origin_attempt'])
    # Full private custody lives on disk; never print identities or paths.
    return {'status':result['status'],'operation':req['operation'],'hardware_access':True}


def run_handoff(root,ctx,req,data):
    execution = importlib.import_module('security_policy_input_execution')
    return execution.single_process(_handoff)(root,ctx,req,data)


def _handoff(root,ctx,req,data):
    core = importlib.import_module('security_policy_backup')
    bundle = importlib.import_module('security_policy_input_bundle')
    execution = importlib.import_module('security_policy_input_execution')
    hardware = importlib.import_module('security_policy_hardware')
    source = req['execution_request']
    op = operator.operator_request(ctx,source['path'],source['sha256'])
    package = operator.load_request(operator.private_file(ctx,op['package_path']),op['package_sha256'])
    operator.bind_sources(ctx,package)
    material = bundle.verify(root,package)
    roles = core.inspect_handoff(root,data,req['origin_attempt'])
    # Compare complete descriptors, paths, identity, routes and protected baselines.
    operator.need(bundle._roles(root,roles,True) == package['roles'])
    readback=importlib.import_module('security_policy_input_readback')
    for row in material['roles']:
        readback.assert_fresh(row['nvs'])
    authority = execution.FileAuthority(root,Path(op['grant_path']),op['grant_sha256'],runtime_sha256=ctx['sha'])
    grant = authority.validate(material['package_sha256'],operation='execute')
    operator.need(not execution.private_path(root,'security-policy-active.lock').exists()
        and not execution.used_path(root,grant.attempt).exists())
    bindings = tuple(hardware.RoleBinding(r['role'],r['private_route'],r['private_identity']) for r in material['roles'])
    backend = operator.make_backend(ctx,bindings,operator.make_transport(ctx,source['path'],source['sha256']))
    execution.material(root,package,backend,False)
    operator.audit_loaded_modules(ctx)
    core.handoff(root,data,req['origin_attempt'],execution_attempt=grant.attempt,
                 package_sha256=material['package_sha256'])
    # Already inside the identical process exclusion lock; frozen execution owns
    # its own exact grant consumption, active journal and per-write live readbacks.
    result = execution.execute.__wrapped__(root,package,authority,backend)
    envelope = {'status':result['status'],'operation':'handoff','hardware_access':True,
                'roles':result.get('roles',[]),'backup_release_check_required':result['status'] != 'pass'}
    return operator.with_diagnostics(ctx,backend,envelope)


def run_rom(ctx,path,sha):
    child = operator.load_request(operator.private_file(ctx,path),sha)
    operator.need(set(child) == {'schema','runtime_sha256','operator_request','argv'}
                  and child['schema'] == 'OT198-INPUT-BACKUP-ROM-1' and child['runtime_sha256'] == ctx['sha'])
    source = child['operator_request']
    operator.need(type(source) is dict and set(source) == {'path','sha256'})
    req,data = request(ctx,source['path'],source['sha256'])
    operator.need(req['operation'] in ('capture','reset'))
    core = importlib.import_module('security_policy_backup')
    root = Path(ctx['manifest']['worktree'])
    authority = core.FileAuthority(root,Path(req['grant_path']),req['grant_sha256'])
    argv = core.admit_child(root,data,authority,child['argv'],operation=req['operation'],
                           origin_attempt=req['origin_attempt'])
    claim=operator.digest((str(Path(path).resolve())+'|'+sha).encode())
    execution=importlib.import_module('security_policy_execution')
    execution.exclusive(execution.private_path(root,'ot198-input-backup-child-'+claim+'.used'),{'request_sha256':sha})
    if argv[11:] == ['run']:
        grant=authority.validate(data,operation=req['operation'],origin_attempt=req['origin_attempt'])
        role=next(r['role'] for r in data['roles'] if r['private_route']==argv[3])
        marker=operator.digest((grant['attempt']+'|'+str(req['origin_attempt'])+'|'+role+'|reset_intent|run').encode())
        execution.exclusive(execution.private_path(root,'ot198-input-backup-reset-'+marker+'.used'),{'grant':grant['raw_sha256']})
    tool = importlib.import_module('esptool')
    operator.audit_loaded_modules(ctx)
    tool.main(argv)
