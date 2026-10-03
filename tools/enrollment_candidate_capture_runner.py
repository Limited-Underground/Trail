"""Isolated original acquisition, private-file handoff and explicit release.

No package, request, profile, identity or grant issuer is provided. The capture
process keeps its OS lease and original ROM custody while a separately reviewed
candidate package is supplied through the designated, initially absent private
handoff file. Waiting and candidate work retain the original fixed ceilings.
"""
import argparse
from dataclasses import dataclass, field
import hashlib
import json
import math
from pathlib import Path
import re
import sys
import time

from enrollment_candidate_controller import Clock, ROLES
from enrollment_candidate_runtime import (Assembly, canonical, decode, descriptor,
    pin_valid, read_pin, regular, verify_assembly)
from enrollment_candidate_rom_adapter import Runtime, HardwareLease, DeviceProfile, identity, opaque_identity
from enrollment_candidate_original_capture import (CaptureSession, CaptureError,
    validate_request, validate_grant, recover_release, ACTIVE)
import enrollment_candidate_runner as candidate


@dataclass(frozen=True)
class PreparedCapture:
    package_path: Path
    package_sha256: str
    operation: str
    evidence_root: Path
    assembly: Assembly
    handoff_path: Path | None
    capture_deadline: float
    cleanup_deadline: float
    request_raw: bytes = field(repr=False)
    grant_raw: bytes = field(repr=False)
    grant_sha256: str = field(repr=False)
    identities: tuple = field(repr=False)
    binding_key: bytes = field(repr=False)
    profiles: tuple = field(repr=False)
    refs_raw: bytes = field(repr=False)

    @property
    def request(self):
        return decode(self.request_raw)


def _new_private_file(path, private):
    path = Path(path)
    candidate.need(path.is_absolute() and path.name and path.parent.is_dir(), 'handoff_invalid')
    regular(path.parent, directory=True)
    candidate.need(path.resolve().is_relative_to(private.resolve()) and not path.exists()
        and not path.is_symlink() and not getattr(path, 'is_junction', lambda: False)(), 'handoff_used')
    return path


