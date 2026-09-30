"""OT-0101e lifecycle composition; inert import/check, no grant issuer.

One candidate session, one additional warm restart, existing original-span
restoration. No passive recorder, continuous runtime evidence or phone automation.
Execute is unavailable with old grants or the uncorrected revision-1 acceptance.
"""
import argparse
import json
from pathlib import Path
import sys
import time

REQUIRED_FILES = {
    'tools/run_gnss_lifecycle_trial.py', 'tools/gnss_lifecycle_observation.py',
    'tools/gnss_observation_trial.py', 'tools/run_gnss_observation_trial.py',
    'tools/ble_confirmation_trial_transport.py', 'tools/ble_startup_diagnostics.py',
    'tools/security_policy_execution.py', 'tools/security_policy_input_readback.py',
    'tools/security_policy_deadline_operator.py', 'tools/security_policy_bundle.py',
    'tools/security_policy_capture.py', 'tools/gnss_counter_capture.py',
    'tests/host/gnss_lifecycle_trial_tests.py',
    'docs/testing/OT-0101e-LIFECYCLE-TRIAL-PLAN-2026-09-28.md',
}


def modules():
    directory = str(Path(__file__).resolve().parent)
    if directory not in sys.path:
        sys.path.insert(0, directory)
    import gnss_observation_trial as engine
    import run_gnss_observation_trial as legacy
    import gnss_lifecycle_observation as lifecycle
    import ble_confirmation_trial_transport as transport
    return engine, legacy, lifecycle, transport


def verify_binding(root, source, expected, *, recovery=False):
    engine, legacy, _, _ = modules()
    legacy.need(source.stat().st_size <= 32768)
    raw = source.read_bytes()
    legacy.need(engine.sha(raw) == expected)
    obj = engine.decode(raw)
    legacy.need(type(obj) is dict and set(obj) ==
        {'schema', 'files', 'runtime_manifest', 'runtime_sha256', 'candidate', 'profile', 'registry', 'apk'})
    legacy.need(obj['schema'] == 'OT0101E-LIFECYCLE-OPERATOR-BINDING-1' and
                obj['profile'] == 'A_STACK_8192')
    files, apk = obj['files'], obj['apk']
    legacy.need(type(files) is dict and REQUIRED_FILES <= set(files))
    legacy.need(type(apk) is dict and set(apk) == {'path', 'bytes', 'sha256', 'signer_sha256'} and
                type(apk['bytes']) is int and apk['bytes'] > 0 and
                type(apk['signer_sha256']) is str and engine.common.HEX64.fullmatch(apk['signer_sha256']))
    legacy.need(apk['path'] in files and files[apk['path']] == apk['sha256'] and obj['registry'] in files)
    for relative, pin in files.items():
        if recovery and relative in (obj['candidate'], obj['registry'], apk['path']):
            continue
        legacy.checked_file(root, relative, pin)
    legacy.checked_file(root, obj['runtime_manifest'], obj['runtime_sha256'])
    if not recovery:
        candidate = legacy.checked_file(root, obj['candidate'], engine.CONNECTION_CANDIDATES[obj['profile']]['sha256'])
        legacy.need(engine.descriptor(candidate) == engine.CONNECTION_CANDIDATES[obj['profile']])
        legacy.need(len(legacy.checked_file(root, apk['path'], apk['sha256'])) == apk['bytes'])
    return obj


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('check', 'execute', 'recover'))
    for name in ('binding', 'request', 'grant', 'ephemeral'):
        parser.add_argument('--' + name, type=Path, required=name == 'binding')
        parser.add_argument('--' + name + '-sha256', required=name == 'binding')
    parser.add_argument('--original-control-verified', action='store_true')
    parser.add_argument('--acceptance-revision-verified', type=int)
    args = parser.parse_args(argv)
    engine, legacy, lifecycle, transport = modules()
    legacy.need(sys.flags.isolated and sys.flags.no_site and sys.dont_write_bytecode)
    root = Path(__file__).resolve().parents[1]
    binding = verify_binding(root, args.binding, args.binding_sha256, recovery=args.mode == 'recover')
    if args.mode == 'check':
        transport.probe_runtime(manifest_path=root / binding['runtime_manifest'],
            manifest_sha256=binding['runtime_sha256'], private_root=root / '.private')
        print('{"operator_inputs":"verified","hardware_access":false,"serial_enumeration":false}')
        return
    legacy.need(all(getattr(args, key) is not None for key in
        ('request', 'request_sha256', 'grant', 'grant_sha256', 'ephemeral', 'ephemeral_sha256')))
    legacy.need(args.request.stat().st_size <= 4096 and args.grant.stat().st_size <= 4096)
    raw = args.request.read_bytes()
    legacy.need(engine.sha(raw) == args.request_sha256)
    request = engine.validate_request(engine.decode(raw))
    legacy.need(request['schema'] == 'OT0101E-LIFECYCLE-REQUEST-1' and
                request['runtime_sha256'] == args.binding_sha256 and request['profile'] == binding['profile'])
    grant_raw = args.grant.read_bytes()
    grant = engine.validate_grant(request, grant_raw, args.grant_sha256, args.mode, time.time)
    ephemeral_path = args.ephemeral.resolve()
    legacy.need(ephemeral_path.is_relative_to(root / '.private') and not args.ephemeral.is_symlink() and
                args.ephemeral.stat().st_size <= 512)
    raw = args.ephemeral.read_bytes()
    legacy.need(engine.sha(raw) == args.ephemeral_sha256)
    ephemeral = engine.decode(raw)
    legacy.need(type(ephemeral) is dict and set(ephemeral) == {'route', 'identity', 'binding_key'})
    if args.mode == 'execute':
        legacy.need(args.original_control_verified and args.acceptance_revision_verified ==
                    request['approved_revision'] and args.acceptance_revision_verified >= 2)
        from gnss_counter_capture import bound_identity, normalized_identity
        legacy.need(bound_identity(root / binding['registry'], 'OT-DEV-002') ==
                    normalized_identity(ephemeral['identity']))
    candidate = b'' if args.mode == 'recover' else legacy.checked_file(
        root, binding['candidate'], request['candidate']['sha256'])
    backend = transport.Transport(manifest_path=root / binding['runtime_manifest'],
        manifest_sha256=binding['runtime_sha256'], private_root=root / '.private',
        route=ephemeral['route'], expected_identity=ephemeral['identity'],
        opaque_binding=request['device_binding'], binding_key=bytes.fromhex(ephemeral['binding_key']),
        candidate=candidate + b'\xff' * (engine.SPANS['application'][1] - len(candidate)),
        recovery_only=args.mode == 'recover', startup_diagnostics=args.mode == 'execute')
    if args.mode == 'execute':
        def observe(restart, record):
            # Do not restart the observation deadline after any stage. Grant
            # expiry can shorten it; restoration remains available afterward.
            remaining = min(lifecycle.PLAN['observation_seconds'], grant['expires_utc'] - time.time())
            legacy.need(remaining > 0)
            return lifecycle.observe(legacy.Lines(sys.stdin), restart,
                                     available_seconds=remaining, on_case=record)
        result = engine.execute(root, request, grant_raw, args.grant_sha256, candidate, backend, observe)
    else:
        result = engine.recover(root, request, grant_raw, args.grant_sha256, backend)
    print(json.dumps(result, separators=(',', ':')))


if __name__ == '__main__':
    try:
        main()
    except BaseException:
        print('{"result":"lifecycle_operator_refused_or_custody_held","instruction":"Inspect custody before any retry."}')
        sys.exit(1)
