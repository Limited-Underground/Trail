"""Six-span OTCAND1 ROM adapter; imports and constructors never access USB.

The custody engine supplies fresh authority and absolute deadlines. This module
does not issue grants. Hardware methods run only when explicitly invoked. The
existing OT212 capsule is reused unchanged; a separately reviewed isolated
operator/bootstrap remains necessary for physical execution. USB/MAC matching
prevents accidental swaps, not cloning; Heltec identity comes from a reviewed
private device profile, separately from ROM chip/flash observations.
"""
from dataclasses import dataclass, field
import base64
import hashlib
import hmac
import importlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import threading
import time

from enrollment_candidate_custody import Admission, SPANS, descriptor, validate_request
from enrollment_candidate_controller import Clock


class AdapterError(RuntimeError):
    """Fixed category only; no routes, identifiers or ROM diagnostics."""


def need(value, category='rom_adapter_refused'):
    if not value:
        raise AdapterError(category)


def identity(value):
    need(type(value) is str and re.fullmatch(
        r'(?:[0-9a-fA-F]{12}|[0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5}|[0-9a-fA-F]{2}(?:-[0-9a-fA-F]{2}){5})', value))
    return value.replace(':', '').replace('-', '').lower()


def opaque_identity(key, value):
    need(type(key) is bytes and len(key) == 32)
    return hmac.new(key, b'OT218-DEVICE\0' + identity(value).encode('ascii'), hashlib.sha256).hexdigest()


@dataclass(frozen=True, repr=False)
class DeviceProfile:
    model: str
    device_binding: str
    flash_bytes: int
    evidence_sha256: str


@dataclass(frozen=True, repr=False)
class PassiveToken:
    binding: str
    generation: int
    nonce: object = field(default_factory=object)


