"""Inert diagnostic entrypoint; physical execution needs fresh pinned authority.

No grants are issued. No candidate runner, adoption, image write or native UI is
available. Imports/default CLI are host-only. Physical execution is restricted
to the maintained retained capture/verify/terminal release APIs in the exact
fourteen-source isolated runtime. The historical quarantine gate is unchanged.
"""
import argparse
import base64
from datetime import datetime, timezone
import hashlib
import hmac
import importlib
import json
import math
import os
from pathlib import Path
import re
import sys
import time

SOURCE_NAMES = {'enrollment_candidate_runtime.py', 'enrollment_candidate_runner.py',
    'enrollment_candidate_capture_runner.py', 'enrollment_candidate_original_capture.py',
    'enrollment_candidate_private_view.py', 'enrollment_candidate_operator.py',
    'enrollment_candidate_rom_adapter.py', 'enrollment_candidate_controller.py',
    'enrollment_candidate_custody.py', 'enrollment_candidate_usb_client.py',
    'ble_confirmation_trial.py', 'security_policy_execution.py',
    'security_policy_input_readback.py', 'security_policy_deadline_operator.py'}
ACTIONS = ['guard_original', 'read_six_spans', 'reset_original']
SCOPE = 'retained_freshness_diagnostic_and_one_guarded_original_boot_per_role'
IMMUTABLE = {'bootloader', 'partition', 'otadata', 'application', 'ota0_prefix'}
REVIEWED_V18_SOURCE_PINS_SHA256 = '97db20166ae9cbe310a42eceba4665d9df1764e394c9c6d2825e7c1df430de63'
REVIEWED_V18_RUNTIME_SHA256 = '0c87bad6c4c75b01271143734fffb1060efa0ef0badf4aec74387a7846edc77a'
REVIEWED_QUARANTINE_SHA256 = '3460aea3cc43b2f8fdca083dc08dbb08372f40737ce9859522ca747f2c561915'
REVIEWED_GATE_SHA256 = '98c8a760f186872d14e55ce3da6037dfbd1dd1298491d3021293f6e0693791bf'
REVIEWED_PRESERVATION_SHA256 = '8f2808026efa55b2d4b8f9d275390aec7ff7b64eb2c20bad4d15b821290ee31a'
PACKAGE_FIELDS = {'schema', 'private_root', 'evidence_root', 'source_root', 'source_pins',
    'assembly', 'capture_request', 'capture_grant', 'identities', 'binding_key', 'profiles',
    'comparator_request', 'legacy_preservation', 'legacy_disposition', 'legacy_gate_module',
    'controller_sha256'}


class DiagnosticError(RuntimeError):
    """Fixed non-identifying refusal category."""


def need(ok, category='diagnostic_input_invalid'):
    if not ok:
        raise DiagnosticError(category)


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode('utf-8')


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def decode(raw):
    def unique(pairs):
        out = {}
        for key, value in pairs:
            need(key not in out)
            out[key] = value
        return out
    try:
        return json.loads(raw, object_pairs_hook=unique, parse_constant=lambda _: need(False))
    except (ValueError, UnicodeError):
        raise DiagnosticError('diagnostic_input_invalid') from None


def path_inside(path, private, directory=False):
    path, private = Path(path), Path(private)
    need(path.is_absolute() and private.is_absolute() and private.name == '.private')
    for item in (path, *path.parents, private, *private.parents):
        need(not item.is_symlink() and not item.is_junction(), 'diagnostic_path_invalid')
    need(private.is_dir() and path.resolve().is_relative_to(private.resolve())
         and (path.is_dir() if directory else path.is_file()), 'diagnostic_path_invalid')
    return path


def read_ref(ref, private):
    need(type(ref) is dict and set(ref) == {'path', 'bytes', 'sha256'}
         and type(ref['path']) is str and type(ref['bytes']) is int
         and 0 <= ref['bytes'] <= 16 * 1024 * 1024
         and type(ref['sha256']) is str and re.fullmatch('[0-9a-f]{64}', ref['sha256']))
    raw = path_inside(ref['path'], private).read_bytes()
    need(len(raw) == ref['bytes'] and sha(raw) == ref['sha256'], 'diagnostic_reference_changed')
    return raw


def reference(path, private):
    path = path_inside(path, private)
    raw = path.read_bytes()
    return {'path': str(path), 'bytes': len(raw), 'sha256': sha(raw)}


def once(path, raw, private):
    path = Path(path)
    path_inside(path.parent, private, True)
    need(not path.is_symlink() and not path.is_junction(), 'diagnostic_path_invalid')
    try:
        with path.open('xb', buffering=0) as stream:
            need(stream.write(raw) == len(raw), 'diagnostic_record_failed')
            os.fsync(stream.fileno())
    except FileExistsError:
        raise DiagnosticError('diagnostic_attempt_used') from None
    need(path.read_bytes() == raw, 'diagnostic_record_failed')


