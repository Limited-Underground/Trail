"""Source-pinned nonradio pair operator. Prepare is read-only and never enumerates.

Private ephemeral stdin is {binding_key:hex, roles:{A:{identity},B:{identity}}}.
No identifiers or serial payloads are printed. Parent retains binding key across
restore-only recovery. Binding/request/grant hashes are trusted reviewed inputs,
not signatures. No grant issuance or installation is performed here.
"""
import argparse
import hashlib
import importlib
import importlib.util
import json
from pathlib import Path
import queue
import sys
import threading
import time

REQUIRED = {'tools/' + n + '.py' for n in (
    'pair_trial_operator', 'pair_confirmation_trial', 'pair_bench_bridge',
    'ble_confirmation_trial', 'ble_confirmation_trial_transport', 'ble_startup_diagnostics',
    'security_policy_bundle', 'security_policy_capture', 'security_policy_deadline_operator',
    'security_policy_execution', 'security_policy_input_readback')}


def need(value):
    if not value: raise ValueError('pair_operator_refused')


def digest(raw): return hashlib.sha256(raw).hexdigest()


def regular(root, relative):
    need(type(relative) is str and relative and not Path(relative).is_absolute())
    unresolved = root / relative
    for item in (unresolved, *unresolved.parents):
        need(not item.is_symlink() and not item.is_junction())
        if item == root: break
    resolved = unresolved.resolve()
    need(resolved.is_relative_to(root.resolve()) and resolved.is_file())
    return resolved


def checked(root, relative, pin):
    raw = regular(root, relative).read_bytes()
    need(digest(raw) == pin)
    return raw


def verify_binding(root, binding_path, pin, *, recovery=False):
    raw = binding_path.read_bytes(); need(digest(raw) == pin)
    binding = json.loads(raw)
    need(type(binding) is dict and set(binding) == {'schema', 'files', 'runtime_manifest',
         'runtime_sha256', 'candidates', 'bridge_dependencies', 'host_python_sha256'})
    need(binding['schema'] == 'OT-PAIR-OPERATOR-1' and type(binding['files']) is dict and REQUIRED <= set(binding['files']))
    need(digest(Path(sys.executable).read_bytes()) == binding['host_python_sha256'])
    for name, expected in binding['files'].items(): checked(root, name, expected)
    checked(root, binding['runtime_manifest'], binding['runtime_sha256'])
    need(type(binding['candidates']) is dict and set(binding['candidates']) == {'A', 'B'})
    candidates = {}
    for role, row in binding['candidates'].items():
        need(type(row) is dict and set(row) == {'path', 'bytes', 'sha256'} and type(row['bytes']) is int and 0 < row['bytes'] <= 733184)
        if not recovery:
            candidates[role] = checked(root, row['path'], row['sha256'])
            need(len(candidates[role]) == row['bytes'])
    if recovery:
        return binding, candidates
    dep = binding['bridge_dependencies']
    need(type(dep) is dict and set(dep) == {'root', 'files'} and type(dep['files']) is dict and dep['files'])
    dep_root = (root / dep['root']).resolve()
    need(dep_root.is_relative_to(root.resolve()) and dep_root.is_dir())
    for name, expected in dep['files'].items():
        need(not name.lower().endswith(('.pyc', '.pyo', '.pth')))
        checked(dep_root, name, expected)
    observed = set()
    for item in dep_root.rglob('*'):
        need(not item.is_symlink() and not item.is_junction())
        if item.is_file(): observed.add(item.relative_to(dep_root).as_posix())
    need(observed == set(dep['files']))
    return binding, candidates


