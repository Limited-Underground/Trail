"""Source-pinned enrolled-status evaluation operator. Prepare is read-only and never enumerates.

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
import secrets
import sys
import threading
import time

REQUIRED = {'tools/' + n + '.py' for n in (
    'enrolled_trial_operator', 'enrolled_confirmation_trial', 'enrolled_pair_bridge', 'enrolled_trace_schema', 'pair_bench_bridge',
    'ble_confirmation_trial', 'ble_confirmation_trial_transport', 'ble_startup_diagnostics',
    'security_policy_bundle', 'security_policy_capture', 'security_policy_deadline_operator',
    'security_policy_execution', 'security_policy_input_readback')}


COMMANDS = frozenset('OPEN SYNC HELLO INIT TIME BEGIN RADIO RFSEND RFPOLL RFSTAT RFREADY RFFINISH SEND FRAME REVIEW STATUS NEXTCONTROL CONTROL TRAFFIC RFCONTROL RFSTATUS SENDSTATUS STATUSFRAME CLOSE UNKNOWN'.split())
SNAPSHOT_FIELDS = ('fault', 'tick_last_us', 'tick_max_us', 'nvs_reads', 'nvs_read_us',
                   'tx_queue_age_ms', 'tx_age_ms', 'service_gap_ms', 'tx_attempts', 'tx_completed')
def valid_snapshot(value):
    return (type(value) is dict and set(value) == set(SNAPSHOT_FIELDS) and
            all(type(value[k]) is int and 0 <= value[k] < 1 << 64 for k in SNAPSHOT_FIELDS)
            and value['fault'] <= 12 and value['tick_last_us'] <= value['tick_max_us']
            and value['tx_completed'] <= value['tx_attempts'] <= 16)

def valid_point(value):
    return (type(value) is tuple and len(value)==2 and value[0] in ('A','B')
            and type(value[1]) is str and value[1] in COMMANDS)

def notify_point(notify, point):
    if valid_point(point):
        try: notify({'operator':'enrolled_failure_point','role':point[0],'command':point[1]})
        except BaseException: pass

def operator_event(event):
    # Repository modules are admitted before load_policy adds tools to sys.path.
    from enrolled_trace_schema import valid_host_timing, valid_target_trace
    if type(event) is str:
        return {'operator':event}
    if valid_host_timing(event):
        return json.loads(json.dumps(event))
    need(type(event) is dict and event.get('role') in ('A','B'))
    if event.get('operator') in ('enrolled_target_trace', 'enrolled_target_trace_partial'):
        need(set(event)=={'operator','role','trace'} and valid_target_trace(event['trace']))
        return {'operator':event['operator'],'role':event['role'],'trace':dict(event['trace'])}
    if event.get('operator') == 'enrolled_target_diagnostic':
        need(set(event)=={'operator','role','snapshot'} and valid_snapshot(event['snapshot']))
        return {'operator':event['operator'],'role':event['role'],'snapshot':dict(event['snapshot'])}
    need(event.get('operator')=='enrolled_failure_point' and set(event)=={'operator','role','command'}
         and type(event['command']) is str and event['command'] in COMMANDS)
    return dict(event)

FAILURE_STAGES = frozenset(
    [prefix + role for prefix in ('open_', 'sync_', 'hello_', 'init_', 'time_', 'begin_',
                                  'radio_', 'review_', 'status_', 'close_') for role in ('A', 'B')]
    + ['handshake_1', 'handshake_2', 'handshake_3', 'wait_local_confirmation',
       'activation_1', 'activation_2', 'activation_3', 'activation_4'])
FAILURE_REASONS = frozenset(('timeout', 'wrong_response_kind', 'invalid_frame_shape',
    'identity_guard', 'partial_write', 'serial_read_failed', 'late_read', 'target_refused', 'unknown'))
def safe_failure(value):
    return (type(value) is tuple and len(value)==2 and type(value[0]) is str and
            type(value[1]) is str and value[0] in FAILURE_STAGES and value[1] in FAILURE_REASONS)

def notify_failure(notify, value):
    if safe_failure(value):
        try: notify('enrolled_failure_'+value[0]+'_'+value[1])
        except BaseException: pass

def notify_cleanup(notify):
    try: notify('enrolled_cleanup_handles_unverified')
    except BaseException: pass


def need(value):
    if not value: raise ValueError('enrolled_operator_refused')


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
    need(binding['schema'] == 'OT-ENROLLED-OPERATOR-1' and type(binding['files']) is dict and REQUIRED <= set(binding['files']))
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
        self.first_failure = None
        self.first_failure_point = None

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
        failed=False;stage='open_A';reason='unknown'
        try:
            endpoints = []
            for role in ('A', 'B'):
                stage='open_'+role;reason='identity_guard'
                route = self.route(role)
                reason='unknown'
                handle = self.serial_factory(port=None, baudrate=115200, timeout=.1, write_timeout=.5)
                self.handles.append(handle)
                handle.dtr = False; handle.rts = False; handle.port = route
                reason='identity_guard';need(self.route(role) == route)
                reason='unknown';handle.open()
                reason='identity_guard';need(self.guard(role, handle, route))
                reason='unknown'
                endpoints.append(self.endpoint_type(handle, lambda role=role, h=handle, p=route: self.guard(role, h, p)))
            stage='sync_A';reason='unknown'
            self.bridge = self.bridge_type(*endpoints, notify=self.notify)
            return self.bridge()
        except BaseException:
            failed=True
            first=getattr(self.bridge,'first_failure',None)
            self.first_failure=first if safe_failure(first) else (stage,reason)
            self.first_failure_point=getattr(self.bridge,'first_failure_point',None)
            if not safe_failure(first):
                self.first_failure_point=(stage[-1], 'OPEN' if stage.startswith('open_') else 'UNKNOWN')
                notify_failure(self.notify,self.first_failure);notify_point(self.notify,self.first_failure_point)
            raise
        finally:
            closed=self.close() and self.assert_idle()
            if not closed:
                if self.first_failure is None:
                    self.first_failure=('close_A','unknown');self.first_failure_point=('A','CLOSE')
                    notify_failure(self.notify,self.first_failure);notify_point(self.notify,self.first_failure_point)
                notify_cleanup(self.notify)
                if not failed:need(False)

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
    engine = importlib.import_module('enrolled_confirmation_trial')
    transport = importlib.import_module('ble_confirmation_trial_transport')
    if not recovery:
        crypto_spec = importlib.util.find_spec('cryptography')
        need(crypto_spec is not None and crypto_spec.origin is not None and
             Path(crypto_spec.origin).resolve() == (root/binding['bridge_dependencies']['root']/'cryptography/__init__.py').resolve())
    bridge = None if recovery else importlib.import_module('enrolled_pair_bridge')
    trace_schema = None if recovery else importlib.import_module('enrolled_trace_schema')
    helpers = None if recovery else importlib.import_module('pair_bench_bridge')
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
    for name, module in [('enrolled_confirmation_trial', engine), ('ble_confirmation_trial_transport', transport), ('enrolled_pair_bridge', bridge), ('enrolled_trace_schema', trace_schema), ('pair_bench_bridge', helpers)]:
        if module is not None:
            need(Path(module.__file__).resolve() == (root/'tools'/ (name+'.py')).resolve())
    return engine, transport, bridge, serial, ports


class EnrolledObservationBridge:
    """Translate only a completed exchange plus verified bridge cleanup to evidence.

    The caller does not persist or export the temporary signer. This successor
    intentionally tests a fresh namespace; retained-device rekey is not granted.
    """
    def __init__(self, inner, *, radio=False, notify=lambda event: None):
        self.inner, self.radio, self.notify = inner, radio, notify
        self.first_failure = None
        self.first_failure_point = None
    def __call__(self):
        try:
            need(self.inner() is True and self.inner.result == 'passed')
            self.inner.assert_idle()
            if self.radio:
                # These are fixed non-identifying observation categories, never
                # interpolated device data or a public hardware identity.
                expected = [[8, 8, 7, 0, 1], [7, 7, 8, 0, 1]]
                observed = self.inner.statistics
                need(type(observed) is list and len(observed) == 2 and
                     all(type(row) is list and len(row) == 5 and
                         all(type(value) is int for value in row) for row in observed)
                     and observed == expected)
                for event in ('enrolled_radio_A_tx_attempts8_tx_completed8_rx_frames7_rx_errors0_stopped1',
                              'enrolled_radio_B_tx_attempts7_tx_completed7_rx_frames8_rx_errors0_stopped1'):
                    self.notify(event)
                    self.inner.assert_idle()
            return 'status_exchanged'
        except BaseException:
            first=getattr(self.inner,'first_failure',None)
            if self.first_failure is None:
                self.first_failure=first if safe_failure(first) else ('close_A','unknown')
                self.first_failure_point=getattr(self.inner,'first_failure_point',None)
                if not safe_failure(first):
                    self.first_failure_point=('A','CLOSE')
                    notify_failure(self.notify,self.first_failure);notify_point(self.notify,self.first_failure_point)
            raise
    def close(self):
        try: return self.inner.close()
        finally: self.inner.signer = None
    def assert_idle(self):
        self.inner.assert_idle()
        return True


def select_bridge(request, bridge_module):
    if bridge_module is None:
        return None
    need(request['case'] in ('enrolled-status-and-restore', 'enrolled-status-radio-and-restore'))
    radio = request['case'] == 'enrolled-status-radio-and-restore'
    def factory(*ends, notify=lambda event: None):
        signer = bridge_module.Ed25519PrivateKey.generate()
        group = 1 + secrets.randbelow((1 << 64) - 1)
        return EnrolledObservationBridge(bridge_module.EnrolledBridge(*ends,
            signer=signer, group=group, epoch=1, radio=radio, notify=notify, require_diagnostics=True,
            require_trace=True, enable_host_timing=True), radio=radio, notify=notify)
    return factory


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
    need(request['runtime_sha256'] == args.binding_sha256 and request['bridge_sha256'] == binding['files']['tools/enrolled_pair_bridge.py'])
    for role in ('A', 'B'):
        need(request['roles'][role]['candidate'] == {k:binding['candidates'][role][k] for k in ('bytes', 'sha256')})
    if args.mode == 'prepare':
        print(json.dumps({'prepared': True, 'hardware_access': False, 'serial_enumeration': False})); return
    raw_grant = args.grant.read_bytes()
    engine.validate_grant(request, raw_grant, args.grant_sha256, args.mode, time.time)
    key, roles = ephemeral(sys.stdin)
    endpoint_factory = (lambda handle, guard: bridge_module.Endpoint(handle, guard, enable_transport_timing=True)) if bridge_module else None
    deferred = DeferredBridge(serial.Serial, ports.comports, transport.identity, roles,
                              select_bridge(request, bridge_module), endpoint_factory,
                              notify=lambda event: print(json.dumps(operator_event(event)), flush=True))
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
        print('{"result":"enrolled_operator_refused_or_custody_held","instruction":"Inspect custody before any retry."}')
        sys.exit(1)