def _legacy(prepared):
    private, data = prepared['private'], prepared['data']
    need(sha(data['legacy_preservation']) == REVIEWED_PRESERVATION_SHA256
         and sha(data['legacy_disposition']) == REVIEWED_QUARANTINE_SHA256
         and sha(data['legacy_gate_module']) == REVIEWED_GATE_SHA256,
         'diagnostic_legacy_review_pin_changed')
    preserved = decode(data['legacy_preservation'])
    need(type(preserved) is dict and preserved.get('schema') == 'OT-LEGACY-SESSION-PRESERVATION-1'
         and preserved.get('before_equals_after') is True and type(preserved.get('files')) is dict
         and len(preserved['files']) == 39)
    session = path_inside(preserved['session'], private, True)
    need(session != prepared['output'] and not prepared['output'].is_relative_to(session)
         and not session.is_relative_to(prepared['output']), 'diagnostic_output_invalid')
    need(set(preserved['files']) == {str(p) for p in session.iterdir() if p.is_file()},
         'diagnostic_legacy_changed')
    for path, ref in preserved['files'].items():
        need(ref['path'] == path and Path(path).parent == session)
        read_ref(ref, private)
    for p in session.iterdir():
        need(not p.name.endswith(('.pending', '.interrupted', '.transfer'))
             and p.name != 'enrollment-candidate-active.json'
             and not re.fullmatch(r'enrollment-candidate-[0-9a-f]{32}\.json', p.name),
             'diagnostic_legacy_custody_changed')
    disposition = decode(data['legacy_disposition'])
    need(disposition.get('schema') == 'OT-MANUAL-RECOVERY-DISPOSITION-1'
         and disposition.get('status') == 'manual_recovery_observed_baseline_unresolved'
         and disposition.get('candidate_eligible') is False
         and disposition.get('automatic_custody_released') is False
         and disposition.get('session') == str(session))
    for ref in disposition['references'].values():
        read_ref(ref, private)
    return preserved


