"""OT-0101e single-device GNSS trial operator; no grant issuer.

Run only against reviewed, exact-hash private inputs. The execute mode can write
one device and is intentionally unusable without a separate one-use grant.
"""
import argparse
import hashlib
import json
from pathlib import Path
import queue
import secrets
import sys
import threading
import time


def need(value):
    if not value:
        raise ValueError('gnss_operator_refused')


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def checked_file(root, relative, expected):
    unresolved = root / relative
    for parent in (unresolved, *unresolved.parents):
        if parent == root.parent:
            break
        need(not parent.is_symlink() and not parent.is_junction())
    target = unresolved.resolve()
    need(target.is_relative_to(root) and not unresolved.is_symlink())
    raw = target.read_bytes()
    need(digest(raw) == expected)
    return raw


def verify_binding(root, binding_path, expected_sha, recovery=False):
    raw = binding_path.read_bytes()
    need(digest(raw) == expected_sha)
    binding = json.loads(raw)
    need(type(binding) is dict and set(binding) ==
         {'schema', 'files', 'runtime_manifest', 'runtime_sha256', 'candidate'})
    need(binding['schema'] == 'OT0101E-GNSS-OPERATOR-BINDING-1')
    required = {'tools/run_gnss_observation_trial.py', 'tools/gnss_observation_trial.py',
                'tools/ble_confirmation_trial_transport.py', 'tools/ble_startup_diagnostics.py',
                'tools/security_policy_execution.py', 'tools/security_policy_input_readback.py',
                'tools/security_policy_deadline_operator.py'}
    need(required <= set(binding['files']))
    for relative, expected in binding['files'].items():
        checked_file(root, relative, expected)
    checked_file(root, binding['runtime_manifest'], binding['runtime_sha256'])
    if not recovery:
        checked_file(root, binding['candidate'],
                     '2808d63b610619a69e14bd1809a56d921bded6efc2720fb08fd204aaaff9feb2')
    return binding


class Lines:
    def __init__(self, stream):
        self.stream = stream

    def read(self, timeout):
        result = queue.Queue(maxsize=1)
        def worker():
            try:
                result.put(self.stream.readline(4097))
            except Exception:
                result.put('')
        threading.Thread(target=worker, daemon=True).start()
        value = result.get(timeout=max(0.001, timeout))
        need(value.endswith('\n') and len(value) <= 4096)
        return json.loads(value)


def observe(lines, emit=print, clock=time.monotonic, token=None):
    """Require saved-owner clock sync, then two privacy-safe screen readings."""
    token = token or secrets.token_hex(16)
    valid = ('fix_observed', 'no_fix_observed', 'stale_observed',
             'invalid_observed', 'unavailable', 'cancelled')
    ready_deadline = clock() + 600
    emit(json.dumps({'observation_token': token, 'next': 'phone_ready',
                     'instruction': 'Operator: inspect the S24 V1-Test screen over ADB. '
                                    'If it is in Add Device, recover the saved-owner route '
                                    'through Find device and Start Bluetooth device service. '
                                    'Wait for protected Ready and display-clock sync, then '
                                    'ask the owner only whether the Heltec shows a time. '
                                    'Reply with token, saved_owner_ready true/false and '
                                    'clock_visible true/false. A false result keeps this '
                                    'checkpoint open for troubleshooting; do not send the '
                                    'actual time or location.'}),
         flush=True)
    try:
        while clock() < ready_deadline:
            ready = lines.read(ready_deadline - clock())
            if clock() >= ready_deadline:
                return 'timeout'
            need(type(ready) is dict and ready.get('token') == token)
            if set(ready) == {'token', 'cancelled'} and ready['cancelled'] is True:
                return 'cancelled'
            need(set(ready) == {'token', 'saved_owner_ready', 'clock_visible'} and
                 type(ready['saved_owner_ready']) is bool and
                 type(ready['clock_visible']) is bool)
            if ready['saved_owner_ready'] and ready['clock_visible']:
                break
            emit(json.dumps({'observation_token': token, 'next': 'phone_retry',
                             'instruction': 'Keep the candidate running. Inspect and '
                                            'recover the saved-owner app route; this '
                                            'checkpoint stays open until its ten-minute '
                                            'deadline. Send a new ready result only after '
                                            'direct app verification and the owner screen '
                                            'check. Do not use Add Device or clear app data.'}),
                 flush=True)
        else:
            return 'timeout'
    except queue.Empty:
        return 'timeout'
    except Exception:
        return 'unavailable'
    deadline = clock() + 540
    emit(json.dumps({'observation_token': token, 'next': 'first_status',
                     'instruction': 'Read the candidate GPS status now. Reply with the token '
                                    'and one status: fix_observed, no_fix_observed, '
                                    'stale_observed, invalid_observed, unavailable, or '
                                    'cancelled. Do not send location, clock time or raw NMEA.'}), flush=True)
    try:
        first = lines.read(min(180, deadline - clock()))
        need(clock() < deadline and type(first) is dict and
             set(first) == {'token', 'status'} and first['token'] == token and
             first['status'] in valid)
        if first['status'] == 'cancelled':
            return 'cancelled'
        first_at = clock()
        emit(json.dumps({'observation_token': token, 'next': 'second_status',
                         'instruction': 'Keep the candidate powered. At least two minutes '
                                        'after the first reading, report the GPS status again '
                                        'and whether the displayed clock advanced. Reply '
                                        'with token, status and clock_advanced true/false. '
                                        'Do not send location, clock time or raw NMEA.'}), flush=True)
        second = lines.read(deadline - clock())
        need(clock() < deadline and clock() - first_at >= 120 and
             type(second) is dict and set(second) ==
             {'token', 'status', 'clock_advanced'} and second['token'] == token and
             second['status'] in valid and type(second['clock_advanced']) is bool)
        if second['status'] == 'cancelled':
            return 'cancelled'
        return {'first_status': first['status'], 'second_status': second['status'],
                'clock_advanced': second['clock_advanced']}
    except queue.Empty:
        return 'timeout'
    except Exception:
        return 'unavailable'