# This exact dispatch is executed by WORKER and directly by fake-SDK tests.
# API references: pinned esptool5.3.1 loader.get_security_info(cache=False),
# ESP32S3ROM.connect(mode, attempts), cmds.read_flash/write_flash/reset_chip.
WORKER_LOGIC = r'''
import base64, contextlib, hashlib, io, math, re, struct, time
class WorkerError(RuntimeError): pass
def require(value):
    if not value: raise WorkerError('rom_worker_refused')
def pin(raw): return {'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
def normalized(value):
    require(type(value) is str and re.fullmatch(r'(?:[0-9a-fA-F]{12}|[0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5}|[0-9a-fA-F]{2}(?:-[0-9a-fA-F]{2}){5})',value))
    return value.replace(':','').replace('-','').lower()
REGIONS={'bootloader':(0,32768),'partition':(0x8000,4096),'otadata':(0x9000,8192),
 'nvs':(0xd000,12288),'application':(0x10000,733184),'ota0_prefix':(0x500000,16384)}
ORIGINAL={'otadata':(1,0,0x9000,0x2000),'nvs':(1,2,0xd000,0x3000),
 'factory':(0,0,0x10000,0x4f0000),'ota_0':(0,0x10,0x500000,0x500000),
 'ota_1':(0,0x11,0xa00000,0x500000),'ot_state':(0x40,0,0xf00000,0x100000)}
CANDIDATE={name:value for name,value in ORIGINAL.items() if name not in ('ota_0','ota_1')}
CANDIDATE['ot238_nvs']=(1,2,0x500000,0x4000)
def layout(raw,expected):
    require(type(raw) is bytes and len(raw)==4096)
    entries={}; at=0
    while at+32<=len(raw):
        block=raw[at:at+32]
        if block==b'\xff'*32: break
        if block[:2]==b'\xeb\xeb':
            require(block[2:16]==b'\xff'*14 and block[16:]==hashlib.md5(raw[:at]).digest())
            at+=32; break
        magic,kind,subtype,offset,size,label,flags=struct.unpack('<HBBII16sI',block)
        require(magic==0x50aa and flags==0 and offset%4096==0 and size%4096==0 and size>0)
        text=label.split(b'\0',1)[0].decode('ascii');require(text not in entries)
        entries[text]=(kind,subtype,offset,size);at+=32
    require(entries==expected and raw[at:]==b'\xff'*(len(raw)-at))
    return True
def strict_reset(strategy,check):
    # Keep pinned reset line choreography, without reopen/retry/error swallow.
    check();require(strategy.port.is_open is True)
    strategy.reset();check()
def execute_request(q,rom_class,cmds,comports,clock=time.monotonic,*,loader=None,reset=None):
    require(type(q) is dict and q.get('schema')=='OT-CANDIDATE-ROM-WORKER-1')
    deadline=q['deadline'];last=None
    def check():
        nonlocal last
        now=clock();require(type(now) in (int,float) and math.isfinite(now)
            and (last is None or now>=last) and type(deadline) in (int,float)
            and math.isfinite(deadline) and now<deadline);last=now
    expected=normalized(q['identity'])
    def route():
        check();ports=list(comports());check();matches=[]
        for port in ports:
            if port.vid==0x303a and port.pid==0x1001:
                try: same=normalized(port.serial_number)==expected
                except Exception: same=False
                if same:matches.append(port)
        require(len(matches)==1 and type(matches[0].device) is str
            and re.fullmatch(r'COM[1-9][0-9]{0,3}',matches[0].device))
        value=matches[0].device;require(sum(p.device==value for p in ports)==1)
        check();return value
    selected=route();op=q['operation']
    if op=='route':return {'ok':True,'closed':True,'route':selected}
    require(op in ('guard','read','write','boot_candidate','restart_candidate','reset_original','hold_rom'))
    require(reset is not None)
    reset.ResetStrategy.__call__=lambda strategy:strict_reset(strategy,check)
    require(q['mode'] in ('normal','restore-only'))
    originals=q['originals'];candidate=q['candidate']
    require(type(originals) is dict and set(originals)==set(REGIONS)
        and type(candidate) is dict and set(candidate)=={'application','partition','ota0_prefix'})
    for name,(_,size) in REGIONS.items():
        value=originals[name];require(type(value) is dict and set(value)=={'bytes','sha256'}
            and value['bytes']==size and re.fullmatch('[0-9a-f]{64}',value['sha256']))
    for name,value in candidate.items():
        require(type(value) is dict and set(value)=={'bytes','sha256'}
            and value['bytes']==REGIONS[name][1] and re.fullmatch('[0-9a-f]{64}',value['sha256']))
    esp=None;result=None;diagnostics=io.StringIO()
    try:
        with contextlib.redirect_stdout(diagnostics),contextlib.redirect_stderr(diagnostics):
            check();require(route()==selected)
            esp=rom_class(selected,115200,False)
            esp.connect('default-reset',attempts=1);check()
            require(esp.CHIP_NAME=='ESP32-S3' and esp.IS_STUB is False
                and esp.sync_stub_detected is False and esp.secure_download_mode is False)
            si=esp.get_security_info(cache=False);check()
            require(type(si) is dict and type(si['flags']) is int and 0<=si['flags']<1<<11
                and not si['flags']&((1<<0)|(1<<2)|(1<<8)|(1<<9)|(1<<10))
                and si['chip_id']==9 and type(si['flash_crypt_cnt']) is int
                and 0<=si['flash_crypt_cnt']<=7 and si['flash_crypt_cnt'].bit_count()%2==0
                and esp.get_secure_boot_enabled()==0 and esp.get_flash_encryption_enabled() is False
                and esp.get_encrypted_download_disabled()==0)
            mac=esp.read_mac('BASE_MAC');require(type(mac) in (tuple,list) and len(mac)==6
                and all(type(x) is int and 0<=x<=255 for x in mac)
                and bytes(mac).hex()==expected);check();require(route()==selected)
            cmds.attach_flash(esp);require(cmds.detect_flash_size(esp)=='16MB' and esp.flash_type()==0);check()
            def read(name):
                check();require(route()==selected)
                offset,size=REGIONS[name]
                raw=cmds.read_flash(esp,offset,size,output=None,flash_size='keep',no_progress=True)
                check();require(type(raw) is bytes and len(raw)==size and route()==selected);return raw
            table=read('partition');kind=None
            if pin(table)==originals['partition']:
                layout(table,ORIGINAL);kind='original'
            elif pin(table)==candidate['partition']:
                layout(table,CANDIDATE);kind='candidate'
            else:require(q['mode']=='restore-only')
            ota=read('otadata')
            factory=(kind=='candidate' or (kind=='original' and ota==b'\xff'*8192))
            require(factory or q['mode']=='restore-only')
            selection='verified-factory' if factory else 'verified-rom-restore-only'
            admission={'layout_sha256':hashlib.sha256(table).hexdigest(),'boot_selection':selection}
            if op=='read':
                name=q['span'];require(name in REGIONS);result={'ok':True,'closed':True,
                    'data':base64.b64encode(read(name)).decode('ascii'),'admission':admission}
            elif op=='write':
                name=q['span'];require(name in REGIONS and name!='bootloader')
                raw=base64.b64decode(q['data'],validate=True);require(len(raw)==REGIONS[name][1])
                if q['write_kind']=='restore':require(q['mode']=='restore-only' and pin(raw)==originals[name])
                else:require(q['write_kind']=='candidate' and q['mode']=='normal'
                    and factory and name in candidate and pin(raw)==candidate[name])
                check();require(route()==selected)
                esp.WRITE_FLASH_ATTEMPTS=1 # SDK otherwise reconnects/retries lost writes.
                require(loader is not None and type(loader.WRITE_BLOCK_ATTEMPTS) is int)
                # Separate loader.flash_block retries must also be single-shot.
                # This is local to the isolated worker, never a capsule edit.
                loader.WRITE_BLOCK_ATTEMPTS=1
                cmds.write_flash(esp,[(REGIONS[name][0],raw)],flash_size='keep',flash_mode='keep',
                    flash_freq='keep',no_compress=True,no_progress=True,force=False,
                    erase_all=False,encrypt=False,ignore_flash_enc_efuse=False)
                check();require(read(name)==raw)
                result={'ok':True,'closed':True,'admission':admission}
            elif op in ('boot_candidate','restart_candidate','reset_original'):
                require(factory)
                if op in ('boot_candidate','restart_candidate'):
                    require(q['mode']=='normal' and kind=='candidate')
                    # Initial provisioning requires the blank named-NVS prefix.
                    # A warm restart preserves the exact already-prepared NVS.
                    names=candidate if op=='boot_candidate' else ('application','partition')
                    for name in names:require(pin(read(name))==candidate[name])
                    for name in ('bootloader','nvs','otadata'):require(pin(read(name))==originals[name])
                else:
                    require(q['mode']=='restore-only' and kind=='original')
                    for name in REGIONS:require(pin(read(name))==originals[name])
                check();require(route()==selected and esp.uses_usb_jtag_serial() is True
                    and esp.uses_usb_otg() is False and esp.uses_hardware_flow_control() is False)
                # Selected S3/native-JTAG path, with checked force-download
                # clear and one pinned HardReset call. No OTG/WDT alternative.
                esp.write_reg(esp.RTC_CNTL_OPTION1_REG,0,esp.RTC_CNTL_FORCE_DOWNLOAD_BOOT_MASK)
                check();value=esp.read_reg(esp.RTC_CNTL_OPTION1_REG)
                require(type(value) is int and value&esp.RTC_CNTL_FORCE_DOWNLOAD_BOOT_MASK==0)
                check();require(route()==selected)
                reset.HardReset(esp._port,uses_usb=False,flow_control=False)();check()
                result={'ok':True,'closed':True,'admission':admission}
            else:result={'ok':True,'closed':True,'admission':admission}
            require(len(diagnostics.getvalue())<=1048576)
    finally:
        if esp is not None:
            esp._port.close();require(esp._port.is_open is False)
    check();return result
'''