def inspect(package_path, expected_sha256):
    """Host-only pins/schema validation, without grants, SDK imports or USB."""
    raw = Path(package_path).read_bytes()
    need(type(expected_sha256) is str and re.fullmatch('[0-9a-f]{64}', expected_sha256)
         and sha(raw) == expected_sha256, 'diagnostic_package_changed')
    package = decode(raw)
    need(type(package) is dict and set(package) == PACKAGE_FIELDS
         and package['schema'] == 'OT-RETAINED-RECOVERY-DIAGNOSTIC-PACKAGE-1')
    private = Path(package['private_root'])
    path_inside(package_path, private)
    output = path_inside(package['evidence_root'], private, True)
    need(output != private and not any(output.iterdir()), 'diagnostic_output_invalid')
    need(package['controller_sha256'] == sha(Path(__file__).read_bytes()), 'diagnostic_controller_changed')
    ref_names = PACKAGE_FIELDS - {'schema', 'private_root', 'evidence_root', 'source_root', 'controller_sha256'}
    data = {name: read_ref(package[name], private) for name in ref_names}
    source_root = path_inside(package['source_root'], private, True)
    need(source_root.name == 'policy' and not output.is_relative_to(source_root.parent),
         'diagnostic_output_invalid')
    pins = decode(data['source_pins'])
    need(sha(data['source_pins']) == REVIEWED_V18_SOURCE_PINS_SHA256
         and pins.get('schema') == 'OT-CANDIDATE-SOURCE-PINS-1' and type(pins.get('files')) is dict
         and set(pins['files']) == {'tools/' + name for name in SOURCE_NAMES})
    for name, pin in pins['files'].items():
        read_ref(dict(pin, path=str(source_root / Path(name).name)), private)
    request, grant = decode(data['capture_request']), decode(data['capture_grant'])
    comparator = decode(data['comparator_request'])
    assembly = decode(data['assembly'])
    need(assembly.get('schema') == 'OT-CANDIDATE-ASSEMBLY-1'
         and assembly.get('root') == str(source_root.parent)
         and assembly.get('source_root') == str(private.parent)
         and assembly.get('source_pins') == package['source_pins']
         and assembly.get('sources') == pins['files'], 'diagnostic_assembly_binding_invalid')
    manifest_raw = read_ref(assembly['runtime'], private)
    manifest = decode(manifest_raw)
    need(sha(manifest_raw) == REVIEWED_V18_RUNTIME_SHA256
         and manifest.get('root') == str(source_root.parent) and manifest.get('worktree') == str(private.parent)
         and request.get('runtime_sha256') == sha(manifest_raw)
         and type(manifest.get('files')) is dict
         and all(manifest['files'].get('policy/' + Path(n).name) == p for n, p in pins['files'].items()),
         'diagnostic_assembly_binding_invalid')
    # The fixed reviewed capsule also contains unchanged base support modules.
    # Admit exactly its complete policy inventory, retaining the strict updated
    # fourteen-source tuple above and checking every support file's own pin.
    policy_files = {name: pin for name, pin in manifest['files'].items() if name.startswith('policy/')}
    actual_policy = list(source_root.rglob('*'))
    need(all(not p.is_symlink() and not p.is_junction() for p in actual_policy)
         and {p.relative_to(source_root.parent).as_posix() for p in actual_policy if p.is_file()}
         == set(policy_files), 'diagnostic_source_tuple_changed')
    for name, pin in policy_files.items():
        read_ref(dict(pin, path=str(source_root.parent / name)), private)
    need(request.get('schema') == 'OT-RETAINED-ORIGINAL-REQUEST-1'
         and set(request.get('roles', {})) == {'A', 'B'}
         and comparator.get('schema') == 'OT-CANDIDATE-REQUEST-1'
         and comparator.get('runtime_sha256') == request['runtime_sha256']
         and set(comparator.get('roles', {})) == {'A', 'B'})
    need(type(grant) is dict and set(grant) == {'schema', 'attempt', 'request_sha256',
         'runtime_sha256', 'operation', 'origin_attempt', 'attempt_count', 'actions',
         'issued_utc', 'capture_expires_utc', 'cleanup_expires_utc'}
         and grant['schema'] == 'OT-ORIGINAL-CAPTURE-GRANT-1' and grant['operation'] == 'capture'
         and grant['origin_attempt'] is None and type(grant['attempt_count']) is int
         and grant['attempt_count'] == 1 and grant['actions'] == ACTIONS
         and type(grant['attempt']) is str and re.fullmatch('[0-9a-f]{32}', grant['attempt'])
         and grant['request_sha256'] == sha(canonical(request))
         and grant['runtime_sha256'] == request['runtime_sha256'])
    need(all(type(grant[k]) is int for k in ('issued_utc', 'capture_expires_utc', 'cleanup_expires_utc'))
         and 0 < grant['capture_expires_utc'] - grant['issued_utc'] <= 2100
         and grant['capture_expires_utc'] <= grant['cleanup_expires_utc']
         and grant['cleanup_expires_utc'] - grant['issued_utc'] <= 3600)
    identities, profiles = decode(data['identities']), decode(data['profiles'])
    need(identities.get('schema') == 'OT-CANDIDATE-IDENTITIES-1'
         and profiles.get('schema') == 'OT-CANDIDATE-PROFILES-1'
         and set(identities.get('roles', {})) == set(profiles.get('roles', {})) == {'A', 'B'}
         and len(data['binding_key']) == 32)
    normalized = {}
    for role in ('A', 'B'):
        value = identities['roles'][role]
        need(type(value) is str and re.fullmatch(r'(?:[0-9a-fA-F]{12}|[0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5}|[0-9a-fA-F]{2}(?:-[0-9a-fA-F]{2}){5})', value))
        normalized[role] = value.replace(':', '').replace('-', '').lower()
        binding = hmac.new(data['binding_key'], b'OT218-DEVICE\0' + normalized[role].encode('ascii'), hashlib.sha256).hexdigest()
        profile, row = profiles['roles'][role], request['roles'][role]
        need(row.get('device_binding') == binding and comparator['roles'][role].get('device_binding') == binding
             and type(profile) is dict and set(profile) == {'model', 'device_binding', 'flash_bytes', 'evidence_sha256'}
             and profile['model'] == 'heltec_v4_esp32s3' and profile['device_binding'] == binding
             and type(profile['flash_bytes']) is int and profile['flash_bytes'] == 16777216
             and profile['evidence_sha256'] == row.get('profile_evidence_sha256'))
    need(normalized['A'] != normalized['B'])
    prepared = {'package': package, 'package_ref': reference(package_path, private), 'data': data,
                'private': private, 'output': output, 'source_root': source_root, 'pins': pins,
                'request': request, 'grant': grant, 'comparator': comparator}
    prepared['preserved'] = _legacy(prepared)
    return prepared


