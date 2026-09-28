"""Exact OT-0101e diagnostic trial composition. Import/check never access devices.

Execute requires a separate one-use grant and an operator attestation that the
same diagnostic APK passed original-firmware saved-owner Ready/clock control.
The attestation is a prerequisite, not independent evidence of that control.
No phone automation, grant issuance, retry or automatic A-to-B progression.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import queue
import secrets
import subprocess
import sys
import threading
import time

MAX_CAPTURE_BYTES = 2 * 1024 * 1024
OBSERVATION_SECONDS = 600
REAP_SECONDS = 5
REQUIRED_FILES = {
    'tools/run_connection_diagnostic_trial.py', 'tools/run_gnss_observation_trial.py',
    'tools/gnss_observation_trial.py', 'tools/connection_diagnostic_capture.py',
    'tools/gnss_counter_capture.py', 'tools/ble_confirmation_trial_transport.py',
    'tools/ble_startup_diagnostics.py', 'tools/security_policy_execution.py',
    'tools/security_policy_input_readback.py', 'tools/security_policy_deadline_operator.py',
    'tools/security_policy_bundle.py', 'tools/security_policy_capture.py',
    'tools/connection_capture_custody.py',
}


def need(value):
    if not value:
        raise ValueError('connection_operator_refused')


def modules():
    directory = str(Path(__file__).resolve().parent)
    if directory not in sys.path:
        sys.path.insert(0, directory)
    import gnss_observation_trial as engine
    import run_gnss_observation_trial as legacy
    import ble_confirmation_trial_transport as transport
    return engine, legacy, transport


def verify_binding(root, binding_path, expected_sha, recovery=False):
    engine, legacy, _ = modules()
    need(binding_path.stat().st_size <= 32768)
    raw = binding_path.read_bytes()
    need(len(raw) <= 32768 and legacy.digest(raw) == expected_sha)
    binding = json.loads(raw)
    need(type(binding) is dict and set(binding) == {
        'schema', 'files', 'runtime_manifest', 'runtime_sha256', 'candidate', 'profile', 'registry', 'apk'})
    need(binding['schema'] == 'OT0101E-CONNECTION-OPERATOR-BINDING-1')
    need(binding['profile'] in engine.CONNECTION_CANDIDATES)
    files = binding['files']
    apk = binding['apk']
    need(type(files) is dict and REQUIRED_FILES <= set(files))
    need(type(apk) is dict and set(apk) == {'path', 'bytes', 'sha256', 'signer_sha256'})
    need(type(apk['bytes']) is int and apk['bytes'] > 0 and
         type(apk['signer_sha256']) is str and engine.common.HEX64.fullmatch(apk['signer_sha256']))
    need(apk['path'] in files and files[apk['path']] == apk['sha256'] and binding['registry'] in files)
    for relative, expected in files.items():
        if recovery and relative in (binding['candidate'], binding['registry'], apk['path']):
            continue
        legacy.checked_file(root, relative, expected)
    legacy.checked_file(root, binding['runtime_manifest'], binding['runtime_sha256'])
    if not recovery:
        candidate = legacy.checked_file(root, binding['candidate'],
                                        engine.CONNECTION_CANDIDATES[binding['profile']]['sha256'])
        need(engine.descriptor(candidate) == engine.CONNECTION_CANDIDATES[binding['profile']])
        need(len(legacy.checked_file(root, apk['path'], apk['sha256'])) == apk['bytes'])
    return binding


# Only the hash-verified runner is imported from the worktree. Serial is loaded
# from the frozen capsule before inserting tools, and origins are audited again.
BOOTSTRAP = r'''
import hashlib,json,pathlib,runpy,sys
try:
    raw=sys.stdin.buffer.readline(4097)
    assert raw.endswith(b'\n') and len(raw)<=4096
    q=json.loads(raw); root=pathlib.Path(q['root']); capsule=pathlib.Path(q['runtime_root'])
    assert sys.flags.isolated and sys.flags.no_site and sys.dont_write_bytecode
    assert pathlib.Path(sys.executable).resolve()==capsule/'python.exe'
    assert sys.version_info[:3]==(3,14,6)
    assert [pathlib.Path(p).resolve() for p in sys.path]==[capsule/p for p in ('Lib','DLLs','packages','policy')]
    import serial
    from serial.tools.list_ports import comports
    assert pathlib.Path(serial.__file__).resolve()==capsule/'packages/serial/__init__.py'
    runner=root/'tools/run_connection_diagnostic_trial.py'
    assert hashlib.sha256(runner.read_bytes()).hexdigest()==q['runner_sha256']
    sys.path.insert(0,str(root/'tools'))
    runpy.run_path(str(runner))['_capture_worker'](q,serial,comports)
except BaseException:
    print('{"worker":"refused"}',flush=True)
    sys.exit(1)
'''


def _capture_worker(q, serial, comports):
    engine, _, _ = modules()
    root = Path(q['root']).resolve()
    binding = verify_binding(root, Path(q['binding']), q['binding_sha256'])
    from security_policy_deadline_operator import verify_manifest
    manifest = verify_manifest(root / binding['runtime_manifest'], binding['runtime_sha256'])
    need(Path(manifest['root']).resolve() == Path(q['runtime_root']).resolve())
    import connection_diagnostic_capture as collector
    from gnss_counter_capture import bound_identity, normalized_identity
    for module in tuple(sys.modules.values()):
        origin = getattr(module, '__file__', None)
        if origin:
            origin = Path(origin).resolve()
            in_capsule = origin.is_relative_to(Path(manifest['root'])) and origin.relative_to(manifest['root']).as_posix() in manifest['files']
            in_tools = origin.is_relative_to(root) and origin.relative_to(root).as_posix() in binding['files']
            need(in_capsule or in_tools)
    if q['mode'] == 'probe':
        print('{"worker":"verified","hardware_access":false}', flush=True)
        return
    need(q['mode'] == 'capture' and engine.common.HEX32.fullmatch(q['attempt']))
    need(bound_identity(root / binding['registry'], 'OT-DEV-002') == normalized_identity(q['identity']))
    remaining = int(q['deadline'] - time.monotonic())
    need(2 <= remaining <= OBSERVATION_SECONDS)
    stopped = threading.Event()

    def control():
        # No buffered stdin lock survives interpreter shutdown. EOF or any input
        # stops this passive child; only the parent can interpret phone results.
        try:
            os.read(sys.stdin.fileno(), 16)
        finally:
            stopped.set()

    threading.Thread(target=control, daemon=True).start()
    result = collector.capture(root / binding['registry'], 'OT-DEV-002', remaining - 1,
        custody_confirmed=True, serial_module=serial, comports=comports,
        stop_requested=lambda: stopped.is_set() or time.monotonic() >= q['deadline'],
        on_armed=lambda: print(collector.ARMED_SIGNAL, file=sys.stderr, flush=True))
    raw = engine.canonical(result) + b'\n'
    need(len(raw) <= MAX_CAPTURE_BYTES)
    engine.write_once(engine.path(root, 'connection-capture-' + q['attempt'] + '.json'), raw)
    print(json.dumps({'capture': engine.descriptor(raw)}, separators=(',', ':')), flush=True)


class CaptureCustody:
    """Prevents *all* ROM calls until the passive child has been reaped."""
    def __init__(self, backend, released_check=None):
        self.backend = backend
        self.serial_released = True
        self.released_check = released_check

    def __getattr__(self, name):
        value = getattr(self.backend, name)
        if name not in ('claim', 'reverify', 'read', 'write', 'boot_candidate', 'reset_original', 'hold'):
            return value
        def guarded(*args, **kwargs):
            need(self.serial_released)
            if name == 'claim' and self.released_check is not None:
                self.serial_released = False
                self.released_check()
                self.serial_released = True
            return value(*args, **kwargs)
        return guarded


def worker_payload(root, binding_path, binding_sha, binding, manifest, **extra):
    # The child runs from the capsule directory. Resolve the same caller path
    # already verified here before relaying it across that cwd boundary.
    value = {'root': str(root), 'binding': str(binding_path.resolve()), 'binding_sha256': binding_sha,
             'runtime_root': manifest['root'],
             'runner_sha256': binding['files']['tools/run_connection_diagnostic_trial.py'], **extra}
    raw = json.dumps(value, separators=(',', ':')).encode('ascii') + b'\n'
    need(len(raw) <= 4096)
    return raw


def launch(manifest, popen=subprocess.Popen):
    env = {key: value for key, value in os.environ.items()
           if not key.upper().startswith(('PYTHON', 'ESPTOOL_'))}
    env['ESPTOOL_CFGFILE'] = str(Path(manifest['root']) / 'esptool.cfg')
    return popen([str(Path(manifest['root']) / 'python.exe'), '-I', '-S', '-B', '-c', BOOTSTRAP],
                 stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                 cwd=manifest['root'], env=env,
                 creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))


def pipe_reader(stream, name, events, failed):
    try:
        while True:
            raw = stream.readline(4097)
            if len(raw) > 4096:
                failed.set()
                return
            events.put_nowait((name, raw))
            if not raw:
                return
    except Exception:
        failed.set()


def read_capture(root, attempt, reply):
    engine, _, _ = modules()
    from connection_diagnostic_capture import validate_capture
    need(type(reply) is dict and set(reply) == {'capture'})
    expected = reply['capture']
    engine.validate_connection_observation({'schema': 'OT0101E-CONNECTION-OBSERVATION-1',
                                            'result': 'capture_failed', 'capture': expected})
    source = engine.path(root, 'connection-capture-' + attempt + '.json')
    need(source.stat().st_size <= MAX_CAPTURE_BYTES)
    raw = source.read_bytes()
    need(len(raw) <= MAX_CAPTURE_BYTES and engine.descriptor(raw) == expected)
    result = validate_capture(json.loads(raw))
    return expected, result


def observe(root, binding_path, binding_sha, binding, manifest, attempt, identity,
            custody, lines, *, emit=print, clock=time.monotonic, popen=subprocess.Popen,
            lifetime=None):
    """One total deadline; one operator result; a two-second tail stays inside it."""
    deadline = clock() + OBSERVATION_SECONDS
    token = secrets.token_hex(16)
    events, failed = queue.Queue(maxsize=8), threading.Event()
    if lifetime is None:
        import connection_capture_custody as lifetime
    child, job = None, None
    killed = False
    armed, reply, phone = False, None, None
    invalid_channel = False
    ended = set()
    stop_at, stop_sent = None, False
    result = 'capture_failed'
    capture = None
    try:
        custody.serial_released = False
        child = launch(manifest, popen)
        # Bootstrap waits for stdin. No serial module, enumeration or open may
        # run until its OS lifetime is assigned and durably discoverable.
        job = lifetime.create_and_assign(root, attempt, child)
        for stream, name in ((child.stdout, 'stdout'), (child.stderr, 'stderr')):
            threading.Thread(target=pipe_reader, args=(stream, name, events, failed), daemon=True).start()
        payload = worker_payload(root, binding_path, binding_sha, binding, manifest,
            mode='capture', attempt=attempt, identity=identity, deadline=deadline)
        child.stdin.write(payload)
        child.stdin.flush()
        while clock() < deadline:
            if failed.is_set():
                invalid_channel = True
                break
            if stop_at is not None and clock() >= stop_at and not stop_sent:
                child.stdin.write(b'STOP\n')
                child.stdin.flush()
                stop_sent = True
            try:
                name, raw = events.get(timeout=min(0.1, max(0.001, deadline - clock())))
            except queue.Empty:
                if child.poll() is not None and ended == {'stdout', 'stderr'}:
                    break
                continue
            if name == 'phone':
                need(armed and phone is None and type(raw) is dict and
                     set(raw) == {'token', 'result'} and raw['token'] == token and
                     raw['result'] in ('ready_clock_observed', 'protocol_info_failure', 'not_ready', 'cancelled'))
                phone = raw['result']
                stop_at = min(deadline, clock() + (0 if phone == 'cancelled' else 2))
            elif name in ('stdout', 'stderr') and not raw:
                ended.add(name)
            elif name == 'stderr' and raw:
                need(not armed and raw in (b'CONNECTION_CAPTURE_ARMED\n', b'CONNECTION_CAPTURE_ARMED\r\n'))
                armed = True
                if child.poll() is not None:
                    continue  # A queued ARM from a closed capture cannot invite a phone attempt.
                emit(json.dumps({'observation_token': token, 'next': 'one_saved_owner_attempt',
                    'capture': 'armed', 'instruction': 'Capture is armed. Make one saved-owner connection attempt '
                    'using the bound diagnostic APK. Do not clear data, pair anew or manually retry. '
                    'Reply with token and result: ready_clock_observed, protocol_info_failure, not_ready, or cancelled.'}), flush=True)
                def read_phone():
                    try:
                        value = lines.read(max(0.001, deadline - clock()))
                        events.put_nowait(('phone', value))
                    except Exception:
                        failed.set()
                threading.Thread(target=read_phone, daemon=True).start()
            elif name == 'stdout' and raw:
                need(reply is None and raw.endswith(b'\n'))
                reply = json.loads(raw)
            if child.poll() is not None and ended == {'stdout', 'stderr'}:
                break
        result = phone if phone is not None else ('timeout' if clock() >= deadline else 'capture_failed')
    except KeyboardInterrupt:
        result = 'cancelled'
    except Exception:
        invalid_channel = True
        result = 'capture_failed'
    finally:
        if child is None:
            custody.serial_released = True
        else:
            # Killing only this passive child releases its OS handle. Never kill
            # the restoration owner; an unreaped child leaves ROM access barred.
            try:
                if job is not None:
                    killed = child.poll() is None
                    job.close()
                if child.poll() is None:
                    killed = True
                    child.kill()
                child.wait(timeout=REAP_SECONDS)
                lifetime.require_released(root, attempt)
                custody.serial_released = True
            finally:
                if custody.serial_released:
                    for stream in (child.stdin, child.stdout, child.stderr):
                        try:
                            stream.close()
                        except Exception:
                            pass
    need(custody.serial_released)
    if child is not None and child.returncode != 0 and not (killed and result in ('cancelled', 'timeout')):
        result = 'capture_failed'
    if reply is not None:
        try:
            capture, evidence = read_capture(root, attempt, reply)
            if evidence.get('stop_reason') not in ('operator_stop', 'window_complete'):
                result = 'capture_failed'
            elif phone is None and evidence.get('stop_reason') == 'window_complete':
                result = 'timeout'
            if not armed:
                result = 'capture_failed'
            if child.returncode != 0:
                result = 'capture_failed'
        except Exception:
            result, capture = 'capture_failed', None
    else:
        result = 'capture_failed' if result not in ('cancelled', 'timeout') else result
    if invalid_channel:
        result = 'capture_failed'
    return {'schema': 'OT0101E-CONNECTION-OBSERVATION-1', 'result': result, 'capture': capture}


def main(argv=None):
    need(sys.flags.isolated and sys.flags.no_site and sys.dont_write_bytecode)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('check', 'execute', 'recover'))
    for name in ('binding', 'request', 'grant', 'ephemeral'):
        parser.add_argument('--' + name, type=Path, required=name == 'binding')
        parser.add_argument('--' + name + '-sha256', required=name == 'binding')
    parser.add_argument('--original-control-verified', action='store_true')
    args = parser.parse_args(argv)
    root = Path(__file__).resolve().parents[1]
    binding = verify_binding(root, args.binding, args.binding_sha256, args.mode == 'recover')
    engine, legacy, transport = modules()
    from security_policy_deadline_operator import verify_manifest
    manifest = verify_manifest(root / binding['runtime_manifest'], binding['runtime_sha256'])
    if args.mode == 'check':
        transport.probe_runtime(manifest_path=root / binding['runtime_manifest'],
            manifest_sha256=binding['runtime_sha256'], private_root=root / '.private')
        child = launch(manifest)
        try:
            stdout, stderr = child.communicate(worker_payload(root, args.binding, args.binding_sha256,
                binding, manifest, mode='probe'), timeout=120)
            need(child.returncode == 0 and stderr == b'' and stdout in (
                b'{"worker":"verified","hardware_access":false}\n',
                b'{"worker":"verified","hardware_access":false}\r\n'))
        finally:
            if child.poll() is None:
                child.kill()
            child.wait(timeout=REAP_SECONDS)
        print('{"operator_inputs":"verified","hardware_access":false,"serial_enumeration":false}')
        return
    need(args.request is not None and args.grant is not None and args.ephemeral is not None)
    need(args.request.stat().st_size <= 4096 and args.grant.stat().st_size <= 4096)
    request_raw = args.request.read_bytes()
    need(len(request_raw) <= 4096 and legacy.digest(request_raw) == args.request_sha256)
    request = engine.validate_request(json.loads(request_raw))
    need(request['schema'] == 'OT0101E-CONNECTION-REQUEST-1' and
         request['runtime_sha256'] == args.binding_sha256 and request['profile'] == binding['profile'])
    grant_raw = args.grant.read_bytes()
    grant = engine.validate_grant(request, grant_raw, args.grant_sha256, args.mode, time.time)
    ephemeral_path = args.ephemeral.resolve()
    need(ephemeral_path.is_relative_to(root / '.private') and not args.ephemeral.is_symlink())
    need(ephemeral_path.stat().st_size <= 512)
    ephemeral_raw = ephemeral_path.read_bytes()
    need(len(ephemeral_raw) <= 512 and legacy.digest(ephemeral_raw) == args.ephemeral_sha256)
    ephemeral = json.loads(ephemeral_raw)
    need(type(ephemeral) is dict and set(ephemeral) == {'route', 'identity', 'binding_key'})
    if args.mode == 'execute':
        need(args.original_control_verified)
        from gnss_counter_capture import bound_identity, normalized_identity
        need(bound_identity(root / binding['registry'], 'OT-DEV-002') == normalized_identity(ephemeral['identity']))
    candidate = b'' if args.mode == 'recover' else legacy.checked_file(
        root, binding['candidate'], request['candidate']['sha256'])
    import connection_capture_custody as lifetime
    backend = CaptureCustody(transport.Transport(manifest_path=root / binding['runtime_manifest'],
        manifest_sha256=binding['runtime_sha256'], private_root=root / '.private',
        route=ephemeral['route'], expected_identity=ephemeral['identity'], opaque_binding=request['device_binding'],
        binding_key=bytes.fromhex(ephemeral['binding_key']),
        candidate=candidate + b'\xff' * (engine.SPANS['application'][1] - len(candidate)),
        recovery_only=args.mode == 'recover'),
        released_check=(lambda: lifetime.require_released(root, grant['origin_attempt']))
            if args.mode == 'recover' else None)
    if args.mode == 'execute':
        result = engine.execute(root, request, grant_raw, args.grant_sha256, candidate, backend,
            lambda: observe(root, args.binding, args.binding_sha256, binding, manifest,
                            grant['attempt'], ephemeral['identity'], backend, legacy.Lines(sys.stdin)))
    else:
        result = engine.recover(root, request, grant_raw, args.grant_sha256, backend)
    print(json.dumps(result, separators=(',', ':')))


if __name__ == '__main__':
    try:
        main()
    except BaseException:
        print('{"result":"connection_operator_refused_or_custody_held","instruction":"Inspect custody before any retry."}')
        sys.exit(1)
