"""OT-218 single-device ROM adapter. Import and construction never open ports.

The trial engine owns authority/custody. USB and ROM matching prevent accidental
swaps, not identity cloning. Raw identifiers and ROM diagnostics remain in memory.
The frozen OT-212 isolated interpreter/packages are reused without alteration.
"""
from __future__ import annotations

import base64
import functools
import hashlib
import hmac
import json
import os
from pathlib import Path
import re
import subprocess
import threading
from ble_startup_diagnostics import WORKER_SUPPORT, validate_startup_result

SPANS = frozenset(((0, 32768), (0x8000, 4096), (0x9000, 8192),
                   (0xd000, 12288), (0x10000, 733184)))
WRITABLE = frozenset(((0xd000, 12288), (0x10000, 733184)))
STATE_READ_SPANS = frozenset(((0xf00000, 524288), (0xf80000, 524288)))
NAMED_SPANS = {'bootloader': (0, 32768), 'partition': (0x8000, 4096),
               'ota': (0x9000, 8192), 'application': (0x10000, 733184), 'nvs': (0xd000, 12288)}
COMPLETE_NAMED_SPANS = dict(NAMED_SPANS, state_0=(0xf00000, 524288),
                            state_1=(0xf80000, 524288))

OPERATIONS = frozenset(('probe', 'verify', 'read', 'read_state', 'read_recovery_state', 'read_bound_state',
                        'restore_state', 'write', 'restart',
                        'restart_capture', 'passive_capture'))
COMMAND_STAGES = frozenset(f'{step}.{stage}'
    for step in ('read_mac', 'flash_id', 'read_flash', 'write_flash', 'restart')
    for stage in ('route_before', 'cli', 'cli.connect', 'cli.command', 'cli.teardown',
                  'output_limit', 'route_after'))
WORKER_STAGES = COMMAND_STAGES | frozenset(('runtime', 'request',
    'read_mac.identity_parse', 'flash_id.flash_parse', 'read_file', 'read_length',
    'write_input', 'startup_capture'))
FAILURE_CATEGORIES = frozenset(('guard', 'serial', 'timeout', 'tool', 'other'))


def validated_worker_failure(value, operation):
    """Accept fixed vocabulary only; never retain arbitrary child diagnostics."""
    need(type(value) is dict and set(value) == {'operation', 'stage', 'category'})
    stage, category = value['stage'], value['category']
    need(type(stage) is str and stage in WORKER_STAGES)
    need(type(category) is str and category in FAILURE_CATEGORIES)
    if stage == 'runtime':
        need(value['operation'] is None)
    if value['operation'] is None:
        need(stage in ('runtime', 'request'))
    else:
        need(type(value['operation']) is str and value['operation'] == operation)
    if stage.startswith('read_flash.') or stage in ('read_file', 'read_length'):
        need(operation in ('read', 'read_state', 'read_recovery_state', 'read_bound_state'))
    if stage.startswith('write_flash.') or stage == 'write_input':
        need(operation in ('write', 'restore_state'))
    if stage.startswith('restart.'):
        need(operation in ('restart', 'restart_capture') and stage != 'restart.route_after')
    if stage == 'startup_capture':
        need(operation in ('restart_capture', 'passive_capture'))
    if operation in ('probe', 'passive_capture'):
        need(stage in ('runtime', 'request', 'startup_capture'))
    return dict(operation=operation, stage=stage, category=category, source='worker')


class TransportError(RuntimeError):
    """Fixed category, never command arguments or device diagnostics."""


def need(value):
    if not value:
        raise TransportError('ble_trial_transport_refused')


def safe(function):
    @functools.wraps(function)
    def invoke(*args, **kwargs):
        try:
            return function(*args, **kwargs)
        except BaseException:
            raise TransportError('ble_trial_transport_refused') from None
    return invoke


def identity(value):
    need(type(value) is str and re.fullmatch(r'(?:[0-9a-fA-F]{12}|[0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5}|[0-9a-fA-F]{2}(?:-[0-9a-fA-F]{2}){5})', value))
    return value.replace(':', '').replace('-', '').lower()