def _admit(package_path, package_sha256, authorization_path, authorization_sha256,
           approved_device_execution, utc=time.time, monotonic=time.monotonic, *, spend_authorization=True):
    need(approved_device_execution is True, 'diagnostic_execution_not_authorized')
    need(authorization_path is not None and authorization_sha256 is not None,
         'diagnostic_authorization_missing')
    prepared = inspect(package_path, package_sha256)
    private, package, grant = prepared['private'], prepared['package'], prepared['grant']
    auth_ref = reference(authorization_path, private)
    need(auth_ref['sha256'] == authorization_sha256, 'diagnostic_authorization_changed')
    auth = decode(read_ref(auth_ref, private))
    fields = {'schema', 'scope', 'owner_approved', 'package_sha256', 'controller_sha256',
        'capture_grant_sha256', 'capture_attempt', 'legacy_disposition_sha256',
        'identities_sha256', 'profiles_sha256', 'actions', 'issued_utc', 'capture_expires_utc',
        'cleanup_expires_utc', 'one_use', 'fresh_identity_profiles_reviewed',
        'rom_entry_and_guarded_original_boot_authorized', 'candidate_operations_authorized'}
    need(type(auth) is dict and set(auth) == fields and auth['schema'] == 'OT-RECOVERY-DIAGNOSTIC-AUTHORIZATION-1'
         and auth['scope'] == SCOPE and auth['owner_approved'] is True and auth['one_use'] is True
         and auth['fresh_identity_profiles_reviewed'] is True
         and auth['rom_entry_and_guarded_original_boot_authorized'] is True
         and auth['candidate_operations_authorized'] is False and auth['actions'] == ACTIONS
         and auth['package_sha256'] == package_sha256
         and auth['controller_sha256'] == package['controller_sha256']
         and auth['capture_grant_sha256'] == package['capture_grant']['sha256']
         and auth['capture_attempt'] == grant['attempt']
         and auth['legacy_disposition_sha256'] == package['legacy_disposition']['sha256']
         and auth['identities_sha256'] == package['identities']['sha256']
         and auth['profiles_sha256'] == package['profiles']['sha256']
         and all(type(auth[k]) is int and auth[k] == grant[k]
         for k in ('issued_utc', 'capture_expires_utc', 'cleanup_expires_utc')),
         'diagnostic_authorization_invalid')
    now, start = utc(), monotonic()
    need(type(now) in (int, float) and type(start) in (int, float)
         and math.isfinite(now) and math.isfinite(start)
         and auth['issued_utc'] <= now < auth['capture_expires_utc'], 'diagnostic_authorization_expired')
    prepared.update(authorization=auth, authorization_ref=auth_ref,
        capture_deadline=min(start + 2100, start + auth['capture_expires_utc'] - now),
        cleanup_deadline=min(start + 3600, start + auth['cleanup_expires_utc'] - now))
    # This one-use authorization marker is spent before importing execution
    # dependencies, after every pure host check. No default/inert call gets here.
    if spend_authorization:
        once(prepared['output'] / ('diagnostic-authorization-used-' + auth_ref['sha256']),
             canonical({'authorization': auth_ref, 'package': prepared['package_ref']}) + b'\n', private)
    return prepared


def _load_dependencies(prepared):
    # Caller must be the separately reviewed -I -S -B -c bootstrap executing this
    # source in an unregistered namespace. Never mutate sys.path or module origins.
    root = prepared['source_root'].parent
    need(sys.flags.isolated and sys.flags.no_site and sys.dont_write_bytecode
         and Path(sys.executable).resolve() == root / 'python.exe'
         and [Path(p).resolve() for p in sys.path] == [root / n for n in ('Lib', 'DLLs', 'packages', 'policy')],
         'diagnostic_isolated_caller_required')
    names = ('enrollment_candidate_controller', 'enrollment_candidate_custody',
             'enrollment_candidate_rom_adapter', 'enrollment_candidate_original_capture',
             'enrollment_candidate_runtime')
    modules = {}
    for name in names:
        module = importlib.import_module(name)
        need(Path(module.__file__).resolve() == prepared['source_root'] / (name + '.py'),
             'diagnostic_module_origin_invalid')
        modules[name] = module
    return modules