class DeferredBridge:
    """Fresh passive leases after both boots; no ROM calls in live guard."""
    def __init__(self, serial_factory, enumerate_ports, identity, roles, bridge_type,
                 endpoint_type, *, notify=lambda event: None):
        self.serial_factory, self.enumerate = serial_factory, enumerate_ports
        self.identity = identity
        self.roles = json.loads(json.dumps(roles))
        self.bridge_type, self.endpoint_type, self.notify = bridge_type, endpoint_type, notify
        self.handles, self.bridge, self.used = [], None, False

    def route(self, role):
        expected = self.identity(self.roles[role]['identity'])
        ports = list(self.enumerate())
        matches = []
        for p in ports:
            if p.vid != 0x303a or p.pid != 0x1001:
                continue
            try:
                if self.identity(p.serial_number) == expected:
                    matches.append(p)
            except Exception:
                continue
        need(len(matches) == 1)
        route = matches[0].device
        need(sum(p.device == route for p in ports) == 1)
        return route

    def guard(self, role, handle, route):
        return handle.is_open is True and self.route(role) == route

    def __call__(self):
        need(not self.used and self.assert_idle())
        self.used = True
        try:
            endpoints = []
            for role in ('A', 'B'):
                route = self.route(role)
                handle = self.serial_factory(port=None, baudrate=115200, timeout=.1, write_timeout=.5)
                self.handles.append(handle)
                handle.dtr = False; handle.rts = False; handle.port = route
                need(self.route(role) == route)
                handle.open()
                need(self.guard(role, handle, route))
                endpoints.append(self.endpoint_type(handle, lambda role=role, h=handle, p=route: self.guard(role, h, p)))
            self.bridge = self.bridge_type(*endpoints, notify=self.notify, diagnostics=True)
            return self.bridge()
        finally:
            need(self.close() and self.assert_idle())

    def close(self):
        okay = True
        if self.bridge is not None:
            try: okay = self.bridge.close() is True
            except BaseException: okay = False
        for handle in self.handles:
            try:
                handle.close()
                okay = (handle.is_open is False) and okay
            except BaseException: okay = False
        return okay

    def assert_idle(self):
        try: return all(h.is_open is False for h in self.handles)
        except BaseException: return False


def ephemeral(stream):
    result = queue.Queue()
    def read():
        try: result.put(stream.readline(4097))
        except BaseException: result.put('')
    threading.Thread(target=read, daemon=True).start()
    raw = result.get(timeout=60); need(0 < len(raw) <= 4096 and raw.endswith('\n'))
    value = json.loads(raw)
    need(type(value) is dict and set(value) == {'binding_key', 'roles'} and set(value['roles']) == {'A', 'B'})
    key = bytes.fromhex(value['binding_key']); need(len(key) == 32)
    for row in value['roles'].values(): need(type(row) is dict and set(row) == {'identity'} and type(row['identity']) is str)
    return key, value['roles']


def load_policy(root, binding, *, recovery=False):
    # All listed source/dependency bytes were checked before these imports.
    sys.path.insert(0, str(root/'tools'))
    capsule = importlib.import_module('security_policy_deadline_operator')
    manifest = capsule.verify_manifest(root/binding['runtime_manifest'], binding['runtime_sha256'])
    need(Path(manifest['worktree']).resolve() == root.resolve())
    sys.path.insert(0, str(Path(manifest['root'])/'packages'))
    if not recovery:
        sys.path.insert(0, str(root/binding['bridge_dependencies']['root']))
    engine = importlib.import_module('pair_confirmation_trial')
    transport = importlib.import_module('ble_confirmation_trial_transport')
    if not recovery:
        crypto_spec = importlib.util.find_spec('cryptography')
        need(crypto_spec is not None and crypto_spec.origin is not None and
             Path(crypto_spec.origin).resolve() == (root/binding['bridge_dependencies']['root']/'cryptography/__init__.py').resolve())
    bridge = None if recovery else importlib.import_module('pair_bench_bridge')
    if not recovery:
        dep_root = (root/binding['bridge_dependencies']['root']).resolve()
        for name, module in tuple(sys.modules.items()):
            if name == '_cffi_backend' or name in ('cffi', 'pycparser') or name.startswith(('cryptography', 'cffi.', 'pycparser.')):
                origin = getattr(module, '__file__', None)
                if origin is not None:
                    actual = Path(origin).resolve()
                    need(actual.is_relative_to(dep_root) and actual.relative_to(dep_root).as_posix() in binding['bridge_dependencies']['files'])
    serial = importlib.import_module('serial')
    ports = importlib.import_module('serial.tools.list_ports')
    need(serial.__version__ == '3.5' and Path(serial.__file__).resolve() == (Path(manifest['root'])/'packages/serial/__init__.py').resolve())
    for name, module in [('pair_confirmation_trial', engine), ('ble_confirmation_trial_transport', transport), ('pair_bench_bridge', bridge)]:
        if module is not None:
            need(Path(module.__file__).resolve() == (root/'tools'/ (name+'.py')).resolve())
    return engine, transport, bridge, serial, ports


