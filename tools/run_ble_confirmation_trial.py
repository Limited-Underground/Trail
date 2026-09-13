"""OT-218 narrow operator. Launch with Python -I -S -B and a reviewed binding SHA.

The binding and this bootstrap are operator-reviewed trusted inputs, not signatures.
No installation or grant issuance. Observation is a human attestation, not a peer proof.
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

APK_SHA = '9f91a3f361221d19089e685b492b5e772e5cf7d658298ff955c4feb2508694a2'
PACKAGE = 'io.github.nbjelanovic.otclient.v1test'
BOOT_HASHES = {'002a0a4b5ea5329c93c39c105099490a01b0ed7d4bd167ee3a5b60322dd0b5b3',
               '2dbfd3b3bc786e88121ebcbd5deb87830b87261a276a70933318ff5377730532'}


def need(value):
    if not value:
        raise ValueError('operator_refused')


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def checked_file(root, relative, expected):
    unresolved = root / relative
    for parent in (unresolved, *unresolved.parents):
        if parent == root.parent:
            break
        need(not parent.is_symlink() and not parent.is_junction())
    path = (root / relative).resolve()
    need(path.is_relative_to(root) and not (root / relative).is_symlink())
    raw = path.read_bytes()
    need(digest(raw) == expected)
    return raw


def verify_binding(root, path, expected, recovery=False):
    raw = path.read_bytes()
    need(digest(raw) == expected)
    binding = json.loads(raw)
    need(binding['schema'] == 'OT218-OPERATOR-BINDING-1')
    need(set(binding) == {'schema', 'files', 'runtime_manifest', 'runtime_sha256', 'candidate', 'apk', 'adb'})
    need({'tools/run_ble_confirmation_trial.py', 'tools/ble_confirmation_trial.py',
          'tools/ble_confirmation_trial_transport.py'} <= set(binding['files']))
    for relative, sha in binding['files'].items():
        checked_file(root, relative, sha)
    checked_file(root, binding['runtime_manifest'], binding['runtime_sha256'])
    if not recovery:
        need(digest(checked_file(root, binding['apk'], APK_SHA)) == APK_SHA)
    return binding


def phone_check(adb, run=subprocess.run):
    def command(args, serial=None):
        env = dict(os.environ)
        env.pop('ANDROID_SERIAL', None)
        if serial:
            env['ANDROID_SERIAL'] = serial
        # adb shell otherwise inherits and consumes the operator's private stdin.
        result = run([str(adb), *args], env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                     stderr=subprocess.PIPE, timeout=20, check=False)
        need(result.returncode == 0)
        return result.stdout.decode('utf-8', errors='strict').strip()
    devices = [line.split()[0] for line in command(['devices']).splitlines()[1:]
               if len(line.split()) == 2 and line.split()[1] == 'device']
    selected = [serial for serial in devices
                if command(['shell', 'getprop', 'ro.product.model'], serial) == 'SM-N986U']
    need(len(selected) == 1)
    serial = selected[0]
    need(command(['shell', 'getprop', 'ro.build.version.sdk'], serial) == '33')
    paths = command(['shell', 'pm', 'path', PACKAGE], serial).splitlines()
    need(len(paths) == 1 and paths[0].startswith('package:/data/app/'))
    apk = paths[0][8:]
    need(all(character.isalnum() or character in '/._-=+~' for character in apk))
    need(command(['shell', 'sha256sum', apk], serial).split()[0] == APK_SHA)
    return {'model': 'SM-N986U', 'sdk': 33, 'apk_sha256': APK_SHA}


class Lines:
    """One bounded stdin consumer; EOF and long lines are refused."""
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
    token = token or secrets.token_hex(16)
    deadline = clock() + 180
    emit(json.dumps({'observation_token': token, 'next': 'ready',
                     'instruction': 'Stay on Device until protected Ready. Reply with token and event.'}), flush=True)
    for expected in ('ready', 'offer', 'local_confirmed'):
        try:
            value = lines.read(deadline - clock())
            need(clock() < deadline and set(value) == {'token', 'event'} and value['token'] == token)
            if value['event'] in ('cancelled', 'refused', 'unavailable'):
                return value['event']
            need(value['event'] == expected)
        except queue.Empty:
            return 'timeout'
        except Exception:
            return 'refused'
        if expected == 'ready':
            deadline = clock() + 60
            emit(json.dumps({'next': 'offer', 'instruction': 'Enter Group once; observe Confirm nearby peer.'}), flush=True)
        elif expected == 'offer':
            emit(json.dumps({'next': 'local_confirmed', 'instruction':
                 'Tap Confirm matching details. Attest only on: Your device confirmed this step. Group joining is not complete.'}), flush=True)
    return 'local_confirmed'


def main():
    need(sys.flags.isolated and sys.flags.no_site and sys.dont_write_bytecode)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['check', 'phone-check', 'execute', 'recover'])
    parser.add_argument('--binding', type=Path, required=True)
    parser.add_argument('--binding-sha256', required=True)
    parser.add_argument('--request', type=Path)
    parser.add_argument('--request-sha256')
    parser.add_argument('--grant', type=Path)
    parser.add_argument('--grant-sha256')
    parser.add_argument('--adb', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    binding = verify_binding(root, args.binding, args.binding_sha256, args.mode == 'recover')
    # Only import local policy after its reviewed source closure was verified.
    sys.path.insert(0, str(root / 'tools'))
    import ble_confirmation_trial as engine
    import ble_confirmation_trial_transport as transport
    candidate = (b'' if args.mode == 'recover' else
                 checked_file(root, binding['candidate'], engine.CANDIDATE['sha256']))
    if args.mode == 'check':
        print(json.dumps({'operator_inputs': 'verified', 'hardware_access': False}))
        return
    if args.mode in ('phone-check', 'execute'):
        need(args.adb is not None)
        need(str(args.adb.resolve()) == binding['adb']['path'])
        need(digest(args.adb.read_bytes()) == binding['adb']['sha256'])
        phone = phone_check(args.adb)
        if args.mode == 'phone-check':
            print(json.dumps(phone))
            return
    need(args.request is not None and args.grant is not None)
    request_raw = args.request.read_bytes()
    need(digest(request_raw) == args.request_sha256)
    request = engine.validate_request(json.loads(request_raw))
    need(request['runtime_sha256'] == args.binding_sha256)
    need(request['protected']['bootloader']['sha256'] in BOOT_HASHES)
    grant_raw = args.grant.read_bytes()
    engine.validate_grant(request, grant_raw, args.grant_sha256, args.mode, time.time)
    lines = Lines(sys.stdin)
    ephemeral = lines.read(60)
    need(set(ephemeral) == {'route', 'identity', 'binding_key'})
    backend = transport.Transport(manifest_path=root / binding['runtime_manifest'],
                                  manifest_sha256=binding['runtime_sha256'], private_root=root / '.private',
                                  route=ephemeral['route'], expected_identity=ephemeral['identity'],
                                  opaque_binding=request['device_binding'], binding_key=bytes.fromhex(ephemeral['binding_key']),
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
        print('{"result":"operator_refused_or_custody_held","instruction":"Inspect custody before any retry."}')
        sys.exit(1)