def main():
    need(sys.flags.isolated and sys.flags.no_site and sys.dont_write_bytecode)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['check', 'execute', 'recover'])
    parser.add_argument('--binding', type=Path, required=True)
    parser.add_argument('--binding-sha256', required=True)
    parser.add_argument('--request', type=Path)
    parser.add_argument('--request-sha256')
    parser.add_argument('--grant', type=Path)
    parser.add_argument('--grant-sha256')
    parser.add_argument('--ephemeral', type=Path)
    parser.add_argument('--ephemeral-sha256')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    binding = verify_binding(root, args.binding, args.binding_sha256, args.mode == 'recover')
    sys.path.insert(0, str(root / 'tools'))
    import gnss_observation_trial as engine
    import ble_confirmation_trial_transport as transport
    candidate = (b'' if args.mode == 'recover' else
                 checked_file(root, binding['candidate'], engine.CANDIDATE['sha256']))
    if args.mode == 'check':
        print(json.dumps({'operator_inputs': 'verified', 'hardware_access': False}))
        return
    need(args.request is not None and args.grant is not None)
    need(args.ephemeral is not None and args.ephemeral_sha256 is not None)
    need(args.request.stat().st_size <= 4096 and args.grant.stat().st_size <= 4096)
    request_raw = args.request.read_bytes()
    need(digest(request_raw) == args.request_sha256)
    request = engine.validate_request(json.loads(request_raw))
    need(request['runtime_sha256'] == args.binding_sha256)
    grant_raw = args.grant.read_bytes()
    engine.validate_grant(request, grant_raw, args.grant_sha256, args.mode, time.time)
    lines = Lines(sys.stdin)
    ephemeral_path = args.ephemeral.resolve()
    need(ephemeral_path.is_relative_to(root / '.private') and
         not args.ephemeral.is_symlink() and args.ephemeral.stat().st_size <= 512)
    ephemeral_raw = ephemeral_path.read_bytes()
    need(digest(ephemeral_raw) == args.ephemeral_sha256)
    ephemeral = json.loads(ephemeral_raw)
    need(type(ephemeral) is dict and set(ephemeral) == {'route', 'identity', 'binding_key'})
    backend = transport.Transport(manifest_path=root / binding['runtime_manifest'],
                                  manifest_sha256=binding['runtime_sha256'],
                                  private_root=root / '.private', route=ephemeral['route'],
                                  expected_identity=ephemeral['identity'],
                                  opaque_binding=request['device_binding'],
                                  binding_key=bytes.fromhex(ephemeral['binding_key']),
                                  candidate=candidate + b'\xff' * (733184 - len(candidate)),
                                  recovery_only=args.mode == 'recover')
    if args.mode == 'execute':
        result = engine.execute(root, request, grant_raw, args.grant_sha256,
                                candidate, backend, lambda: observe(lines))
    else:
        result = engine.recover(root, request, grant_raw, args.grant_sha256, backend)
    print(json.dumps(result))


if __name__ == '__main__':
    try:
        main()
    except BaseException:
        print('{"result":"gnss_operator_refused_or_custody_held","instruction":"Inspect custody before any retry."}')
        sys.exit(1)
