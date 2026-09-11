"""Read/reset-only private backup and explicit held-ROM handoff. No grant issuer."""
from pathlib import Path
import copy
import os
import math
import re
import time
import security_policy_execution as execution

BackupError = execution.ExecutionError
need = execution.need
canonical = execution.canonical
sha = execution.sha
decode = execution.decode
private_path = execution.private_path
exclusive = execution.exclusive
single_process = execution.single_process
HEX32, HEX64 = execution.HEX32, execution.HEX64
SPANS = {'bootloader': (0,32768), 'partition': (0x8000,4096),
         'ota': (0x9000,8192), 'application': (0x10000,589824), 'nvs': (0xd000,12288)}
ACTIVE = 'security-policy-backup-active.lock'

def digest(value):
    return sha(canonical(value))

def validate_request(value):
    try:
        obj = copy.deepcopy(value)
        need(set(obj) == {'schema','runtime_sha256','finish','roles'} and
             obj['schema'] == 'OT190-BACKUP-REQUEST-1' and HEX64.fullmatch(obj['runtime_sha256'])
             and obj['finish'] in ('hold','reset') and type(obj['roles']) is list
             and len(obj['roles']) == 2, 'request_invalid')
        for role, row in zip(('A','B'), obj['roles']):
            need(set(row) == {'role','private_route','private_identity','expected'} and row['role'] == role
                 and type(row['private_route']) is str and re.fullmatch(r'COM[1-9][0-9]{0,3}',row['private_route'])
                 and re.fullmatch(r'(?:[0-9a-fA-F]{12}|(?:[0-9a-fA-F]{2}[:-]){5}[0-9a-fA-F]{2})',row['private_identity']), 'request_invalid')
            row['private_identity'] = row['private_identity'].replace(':','').replace('-','').lower()
            need(set(row['expected']) == set(SPANS)-{'nvs'}, 'request_invalid')
            need(row['expected']['partition']['sha256'] == execution.bundle.PARTITION_SHA
                 and row['expected']['ota']['sha256'] == execution.bundle.OTA_SHA, 'boot_selection_invalid')
            for name, desc in row['expected'].items():
                need(set(desc) == {'bytes','sha256'} and type(desc['bytes']) is int
                     and desc['bytes'] == SPANS[name][1] and HEX64.fullmatch(desc['sha256']), 'request_invalid')
        need(obj['roles'][0]['private_route'] != obj['roles'][1]['private_route'] and
             obj['roles'][0]['private_identity'] != obj['roles'][1]['private_identity'], 'request_invalid')
        return obj
    except Exception:
        raise BackupError('request_invalid') from None

class FileAuthority:
    def __init__(self, root, path, expected_sha256, *, utc=time.time):
        self.root, self.path, self.expected, self.utc = Path(root), Path(path), expected_sha256, utc

    def validate(self, request, *, operation='capture', origin_attempt=None):
        try:
            request = validate_request(request)
            need(self.path == private_path(self.root,self.path.name), 'authority_invalid')
            raw = self.path.read_bytes()
            need(len(raw) <= 4096 and sha(raw) == self.expected and HEX64.fullmatch(self.expected), 'authority_invalid')
            obj = decode(raw)
            need(set(obj) == {'schema','attempt','request_sha256','runtime_sha256','operation','origin_attempt',
                 'attempt_count','radio_allowed','actions','issued_utc','expires_utc'}, 'authority_invalid')
            actions = ['rom_read','rom_hold'] if operation == 'capture' and request['finish'] == 'hold' else ['rom_read','original_reset']
            need(obj['schema'] == 'OT190-BACKUP-GRANT-1' and HEX32.fullmatch(obj['attempt'])
                 and obj['request_sha256'] == digest(request) and obj['runtime_sha256'] == request['runtime_sha256']
                 and obj['operation'] == operation and operation in ('capture','reset')
                 and obj['origin_attempt'] == origin_attempt and (origin_attempt is None if operation == 'capture' else bool(HEX32.fullmatch(origin_attempt)))
                 and type(obj['attempt_count']) is int and obj['attempt_count'] == 1 and obj['radio_allowed'] is False
                 and obj['actions'] == actions and type(obj['issued_utc']) is int and type(obj['expires_utc']) is int
                 and 0 < obj['expires_utc']-obj['issued_utc'] <= 3600
                 and obj['issued_utc'] <= self.utc() < obj['expires_utc'], 'authority_invalid')
            return {**obj,'raw_sha256': self.expected}
        except Exception:
            raise BackupError('authority_invalid') from None