WORKER = WORKER_LOGIC + r'''
import json,os,pathlib,sys
try:
    q=json.loads(sys.stdin.buffer.read(3000001));m=q['manifest'];root=pathlib.Path(m['root'])
    require(sys.flags.isolated and sys.flags.no_site and sys.dont_write_bytecode
        and pathlib.Path(sys.executable).resolve()==root/'python.exe'
        and sys.version_info[:3]==(3,14,6)
        and [pathlib.Path(p).resolve() for p in sys.path]==[root/p for p in ('Lib','DLLs','packages','policy')])
    for name,p in m['files'].items():
        path=root/name;require(not any(x.is_symlink() or getattr(x,'is_junction',lambda:False)()
            for x in (path,*path.parents)))
        raw=path.read_bytes();require(pin(raw)==p)
    require({p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()}==set(m['files'])
        and (root/'python314._pth').read_bytes()==b'Lib\nDLLs\npackages\npolicy\n'
        and (root/'esptool.cfg').read_bytes()==b'[esptool]\n')
    import esptool,serial
    import esptool.cmds as cmds
    import esptool.loader as loader
    import esptool.reset as reset
    from esptool.targets.esp32s3 import ESP32S3ROM
    from serial.tools.list_ports import comports
    require(esptool.__version__=='5.3.1' and serial.__version__=='3.5')
    for module in tuple(sys.modules.values()):
        origin=getattr(module,'__file__',None)
        if origin:
            path=pathlib.Path(origin).resolve();require(path.is_relative_to(root)
                and path.relative_to(root).as_posix() in m['files'])
    answer=execute_request(q,ESP32S3ROM,cmds,comports,loader=loader,reset=reset)
    print(json.dumps(answer,separators=(',',':')))
except BaseException:
    print('{"ok":false,"closed":false}');sys.exit(1)
'''