class GuardedRuntime:
    """Permit only exact capture-bound maintained operations; record validated proof."""
    def __init__(self, runtime, prepared, identities):
        self.runtime, self.prepared, self.identities = runtime, prepared, identities
        self.proofs, self.reset_attempted, self.authorities = [], set(), {}

    def __getattr__(self, name):
        return getattr(self.runtime, name)

    def invoke(self, payload, deadline):
        op = payload.get('operation')
        need(op in {'guard', 'read', 'guarded_read', 'release_snapshot', 'reset_original'}
             and payload.get('mode') == 'capture-only', 'diagnostic_operation_refused')
        authority = payload.get('authority')
        need(type(authority) is dict and authority.get('role') in ('A', 'B'), 'diagnostic_operation_refused')
        role = authority['role']
        prepared, grant = self.prepared, self.prepared['grant']
        expected = {'schema': 'OT-CANDIDATE-CAPTURE-AUTHORITY-1',
            'runtime_sha256': prepared['request']['runtime_sha256'],
            'request_sha256': grant['request_sha256'], 'grant_sha256': prepared['package']['capture_grant']['sha256'],
            'attempt': grant['attempt'], 'role': role,
            'device_binding': prepared['request']['roles'][role]['device_binding'], 'actions': ACTIONS,
            'capture_deadline': authority.get('capture_deadline'), 'cleanup_deadline': authority.get('cleanup_deadline')}
        need(authority == expected and payload.get('identity') == self.identities[role],
             'diagnostic_operation_refused')
        need(all(type(authority[k]) in (int, float) and math.isfinite(authority[k])
                 and authority[k] <= prepared[k] for k in ('capture_deadline', 'cleanup_deadline'))
             and authority['capture_deadline'] <= authority['cleanup_deadline'],
             'diagnostic_deadline_extended')
        need(role not in self.authorities or self.authorities[role] == authority,
             'diagnostic_operation_refused')
        self.authorities.setdefault(role, dict(authority))
        ceiling = prepared['cleanup_deadline'] if op in ('release_snapshot', 'reset_original') else prepared['capture_deadline']
        need(type(deadline) in (int, float) and math.isfinite(deadline) and deadline <= ceiling,
             'diagnostic_deadline_extended')
        if op == 'reset_original':
            need(role not in self.reset_attempted, 'diagnostic_reset_replay_refused')
            self.reset_attempted.add(role)
        answer = self.runtime.invoke(payload, deadline)
        need(type(answer) is dict and answer.get('ok') is True and answer.get('closed') is True
             and type(answer.get('admission')) is dict
             and answer['admission'].get('boot_selection') == 'verified-factory'
             and answer['admission'].get('layout_sha256') == prepared['comparator']['roles'][role]['originals']['partition']['sha256'],
             'diagnostic_worker_proof_invalid')
        proof = {'role': role, 'operation': op, 'validated_success': True,
                 'factory_original_layout': True, 'deadline': deadline}
        if op in ('release_snapshot', 'reset_original'):
            expected_originals = prepared['comparator']['roles'][role]['originals']
            need(payload.get('observed') == expected_originals, 'diagnostic_worker_proof_invalid')
            proof.update(immutable_five={n: expected_originals[n] for n in sorted(IMMUTABLE)},
                         safe_current_nvs_guarded=True)
            if op == 'reset_original':
                proof['reset_current_nvs'] = payload.get('release_nvs', expected_originals['nvs'])
        self.proofs.append(proof)
        return answer


def execute(package_path, package_sha256, authorization_path=None, authorization_sha256=None,
            *, approved_device_execution=False):
    """Future physical entry: all current authorization/pins must be supplied."""
    prepared = _admit(package_path, package_sha256, authorization_path,
                      authorization_sha256, approved_device_execution)
    modules = _load_dependencies(prepared)
    return _run_admitted(prepared, modules)