def preflight(package_path, package_sha256, *, utc=time.time, monotonic=time.monotonic,
              assembly_verifier=verify_assembly):
    """Inert, nonconsuming package admission with an initially absent handoff."""
    clock = Clock(monotonic)
    start, started_utc = clock.now(), utc()
    candidate.need(type(started_utc) in (int, float) and math.isfinite(started_utc), 'clock_invalid')
    path = regular(package_path)
    raw = path.read_bytes()
    candidate.need(len(raw) <= 32768 and type(package_sha256) is str
        and hashlib.sha256(raw).hexdigest() == package_sha256, 'package_changed')
    package = decode(raw)
    candidate.need(type(package) is dict and set(package) == {'schema', 'operation', 'evidence_root',
        'assembly', 'request', 'grant', 'identities', 'binding_key', 'profiles', 'handoff'}
        and package['schema'] == 'OT-ORIGINAL-CAPTURE-RUNNER-1'
        and package['operation'] in ('capture', 'release'), 'package_invalid')
    assembly_ref = package['assembly']
    candidate.need(type(assembly_ref) is dict and set(assembly_ref) == {'path', 'bytes', 'sha256'}
        and pin_valid({k: assembly_ref[k] for k in ('bytes', 'sha256')}), 'reference_invalid')
    assembly = assembly_verifier(assembly_ref['path'], assembly_ref['sha256'])
    candidate.need(type(assembly) is Assembly, 'assembly_invalid')
    private = regular(assembly.worktree / '.private', directory=True)
    candidate.need(path.resolve().is_relative_to(private.resolve()), 'path_invalid')
    refs = {name: package[name] for name in ('assembly', 'request', 'grant', 'identities', 'binding_key', 'profiles')}
    data = {name: candidate._ref(ref, private) for name, ref in refs.items()}
    evidence_root = regular(package['evidence_root'], directory=True)
    candidate.need(evidence_root.resolve().is_relative_to(private.resolve()) and evidence_root != private,
        'evidence_root_invalid')
    sidecar = decode(data['assembly'])
    base = decode(candidate._ref(sidecar['base'], private))
    candidate.need(not any(evidence_root.resolve().is_relative_to(Path(root).resolve())
        for root in (assembly.root, base['root'])), 'evidence_root_invalid')
    request = validate_request(decode(data['request']))
    grant = validate_grant(request, data['grant'], refs['grant']['sha256'], package['operation'], utc)
    candidate.need(request['runtime_sha256'] == assembly.manifest_sha256, 'runtime_binding_invalid')
    candidate.need(grant['issued_utc'] <= started_utc < grant['capture_expires_utc'], 'grant_expired')
    capture_deadline = start + grant['capture_expires_utc'] - started_utc
    cleanup_deadline = start + grant['cleanup_expires_utc'] - started_utc
    identities = decode(data['identities'])
    candidate.need(type(identities) is dict and set(identities) == {'schema', 'roles'}
        and identities['schema'] == 'OT-CANDIDATE-IDENTITIES-1'
        and type(identities['roles']) is dict and set(identities['roles']) == set(ROLES), 'identity_invalid')
    bound = {r: identity(identities['roles'][r]) for r in ROLES}
    key = data['binding_key']
    candidate.need(len(key) == 32 and bound['A'] != bound['B'], 'identity_invalid')
    profiles = decode(data['profiles'])
    candidate.need(type(profiles) is dict and set(profiles) == {'schema', 'roles'}
        and profiles['schema'] == 'OT-CANDIDATE-PROFILES-1'
        and type(profiles['roles']) is dict and set(profiles['roles']) == set(ROLES), 'profile_invalid')
    checked = {}
    for role in ROLES:
        row = profiles['roles'][role]
        binding = request['roles'][role]['device_binding']
        candidate.need(opaque_identity(key, bound[role]) == binding, 'identity_binding_invalid')
        candidate.need(type(row) is dict and set(row) == {'model', 'device_binding', 'flash_bytes', 'evidence_sha256'}
            and row['model'] == 'heltec_v4_esp32s3' and row['device_binding'] == binding
            and type(row['flash_bytes']) is int and row['flash_bytes'] == 16777216
            and row['evidence_sha256'] == request['roles'][role]['profile_evidence_sha256'], 'profile_invalid')
        checked[role] = DeviceProfile(**row)
    candidate.need(not (evidence_root / ('enrollment-original-used-' + grant['attempt'])).exists(), 'grant_used')
    if package['operation'] == 'capture':
        candidate.need(not (evidence_root / ACTIVE).exists()
            and not (evidence_root / candidate.ACTIVE).exists(), 'custody_held')
        handoff = _new_private_file(package['handoff'], private)
        candidate.need(not any(handoff.resolve().is_relative_to(Path(root).resolve())
            for root in (assembly.root, base['root'])), 'handoff_invalid')
        candidate.need(not any((evidence_root / ('enrollment-original-' + grant['attempt'] + suffix)).exists()
            for suffix in ('.json', '.pending', '-pins.json', '-handoff.json')), 'capture_session_used')
    else:
        candidate.need(package['handoff'] is None, 'handoff_invalid')
        handoff = None
        active = decode(regular(evidence_root / ACTIVE).read_bytes())
        candidate.need(active == {'attempt': grant['origin_attempt'],
            'request_sha256': hashlib.sha256(canonical(request)).hexdigest()}, 'journal_invalid')
    candidate.need(not any((evidence_root / ('enrollment-original-runner-' + grant['attempt'] + suffix)).exists()
        for suffix in ('.json', '.pending', '.unaccepted')), 'runner_attempt_used')
    clock.check(capture_deadline)
    return PreparedCapture(path, package_sha256, package['operation'], evidence_root, assembly, handoff,
        capture_deadline, cleanup_deadline, canonical(request), data['grant'], refs['grant']['sha256'],
        tuple(bound.items()), key, tuple(checked.items()), canonical(refs))