def select_bridge(request, bridge_module):
    if bridge_module is None:
        return None
    need(request['case'] in ('nonradio-confirm-and-restore', 'radio-confirm-and-restore'))
    return (bridge_module.RadioPairBridge if request['case'] == 'radio-confirm-and-restore'
            else bridge_module.PairBridge)


def main():
    need(sys.flags.isolated and sys.flags.no_site and sys.dont_write_bytecode)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['prepare', 'execute', 'recover'])
    for name in ('binding', 'request', 'grant'):
        parser.add_argument('--'+name, type=Path, required=name != 'grant')
        parser.add_argument('--'+name+'-sha256', required=name != 'grant')
    args = parser.parse_args()
    need((args.mode == 'prepare' and args.grant is None and args.grant_sha256 is None)
         or (args.mode != 'prepare' and args.grant is not None and args.grant_sha256 is not None))
    root = Path(__file__).resolve().parents[1]
    binding, candidates = verify_binding(root, args.binding, args.binding_sha256, recovery=args.mode == 'recover')
    engine, transport, bridge_module, serial, ports = load_policy(root, binding, recovery=args.mode == 'recover')
    raw = args.request.read_bytes(); need(digest(raw) == args.request_sha256)
    request = engine.validate_request(json.loads(raw))
    need(request['runtime_sha256'] == args.binding_sha256 and request['bridge_sha256'] == binding['files']['tools/pair_bench_bridge.py'])
    for role in ('A', 'B'):
        need(request['roles'][role]['candidate'] == {k:binding['candidates'][role][k] for k in ('bytes', 'sha256')})
    if args.mode == 'prepare':
        print(json.dumps({'prepared': True, 'hardware_access': False, 'serial_enumeration': False})); return
    raw_grant = args.grant.read_bytes()
    engine.validate_grant(request, raw_grant, args.grant_sha256, args.mode, time.time)
    key, roles = ephemeral(sys.stdin)
    deferred = DeferredBridge(serial.Serial, ports.comports, transport.identity, roles,
                              select_bridge(request, bridge_module), getattr(bridge_module, 'Endpoint', None),
                              notify=lambda event: print(json.dumps({'operator':event}), flush=True))
    backends = {}
    for role in ('A', 'B'):
        need(transport.opaque_identity(key, roles[role]['identity']) == request['roles'][role]['device_binding'])
        backends[role] = transport.Transport(manifest_path=root/binding['runtime_manifest'],
            manifest_sha256=binding['runtime_sha256'], private_root=root/'.private',
            route=deferred.route(role), expected_identity=roles[role]['identity'],
            opaque_binding=request['roles'][role]['device_binding'], binding_key=key,
            candidate=candidates.get(role, b'').ljust(733184, b'\xff'), recovery_only=args.mode == 'recover')
    if args.mode == 'execute':
        result = engine.execute(root, request, raw_grant, args.grant_sha256, candidates, backends, deferred)
    else:
        result = engine.recover(root, request, raw_grant, args.grant_sha256, backends, deferred.assert_idle)
    print(json.dumps(result), flush=True)


if __name__ == '__main__':
    try: main()
    except BaseException:
        print('{"result":"pair_operator_refused_or_custody_held","instruction":"Inspect custody before any retry."}')
        sys.exit(1)