def _private(path):
    path = Path(path)
    need(path.is_absolute() and path.name == '.private')
    for part in (path, *path.parents):
        need(not part.is_symlink() and not getattr(part, 'is_junction', lambda: False)())
    return path


class Runtime:
    def __init__(self, manifest_path, manifest_sha256, private_root, *,
                 subprocess_run=subprocess.run, manifest_verifier=None, monotonic=time.monotonic):
        self.manifest_path, self.manifest_sha256 = Path(manifest_path), manifest_sha256
        self.private_root = _private(private_root)
        need(type(manifest_sha256) is str and re.fullmatch('[0-9a-f]{64}', manifest_sha256))
        self.runner, self.verifier, self.clock = subprocess_run, manifest_verifier, Clock(monotonic)
        self._active, self._lock = False, threading.Lock()

    def verify(self, deadline):
        self.clock.check(deadline)
        if self.verifier is None:
            from security_policy_deadline_operator import verify_manifest
            manifest = verify_manifest(self.manifest_path, self.manifest_sha256)
        else:
            manifest = self.verifier(self.manifest_path, self.manifest_sha256)
        self.clock.check(deadline)
        need(type(manifest) is dict and self.private_root.resolve() ==
             (Path(manifest['worktree']) / '.private').resolve())
        return manifest

    def serial_api(self, deadline):
        manifest = self.verify(deadline)
        root = Path(manifest['root']).resolve()
        need(sys.flags.isolated and sys.flags.no_site and sys.dont_write_bytecode
             and Path(sys.executable).resolve() == root / 'python.exe'
             and sys.version_info[:3] == (3, 14, 6)
             and [Path(p).resolve() for p in sys.path] ==
             [root / p for p in ('Lib', 'DLLs', 'packages', 'policy')], 'isolated_caller_required')
        # Do not alter sys.path/sys.modules to assemble an ambient caller.
        for module in tuple(sys.modules.values()):
            origin = getattr(module, '__file__', None)
            if origin:
                path = Path(origin).resolve()
                need(path.is_relative_to(root) and path.relative_to(root).as_posix()
                     in manifest['files'], 'isolated_caller_required')
        serial = importlib.import_module('serial')
        ports = importlib.import_module('serial.tools.list_ports')
        need(serial.__version__ == '3.5')
        for name, module in tuple(sys.modules.items()):
            if name == 'serial' or name.startswith('serial.'):
                path = Path(module.__file__).resolve()
                need(path.is_relative_to(root) and path.relative_to(root).as_posix() in manifest['files'])
        self.clock.check(deadline)
        return serial.Serial, ports.comports

    def invoke(self, payload, deadline):
        need(self._lock.acquire(blocking=False), 'runtime_busy')
        try:
            self.clock.check(deadline)
            manifest = self.verify(deadline)
            query = dict(payload, manifest=manifest, deadline=deadline,
                         schema='OT-CANDIDATE-ROM-WORKER-1')
            raw = json.dumps(query, separators=(',', ':'), allow_nan=False).encode('ascii')
            need(len(raw) <= 3000000)
            env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('PYTHON', 'ESPTOOL_'))}
            env['ESPTOOL_CFGFILE'] = str(Path(manifest['root']) / 'esptool.cfg')
            self._active = True
            remaining = deadline - self.clock.now()
            need(remaining > 0, 'deadline_expired')
            result = self.runner([str(Path(manifest['root']) / 'python.exe'), '-I', '-S', '-B', '-c', WORKER],
                input=raw, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=remaining,
                check=False, cwd=manifest['root'], env=env)
            self.clock.check(deadline)
            need(result.returncode == 0 and type(result.stdout) is bytes and len(result.stdout) <= 1100000
                 and type(result.stderr) is bytes and len(result.stderr) <= 1048576)
            answer = json.loads(result.stdout)
            op = payload['operation']
            keys = {'ok', 'closed', 'route'} if op == 'route' else {'ok', 'closed', 'admission', 'data'} if op == 'read' else {'ok', 'closed', 'admission'}
            need(type(answer) is dict and set(answer) == keys and answer['ok'] is True and answer['closed'] is True)
            if op == 'route':
                need(type(answer['route']) is str and re.fullmatch(r'COM[1-9][0-9]{0,3}', answer['route']))
            else:
                admission = answer['admission']
                need(type(admission) is dict and set(admission) == {'layout_sha256', 'boot_selection'}
                     and type(admission['layout_sha256']) is str and re.fullmatch('[0-9a-f]{64}', admission['layout_sha256'])
                     and admission['boot_selection'] in ('verified-factory', 'verified-rom-restore-only'))
            return answer
        except AdapterError:
            raise
        except BaseException:
            raise AdapterError('rom_operation_failed') from None
        finally:
            self._active = False
            self._lock.release()

    def route(self, expected_identity, deadline):
        return self.invoke({'operation': 'route', 'identity': identity(expected_identity)}, deadline)['route']

    def assert_idle(self):
        return not self._active and not self._lock.locked()