def path_for(root, attempt, suffix):
    need(type(attempt) is str and HEX32.fullmatch(attempt), 'attempt_invalid')
    return private_path(root, f'security-policy-backup-{attempt}-{suffix}')

class Journal:
    def __init__(self, root, attempt):
        self.root, self.attempt = Path(root), attempt
        self.path = path_for(root,attempt,'journal.jsonl')
    def load(self):
        raw = self.path.read_bytes()
        need(len(raw) <= 131072 and raw.endswith(b'\n'), 'journal_invalid')
        rows = [decode(line) for line in raw.splitlines()]
        need(rows and rows[0]['event'] == 'created', 'journal_invalid')
        first = rows[0]
        need(set(first) == {'seq','event','request','grant'} and validate_request(first['request']) == first['request'], 'journal_invalid')
        states = {'A': None, 'B': None}
        complete, handed = False, False
        for i,row in enumerate(rows):
            need(type(row) is dict and type(row.get('seq')) is int and row['seq'] == i, 'journal_invalid')
            event = row.get('event')
            if i == 0:
                continue
            if event in ('read_intent','verified','reset_verify','reset_intent','reset_done'):
                need(set(row) == {'seq','event','role'} and row['role'] in states, 'journal_invalid')
                role = row['role']
                allowed = {'read_intent': {None}, 'verified': {'read_intent'},
                           'reset_verify': {None,'read_intent','verified','reset_verify'},
                           'reset_intent': {'reset_verify'}, 'reset_done': {'reset_intent'}}
                need(states[role] in allowed[event], 'journal_invalid')
                if event in ('read_intent','verified'):
                    need(not complete and not handed, 'journal_invalid')
                states[role] = event
            elif event == 'complete':
                need(set(row) == {'seq','event','receipt_sha256'} and HEX64.fullmatch(row['receipt_sha256'])
                     and not complete and all(v in ('verified','reset_done') for v in states.values()), 'journal_invalid')
                complete = True
            elif event == 'handoff':
                need(set(row) == {'seq','event','execution_attempt','package_sha256'} and complete and not handed
                     and HEX32.fullmatch(row['execution_attempt']) and HEX64.fullmatch(row['package_sha256']), 'journal_invalid')
                handed = True
            elif event == 'release_authorized':
                need(set(row) == {'seq','event','grant'} and type(row['grant']) is dict, 'journal_invalid')
            else:
                raise BackupError('journal_invalid')
        return rows
    def add(self,event,**fields):
        rows = self.load()
        raw = canonical({'seq':len(rows),'event':event,**fields})+b'\n'
        with self.path.open('ab',buffering=0) as stream:
            need(stream.write(raw) == len(raw), 'journal_write_failed')
            os.fsync(stream.fileno())
        need(self.load()[-1] == decode(raw), 'journal_write_failed')

def _binding(request,backend):
    actual = [(b.role,b.private_route,b.private_identity.replace(':','').replace('-','').lower()) for b in backend.bindings]
    need(actual == [(r['role'],r['private_route'],r['private_identity']) for r in request['roles']]
         and backend.assert_idle() is True, 'backend_binding_changed')

def _consume(root,request,authority,operation,origin=None):
    grant = authority.validate(request,operation=operation,origin_attempt=origin)
    exclusive(path_for(root,grant['attempt'],'grant.used'), {'grant':grant['raw_sha256'],'request':digest(request)})
    return grant

def _read(backend,row,include_nvs):
    result = {}
    for name,(offset,size) in SPANS.items():
        if name == 'nvs' and not include_nvs:
            continue
        raw = backend.read(row['role'],offset,size)
        need(type(raw) is bytes and len(raw) == size, 'readback_invalid')
        if name != 'nvs':
            need({'bytes':len(raw),'sha256':sha(raw)} == row['expected'][name], 'original_changed')
        result[name] = raw
    return result

