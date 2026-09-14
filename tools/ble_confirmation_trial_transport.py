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
NAMED_SPANS = {'bootloader': (0, 32768), 'partition': (0x8000, 4096),
               'ota': (0x9000, 8192), 'application': (0x10000, 733184), 'nvs': (0xd000, 12288)}


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
def need(x):
    if not x: raise ValueError()
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
    if q['operation']=='probe':
        print('{"ok":true}')
        raise ProbeFinished()
    route=q['route']; expected=ident(q['identity'])
    need(isinstance(route,str) and re.fullmatch(r'COM[1-9][0-9]{0,3}',route))
    if q['operation']=='passive_capture':
        # No ROM verification here: it would reset the running candidate.
        print(json.dumps({'ok':True,'startup_diagnostics':capture_startup(serial,comports,expected)},separators=(',',':')))
        raise ProbeFinished()
    def passive():
        ports=list(comports())
        routes=[p for p in ports if p.device==route]
        matches=[p for p in ports if p.vid==0x303a and p.pid==0x1001 and ident(p.serial_number)==expected]
        need(len(routes)==len(matches)==1 and routes[0] is matches[0])
    def run(operation,restart=False):
        # Preserve the accepted explicit hardware release after the ROM run.
        passive()
        argv=['--chip','esp32s3','--port',route,'--baud','115200','--before','default-reset','--after','hard-reset' if restart else 'no-reset','--no-stub']+operation
        out=io.StringIO()
        with contextlib.redirect_stdout(out),contextlib.redirect_stderr(out): esptool.main(argv)
        value=out.getvalue(); need(len(value)<=1048576)
        if not restart: passive()
        return value
    text=run(['read-mac'])
    values=re.findall(r'(?im)^\s*MAC:\s*([^\r\n]+?)\s*$',text)
    need(len(values) in (1,2) and all(ident(v)==expected for v in values))
    text=run(['flash-id']); need('ESP32-S3' in text and re.search(r'(?m)^Detected flash size:\s*16MB\s*$',text))
    op=q['operation']; result={'ok':True}
    spans={(0,32768),(0x8000,4096),(0x9000,8192),(0xd000,12288),(0x10000,733184)}
    if op in ('read','write'):
        offset=q['offset']; size=q['size']; need(type(offset) is int and type(size) is int and (offset,size) in spans)
        private=pathlib.Path(q['private_root']); need(private.is_absolute() and private.name=='.private' and private.is_dir())
        need(not any(p.is_symlink() or getattr(p,'is_junction',lambda:False)() for p in (private,*private.parents)))
        with tempfile.TemporaryDirectory(prefix='ot218-rom-',dir=private) as temp:
            p=pathlib.Path(temp)/'region.bin'
            if op=='read':
                run(['read-flash',hex(offset),str(size),str(p)]); raw=p.read_bytes(); need(len(raw)==size)
                result['data']=base64.b64encode(raw).decode('ascii')
            else:
                need((offset,size) in {(0xd000,12288),(0x10000,733184)})
                raw=base64.b64decode(q['data'],validate=True); need(len(raw)==size)
                p.write_bytes(raw); run(['write-flash','--flash-size','16MB',hex(offset),str(p)])
    elif op in ('restart', 'restart_capture'):
        run(['run'],True)
        if op=='restart_capture':
            result['startup_diagnostics']=capture_startup(serial,comports,expected)
    else: need(op=='verify')
    print(json.dumps(result,separators=(',',':')))
except ProbeFinished:
    pass
except BaseException:
    print('{"ok":false}'); sys.exit(1)
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
        self._lock = threading.RLock()

    def __repr__(self):
        return '<OT218 single-device ROM transport>'

    def _manifest(self):
        if self._verifier is None:
            from security_policy_deadline_operator import verify_manifest
            return verify_manifest(self._manifest_path, self._manifest_sha)
        return self._verifier(self._manifest_path, self._manifest_sha)

    def _operation(self, operation, **fields):
        need(self._claimed or operation == 'probe')
        if operation not in ('probe', 'passive_capture'):
            self._candidate_booted = False
        manifest = self._manifest()
        need(self._private.resolve() == (Path(manifest['worktree']) / '.private').resolve())
        payload = dict(manifest=manifest, route=self._route, identity=self._identity,
                       private_root=str(self._private), operation=operation, **fields)
        env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('PYTHON', 'ESPTOOL_'))}
        env['ESPTOOL_CFGFILE'] = str(Path(manifest['root']) / 'esptool.cfg')
        result = self._run([str(Path(manifest['root']) / 'python.exe'), '-I', '-S', '-B', '-c', WORKER],
                           input=json.dumps(payload, separators=(',', ':')).encode(),
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=240,
                           check=False, cwd=manifest['root'], env=env)
        need(result.returncode == 0 and type(result.stdout) is bytes and len(result.stdout) <= 1100000)
        obj = json.loads(result.stdout)
        expected_keys = ({'ok', 'data'} if operation == 'read' else
                         {'ok', 'startup_diagnostics'} if operation in ('restart_capture', 'passive_capture') else {'ok'})
        need(type(obj) is dict and obj.get('ok') is True and set(obj) == expected_keys)
        if operation in ('restart_capture', 'passive_capture'):
            validate_startup_result(obj['startup_diagnostics'])
        return obj

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
    instance._lock = threading.RLock()
    return instance.probe_runtime()