class HardwareLease:
    def __init__(self, private_root, *, monotonic=time.monotonic):
        self.private_root, self.clock = _private(private_root), Clock(monotonic)
        self._file, self._states, self._command = None, {}, threading.Lock()

    def acquire(self, deadline):
        self.clock.check(deadline)
        if self._file is not None:
            return True
        need(self.private_root.is_dir())
        path = self.private_root / 'enrollment-candidate-hardware.lock'
        need(not path.is_symlink() and not getattr(path, 'is_junction', lambda: False)())
        handle = path.open('a+b', buffering=0)
        try:
            handle.seek(0)
            if os.name == 'nt':
                import msvcrt
                msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            if os.fstat(handle.fileno()).st_size == 0:
                handle.write(b'\0');os.fsync(handle.fileno())
            self.clock.check(deadline)
            self._file = handle
            return True
        except BaseException:
            handle.close()
            raise AdapterError('hardware_lease_busy') from None

    def _state(self, binding):
        need(type(binding) is str and re.fullmatch('[0-9a-f]{64}', binding))
        return self._states.setdefault(binding, {'mode': 'unclaimed', 'generation': 0,
            'token': None, 'handle': None, 'route': None, 'uncertain': False})

    def enter_rom(self, binding, deadline):
        self.acquire(deadline)
        state = self._state(binding)
        need(not state['uncertain'] and state['mode'] != 'passive', 'passive_owner_active')
        state['mode'] = 'rom'

    def mark_boot(self, binding, original=False):
        state = self._state(binding)
        need(state['mode'] == 'rom')
        state['mode'] = 'original-ready' if original else 'app-ready'

    def begin_passive(self, binding, generation, deadline):
        self.clock.check(deadline)
        need(self._file is not None)
        state = self._state(binding)
        need(not state['uncertain'] and state['mode'] in ('app-ready', 'app-released')
             and type(generation) is int and generation > state['generation'], 'passive_owner_active')
        token = PassiveToken(binding, generation)
        state.update(mode='passive', generation=generation, token=token, handle=None, route=None)
        return token

    def attach_passive(self, token, handle, route):
        state = self._state(token.binding)
        need(state['token'] is token and state['mode'] == 'passive' and state['handle'] is None
             and handle.is_open is False and type(route) is str and re.fullmatch(r'COM[1-9][0-9]{0,3}', route))
        state.update(handle=handle, route=route)

    def guard_passive(self, token, handle):
        state = self._state(token.binding)
        return (not state['uncertain'] and state['mode'] == 'passive' and state['token'] is token
                and state['handle'] is handle and handle.is_open is True and handle.port == state['route'])

    def release_passive(self, token, handle):
        state = self._state(token.binding)
        if state['uncertain']:
            return False
        valid = (state['token'] is token and state['mode'] == 'passive'
                 and state['handle'] is handle and handle.is_open is False)
        if not valid:
            state['uncertain'] = True
            return False
        state.update(mode='app-released', token=None, handle=None, route=None)
        return True

    def abort_passive(self, token):
        state = self._state(token.binding)
        need(state['token'] is token and state['mode'] == 'passive' and state['handle'] is None
             and not state['uncertain'])
        state.update(mode='app-released', token=None, route=None)
        return True

    def assert_idle(self):
        return not self._command.locked() and all(
            not state['uncertain'] and state['mode'] != 'passive' for state in self._states.values())

    def close(self):
        if not self.assert_idle() or any(state['mode'] not in ('original-ready', 'unclaimed') for state in self._states.values()):
            return False
        if self._file is not None:
            handle, self._file = self._file, None
            try:
                handle.seek(0)
                if os.name == 'nt':
                    import msvcrt
                    msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
                else:
                    import fcntl
                    fcntl.flock(handle.fileno(), fcntl.LOCK_UN)
            finally:
                handle.close()
        return True