def _run_admitted(prepared, modules):
    capture = modules['enrollment_candidate_original_capture']
    rom = modules['enrollment_candidate_rom_adapter']
    runtime_module = modules['enrollment_candidate_runtime']
    package, data = prepared['package'], prepared['data']
    private, output = prepared['private'], prepared['output']
    assembly = runtime_module.verify_assembly(package['assembly']['path'], package['assembly']['sha256'], require_isolated=True)
    need(assembly.root == prepared['source_root'].parent
         and assembly.manifest_sha256 == prepared['request']['runtime_sha256'], 'diagnostic_runtime_invalid')
    request = capture.validate_request(prepared['request'])
    capture.validate_grant(request, data['capture_grant'], package['capture_grant']['sha256'], 'capture', time.time)
    identities_value, profiles_value = decode(data['identities']), decode(data['profiles'])
    need(identities_value.get('schema') == 'OT-CANDIDATE-IDENTITIES-1'
         and profiles_value.get('schema') == 'OT-CANDIDATE-PROFILES-1'
         and set(identities_value.get('roles', {})) == {'A', 'B'}
         and set(profiles_value.get('roles', {})) == {'A', 'B'})
    identities = {r: rom.identity(identities_value['roles'][r]) for r in ('A', 'B')}
    profiles = {r: rom.DeviceProfile(**profiles_value['roles'][r]) for r in ('A', 'B')}
    need(len(data['binding_key']) == 32)
    runtime = GuardedRuntime(rom.Runtime(assembly.manifest_path, assembly.manifest_sha256, private,
                                       monotonic=time.monotonic), prepared, identities)
    lease, session, fault, cleanup_fault, lease_closed = rom.HardwareLease(private, monotonic=time.monotonic), None, None, None, False
    captured, verified = False, False
    try:
        _legacy(prepared)
        session = capture.CaptureSession(private, output, request, data['capture_grant'],
            package['capture_grant']['sha256'], runtime=runtime, lease=lease,
            identities=identities, binding_key=data['binding_key'], profiles=profiles,
            capture_deadline=prepared['capture_deadline'], cleanup_deadline=prepared['cleanup_deadline'],
            utc=time.time, monotonic=time.monotonic)
        result = session.capture()
        captured = result.capture_complete is True
        need(captured and result.outcome == 'captured', 'diagnostic_capture_incomplete')
        verified = session.verify(prepared['comparator'], runtime=runtime, lease=lease,
            identities=identities, binding_key=data['binding_key'], profiles=profiles,
            deadline=prepared['capture_deadline']) is True
    except BaseException:
        fault = 'diagnostic_capture_or_freshness_failed'
    finally:
        if session is not None and session.result.outcome != 'not_started':
            try:
                session.release()
            except BaseException:
                cleanup_fault = 'diagnostic_terminal_release_failed'
        try:
            lease_closed = lease.close() is True
        except BaseException:
            cleanup_fault = cleanup_fault or 'diagnostic_lease_close_failed'
        _legacy(prepared)
    result = session.result if session is not None else None
    rows = result.roles if result is not None else {}
    if result is not None and not isinstance(rows, dict):
        rows = {}
    drift = {}
    for role in ('A', 'B'):
        name = f'enrollment-original-{prepared["grant"]["attempt"]}-{role}-current-nvs-drift'
        binary, evidence = output / (name + '.bin'), output / (name + '.json')
        if binary.is_file() and evidence.is_file():
            drift[role] = {'data': reference(binary, private), 'evidence': reference(evidence, private)}
    receipt = {'schema': 'OT-RECOVERY-DIAGNOSTIC-RECEIPT-1', 'origin': 'live_authorized_controller',
        'recorded_at': datetime.now(timezone.utc).isoformat(), 'package': prepared['package_ref'],
        'authorization': prepared['authorization_ref'], 'controller_sha256': package['controller_sha256'],
        'source_pins': package['source_pins'], 'legacy_preservation': package['legacy_preservation'],
        'legacy_disposition': package['legacy_disposition'], 'capture_attempt': prepared['grant']['attempt'],
        'capture_deadline': prepared['capture_deadline'], 'cleanup_deadline': prepared['cleanup_deadline'],
        'capture_complete': captured, 'freshness_verified': verified, 'first_failure': fault,
        'cleanup_failure': cleanup_fault or (result.cleanup_failure if result is not None else None),
        'roles': rows, 'proofs': runtime.proofs, 'reset_attempted': sorted(runtime.reset_attempted),
        'lease_closed': lease_closed, 'current_nvs_drift': drift,
        'automatic_legacy_custody_released': False, 'candidate_eligible': False,
        'owner_observation': 'separate_pinned_observation_required'}
    if result is not None and result.journal_path is not None:
        receipt['diagnostic_journal'] = reference(result.journal_path, private)
    destination = output / 'diagnostic-receipt.json'
    once(destination, canonical(receipt) + b'\n', private)
    return reference(destination, private)