def _recheck(prepared, assembly_verifier):
    private = prepared.assembly.worktree / '.private'
    candidate.need(hashlib.sha256(regular(prepared.package_path).read_bytes()).hexdigest()
        == prepared.package_sha256, 'package_changed')
    for ref in decode(prepared.refs_raw).values():
        candidate._ref(ref, private)
    assembly_verifier(prepared.assembly.assembly_path, prepared.assembly.assembly_sha256)


def _await_handoff(prepared, session, clock, wait):
    private = prepared.assembly.worktree / '.private'
    ceiling = min(prepared.capture_deadline, session.capture_deadline)
    while not prepared.handoff_path.exists():
        clock.check(ceiling)
        remaining = ceiling - clock.now()
        wait(min(.1, remaining))
    clock.check(ceiling)
    raw = regular(prepared.handoff_path).read_bytes()
    candidate.need(len(raw) <= 8192, 'handoff_invalid')
    ref = decode(raw)
    candidate.need(type(ref) is dict and set(ref) == {'path', 'bytes', 'sha256'}
        and pin_valid({k: ref[k] for k in ('bytes', 'sha256')}), 'handoff_invalid')
    candidate._ref(ref, private)
    handoff_pin = descriptor(raw)
    candidate._write_once(prepared.evidence_root / ('enrollment-original-' +
        decode(prepared.grant_raw)['attempt'] + '-handoff.json'), canonical({'schema':
        'OT-ORIGINAL-CAPTURE-HANDOFF-1', 'handoff': handoff_pin, 'candidate_package': ref}) + b'\n')
    def unchanged():
        candidate.need(descriptor(regular(prepared.handoff_path).read_bytes()) == handoff_pin, 'handoff_changed')
        candidate._ref(ref, private)
        return True
    return ref, unchanged


@dataclass(frozen=True)
class CaptureRunnerResult:
    operation: str
    outcome: str
    first_failure: tuple | None
    cleanup_failure: tuple | None
    capture: object
    candidate: object
    lease_released: bool
    candidate_cleanup_failure: tuple | None = None
    capture_cleanup_failure: tuple | None = None
    receipt_path: Path | None = None