class ROMBackend:
    def __init__(self, runtime, lease, *, request, role, images, binding_key,
                 expected_identity, device_profile, recovery_only=False):
        request = validate_request(request)
        need(role in ('A', 'B') and type(recovery_only) is bool)
        need(runtime.manifest_sha256 == request['runtime_sha256'], 'runtime_binding_invalid')
        self.runtime, self.lease, self.request, self.role = runtime, lease, request, role
        self._identity, self._binding = identity(expected_identity), request['roles'][role]['device_binding']
        need(hmac.compare_digest(opaque_identity(binding_key, self._identity), self._binding))
        need(type(device_profile) is DeviceProfile and device_profile.model == 'heltec_v4_esp32s3'
             and device_profile.device_binding == self._binding and device_profile.flash_bytes == 16777216
             and type(device_profile.evidence_sha256) is str and re.fullmatch('[0-9a-f]{64}', device_profile.evidence_sha256))
        need(type(images) is dict and set(images) == {'application', 'partition'})
        for name, raw in images.items():
            need(type(raw) is bytes and descriptor(raw) == request['images'][name])
        self._candidate = {'application': images['application'].ljust(SPANS['application'][1], b'\xff'),
            'partition': images['partition'], 'ota0_prefix': b'\xff' * SPANS['ota0_prefix'][1]}
        self._profile, self._restore_only = device_profile, recovery_only
        self._claimed, self._mutated, self._failed = False, False, False
        self._captures, self._counts = {}, {}
        self._mode, self._lock = 'idle', threading.RLock()
        self._execute_deadline, self._restore_deadline = None, None
        self._initial_boot, self._warm_restart = False, False

    def __repr__(self):
        return '<OTCAND1 six-span ROM backend>'

    def _call(self, operation, deadline, **fields):
        need(self._claimed and self._mode == 'rom', 'rom_owner_required')
        need(self.runtime.manifest_sha256 == self.request['runtime_sha256'], 'runtime_binding_invalid')
        self._deadline(deadline)
        state = self.lease._state(self._binding)
        need(not state['uncertain'] and state['mode'] == 'rom', 'passive_owner_active')
        need(self.lease._command.acquire(blocking=False), 'hardware_lease_busy')
        try:
            payload = {'operation': operation, 'identity': self._identity,
                'mode': 'restore-only' if self._restore_only else 'normal',
                'originals': self.request['roles'][self.role]['originals'],
                'candidate': {name: descriptor(raw) for name, raw in self._candidate.items()}, **fields}
            return self.runtime.invoke(payload, deadline)
        except BaseException:
            self._failed = True
            self._mode = 'failed'
            raise
        finally:
            self.lease._command.release()

    def _admission(self, result):
        row = result['admission']
        return Admission(self._binding, self._profile.model, self._profile.flash_bytes,
            'verified-write-permitted', row['layout_sha256'], row['boot_selection'], True)

    def _deadline(self, deadline):
        bound = self._restore_deadline if self._restore_only else self._execute_deadline
        need(type(deadline) in (int, float) and math.isfinite(deadline)
             and bound is not None and deadline <= bound, 'deadline_extended')
        self.runtime.clock.check(deadline)

    def _safe_nvs(self, deadline):
        # Reuse the maintained parser unchanged. A safe captured default-NVS
        # image plus the worker's immediate exact-pin check prevents an original
        # reset marker from escaping the six-span custody boundary at boot.
        from ble_confirmation_trial import safe_reset_marker
        answer = self._call('read', deadline, span='nvs')
        raw = base64.b64decode(answer['data'], validate=True)
        need(descriptor(raw) == self.request['roles'][self.role]['originals']['nvs'])
        try:
            need(safe_reset_marker(raw) is True, 'pending_reset_storage')
        except BaseException:
            raise AdapterError('pending_reset_storage') from None
        self._deadline(deadline)

    def claim(self, binding, deadline):
        with self._lock:
            need(binding == self._binding)
            self.lease.enter_rom(binding, deadline)
            # Reclaim after a trial/write error is terminal restoration only.
            self._restore_only = self._restore_only or self._claimed or self._mutated or self._failed
            if self._restore_only:
                if self._restore_deadline is None:
                    self._restore_deadline = deadline
            elif self._execute_deadline is None:
                self._execute_deadline = deadline
            self._claimed, self._mode = True, 'rom'
            result = self._admission(self._call('guard', deadline))
            if not self._restore_only:
                self._safe_nvs(deadline)
            return result

    def guard(self, binding, deadline):
        with self._lock:
            need(binding == self._binding)
            return self._admission(self._call('guard', deadline))

    def read(self, offset, size, deadline):
        with self._lock:
            name = next((name for name, span in SPANS.items() if span == (offset, size)), None)
            need(name is not None and type(offset) is int and type(size) is int)
            answer = self._call('read', deadline, span=name)
            raw = base64.b64decode(answer['data'], validate=True)
            need(len(raw) == size)
            if not self._mutated and not self._restore_only:
                need(descriptor(raw) == self.request['roles'][self.role]['originals'][name])
                self._captures[name] = raw
                self._counts[name] = self._counts.get(name, 0) + 1
            return raw

    def write(self, offset, raw, deadline):
        with self._lock:
            name = next((name for name, span in SPANS.items() if span == (offset, len(raw))), None)
            need(name is not None and name != 'bootloader' and type(raw) is bytes)
            if self._restore_only:
                need(descriptor(raw) == self.request['roles'][self.role]['originals'][name])
                kind = 'restore'
            else:
                need(set(self._captures) == set(SPANS) and all(self._counts.get(n, 0) >= 2 for n in SPANS)
                     and name in self._candidate and raw == self._candidate[name])
                kind = 'candidate'
            self._mutated = True # Never recapture originals after uncertain write.
            self._call('write', deadline, span=name, write_kind=kind,
                       data=base64.b64encode(raw).decode('ascii'))
            return True

    def hold_rom(self, deadline):
        with self._lock:
            self._call('hold_rom', deadline)
            return True

    def boot_candidate(self, deadline):
        with self._lock:
            need(not self._restore_only and not self._initial_boot)
            self._initial_boot = True # One attempt, including ambiguous failures.
            self._safe_nvs(deadline)
            self._call('boot_candidate', deadline)
            self.lease.mark_boot(self._binding)
            self._mode = 'app'
            return True

    def restart_candidate(self, deadline):
        """One case-bound warm restart; preserves prepared candidate storage.

        Called by the operator's injected sequential pair restart only after
        both passive handles have verified closure. It is not a new claim,
        provisioning action, deadline renewal, or recovery retry.
        """
        with self._lock:
            need(self.request['case'] in ('retained_rekey', 'recovery_after_A_commit',
                 'recovery_after_B_commit') and self._initial_boot and not self._warm_restart
                 and not self._restore_only and not self._failed and self._mode == 'app',
                 'restart_not_admitted')
            self._deadline(deadline)
            need(self.lease.assert_idle() is True
                 and self.lease._state(self._binding)['mode'] == 'app-released',
                 'passive_owner_active')
            self._warm_restart = True
            self.lease.enter_rom(self._binding, deadline)
            self._mode = 'rom'
            self._safe_nvs(deadline)
            self._call('restart_candidate', deadline)
            self.lease.mark_boot(self._binding)
            self._mode = 'app'
            return True

    def reset_original(self, deadline):
        with self._lock:
            need(self._restore_only)
            self._safe_nvs(deadline)
            self._call('reset_original', deadline)
            self.lease.mark_boot(self._binding, original=True)
            self._mode = 'original'
            return True

    def close(self):
        return self.runtime.assert_idle() is True and self.lease._state(self._binding)['mode'] != 'passive'

    def assert_idle(self):
        return self.close()