def opaque_identity(key, value):
    need(type(key) is bytes and len(key) == 32)
    return hmac.new(key, b'OT218-DEVICE\0' + identity(value).encode('ascii'), hashlib.sha256).hexdigest()


def rom_argv(route, operation, restart=False):
    """Accepted ROM release sequence; no general command API is exposed."""
    return ['--chip', 'esp32s3', '--port', route, '--baud', '115200',
            '--before', 'default-reset', '--after',
            'hard-reset' if restart else 'no-reset', '--no-stub'] + operation


# Executed by the existing isolated, hash-verified interpreter. Stdin is the
# only channel carrying the route/identity; neither is in argv or any file.
WORKER = WORKER_SUPPORT + r'''
import base64, contextlib, hashlib, io, json, os, pathlib, re, sys, tempfile
class ProbeFinished(Exception): pass
class GuardFailure(Exception): pass
stage='runtime'; operation=None; first_cli_failure=None
def need(x):
    if not x: raise GuardFailure()
def failure_category(error):
    if isinstance(error,GuardFailure): return 'guard'
    if isinstance(error,TimeoutError): return 'timeout'
    if 'serial' in globals() and isinstance(error,getattr(serial,'SerialException',())): return 'serial'
    if 'esptool' in globals() and isinstance(error,getattr(esptool,'FatalError',())): return 'tool'
    return 'other'
def observed_main(argv,step):
    # Observe pinned CLI boundaries before Click cleanup can replace an error.
    # The vendor functions still own every argument, return, exception and port.
    originals={}
    def wrap(function,phase):
        def invoke(*args,**kwargs):
            global stage, first_cli_failure
            stage=step+'.cli.'+phase
            try:
                value=function(*args,**kwargs)
            except BaseException as error:
                if first_cli_failure is None:
                    first_cli_failure=(stage,failure_category(error))
                raise
            if phase!='teardown': stage=step+'.cli'
            return value
        return invoke
    try:
        command={'read_mac':'read_mac','flash_id':'flash_id','read_flash':'read_flash','write_flash':'write_flash','restart':'run'}[step]
        for name,phase in (('get_default_connected_device','connect'),(command,'command'),('reset_chip','teardown')):
            originals[name]=getattr(esptool,name)
            setattr(esptool,name,wrap(originals[name],phase))
        esptool.main(argv)
    finally:
        for name,function in originals.items(): setattr(esptool,name,function)
def ident(x):
    need(isinstance(x,str) and re.fullmatch(r'(?:[0-9a-fA-F]{12}|[0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5}|[0-9a-fA-F]{2}(?:-[0-9a-fA-F]{2}){5})',x))
    return x.replace(':','').replace('-','').lower()
try:
    q=json.loads(sys.stdin.buffer.read(2500000)); m=q['manifest']; root=pathlib.Path(m['root'])
    need(sys.flags.isolated and sys.flags.no_site and sys.dont_write_bytecode)
    need(pathlib.Path(sys.executable).resolve()==root/'python.exe')
    need(sys.version_info[:3]==(3,14,6))
    need([pathlib.Path(x).resolve() for x in sys.path]==[root/x for x in ('Lib','DLLs','packages','policy')])
    for name,pin in m['files'].items():
        p=root/name
        need(not any(x.is_symlink() or getattr(x,'is_junction',lambda:False)() for x in (p,*p.parents)))
        raw=p.read_bytes(); need(len(raw)==pin['bytes'] and hashlib.sha256(raw).hexdigest()==pin['sha256'])
    need({p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()}==set(m['files']))
    need((root/'python314._pth').read_bytes()==b'Lib\nDLLs\npackages\npolicy\n')
    need((root/'esptool.cfg').read_bytes()==b'[esptool]\n')
    import esptool, serial
    from serial.tools.list_ports import comports
    need(esptool.__version__=='5.3.1' and serial.__version__=='3.5')
    for module in tuple(sys.modules.values()):
        origin=getattr(module,'__file__',None)
        if origin:
            p=pathlib.Path(origin).resolve(); need(p.is_relative_to(root) and p.relative_to(root).as_posix() in m['files'])
    stage='request'
    need(q['operation'] in ('probe','verify','read','read_state','read_recovery_state','read_bound_state','restore_state','write','restart','restart_capture','passive_capture'))
    operation=q['operation']
    if operation in ('read_state','read_recovery_state','read_bound_state','restore_state'):
        # Parent custody owns capture/recovery lifetimes and exact original
        # restore authority. The child admits only the two fixed state halves.
        fields={'manifest','route','identity','private_root','operation','offset','size'}
        if operation=='restore_state': fields|={'data','original_sha256'}
        need(set(q)==fields)
        need(type(q['offset']) is int and type(q['size']) is int and
            (q['offset'],q['size']) in {(0xf00000,524288),(0xf80000,524288)})
        if operation=='restore_state':
            need(type(q['data']) is str and len(q['data'])==4*((q['size']+2)//3))
            need(type(q['original_sha256']) is str and re.fullmatch(r'[0-9a-f]{64}',q['original_sha256']))
            raw=base64.b64decode(q['data'],validate=True)
            need(len(raw)==q['size'] and base64.b64encode(raw).decode('ascii')==q['data'] and
                hashlib.sha256(raw).hexdigest()==q['original_sha256'])
    if operation=='probe':
        print('{"ok":true}')
        raise ProbeFinished()
    route=q['route']; expected=ident(q['identity'])
    need(isinstance(route,str) and re.fullmatch(r'COM[1-9][0-9]{0,3}',route))
    if q['operation']=='passive_capture':
        # No ROM verification here: it would reset the running candidate.
        stage='startup_capture'
        print(json.dumps({'ok':True,'startup_diagnostics':capture_startup(serial,comports,expected)},separators=(',',':')))
        raise ProbeFinished()
    def passive():
        ports=list(comports())
        routes=[p for p in ports if p.device==route]
        matches=[p for p in ports if p.vid==0x303a and p.pid==0x1001 and ident(p.serial_number)==expected]
        need(len(routes)==len(matches)==1 and routes[0] is matches[0])
    def run(operation,restart=False):
        # Preserve the accepted explicit hardware release after the ROM run.
        global stage
        step={'read-mac':'read_mac','flash-id':'flash_id','read-flash':'read_flash','write-flash':'write_flash','run':'restart'}[operation[0]]
        stage=step+'.route_before'
        passive()
        argv=['--chip','esp32s3','--port',route,'--baud','115200','--before','default-reset','--after','hard-reset' if restart else 'no-reset','--no-stub']+operation
        out=io.StringIO()
        stage=step+'.cli'
        with contextlib.redirect_stdout(out),contextlib.redirect_stderr(out): observed_main(argv,step)
        stage=step+'.output_limit'
        value=out.getvalue(); need(len(value)<=1048576)
        if not restart:
            stage=step+'.route_after'
            passive()
        return value
    text=run(['read-mac'])
    stage='read_mac.identity_parse'
    values=re.findall(r'(?im)^\s*MAC:\s*([^\r\n]+?)\s*$',text)
    need(len(values) in (1,2) and all(ident(v)==expected for v in values))
    text=run(['flash-id']); stage='flash_id.flash_parse'
    need('ESP32-S3' in text and re.search(r'(?m)^Detected flash size:\s*16MB\s*$',text))
    op=q['operation']; result={'ok':True}
    stage='request'
    spans={(0,32768),(0x8000,4096),(0x9000,8192),(0xd000,12288),(0x10000,733184)}
    if op in ('read','read_state','read_recovery_state','read_bound_state','restore_state','write'):
        allowed={(0xf00000,524288),(0xf80000,524288)} if op in ('read_state','read_recovery_state','read_bound_state','restore_state') else spans
        offset=q['offset']; size=q['size']; need(type(offset) is int and type(size) is int and (offset,size) in allowed)
        private=pathlib.Path(q['private_root']); need(private.is_absolute() and private.name=='.private' and private.is_dir())
        need(not any(p.is_symlink() or getattr(p,'is_junction',lambda:False)() for p in (private,*private.parents)))
        with tempfile.TemporaryDirectory(prefix='ot218-rom-',dir=private) as temp:
            p=pathlib.Path(temp)/'region.bin'
            if op in ('read','read_state','read_recovery_state','read_bound_state'):
                run(['read-flash',hex(offset),str(size),str(p)])
                stage='read_file'; raw=p.read_bytes()
                stage='read_length'; need(len(raw)==size)
                result['data']=base64.b64encode(raw).decode('ascii')
            else:
                stage='write_input'
                need((offset,size) in ({(0xf00000,524288),(0xf80000,524288)} if op=='restore_state' else {(0xd000,12288),(0x10000,733184)}))
                raw=base64.b64decode(q['data'],validate=True); need(len(raw)==size)
                if op=='restore_state': need(hashlib.sha256(raw).hexdigest()==q['original_sha256'])
                p.write_bytes(raw); run(['write-flash','--flash-size','16MB',hex(offset),str(p)])
    elif op in ('restart', 'restart_capture'):
        run(['run'],True)
        if op=='restart_capture':
            stage='startup_capture'
            result['startup_diagnostics']=capture_startup(serial,comports,expected)
    else: need(op=='verify')
    print(json.dumps(result,separators=(',',':')))
except ProbeFinished:
    pass
except BaseException as error:
    failed_stage,category=first_cli_failure or (stage,failure_category(error))
    print(json.dumps({'ok':False,'failure':{'operation':operation,'stage':failed_stage,'category':category}},separators=(',',':')))
    sys.exit(1)
'''