def consume_receipt(receipt_ref, owner_observation_ref, independent_review_ref, destination, private):
    """Link only reviewed new evidence; never release legacy/candidate authority."""
    receipt = decode(read_ref(receipt_ref, private))
    owner = decode(read_ref(owner_observation_ref, private))
    review = decode(read_ref(independent_review_ref, private))
    need(receipt.get('schema') == 'OT-RECOVERY-DIAGNOSTIC-RECEIPT-1'
         and receipt.get('origin') == 'live_authorized_controller'
         and receipt.get('candidate_eligible') is False
         and receipt.get('automatic_legacy_custody_released') is False
         and receipt.get('capture_complete') is True and receipt.get('lease_closed') is True
         and receipt.get('cleanup_failure') is None and set(receipt.get('roles', {})) == {'A', 'B'},
         'diagnostic_receipt_unresolved')
    package = decode(read_ref(receipt['package'], private))
    root = path_inside(package['evidence_root'], private, True)
    need(Path(receipt_ref['path']) == root / 'diagnostic-receipt.json'
         and Path(destination).parent == root, 'diagnostic_output_invalid')
    need(receipt.get('controller_sha256') == sha(Path(__file__).read_bytes())
         == package['controller_sha256'] and receipt.get('source_pins') == package['source_pins'],
         'diagnostic_controller_changed')
    comparator = decode(read_ref(package['comparator_request'], private))
    need(sha(read_ref(receipt['source_pins'], private)) == REVIEWED_V18_SOURCE_PINS_SHA256,
         'diagnostic_source_tuple_changed')
    auth = decode(read_ref(receipt['authorization'], private))
    need(auth.get('schema') == 'OT-RECOVERY-DIAGNOSTIC-AUTHORIZATION-1'
         and auth.get('scope') == SCOPE and auth.get('owner_approved') is True
         and auth.get('one_use') is True and auth.get('candidate_operations_authorized') is False
         and auth.get('fresh_identity_profiles_reviewed') is True
         and auth.get('rom_entry_and_guarded_original_boot_authorized') is True
         and auth.get('actions') == ACTIONS and auth.get('package_sha256') == receipt['package']['sha256']
         and auth.get('controller_sha256') == receipt['controller_sha256']
         and auth.get('capture_grant_sha256') == package['capture_grant']['sha256']
         and auth.get('capture_attempt') == receipt.get('capture_attempt')
         and auth.get('legacy_disposition_sha256') == package['legacy_disposition']['sha256']
         and auth.get('identities_sha256') == package['identities']['sha256']
         and auth.get('profiles_sha256') == package['profiles']['sha256'], 'diagnostic_authorization_invalid')
    grant = decode(read_ref(package['capture_grant'], private))
    need(grant.get('schema') == 'OT-ORIGINAL-CAPTURE-GRANT-1' and grant.get('operation') == 'capture'
         and grant.get('origin_attempt') is None and grant.get('attempt') == receipt['capture_attempt']
         and grant.get('actions') == ACTIONS and grant.get('attempt_count') == 1
         and all(type(auth.get(k)) is int and auth[k] == grant.get(k)
         for k in ('issued_utc', 'capture_expires_utc', 'cleanup_expires_utc'))
         and 0 < auth['capture_expires_utc'] - auth['issued_utc'] <= 2100
         and auth['capture_expires_utc'] <= auth['cleanup_expires_utc']
         and auth['cleanup_expires_utc'] - auth['issued_utc'] <= 3600,
         'diagnostic_authorization_invalid')
    read_ref(receipt['legacy_disposition'], private)
    read_ref(receipt['legacy_preservation'], private)
    need(receipt['legacy_disposition'] == package['legacy_disposition']
         and receipt['legacy_preservation'] == package['legacy_preservation'])
    _legacy({'private': Path(private), 'output': Path(package['evidence_root']), 'data': {
        'legacy_preservation': read_ref(receipt['legacy_preservation'], private),
        'legacy_disposition': read_ref(receipt['legacy_disposition'], private),
        'legacy_gate_module': read_ref(package['legacy_gate_module'], private)}})
    need(Path(receipt['diagnostic_journal']['path']) == root / ('enrollment-original-' + receipt['capture_attempt'] + '.json'),
         'diagnostic_receipt_unresolved')
    journal = decode(read_ref(receipt['diagnostic_journal'], private))
    need(journal.get('phase') == 'released' and journal.get('capture_complete') is True
         and journal.get('candidate') is None and journal.get('cleanup_failure') is None
         and journal.get('roles') == receipt['roles']
         and journal.get('request') == decode(read_ref(package['capture_request'], private))
         and journal.get('grant_sha256') == package['capture_grant']['sha256']
         and journal.get('attempt') == receipt.get('capture_attempt'),
         'diagnostic_receipt_unresolved')
    need(grant.get('request_sha256') == sha(canonical(journal['request']))
         and grant.get('runtime_sha256') == journal['request'].get('runtime_sha256')
         == comparator.get('runtime_sha256'), 'diagnostic_receipt_unresolved')
    need(receipt.get('reset_attempted') == ['A', 'B'], 'diagnostic_receipt_unresolved')
    drift_map = receipt.get('current_nvs_drift')
    need(type(drift_map) is dict and set(drift_map) <= {'A', 'B'}, 'diagnostic_receipt_unresolved')
    for role in ('A', 'B'):
        row = receipt['roles'][role]
        need(comparator['roles'][role].get('device_binding') == journal['request']['roles'][role].get('device_binding'),
             'diagnostic_receipt_unresolved')
        need(row.get('owner') == 'capture' and row.get('entered') is True
             and row.get('original_boot_allowed') is True and row.get('handles_closed') is True,
             'diagnostic_receipt_unresolved')
        need(row.get('pins') == comparator['roles'][role]['originals'], 'diagnostic_receipt_unresolved')
        resets = [p for p in receipt['proofs'] if p.get('role') == role and p.get('operation') == 'reset_original']
        need(len(resets) == 1 and resets[0].get('validated_success') is True
             and resets[0].get('factory_original_layout') is True
             and resets[0].get('safe_current_nvs_guarded') is True
             and resets[0].get('immutable_five') == {n: comparator['roles'][role]['originals'][n] for n in sorted(IMMUTABLE)},
             'diagnostic_receipt_unresolved')
        reset_pin = resets[0].get('reset_current_nvs')
        baseline_pin = comparator['roles'][role]['originals']['nvs']
        if reset_pin == baseline_pin:
            need(role not in drift_map and 'release_nvs_evidence' not in row, 'diagnostic_nvs_evidence_invalid')
        else:
            drift = drift_map.get(role)
            need(type(drift) is dict and set(drift) == {'data', 'evidence'}, 'diagnostic_nvs_evidence_invalid')
            raw, evidence_raw = read_ref(drift['data'], private), read_ref(drift['evidence'], private)
            evidence = decode(evidence_raw)
            need(len(raw) == 12288 and reset_pin == {k: drift['data'][k] for k in ('bytes', 'sha256')}
                 and evidence == {'schema': 'OT-ORIGINAL-NVS-DRIFT-EVIDENCE-1',
                    'attempt': receipt['capture_attempt'], 'role': role,
                    'device_binding': comparator['roles'][role]['device_binding'],
                    'request_sha256': grant['request_sha256'],
                    'grant_sha256': package['capture_grant']['sha256'],
                    'runtime_sha256': comparator['runtime_sha256'], 'retained_nvs': baseline_pin,
                    'current_nvs': reset_pin, 'purpose': 'cleanup_only_not_retained_baseline'}
                 and row.get('release_nvs_evidence') == {k: drift['evidence'][k] for k in ('bytes', 'sha256')},
                 'diagnostic_nvs_evidence_invalid')
    need(owner.get('schema') == 'OT-RECOVERY-DIAGNOSTIC-OWNER-OBSERVATION-1'
         and owner.get('diagnostic_receipt_sha256') == receipt_ref['sha256']
         and owner.get('roles') == {'A': 'usual_screen_owner_confirmed', 'B': 'usual_screen_owner_confirmed'},
         'diagnostic_owner_observation_missing')
    need(review.get('schema') == 'OT-RECOVERY-DIAGNOSTIC-INDEPENDENT-REVIEW-1'
         and review.get('accepted') is True and review.get('diagnostic_receipt_sha256') == receipt_ref['sha256']
         and review.get('owner_observation_sha256') == owner_observation_ref['sha256']
         and review.get('controller_sha256') == receipt['controller_sha256']
         and review.get('source_pins_sha256') == receipt['source_pins']['sha256']
         and review.get('scope') == 'new_diagnostic_evidence_linkage_only', 'diagnostic_review_missing')
    need(Path(destination).parent == Path(receipt_ref['path']).parent, 'diagnostic_output_invalid')
    linkage = {'schema': 'OT-RECOVERY-DIAGNOSTIC-LINKAGE-1', 'status': 'fresh_diagnostic_custody_settled',
        'diagnostic_receipt': receipt_ref, 'owner_observation': owner_observation_ref,
        'independent_review': independent_review_ref, 'automatic_legacy_custody_released': False,
        'candidate_eligible': False, 'historical_byte_equality': 'unknown',
        'next_gate': 'separate_reviewed_operational_gate_decision_and_candidate_authorization'}
    once(destination, canonical(linkage) + b'\n', private)
    return reference(destination, private)