def run(package_path, package_sha256, mode, *, utc=time.time, monotonic=time.monotonic,
        runtime_factory=Runtime, lease_factory=HardwareLease, capture_backend_factory=None,
        candidate_backend_factory=candidate.ROMBackend, operator_factory=candidate.CandidateOperator,
        view_factory=candidate._view_factory, assembly_verifier=verify_assembly, wait=time.sleep,
        require_isolated=False, capture_deadline=None, cleanup_deadline=None):
    prepared = preflight(package_path, package_sha256, utc=utc, monotonic=monotonic,
        assembly_verifier=assembly_verifier)
    candidate.need(mode in ('capture', 'release') and mode == prepared.operation, 'mode_invalid')
    if require_isolated:
        verify_assembly(prepared.assembly.assembly_path, prepared.assembly.assembly_sha256, require_isolated=True)
    for supplied in (capture_deadline, cleanup_deadline):
        candidate.need(supplied is None or (type(supplied) in (int, float) and math.isfinite(supplied)), 'deadline_invalid')
    execution = min(prepared.capture_deadline, capture_deadline) if capture_deadline is not None else prepared.capture_deadline
    restoration = min(prepared.cleanup_deadline, cleanup_deadline) if cleanup_deadline is not None else prepared.cleanup_deadline
    candidate.need(execution <= restoration, 'deadline_invalid')
    clock = Clock(monotonic)
    clock.check(execution)
    _recheck(prepared, assembly_verifier)
    private = prepared.assembly.worktree / '.private'
    runtime, lease, session, capture_result, candidate_result = None, None, None, None, None
    first, cleanup, released, stage = None, None, False, 'composition'
    candidate_cleanup = capture_cleanup = None
    try:
        runtime = runtime_factory(prepared.assembly.manifest_path, prepared.assembly.manifest_sha256,
            private, monotonic=monotonic)
        lease = lease_factory(private, monotonic=monotonic)
        kwargs = dict(runtime=runtime, lease=lease, identities=dict(prepared.identities),
            binding_key=prepared.binding_key, profiles=dict(prepared.profiles), utc=utc, monotonic=monotonic,
            backend_factory=capture_backend_factory, capture_deadline=execution, cleanup_deadline=restoration)
        _recheck(prepared, assembly_verifier)
        if mode == 'capture':
            _new_private_file(prepared.handoff_path, private)
            session = CaptureSession(private, prepared.evidence_root, prepared.request, prepared.grant_raw,
                prepared.grant_sha256, **kwargs)
            stage = 'capture'
            capture_result = session.capture()
            candidate.need(capture_result.outcome == 'captured' and capture_result.capture_complete,
                'capture_incomplete')
            stage = 'handoff'
            ref, unchanged = _await_handoff(prepared, session, clock, wait)
            # The externally supplied candidate package remains subject to its
            # normal exact request/grant/identity/profile/runtime admission.
            stage = 'candidate'
            candidate_result = candidate.run(ref['path'], ref['sha256'], 'execute', utc=utc,
                monotonic=monotonic, backend_factory=candidate_backend_factory,
                operator_factory=operator_factory, view_factory=view_factory,
                assembly_verifier=assembly_verifier, require_isolated=require_isolated,
                execute_deadline=min(execution, session.capture_deadline),
                restore_deadline=min(restoration, session.cleanup_deadline),
                capture_session=session, held_runtime=runtime, held_lease=lease, handoff_validator=unchanged)
            first = candidate_result.first_failure
            candidate_cleanup = candidate_result.cleanup_failure
        else:
            stage = 'release'
            capture_result = recover_release(private, prepared.evidence_root, prepared.request,
                prepared.grant_raw, prepared.grant_sha256, **kwargs)
            first = capture_result.first_failure
    except BaseException as error:
        first = first or (stage, candidate._category(error))
    finally:
        if session is not None and session.result.outcome != 'not_started':
            try:
                capture_result = session.release()
            except BaseException as error:
                capture_cleanup = ('capture_cleanup', candidate._category(error))
        if capture_result is not None:
            first = capture_result.first_failure or first
            capture_cleanup = capture_result.cleanup_failure or capture_cleanup
        cleanup = candidate_cleanup or capture_cleanup
        try:
            released = lease is not None and lease.close() is True
        except BaseException:
            released = False
        if not released:
            cleanup = cleanup or ('cleanup', 'handles_not_closed')
    try:
        clock.check(restoration)
    except BaseException as error:
        cleanup = cleanup or ('cleanup', candidate._category(error))
        first = first or cleanup
    held = (capture_result is not None and not capture_result.custody_released) or (
        prepared.evidence_root / ACTIVE).exists() or (
        candidate_result is not None and candidate_result.outcome == 'held')
    success = first is None and cleanup is None and released and capture_result is not None
    outcome = ('held' if held else 'released' if success and mode == 'release' else
        'passed' if success and candidate_result is not None and candidate_result.outcome == 'passed' else 'failed')
    # Persist the composed outcome, including pre-candidate failures which the
    # two custody journals cannot otherwise attribute to the package/handoff.
    attempt = decode(prepared.grant_raw)['attempt']
    receipt_path = prepared.evidence_root / ('enrollment-original-runner-' + attempt + '.json')
    pending = receipt_path.with_suffix('.pending')
    created = set()
    receipt = {'schema': 'OT-ORIGINAL-CAPTURE-RUNNER-RESULT-1', 'operation': mode, 'outcome': outcome,
        'package_sha256': prepared.package_sha256, 'assembly_sha256': prepared.assembly.assembly_sha256,
        'request_sha256': hashlib.sha256(prepared.request_raw).hexdigest(), 'grant_sha256': prepared.grant_sha256,
        'attempt': attempt, 'capture_deadline': execution, 'cleanup_deadline': restoration,
        'first_failure': first, 'cleanup_failure': cleanup, 'candidate_cleanup_failure': candidate_cleanup,
        'capture_cleanup_failure': capture_cleanup, 'lease_released': released,
        'capture_complete': bool(capture_result and capture_result.capture_complete),
        'capture_custody_released': bool(capture_result and capture_result.custody_released),
        'candidate_outcome': candidate_result.outcome if candidate_result else None}
    try:
        clock.check(restoration)
        candidate._write_once(pending, canonical(receipt) + b'\n', on_create=created.add)
        clock.check(restoration)
        candidate.need(not receipt_path.exists(), 'receipt_exists')
        pending.rename(receipt_path)
        created.discard(pending)
        created.add(receipt_path)
        clock.check(restoration)
    except BaseException as error:
        cleanup = cleanup or ('receipt', candidate._category(error))
        first = first or cleanup
        outcome = 'held' if held else 'failed'
        try:
            unaccepted = receipt_path.with_suffix('.unaccepted')
            candidate.need(not unaccepted.exists(), 'receipt_exists')
            if receipt_path in created and receipt_path.exists(): receipt_path.rename(unaccepted)
            elif pending in created and pending.exists(): pending.rename(unaccepted)
            receipt.update(outcome=outcome, first_failure=first, cleanup_failure=cleanup)
            candidate._write_once(receipt_path, canonical(receipt) + b'\n')
        except BaseException:
            receipt_path = None
    return CaptureRunnerResult(mode, outcome, first, cleanup, capture_result, candidate_result, released,
        candidate_cleanup, capture_cleanup, receipt_path)