def _snapshot(path,raw):
    temporary = path.with_name(path.name+'.pending')
    fd = os.open(temporary,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    with os.fdopen(fd,'wb',buffering=0) as stream:
        need(stream.write(raw) == len(raw), 'snapshot_write_failed')
        os.fsync(stream.fileno())
    need(temporary.read_bytes() == raw, 'snapshot_write_failed')
    os.link(temporary,path)
    temporary.unlink()
    need(path.read_bytes() == raw, 'snapshot_write_failed')
    return {'path':str(path),'bytes':len(raw),'sha256':sha(raw)}

def _clear(root,attempt):
    path = private_path(root,ACTIVE)
    need(decode(path.read_bytes())['attempt'] == attempt, 'active_changed')
    path.unlink()

@single_process
def backup(root,request,authority,backend,*,utc=time.time):
    request = validate_request(request)
    _binding(request,backend)
    need(not private_path(root,'security-policy-active.lock').exists() and not private_path(root,ACTIVE).exists(), 'active_attempt_pending')
    grant = _consume(root,request,authority,'capture')
    attempt = grant['attempt']
    exclusive(private_path(root,ACTIVE), {'attempt':attempt,'request':digest(request)})
    journal = Journal(root,attempt)
    exclusive(journal.path, {'seq':0,'event':'created','request':request,'grant':grant})
    roles = []
    for row in request['roles']:
        journal.add('read_intent',role=row['role'])
        data = _read(backend,row,True)
        record = {key:row[key] for key in ('role','private_route','private_identity')}
        for name in ('application','nvs'):
            record[name] = _snapshot(path_for(root,attempt,row['role']+'-'+name+'.bin'),data[name])
        record['protected'] = {name:row['expected'][name] for name in ('bootloader','partition','ota')}
        roles.append(record)
        journal.add('verified',role=row['role'])
    if request['finish'] == 'reset':
        for row in request['roles']:
            journal.add('reset_verify',role=row['role'])
            _read(backend,row,False)
            journal.add('reset_intent',role=row['role'])
            need(backend.reset(row['role']) is True, 'reset_unconfirmed')
            journal.add('reset_done',role=row['role'])
    receipt = {'schema':'OT190-BACKUP-RECEIPT-1','attempt':attempt,'request_sha256':digest(request),
               'status':'held' if request['finish'] == 'hold' else 'reset_nvs_stale',
               'expires_utc':grant['expires_utc'],'roles':roles,'usable_for_handoff':request['finish'] == 'hold'}
    exclusive(path_for(root,attempt,'receipt.json'),receipt)
    journal.add('complete',receipt_sha256=digest(receipt))
    if request['finish'] == 'reset':
        _clear(root,attempt)
    return receipt

def inspect_handoff(root,request,attempt,*,utc=time.time):
    request = validate_request(request)
    rows = Journal(root,attempt).load()
    need(rows[0]['request'] == request and rows[-1]['event'] == 'complete'
         and decode(private_path(root,ACTIVE).read_bytes()) == {'attempt':attempt,'request':digest(request)}
         and not private_path(root,'security-policy-active.lock').exists(), 'handoff_unavailable')
    receipt = decode(path_for(root,attempt,'receipt.json').read_bytes())
    original_grant = rows[0]['grant']
    need(decode(path_for(root,attempt,'grant.used').read_bytes()) == {'grant':original_grant['raw_sha256'],'request':digest(request)}, 'grant_changed')
    now = utc()
    need(type(now) in (int,float) and math.isfinite(now) and original_grant['issued_utc'] <= now, 'hold_clock_invalid')
    need(set(receipt) == {'schema','attempt','request_sha256','status','expires_utc','roles','usable_for_handoff'}
         and receipt['schema'] == 'OT190-BACKUP-RECEIPT-1' and receipt['attempt'] == attempt
         and receipt['request_sha256'] == digest(request) and receipt['expires_utc'] == original_grant['expires_utc']
         and type(receipt['roles']) is list and len(receipt['roles']) == 2, 'receipt_invalid')
    need(digest(receipt) == rows[-1]['receipt_sha256'] and receipt['usable_for_handoff'] is True
         and receipt['status'] == 'held' and now < receipt['expires_utc'], 'handoff_unavailable')
    result = []
    for row, expected in zip(receipt['roles'],request['roles']):
        need(set(row) == {'role','private_route','private_identity','application','nvs','protected'}
             and all(row[k] == expected[k] for k in ('role','private_route','private_identity'))
             and row['protected'] == {k:expected['expected'][k] for k in ('bootloader','partition','ota')}, 'receipt_invalid')
        need({k:row['application'][k] for k in ('bytes','sha256')} == expected['expected']['application']
             and row['nvs']['bytes'] == 12288, 'receipt_invalid')
        out = copy.deepcopy(row)
        for name in ('application','nvs'):
            desc = row[name]
            path = path_for(root,attempt,row['role']+'-'+name+'.bin')
            need(str(path) == desc['path'] and {'bytes':path.stat().st_size,'sha256':sha(path.read_bytes())}
                 == {k:desc[k] for k in ('bytes','sha256')}, 'snapshot_changed')
            out[name] = path
        result.append(out)
    return tuple(result)

def handoff(root,request,attempt,*,execution_attempt,package_sha256,utc=time.time):
    need(HEX32.fullmatch(execution_attempt) and HEX64.fullmatch(package_sha256), 'handoff_invalid')
    roles = inspect_handoff(root,request,attempt,utc=utc)
    Journal(root,attempt).add('handoff',execution_attempt=execution_attempt,package_sha256=package_sha256)
    _clear(root,attempt)
    return roles

def _release_roles(root,rows):
    handed = [r for r in rows if r['event'] == 'handoff']
    if not handed:
        need(not private_path(root,'security-policy-active.lock').exists(), 'legacy_pending')
        return {'A','B'}
    hand = handed[-1]
    path = private_path(root,'security-policy-'+hand['execution_attempt']+'.jsonl')
    active = private_path(root,'security-policy-active.lock')
    if active.exists():
        need(decode(active.read_bytes()) == {'attempt':hand['execution_attempt'],'package_sha256':hand['package_sha256']}, 'legacy_pending')
    if not path.exists():
        need(not active.exists(), 'legacy_pending')
        return {'A','B'}
    raw = path.read_bytes()
    need(len(raw) <= 131072 and raw.endswith(b'\n'), 'legacy_pending')
    events = [decode(line) for line in raw.splitlines()]
    execution.Journal.validate(events,{'attempt':hand['execution_attempt'],'package_sha256':hand['package_sha256']})
    need(active.exists() or events[-1]['event'] == 'finished', 'legacy_pending')
    release = set()
    for role in ('A','B'):
        names = [r['event'] for r in events if r.get('role') == role]
        need('serial_open_intent' not in names or 'serial_closed' in names, 'serial_pending')
        if 'original_booted' in names:
            continue
        need('candidate_write_intent' not in names, 'legacy_recovery_required')
        release.add(role)
    return release

@single_process
def reset_backup(root,request,authority,backend,*,origin_attempt,utc=time.time):
    request = validate_request(request)
    _binding(request,backend)
    journal = Journal(root,origin_attempt)
    rows = journal.load()
    need(rows[0]['request'] == request, 'origin_changed')
    release = _release_roles(root,rows)
    completed = {r['role'] for r in rows if r['event'] == 'reset_done'}
    uncertain = {r['role'] for r in rows if r['event'] == 'reset_intent'}-completed
    need(not uncertain, 'original_reset_uncertain')
    release -= completed
    grant = _consume(root,request,authority,'reset',origin_attempt)
    active = private_path(root,ACTIVE)
    if active.exists():
        need(decode(active.read_bytes()) == {'attempt':origin_attempt,'request':digest(request)}, 'active_changed')
    else:
        exclusive(active,{'attempt':origin_attempt,'request':digest(request)})
    journal.add('release_authorized',grant=grant)
    for row in request['roles']:
        if row['role'] not in release:
            continue
        journal.add('reset_verify',role=row['role'])
        _read(backend,row,False)
        journal.add('reset_intent',role=row['role'])
        need(backend.reset(row['role']) is True, 'reset_unconfirmed')
        journal.add('reset_done',role=row['role'])
    result = {'status':'reset_nvs_stale','usable_for_handoff':False,'attempt':grant['attempt'],'origin_attempt':origin_attempt}
    exclusive(path_for(root,grant['attempt'],'receipt.json'),result)
    handed = [r for r in rows if r['event'] == 'handoff']
    legacy_active = private_path(root,'security-policy-active.lock')
    if handed and legacy_active.exists():
        hand = handed[-1]
        legacy_path = private_path(root,'security-policy-'+hand['execution_attempt']+'.jsonl')
        _release_roles(root,journal.load())
        need(decode(legacy_active.read_bytes()) == {'attempt':hand['execution_attempt'],'package_sha256':hand['package_sha256']}, 'legacy_pending')
        exclusive(path_for(root,grant['attempt'],'legacy-reconciled.json'),
                  {'schema':'OT190-LEGACY-RECONCILIATION-1','execution_attempt':hand['execution_attempt'],
                   'package_sha256':hand['package_sha256'],'journal_sha256':sha(legacy_path.read_bytes()),
                   'release_grant_sha256':grant['raw_sha256'],'backup_origin':origin_attempt})
        legacy_active.unlink()
    _clear(root,origin_attempt)
    return result

def admit_child(root,request,authority,argv,*,operation='capture',origin_attempt=None):
    request = validate_request(request)
    grant = authority.validate(request,operation=operation,origin_attempt=origin_attempt)
    need(decode(path_for(root,grant['attempt'],'grant.used').read_bytes()) == {'grant':grant['raw_sha256'],'request':digest(request)}, 'authority_unconsumed')
    attempt = grant['attempt'] if operation == 'capture' else origin_attempt
    need(decode(private_path(root,ACTIVE).read_bytes()) == {'attempt':attempt,'request':digest(request)}, 'active_changed')
    rows = Journal(root,attempt).load()
    need(rows[0]['request'] == request, 'request_changed')
    if operation == 'reset':
        need(next((r['grant'] for r in reversed(rows) if r['event'] == 'release_authorized'),None) == grant, 'authority_unconsumed')
    else:
        need(rows[0]['grant'] == grant and not any(r['event'] in ('release_authorized','handoff','complete') for r in rows), 'authority_changed')
    need(type(argv) is list and all(type(s) is str for s in argv) and 12 <= len(argv) <= 15 and sum(map(len,argv)) <= 4096, 'rom_scope_invalid')
    role = next((r['role'] for r in request['roles'] if r['private_route'] == argv[3]),None)
    phase = rows[-1]['event'] if rows[-1].get('role') == role else None
    prefix = ['--chip','esp32s3','--port',argv[3],'--baud','115200','--before','default-reset','--after']
    need(role is not None and argv[:9] == prefix and argv[10] == '--no-stub', 'rom_scope_invalid')
    command = argv[11:]
    reset = command == ['run']
    need(argv[9] == ('hard-reset' if reset else 'no-reset'), 'rom_scope_invalid')
    if reset:
        need(phase == 'reset_intent' and (operation == 'reset' or request['finish'] == 'reset'), 'rom_scope_invalid')
    else:
        need(phase in ('read_intent','reset_verify','reset_intent'), 'rom_scope_invalid')
        if command not in (['read-mac'],['flash-id']):
            need(len(command) == 4 and command[0] == 'read-flash', 'rom_scope_invalid')
            try:
                span = (int(command[1],0),int(command[2],0))
            except ValueError:
                raise BackupError('rom_scope_invalid') from None
            need(span in SPANS.values() and phase != 'reset_intent', 'rom_scope_invalid')
            target = Path(command[3])
            private = private_path(root,'sentinel').parent
            need(target.name == 'region.bin' and target.parent.parent == private
                 and target.parent.name.startswith('ot188-read-') and not target.exists(), 'rom_scope_invalid')
            for part in (target.parent,*target.parent.parents):
                need(not part.is_symlink() and not (getattr(part.lstat(),'st_file_attributes',0)&0x400), 'rom_scope_invalid')
    return argv