def summarize_nvs_drift(retained_ref, current_ref, private):
    """Offline byte/page counts only; no keys, values or identifying bytes returned."""
    before, after = read_ref(retained_ref, private), read_ref(current_ref, private)
    need(len(before) == len(after) == 12288)
    return {'schema': 'OT-PRIVATE-NVS-DRIFT-COUNTS-1',
        'classification': 'equal' if before == after else 'divergent_not_new_baseline',
        'changed_bytes': sum(a != b for a, b in zip(before, after)),
        'changed_pages': sum(before[n:n + 4096] != after[n:n + 4096] for n in range(0, 12288, 4096)),
        'structural_safety': 'requires_successful_source_bound_release_worker_proof',
        'retained_baseline_changed': False}


def main(argv=None):
    parser = argparse.ArgumentParser(description='Recovery diagnostic; default is inert refusal.')
    parser.add_argument('--inspect', action='store_true')
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--package')
    parser.add_argument('--package-sha256')
    parser.add_argument('--authorization')
    parser.add_argument('--authorization-sha256')
    args = parser.parse_args(argv)
    try:
        need(args.inspect != args.execute and args.package and args.package_sha256,
             'diagnostic_execution_not_authorized')
        if args.inspect:
            inspect(args.package, args.package_sha256)
            answer = {'outcome': 'host_inputs_inspected', 'device_actions': 0, 'candidate_eligible': False}
        else:
            execute(args.package, args.package_sha256, args.authorization,
                    args.authorization_sha256, approved_device_execution=True)
            answer = {'outcome': 'diagnostic_receipt_recorded', 'candidate_eligible': False}
    except BaseException:
        answer = {'outcome': 'refused', 'device_actions': 'unknown_may_have_started' if args.execute else 0,
                  'candidate_eligible': False}
    print(json.dumps(answer, separators=(',', ':')))
    return 1 if answer['outcome'] == 'refused' else 0


if __name__ == '__main__':
    sys.exit(main())