def main(argv=None):
    class Parser(argparse.ArgumentParser):
        def error(self, message):
            raise candidate.RunnerError('runner_refused')
    parser = Parser(description='Original six-span acquisition and held-ROM handoff; no authority issuer.')
    parser.add_argument('--mode', choices=('capture-preflight', 'capture', 'release'), required=True)
    parser.add_argument('--assembly', required=True)
    parser.add_argument('--assembly-sha256', required=True)
    parser.add_argument('--package')
    parser.add_argument('--package-sha256')
    parser.add_argument('--capture-deadline', type=float)
    parser.add_argument('--cleanup-deadline', type=float)
    mode = 'capture-preflight'
    try:
        args = parser.parse_args(argv)
        mode = args.mode
        assembly = verify_assembly(args.assembly, args.assembly_sha256, require_isolated=True)
        candidate.need((args.package is None) == (args.package_sha256 is None), 'package_invalid')
        candidate.need((args.capture_deadline is None) == (args.cleanup_deadline is None), 'deadline_invalid')
        if args.package is not None:
            prepared = preflight(args.package, args.package_sha256)
            candidate.need(prepared.assembly.assembly_sha256 == assembly.assembly_sha256
                and prepared.assembly.assembly_path == assembly.assembly_path, 'assembly_invalid')
        if mode == 'capture-preflight':
            candidate.need(args.capture_deadline is None, 'deadline_invalid')
            outcome, category = 'ready', None
        else:
            candidate.need(args.package is not None, 'package_invalid')
            result = run(args.package, args.package_sha256, mode, require_isolated=True,
                capture_deadline=args.capture_deadline, cleanup_deadline=args.cleanup_deadline)
            outcome = result.outcome
            fault = result.first_failure or result.cleanup_failure
            category = fault[1] if fault else None
    except SystemExit:
        raise
    except BaseException as error:
        outcome, category = 'failed', candidate._category(error)
    print(json.dumps({'schema': 'OT-ORIGINAL-CAPTURE-LAUNCH-1', 'mode': mode,
        'outcome': outcome, 'category': category}, separators=(',', ':')))
    return 0 if outcome in ('ready', 'passed', 'released') else 1


if __name__ == '__main__':
    sys.exit(main())