class Transport:
    @safe
    def __init__(self, *, manifest_path, manifest_sha256, private_root, route,
                 expected_identity, opaque_binding, binding_key, candidate,
                 subprocess_run=subprocess.run, manifest_verifier=None, recovery_only=False,
                 startup_diagnostics=False, confirmation_diagnostics=False):
        self._manifest_path = Path(manifest_path)
        self._manifest_sha = manifest_sha256
        self._private = Path(private_root)
        need(self._private.is_absolute() and self._private.name == '.private' and self._private.is_dir())
        need(type(route) is str and re.fullmatch(r'COM[1-9][0-9]{0,3}', route))
        self._route, self._identity = route, identity(expected_identity)
        need(type(opaque_binding) is str and hmac.compare_digest(opaque_identity(binding_key, self._identity), opaque_binding))
        need(type(candidate) is bytes and len(candidate) == 733184)
        self._binding, self._candidate = opaque_binding, candidate
        self._run, self._verifier = subprocess_run, manifest_verifier
        self._captured, self._originals = {}, None
        self._claimed, self._mutated = False, False
        need(type(recovery_only) is bool)
        need(type(startup_diagnostics) is bool and not (recovery_only and startup_diagnostics))
        need(type(confirmation_diagnostics) is bool and
             (not confirmation_diagnostics or (startup_diagnostics and not recovery_only)))
        self._startup_enabled = startup_diagnostics
        self._confirmation_enabled = confirmation_diagnostics
        self._candidate_booted = self._post_capture_used = False
        self.startup_diagnostics = None
        self.post_confirmation_diagnostics = None
        self._recovery_only = recovery_only
        self._failures = []
        self._lock = threading.RLock()

    def __repr__(self):
        return '<OT218 single-device ROM transport>'

    @property
    def failure_diagnostics(self):
        """Bounded, privacy-safe history; cleanup cannot erase a primary failure."""
        with self._lock:
            return tuple(dict(value) for value in self._failures)

    def _manifest(self):
        if self._verifier is None:
            from security_policy_deadline_operator import verify_manifest
            return verify_manifest(self._manifest_path, self._manifest_sha)
        return self._verifier(self._manifest_path, self._manifest_sha)

    def _operation(self, operation, **fields):
        stage, failure = 'request', None
        try:
            need(operation in OPERATIONS and (self._claimed or operation == 'probe'))
            self._admit_operation(operation, fields)
            if operation == 'read_state':
                need(set(fields) == {'offset', 'size'} and type(fields['offset']) is int
                     and type(fields['size']) is int and
                     (fields['offset'], fields['size']) in STATE_READ_SPANS)
                need(not self._mutated and not self._recovery_only and self._originals is None)
            if operation not in ('probe', 'passive_capture'):
                self._candidate_booted = False
            stage = 'manifest'
            manifest = self._manifest()
            need(self._private.resolve() == (Path(manifest['worktree']) / '.private').resolve())
            payload = dict(manifest=manifest, route=self._route, identity=self._identity,
                           private_root=str(self._private), operation=operation, **fields)
            env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('PYTHON', 'ESPTOOL_'))}
            env['ESPTOOL_CFGFILE'] = str(Path(manifest['root']) / 'esptool.cfg')
            stage = 'worker_launch'
            result = self._run([str(Path(manifest['root']) / 'python.exe'), '-I', '-S', '-B', '-c', WORKER],
                               input=json.dumps(payload, separators=(',', ':')).encode(),
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=240,
                               check=False, cwd=manifest['root'], env=env)
            stage = 'worker_reply'
            need(type(result.stdout) is bytes and len(result.stdout) <= 1100000)
            obj = json.loads(result.stdout)
            if result.returncode == 1 and type(obj) is dict and obj.get('ok') is False and set(obj) == {'ok', 'failure'}:
                failure = validated_worker_failure(obj['failure'], operation)
                raise TransportError('ble_trial_transport_refused')
            need(result.returncode == 0)
            expected_keys = ({'ok', 'data'} if operation in ('read', 'read_state', 'read_recovery_state', 'read_bound_state') else
                             {'ok', 'startup_diagnostics'} if operation in ('restart_capture', 'passive_capture') else {'ok'})
            need(type(obj) is dict and obj.get('ok') is True and set(obj) == expected_keys)
            if operation in ('read', 'read_state', 'read_recovery_state', 'read_bound_state'):
                stage = 'read_data'
                if operation in ('read_state', 'read_recovery_state', 'read_bound_state'):
                    need(type(obj['data']) is str and
                         len(obj['data']) == 4 * ((fields['size'] + 2) // 3))
                raw = base64.b64decode(obj['data'], validate=True)
                need(len(raw) == fields['size'])
            if operation in ('restart_capture', 'passive_capture'):
                stage = 'startup_validation'
                validate_startup_result(obj['startup_diagnostics'])
            return obj
        except BaseException as error:
            if failure is None:
                category = ('timeout' if isinstance(error, subprocess.TimeoutExpired) else
                            'process' if isinstance(error, OSError) and stage == 'worker_launch' else
                            'guard' if isinstance(error, TransportError) or stage in ('worker_reply', 'read_data', 'startup_validation') else 'other')
                failure = dict(operation=operation if operation in OPERATIONS else None,
                               stage=stage, category=category, source='parent')
            # Keep the first failure plus the most recent fifteen, including cleanup.
            if len(self._failures) == 16:
                del self._failures[1]
            self._failures.append(failure)
            raise

    def _admit_operation(self, operation, fields):
        # Complete state authority exists only on the explicit typed adapter.
        need(operation not in ('read_recovery_state', 'read_bound_state', 'restore_state'))

    @safe
    def probe_runtime(self):
        """Verify capsule and isolated imports only; never enumerate/open ports."""
        with self._lock:
            need(not self._claimed)
            self._operation('probe')
            return {'status': 'pass', 'hardware_access': False, 'serial_enumeration': False}

    @safe
    def claim(self, opaque_binding):
        with self._lock:
            need(not self._claimed and opaque_binding == self._binding)
            self._claimed = True
            self._operation('verify')
            return True

    @safe
    def reverify(self, opaque_binding):
        with self._lock:
            need(opaque_binding == self._binding)
            self._operation('verify')
            return True

    @safe
    def read(self, offset, size):
        with self._lock:
            need(type(offset) is int and type(size) is int and (offset, size) in SPANS)
            raw = base64.b64decode(self._operation('read', offset=offset, size=size)['data'], validate=True)
            need(len(raw) == size)
            if not self._mutated and self._originals is None:
                self._captured[(offset, size)] = raw
            return raw

    @safe
    def read_state_chunk(self, offset, size):
        """Original-only opaque state capture; never part of legacy write custody."""
        with self._lock:
            raw = base64.b64decode(self._operation('read_state', offset=offset, size=size)['data'], validate=True)
            need(len(raw) == size)
            return raw

    @safe
    def bind_originals(self, captured):
        with self._lock:
            values = self._normalize_originals(captured)
            need(not self._recovery_only)
            if self._originals is not None:
                need(values == self._originals)
                return True
            need(not self._mutated and set(self._captured) == SPANS and values == self._captured)
            self._originals = dict(self._captured)
            return True

    def _normalize_originals(self, captured):
        need(type(captured) is dict)
        if set(captured) == set(NAMED_SPANS):
            captured = {NAMED_SPANS[name]: raw for name, raw in captured.items()}
        need(set(captured) == SPANS)
        need(all(type(raw) is bytes and len(raw) == span[1] for span, raw in captured.items()))
        return dict(captured)

    @safe
    def bind_recovery_originals(self, captured):
        """Engine supplies hash-verified immutable custody under a fresh grant.

        Retained originals cannot be compared with live candidate bytes. This
        explicit mode permanently excludes candidate write and candidate boot.
        """
        with self._lock:
            need(self._claimed)
            values = self._normalize_originals(captured)
            need(self._originals is None or (self._recovery_only and self._originals == values))
            self._originals, self._recovery_only = values, True
            return True

    @safe
    def write(self, offset, raw):
        with self._lock:
            need(type(offset) is int and type(raw) is bytes and (offset, len(raw)) in WRITABLE and self._originals is not None)
            need(raw == self._originals[(offset, len(raw))] or (not self._recovery_only and offset == 0x10000 and raw == self._candidate))
            self._mutated = True  # An uncertain write cannot create new originals.
            self._operation('write', offset=offset, size=len(raw), data=base64.b64encode(raw).decode('ascii'))
            return True

    @safe
    def boot_candidate(self):
        with self._lock:
            need(not self._recovery_only and self._originals is not None and self.read(0x10000, 733184) == self._candidate)
            response = self._operation('restart_capture' if self._startup_enabled else 'restart')
            if self._startup_enabled:
                self.startup_diagnostics = response['startup_diagnostics']
            self._candidate_booted = True
            return True

    @safe
    def capture_after_confirmation(self):
        """One passive observation of the still-running candidate; never ROM sync."""
        with self._lock:
            need(self._confirmation_enabled and not self._recovery_only and
                 self._candidate_booted and not self._post_capture_used)
            self._post_capture_used = True
            response = self._operation('passive_capture')
            self.post_confirmation_diagnostics = response['startup_diagnostics']
            return self.post_confirmation_diagnostics

    @safe
    def reset_original(self):
        with self._lock:
            need(self._originals is not None)
            for (offset, size), raw in self._originals.items():
                need(self.read(offset, size) == raw)
            self._operation('restart')
            return True

    @safe
    def hold(self):
        with self._lock:
            self._operation('verify')  # ROM operations always finish no-reset.
            return True


class CompleteCustodyTransport(Transport):
    """Seven-region standard trial, separate from the legacy five-region grant.

    Normal binding proves the bytes came from this instance's read-only capture.
    Recovery binding consumes engine-validated custody under fresh recovery
    authority. The engine owns journals/grants and serial observation closure.
    """
    @safe
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self._state_captured = {}
        self._complete_originals = None
        self._original_boot_attempted = False
        self._restart_kind = None
        self._effect_callback = False

    def __repr__(self):
        return '<OT0304 complete-custody ROM transport>'

    def _complete_ready(self):
        need(self._claimed and self._complete_originals is not None)

    def _complete_writable(self):
        self._complete_ready()
        need(not self._original_boot_attempted and not self._effect_callback)

    def _normalize_complete(self, captured):
        need(type(captured) is dict and set(captured) == set(COMPLETE_NAMED_SPANS))
        need(all(type(raw) is bytes and len(raw) == COMPLETE_NAMED_SPANS[name][1]
                 for name, raw in captured.items()))
        return dict(captured)

    @safe
    def read_state_chunk(self, offset, size):
        with self._lock:
            raw = super().read_state_chunk(offset, size)
            self._state_captured[(offset, size)] = raw
            return raw

    @safe
    def bind_originals(self, captured):
        need(False)  # Five regions cannot authorize this adapter's mutations.

    @safe
    def bind_recovery_originals(self, captured):
        need(False)

    @safe
    def bind_complete_originals(self, captured):
        with self._lock:
            values = self._normalize_complete(captured)
            need(self._claimed and not self._recovery_only and not self._mutated and
                 not self._original_boot_attempted and self._complete_originals is None and
                 self._originals is None)
            need(set(self._state_captured) == STATE_READ_SPANS and
                 all(values[name] == self._state_captured[span]
                     for name, span in COMPLETE_NAMED_SPANS.items() if span in STATE_READ_SPANS))
            super().bind_originals({name: values[name] for name in NAMED_SPANS})
            self._complete_originals = values
            return True

    @safe
    def bind_complete_recovery_originals(self, captured):
        """Fresh recovery-only instance; engine must verify custody and grant."""
        with self._lock:
            values = self._normalize_complete(captured)
            need(self._claimed and self._recovery_only and not self._mutated and
                 not self._original_boot_attempted and self._complete_originals is None and
                 self._originals is None)
            super().bind_recovery_originals({name: values[name] for name in NAMED_SPANS})
            self._complete_originals = values
            return True

    def _operation(self, operation, **fields):
        with self._lock:
            return super()._operation(operation, **fields)

    @safe
    def _admit_operation(self, operation, fields):
        need(not self._effect_callback)
        if operation == 'read_recovery_state':
            need(self._recovery_only and self._complete_originals is None and
                 self._originals is None and not self._mutated and not self._original_boot_attempted)
            need(set(fields) == {'offset', 'size'} and type(fields['offset']) is int and
                 type(fields['size']) is int and
                 (fields['offset'], fields['size']) in STATE_READ_SPANS)
        elif operation in ('read_bound_state', 'restore_state'):
            self._complete_ready()
            expected = {'offset', 'size'}
            if operation == 'restore_state': expected |= {'data', 'original_sha256'}
            need(set(fields) == expected and type(fields['offset']) is int and
                 type(fields['size']) is int and
                 (fields['offset'], fields['size']) in STATE_READ_SPANS)
            if operation == 'restore_state':
                self._complete_writable()
                need(type(fields['data']) is str and
                     len(fields['data']) == 4 * ((fields['size'] + 2) // 3))
                raw = base64.b64decode(fields['data'], validate=True)
                name = next(name for name, span in COMPLETE_NAMED_SPANS.items()
                            if span == (fields['offset'], fields['size']))
                need(raw == self._complete_originals[name] and
                     base64.b64encode(raw).decode('ascii') == fields['data'] and
                     type(fields['original_sha256']) is str and
                     fields['original_sha256'] == hashlib.sha256(raw).hexdigest())
                self._mutated = True
        elif operation == 'write':
            self._complete_writable()
            need(set(fields) == {'offset', 'size', 'data'} and type(fields['offset']) is int
                 and type(fields['size']) is int and
                 (fields['offset'], fields['size']) in WRITABLE and type(fields['data']) is str)
            raw = base64.b64decode(fields['data'], validate=True)
            need(len(raw) == fields['size'] and
                 base64.b64encode(raw).decode('ascii') == fields['data'])
            need(raw == self._originals[(fields['offset'], fields['size'])] or
                 (not self._recovery_only and fields['offset'] == 0x10000 and raw == self._candidate))
            self._mutated = True
        elif operation in ('restart', 'restart_capture'):
            self._complete_ready()
            need(not fields)
            need((self._restart_kind == 'candidate' and not self._recovery_only and
                  not self._original_boot_attempted) or
                 (self._restart_kind == 'original' and self._original_boot_attempted and
                  operation == 'restart'))

    @safe
    def read_recovery_state_chunk(self, offset, size):
        """Fresh restore-only grant may finish an untouched partial capture."""
        with self._lock:
            return base64.b64decode(self._operation('read_recovery_state', offset=offset,
                size=size)['data'], validate=True)

    @safe
    def read_bound_state_chunk(self, offset, size):
        with self._lock:
            return base64.b64decode(self._operation('read_bound_state', offset=offset,
                size=size)['data'], validate=True)

    @safe
    def restore_state_chunk(self, offset, raw):
        with self._lock:
            need(type(raw) is bytes)
            self._operation('restore_state', offset=offset, size=len(raw),
                data=base64.b64encode(raw).decode('ascii'),
                original_sha256=hashlib.sha256(raw).hexdigest())
            return True

    @safe
    def write(self, offset, raw):
        with self._lock:
            self._complete_writable()
            return super().write(offset, raw)

    def _callback(self, callback):
        need(callable(callback))
        self._effect_callback = True
        try:
            callback()
        finally:
            self._effect_callback = False

    @safe
    def boot_candidate(self, *, before_boot):
        with self._lock:
            self._complete_writable()
            need(not self._recovery_only and callable(before_boot))
            need(self.read(0x10000, 733184) == self._candidate)
            self._callback(before_boot)  # Durable intent/deadline after slow ROM read.
            self._restart_kind = 'candidate'
            try:
                response = self._operation('restart_capture' if self._startup_enabled else 'restart')
            finally:
                self._restart_kind = None
            if self._startup_enabled:
                self.startup_diagnostics = response['startup_diagnostics']
            self._candidate_booted = True
            return True

    @safe
    def reset_original(self):
        need(False)  # Final equality includes opaque state and durable boot intent.

    @safe
    def reset_complete_original(self, *, before_reset):
        with self._lock:
            self._complete_writable()
            need(callable(before_reset))
            for name, (offset, size) in COMPLETE_NAMED_SPANS.items():
                read = self.read_bound_state_chunk if (offset, size) in STATE_READ_SPANS else self.read
                need(read(offset, size) == self._complete_originals[name])
            # Fence before callback and native restart: uncertainty never grants a
            # second NVS/state replay, including a callback that cannot persist.
            self._original_boot_attempted = True
            self._callback(before_reset)
            self._restart_kind = 'original'
            try:
                self._operation('restart')
            finally:
                self._restart_kind = None
            return True


@safe
def probe_runtime(*, manifest_path, manifest_sha256, private_root,
                  subprocess_run=subprocess.run, manifest_verifier=None):
    """Device-free entry into the same exact isolated child used by Transport."""
    instance = Transport.__new__(Transport)
    instance._manifest_path, instance._manifest_sha = Path(manifest_path), manifest_sha256
    instance._private = Path(private_root)
    need(instance._private.is_absolute() and instance._private.name == '.private' and instance._private.is_dir())
    instance._run, instance._verifier = subprocess_run, manifest_verifier
    instance._claimed = False
    instance._route = instance._identity = None
    instance._failures = []
    instance._lock = threading.RLock()
    return instance.probe_runtime()
