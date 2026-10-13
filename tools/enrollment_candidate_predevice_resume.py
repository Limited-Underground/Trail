"""Reviewed pre-device readiness succession; historical UI evidence stays immutable.

No grant, USB, transport or recovery capability lives here. A fresh real chat
approval precedes one visible current-readability/ready checkpoint. Creation is
one-shot and requires both predecessor and successor to be entirely pre-device.
"""
import datetime
import hashlib
import os
from pathlib import Path
import sys
import time

import enrollment_candidate_runtime as runtime

CHANGED_EXTERNAL = frozenset(('enrollment_candidate_interactive_launch.py',
    'enrollment_candidate_visible_session.py', 'enrollment_candidate_owner_workflow.py'))
NAME = 'enrollment_candidate_predevice_resume.py'
MANIFEST_SCHEMA = 'OT-PREDEVICE-SUCCESSION-1'
TRANSITION_MANIFEST_SCHEMA = 'OT-PREDEVICE-SUCCESSION-2'
TRANSITION_SCHEMA = 'OT-PREDEVICE-RUNTIME-TRANSITION-1'
TRANSITION_PINS = {
    'enrollment_candidate_runner.py': {
        'before': dict(bytes=59025, sha256='da5da752db7d2f0febe79a0a60a6ed91a4998da51804df0b5f08d975943cf293'),
        'after': dict(bytes=61076, sha256='56bbf37b2fd9a848ad8120a196d8be9cc03ab05895af90e5acf249f3a20079f0')},
    'enrollment_candidate_rom_adapter.py': {
        'before': dict(bytes=65316, sha256='deac4455d43cb0e724fb6b9423959fef548d35bc966afe023aebc3f5805ec271'),
        'after': dict(bytes=65336, sha256='57e85e3c81d06acd46c4e0f6a29fb915a716df79db5402ef77409e9802360b0d')}}
TRANSITION_REVIEW_PIN = dict(bytes=6915, sha256='216ab3c0bc8dd2e080388e5186e5ecbc9e18deb9d6dd4c7734c89e2f8ec93275')
CLEANUP_TRANSITION_KIND = 'ot0238c-capture-mismatch-cleanup'
CLEANUP_TRANSITION_PINS = {
    'enrollment_candidate_runner.py': {
        'before': dict(bytes=59025, sha256='da5da752db7d2f0febe79a0a60a6ed91a4998da51804df0b5f08d975943cf293'),
        'after': dict(bytes=61076, sha256='56bbf37b2fd9a848ad8120a196d8be9cc03ab05895af90e5acf249f3a20079f0')},
    'enrollment_candidate_rom_adapter.py': {
        'before': dict(bytes=65316, sha256='deac4455d43cb0e724fb6b9423959fef548d35bc966afe023aebc3f5805ec271'),
        'after': dict(bytes=69318, sha256='5a340c902e51afa3e3024712c0517f3a1d3ff291aa542d7ed37dcc33b0fda391')},
    'enrollment_candidate_original_capture.py': {
        'before': dict(bytes=51062, sha256='2a0f4d9a53b0d726aa57173352dd8bc6b0b2ef859eeb1ee47fdcbb53bca152fd'),
        'after': dict(bytes=55424, sha256='e614b8ac1c02512e90aa60bf1b3efbaa4e935b12bbd6815e85962860eb861c27')},
    'enrollment_candidate_capture_runner.py': {
        'before': dict(bytes=21441, sha256='deb972b8eb10ee8a9733a9b2c201b23e164dce46d2e4bc96339f9a620f2059be'),
        'after': dict(bytes=22439, sha256='f92cb5929c9994f5e258252179db60d8f3baafefdc9653d57f93c31c14f0d150')}}
CLEANUP_EVIDENCE_PINS = {
    'independent-source-review.json': dict(bytes=1068,
        sha256='16fb0ff81046fc250ad87c8674b9fed54024a38b6ece1326508d9a74882b9dfe'),
    'source-changes.json': dict(bytes=1533,
        sha256='dea36d14db8aad4a80c432e7d0e5553e2012a6a02d0770d5cecdfbed31fd709f'),
    'child-validation.json': dict(bytes=3032,
        sha256='6ad115709503a45ea38fe7b451cb95670d204fe98101dcaff67732f0b0e72cf7')}
RECOVERY_GATE_SCHEMA = 'OT0238C-LAUNCHER-RECOVERY-GATE-BINDINGS-1'
RECOVERY_GATE_PINS = {
    'module': dict(bytes=19104,
        sha256='98c8a760f186872d14e55ce3da6037dfbd1dd1298491d3021293f6e0693791bf'),
    'disposition': dict(bytes=11745,
        sha256='3460aea3cc43b2f8fdca083dc08dbb08372f40737ce9859522ca747f2c561915')}
SETTINGS_TRANSITION_KIND = 'ot0238c-retained-firmware-current-settings'
SETTINGS_NAME = 'enrollment_candidate_settings_successor.py'
FULL_TRANSITION_KIND = 'ot0238c-reviewed-capture-core-full-successor'
FULL_STAGE = 'ot0238c-full-successor-admission-20261012'
REVOKE_TRANSITION_KIND = 'ot0238e-reviewed-revoke-successor'
REVOKE_STAGE = 'ot0238e-revoke-admission-20261013'
SETTINGS_AFTER_PINS = {'enrollment_candidate_rom_adapter.py': {'bytes': 70957, 'sha256': 'cc207e17bea17355fd9228cd221235c2a7ce5d777d0cbafde6134df8e98c8f4b'}, 'enrollment_candidate_original_capture.py': {'bytes': 56311, 'sha256': '866192e7dd6ebf95e47d5dc519ff9a475bc74ebd50f57a0a7e48bfa0e23f04e1'}, 'enrollment_candidate_capture_runner.py': {'bytes': 23027, 'sha256': 'fbaf3d0fc820210fbac6371cdf7c2ebea1bdb8744738612bfba0480785abbd02'}}
# Reviewed capture-runner bytes require a protected route; they do not select
# the full mode. Runtime is shared by historical routes and is enforced only
# by the explicit full profile/cumulative transition, not by route detection.
REVIEWED_CORE_AFTER_PINS = {
    'enrollment_candidate_capture_runner.py': dict(bytes=23998,
        sha256='6a86517612a6171df25d921639ce796ec947a02674ec6ae8f38249eaec154f3f')}
READINESS_SCHEMA = 'OT-PREDEVICE-READINESS-1'
INTENT_SCHEMA = 'OT-PREDEVICE-APPROVAL-INTENT-1'


def api():
    import enrollment_candidate_interactive_launch as visible
    return visible


def record(reference, private=None):
    return runtime.decode(api().read_reference(reference, private=private))


def recovery_gate(transition):
    """Validate the fixed private gate closure without granting entry."""
    visible = api()
    private = visible.ROOT / '.private'
    root = private / 'ot0238c-launcher-recovery-20261011' / 'recovery'
    expected = dict(schema=RECOVERY_GATE_SCHEMA, expected_private_root=str(private),
        module=dict(path=str(root / 'host-recovery-gate.py'), **RECOVERY_GATE_PINS['module']),
        disposition=dict(path=str(root / 'manual-recovery-observed-baseline-unresolved.json'),
            **RECOVERY_GATE_PINS['disposition']))
    try:
        visible.need(transition.get('recovery_gate') == expected, 'recovery_gate_invalid')
        raw = visible.read_reference(expected['module'], private=private)
        visible.read_reference(expected['disposition'], private=private)
    except Exception:
        raise visible.AdmissionError('recovery_gate_invalid') from None
    return expected, raw


def require_settings_gate(value, transition):
    """Load only binding-pinned host policy in an unregistered namespace."""
    visible = api()
    private = visible.ROOT / '.private'
    try:
        path = visible.ROOT / 'tools' / SETTINGS_NAME
        expected = dict(path=str(path), **value['sources'][SETTINGS_NAME])
        visible.need(transition.get('settings_gate') == expected, 'recovery_gate_invalid')
        raw = visible.read_reference(expected)
        scope = {'__name__': '_ot_bound_settings_successor', '__file__': str(path)}
        exec(compile(raw, str(path), 'exec'), scope)
        guard = scope.get('validate_settings_successor')
        visible.need(callable(guard) and guard(value, transition, private) is True,
            'recovery_gate_invalid')
        return True
    except Exception:
        raise visible.AdmissionError('recovery_gate_invalid') from None


def require_recovery_gate(value):
    """Refuse unresolved cleanup entry before SDK, UI, markers or authority.

    Old bindings and transitions retain their existing admission path. The
    exact v18 source pins also require this kind, preventing removal/renaming
    of the transition from hiding its recovery hold.
    """
    visible = api()
    sources = value.get('sources')
    cleanup_sources = type(sources) is dict and any(sources.get(name) == pair['after']
        for name, pair in CLEANUP_TRANSITION_PINS.items() if name != 'enrollment_candidate_runner.py')
    settings_sources = type(sources) is dict and (SETTINGS_NAME in sources or any(sources.get(name) == pin
        for name, pin in SETTINGS_AFTER_PINS.items()) or any(
            sources.get(name) == pin for name, pin in REVIEWED_CORE_AFTER_PINS.items()))
    settings_parent = Path(value.get('session', {}).get('path', '')).parent == (
        visible.ROOT / '.private/ot0238c-settings-check-20261011/prestage-v2')
    full_parent = Path(value.get('session', {}).get('path', '')).parent in (
        visible.ROOT / '.private' / FULL_STAGE / 'prestage',
        visible.ROOT / '.private' / REVOKE_STAGE / 'prestage')
    cleanup_parent = Path(value.get('session', {}).get('path', '')).parent == (
        visible.ROOT / '.private/ot0238c-launcher-recovery-20261011/prestage')
    if value.get('schema') != 'OT-VISIBLE-SESSION-BINDING-2':
        visible.need(not cleanup_sources and not cleanup_parent and not settings_sources and not settings_parent and not full_parent, 'recovery_gate_invalid')
        return
    visible.need('predevice' in value, 'recovery_gate_invalid')
    item = record(value['predevice'], visible.ROOT / '.private')
    transition = item.get('runtime_transition') if type(item) is dict else None
    cleanup_kind = type(transition) is dict and transition.get('kind') == CLEANUP_TRANSITION_KIND
    settings_kind = type(transition) is dict and transition.get('kind') == SETTINGS_TRANSITION_KIND
    full_kind = type(transition) is dict and transition.get('kind') in (FULL_TRANSITION_KIND, REVOKE_TRANSITION_KIND)
    if full_kind or full_parent:
        visible.need(type(item) is dict and item.get('schema') == TRANSITION_MANIFEST_SCHEMA
            and full_kind and full_parent, 'recovery_gate_invalid')
        result = require_settings_gate(value, transition)
        from enrollment_candidate_settings_successor import full_successor_profile
        profile, _, _, _ = full_successor_profile(visible.ROOT / '.private', transition['kind'])
        previous = item.get('predecessor')
        visible.need(type(previous) is dict and previous.get('binding') == profile['predecessor'], 'recovery_gate_invalid')
        old = record(profile['predecessor'], visible.ROOT / '.private')
        visible.need(old.get('schema') == 'OT-VISIBLE-SESSION-BINDING-1', 'recovery_gate_invalid')
        # Inventory-only historical pre-device custody is checked before the
        # owner workflow creates its first current marker; no process/native call.
        predecessor_idle(item, old, process_checker=lambda previous: True)
        return result
    if settings_kind or settings_parent or settings_sources:
        visible.need(type(item) is dict and item.get('schema') == TRANSITION_MANIFEST_SCHEMA
            and settings_kind and settings_parent, 'recovery_gate_invalid')
        return require_settings_gate(value, transition)
    if not cleanup_sources and not cleanup_kind and not cleanup_parent:
        return
    visible.need(type(item) is dict and item.get('schema') == TRANSITION_MANIFEST_SCHEMA
        and cleanup_kind, 'recovery_gate_invalid')
    gate, raw = recovery_gate(transition)
    scope = {'__name__': '_ot_fixed_recovery_gate', '__file__': gate['module']['path']}
    try:
        exec(compile(raw, gate['module']['path'], 'exec'), scope)
        guard = scope.get('require_candidate_entry_allowed')
        visible.need(callable(guard), 'recovery_gate_invalid')
        guard(gate['disposition']['path'], gate['disposition']['sha256'], gate['expected_private_root'])
    except Exception as error:
        category = ('recovery_baseline_unresolved' if type(error) is scope.get('RecoveryDispositionError')
            and str(error) == 'recovery_baseline_unresolved' else 'recovery_gate_invalid')
        raise visible.AdmissionError(category) from None
    # This disposition cannot authorize a future state, even if a guard were
    # accidentally changed to return a truthy value during a later correction.
    raise visible.AdmissionError('recovery_gate_invalid')


def runtime_transition(item, old, value):
    """Compare pinned metadata only; current admission still verifies the SDK.

    Historical assemblies cannot verify against today's changed sources.
    Their immutable binding and archives anchor the comparison instead. Never
    inventory the historical SDK or import its modules in the readiness loop.
    """
    visible = api()
    need = visible.need
    private = visible.ROOT / '.private'
    transition = item['runtime_transition']
    keys = {'schema', 'kind', 'review', 'predecessor_assembly', 'successor_assembly',
        'predecessor_runtime', 'successor_runtime', 'sources'}
    need(type(transition) is dict, 'resume_transition_invalid')
    cleanup = transition.get('kind') == CLEANUP_TRANSITION_KIND
    revoke = transition.get('kind') == REVOKE_TRANSITION_KIND
    full = revoke or transition.get('kind') == FULL_TRANSITION_KIND
    settings = full or transition.get('kind') == SETTINGS_TRANSITION_KIND
    need(set(transition) == (keys | {'settings_gate', 'settings_successor', 'admission_profile', 'core_review', 'fixed_inputs'} if revoke else
            keys | {'settings_gate', 'settings_successor', 'admission_profile', 'core_review'} if full else
            keys | {'settings_gate', 'settings_successor'} if settings else
            keys | {'cleanup_review', 'recovery_gate'} if cleanup else keys)
        and transition['schema'] == TRANSITION_SCHEMA
        and transition['kind'] in ('ot0238c-restart-diagnostic', CLEANUP_TRANSITION_KIND, SETTINGS_TRANSITION_KIND, FULL_TRANSITION_KIND, REVOKE_TRANSITION_KIND),
        'resume_transition_invalid')
    expected_pins = (transition['sources'] if settings else
        CLEANUP_TRANSITION_PINS if cleanup else TRANSITION_PINS)
    if settings:
        require_settings_gate(value, transition)
        need(set(expected_pins) == set(TRANSITION_PINS) | {'enrollment_candidate_original_capture.py',
            'enrollment_candidate_capture_runner.py'} | ({'enrollment_candidate_runtime.py'} if full else set()), 'resume_transition_changed')
    need(transition['sources'] == expected_pins
        and all(runtime.pin_valid(pin) for pair in transition['sources'].values() for pin in pair.values()),
        'resume_transition_changed')
    review_ref = transition['review']
    review = record(review_ref, private)
    need(settings or (review_ref == dict(path=str(private / 'ot0238c-retained-diagnosis-20261010' / 'independent-review.json'),
        **TRANSITION_REVIEW_PIN)
        and review.get('schema') == 'OT0238C-INDEPENDENT-RESTART-DIAGNOSTIC-REVIEW-1'
        and review.get('outcome') == 'no_unresolved_findings' and review.get('findings') == []), 'review_required')
    if cleanup:
        # These exact reviewed bytes confer only the fixed host-source delta.
        # The successor manifest/session reviews and current admission remain
        # independent gates; review prose or a caller approval flag is authority
        # for none of them.
        evidence_root = private / 'ot0238c-capture-mismatch-20261011'
        refs = {name: dict(path=str(evidence_root / name), **pin)
            for name, pin in CLEANUP_EVIDENCE_PINS.items()}
        need(transition['cleanup_review'] == refs['independent-source-review.json'], 'review_required')
        cleanup_review = record(transition['cleanup_review'], private)
        source_ref, validation_ref = refs['source-changes.json'], refs['child-validation.json']
        need(cleanup_review.get('schema') == 'OT-HOST-INDEPENDENT-REVIEW-1'
            and cleanup_review.get('outcome') == 'approved_host_source_only'
            and cleanup_review.get('remaining_findings') == []
            and cleanup_review.get('source_manifest') == {k:source_ref[k] for k in ('path', 'sha256')}
            and cleanup_review.get('validation') == dict(
                {k:validation_ref[k] for k in ('path', 'sha256')},
                tests=137, all_four_isolated_exit_codes=0), 'review_required')
        record(source_ref, private)
        record(validation_ref, private)
        recovery_gate(transition)
    assemblies = []
    manifests = []
    for side, binding, which in (('predecessor', old, 'before'), ('successor', value, 'after')):
        need(transition[side + '_assembly'] == binding['assembly'], 'resume_transition_changed')
        assembly = record(transition[side + '_assembly'], private)
        need(type(assembly) is dict and set(assembly) == {'schema', 'root', 'source_root', 'base',
            'source_pins', 'sources', 'runtime'} and assembly['schema'] == 'OT-CANDIDATE-ASSEMBLY-1'
            and assembly['source_root'] == value['worktree']
            and transition[side + '_runtime'] == assembly['runtime'], 'resume_transition_invalid')
        root = runtime.regular(assembly['root'], directory=True)
        need(root.resolve().is_relative_to(private.resolve()) and root.resolve() != private.resolve(),
            'resume_transition_invalid')
        base = record(assembly['base'], private)
        pins = record(assembly['source_pins'], private)
        sources = assembly['sources']
        need(type(pins) is dict and set(pins) == {'schema', 'files'}
            and pins['schema'] == 'OT-CANDIDATE-SOURCE-PINS-1'
            and type(sources) is dict and pins['files'] == sources
            and set(sources) == {'tools/' + name for name in runtime.SOURCE_NAMES}
            and all(runtime.pin_valid(pin) for pin in sources.values()), 'resume_transition_invalid')
        need(all(sources['tools/' + name] == binding['sources'][name] for name in runtime.SOURCE_NAMES),
            'resume_sources_changed')
        # Frozen actual manifests exceed the generic control-record bound.
        manifest = runtime.decode(visible.read_reference(assembly['runtime'], private=private))
        files = dict(base['files'])
        for name, pin in sources.items():
            target = 'policy/' + Path(name).name
            need(target not in files or files[target] == pin, 'resume_transition_invalid')
            files[target] = pin
        need(manifest == dict(base, root=assembly['root'], files=files)
            and base['worktree'] == value['worktree'], 'resume_transition_changed')
        for name, pair in expected_pins.items():
            need(sources['tools/' + name] == pair[which]
                and manifest['files']['policy/' + name] == pair[which], 'resume_transition_changed')
        assemblies.append(assembly)
        manifests.append(manifest)
    before, after = assemblies
    need(before['base'] == after['base'] and before['root'] != after['root']
        and {k:v for k,v in before.items() if k not in ('root', 'runtime', 'source_pins', 'sources')}
        == {k:v for k,v in after.items() if k not in ('root', 'runtime', 'source_pins', 'sources')},
        'resume_transition_changed')
    changed_sources = {'tools/' + name for name in expected_pins}
    need({k:v for k,v in before['sources'].items() if k not in changed_sources}
        == {k:v for k,v in after['sources'].items() if k not in changed_sources}, 'resume_sources_changed')
    before, after = manifests
    changed_files = {'policy/' + name for name in expected_pins}
    need(set(before) == set(after) and set(before['files']) == set(after['files'])
        and {k:v for k,v in before.items() if k not in ('root', 'files')}
        == {k:v for k,v in after.items() if k not in ('root', 'files')}
        and {k:v for k,v in before['files'].items() if k not in changed_files}
        == {k:v for k,v in after['files'].items() if k not in changed_files}, 'resume_transition_changed')


def manifest(value):
    visible = api()
    need = visible.need
    need(value['schema'] == 'OT-VISIBLE-SESSION-BINDING-2', 'resume_invalid')
    item = record(value['predevice'], visible.ROOT / '.private')
    keys = {'schema', 'predecessor', 'successor', 'review'}
    need(type(item) is dict and ((set(item) == keys and item.get('schema') == MANIFEST_SCHEMA)
        or (set(item) == keys | {'runtime_transition'} and item.get('schema') == TRANSITION_MANIFEST_SCHEMA)),
        'resume_invalid')
    transition = item['schema'] == TRANSITION_MANIFEST_SCHEMA
    successor = item['successor']
    need(type(successor) is dict and set(successor) == {'session_nonce', 'readiness_root', 'session_output',
        'session', 'assembly', 'sources', 'fixed_inputs', 'procedure', 'static_review'}, 'resume_invalid')
    for name in ('session_nonce', 'readiness_root', 'session_output', 'session', 'assembly', 'sources'):
        need(successor[name] == value[name], 'resume_changed')
    for name in ('fixed_inputs', 'procedure', 'static_review'):
        visible.read_reference(successor[name], private=visible.ROOT / '.private')
    review = record(item['review'], visible.ROOT / '.private')
    need(type(review) is dict and set(review) == {'schema', 'manifest_core_sha256', 'outcome', 'findings',
        'hardware_access', 'live_grants_created'} and review['schema'] == 'OT-PREDEVICE-SUCCESSION-REVIEW-1'
        and review['manifest_core_sha256'] == runtime.descriptor(runtime.canonical({k:v for k,v in item.items() if k != 'review'}))['sha256']
        and review['outcome'] == 'no_unresolved_findings' and review['findings'] == []
        and review['hardware_access'] is False and review['live_grants_created'] is False, 'review_required')
    previous = item['predecessor']
    need(type(previous) is dict and set(previous) == {'binding', 'plan', 'archives', 'readiness', 'proof',
        'owner', 'practice', 'inventory', 'observation', 'terminal_outcome'}, 'resume_invalid')
    need(previous['terminal_outcome'] in ('UNKNOWN', 'preauth_wait_expired'), 'resume_terminal_invalid')
    record(previous['observation'], visible.ROOT / '.private')
    old = record(previous['binding'], visible.ROOT / '.private')
    need(old.get('schema') == 'OT-VISIBLE-SESSION-BINDING-1' and old['worktree'] == value['worktree']
        and old['host'] == value['host'] and (transition or old['assembly'] == value['assembly'])
        and old['session_nonce'] != value['session_nonce'], 'resume_changed')
    visible.read_reference(old['session'], private=visible.ROOT / '.private')
    old_review = record(old['review'], visible.ROOT / '.private')
    need(old_review == dict(schema='OT-VISIBLE-SESSION-REVIEW-1',
        binding_core_sha256=runtime.descriptor(runtime.canonical(visible.binding_core(old)))['sha256'],
        outcome='no_unresolved_findings',findings=[],hardware_access=False,live_grants_created=False), 'review_required')
    need(set(old['sources']) == set(visible.SOURCE_NAMES)
        and set(previous['archives']) == set(old['sources']), 'resume_sources_invalid')
    if transition:
        runtime_transition(item, old, value)
    full = transition and item['runtime_transition']['kind'] in (FULL_TRANSITION_KIND, REVOKE_TRANSITION_KIND)
    settings = full or transition and item['runtime_transition']['kind'] == SETTINGS_TRANSITION_KIND
    if full:
        from enrollment_candidate_settings_successor import full_successor_profile
        profile, _, _, _ = full_successor_profile(visible.ROOT / '.private', item['runtime_transition']['kind'])
        need(previous['binding'] == profile['predecessor'], 'resume_changed')
    transition_pins = (item['runtime_transition']['sources'] if settings else CLEANUP_TRANSITION_PINS if transition
        and item['runtime_transition']['kind'] == CLEANUP_TRANSITION_KIND else TRANSITION_PINS)
    for name, pin in old['sources'].items():
        archived = previous['archives'][name]
        need({k:archived[k] for k in ('bytes','sha256')} == pin, 'resume_sources_changed')
        visible.read_reference(archived, private=visible.ROOT / '.private')
        if name not in CHANGED_EXTERNAL and not (transition and name in transition_pins):
            need(value['sources'][name] == pin, 'resume_sources_changed')
    # UI and other core sources stay identical; v2 admits only its fixed delta.
    need(set(value['sources']) == set(old['sources']) | {NAME} | ({SETTINGS_NAME} if settings else set()), 'resume_sources_invalid')
    old_plan = record(previous['plan'], visible.ROOT / '.private')
    old_plan_review = record(old_plan['review'], visible.ROOT / '.private')
    need(old_plan['binding'] == previous['binding'] and old_plan['session_nonce'] == old['session_nonce']
        and old_plan_review == dict(schema='OT-OWNER-WORKFLOW-REVIEW-1',
        plan_core_sha256=runtime.descriptor(runtime.canonical({k:v for k,v in old_plan.items() if k!='review'}))['sha256'],
        outcome='no_unresolved_findings',findings=[],hardware_access=False,live_grants_created=False), 'review_required')
    root = Path(old['readiness_root'])
    need(previous['readiness'] == visible.reference(root/'readiness.json')
        and previous['proof'] == visible.reference(root/'neutral-proof.json')
        and previous['practice'] == visible.reference(root/'practice.json'), 'resume_evidence_changed')
    readiness = record(previous['readiness'], root)
    need(readiness['binding'] == previous['binding'] and readiness['proof'] == previous['proof']
        and readiness['owner'] == previous['owner'], 'resume_evidence_changed')
    return item, old


def predecessor_idle(item, old, *, process_checker):
    visible = api()
    root = runtime.regular(old['session_output'], directory=True)
    inventory = item['predecessor']['inventory']
    permitted = {'owner-workflow-start.json', 'owner-workflow-predevice-result.json'}
    visible.need(type(inventory) is dict and set(inventory) <= permitted
        and 'owner-workflow-start.json' in inventory, 'resume_state_invalid')
    visible.need({p.name for p in root.iterdir()} == set(inventory), 'resume_device_state_present')
    for name, ref in inventory.items():
        visible.need(ref == visible.reference(root/name), 'resume_evidence_changed')
    start = record(inventory['owner-workflow-start.json'], root)
    visible.need(start == dict(schema='OT-OWNER-WORKFLOW-START-1',session_nonce=old['session_nonce'],
        binding=item['predecessor']['binding']), 'resume_state_invalid')
    if item['predecessor']['terminal_outcome'] == 'preauth_wait_expired':
        terminal = record(inventory['owner-workflow-predevice-result.json'],root)
        visible.need(terminal.get('schema') == 'OT-OWNER-PREDEVICE-RESULT-1'
            and terminal.get('outcome') == 'wait_expired' and terminal.get('phase') == 'waiting_trial_authorization'
            and terminal.get('binding') == item['predecessor']['binding']
            and terminal.get('device_attempt_started') is False, 'resume_terminal_invalid')
    visible.need(process_checker(item['predecessor']) is True, 'predecessor_process_unresolved')


def no_predecessor_process(previous):
    """Read-only Windows process inspection; uncertain Python commandlines refuse.

    Never launched by host tests. No process token change, launch or termination.
    """
    visible = api()
    visible.need(os.name == 'nt', 'predecessor_process_unresolved')
    import ctypes as c
    from ctypes import wintypes as w
    class Entry(c.Structure):
        _fields_=[('size',w.DWORD),('usage',w.DWORD),('pid',w.DWORD),('heap',c.c_size_t),
            ('module',w.DWORD),('threads',w.DWORD),('parent',w.DWORD),('priority',w.LONG),
            ('flags',w.DWORD),('exe',w.WCHAR*260)]
    class Unicode(c.Structure):
        _fields_=[('length',w.USHORT),('maximum',w.USHORT),('buffer',c.c_void_p)]
    kernel=c.WinDLL('kernel32',use_last_error=True)
    native=c.WinDLL('ntdll')
    kernel.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];kernel.CreateToolhelp32Snapshot.restype=w.HANDLE
    kernel.Process32FirstW.argtypes=kernel.Process32NextW.argtypes=[w.HANDLE,c.POINTER(Entry)]
    kernel.Process32FirstW.restype=kernel.Process32NextW.restype=w.BOOL
    kernel.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];kernel.OpenProcess.restype=w.HANDLE
    kernel.CloseHandle.argtypes=[w.HANDLE];kernel.CloseHandle.restype=w.BOOL
    native.NtQueryInformationProcess.argtypes=[w.HANDLE,c.c_int,c.c_void_p,w.ULONG,c.POINTER(w.ULONG)]
    native.NtQueryInformationProcess.restype=w.LONG
    snapshot=kernel.CreateToolhelp32Snapshot(2,0)
    visible.need(snapshot and snapshot != c.c_void_p(-1).value,'predecessor_process_unresolved')
    needles=[str(previous[k]['path']).replace('/','\\').casefold() for k in ('binding','plan')]
    try:
        entry=Entry();entry.size=c.sizeof(Entry)
        found=kernel.Process32FirstW(snapshot,c.byref(entry))
        visible.need(found,'predecessor_process_unresolved')
        while found:
            if entry.exe.casefold() in ('python.exe','pythonw.exe'):
                handle=kernel.OpenProcess(0x1000,False,entry.pid)
                visible.need(handle,'predecessor_process_unresolved')
                try:
                    required=w.ULONG()
                    native.NtQueryInformationProcess(handle,60,None,0,c.byref(required))
                    visible.need(c.sizeof(Unicode) <= required.value <= 65536,'predecessor_process_unresolved')
                    buf=c.create_string_buffer(required.value)
                    status=native.NtQueryInformationProcess(handle,60,buf,len(buf),c.byref(required))
                    visible.need(status == 0,'predecessor_process_unresolved')
                    value=Unicode.from_buffer(buf)
                    base=c.addressof(buf)
                    visible.need(value.length % 2 == 0 and value.buffer is not None
                        and base <= value.buffer <= base+len(buf)-value.length,'predecessor_process_unresolved')
                    command=c.wstring_at(value.buffer,value.length//2).replace('/','\\').casefold()
                    visible.need(not any(needle in command for needle in needles),'predecessor_process_active')
                finally:
                    visible.need(kernel.CloseHandle(handle),'predecessor_process_unresolved')
            found=kernel.Process32NextW(snapshot,c.byref(entry))
        visible.need(c.get_last_error() in (0,18),'predecessor_process_unresolved')
        return True
    finally:
        visible.need(kernel.CloseHandle(snapshot),'predecessor_process_unresolved')


def history(value, bound, admission, monotonic, utc):
    """Validate immutable historical evidence; this confers no current authority.

    The old practice rechecked readiness at its completion. Its pinned timestamp
    supplies that historical validation context, never today's logon or clock.
    Current native admission and readiness remain separate in revalidate/require.
    """
    visible=api()
    item,old=manifest(value)
    previous=item['predecessor']
    root=Path(old['readiness_root'])
    proof=record(previous['proof'],root)
    owner=record(previous['owner'],root)
    practice=visible.require_practice(old,previous['binding']['sha256'])
    visible.need(practice == record(previous['practice'],root),'resume_evidence_changed')
    stamp=practice['finished_utc']
    visible.need(all(visible.finite(t) for t in
        (stamp,proof['finished_utc'],proof['finished_monotonic'],owner['received_utc']))
        and proof['finished_utc'] <= owner['received_utc'] <= stamp,'resume_evidence_changed')
    historical_monotonic=proof['finished_monotonic']+stamp-proof['finished_utc']
    got=visible._require_ready(previous['binding']['path'],previous['binding']['sha256'],
        admission=lambda:proof['scope'],monotonic=lambda:historical_monotonic,utc=lambda:stamp,
        binding_admitter=lambda p,h:(old,previous['binding']),fresh=True)
    visible.need(got == old,'resume_evidence_changed')
    return item,old


def selection(item):
    """Resolve only the independently reviewed first/retained/revoke selection."""
    visible = api()
    fixed = record(item['successor']['fixed_inputs'], visible.ROOT / '.private')
    visible.need(type(fixed) is dict and type(fixed.get('case')) is str
        and fixed['case'] in ('first', 'retained_rekey', 'revoke')
        and type(fixed.get('group')) is int and fixed['group'] == 1,
        'owner_authorization_stale')
    return fixed['case'], fixed['group']


def intent(value, bound, item, utc, *, fresh=True):
    visible=api()
    case, group = selection(item)
    ref=visible.reference(Path(value['session_output'])/'owner-approval-intent.json')
    reply=record(ref,Path(value['session_output']))
    keys={'schema','approved','source','received_utc','binding','case','group','execute_seconds','total_seconds','roles_ready'}
    visible.need(type(reply) is dict and set(reply)==keys and reply['schema']==INTENT_SCHEMA
        and reply['approved'] is True and reply['source']=='fresh_human_chat_reply'
        and reply['binding']==bound and reply['case']==case and type(reply['group']) is int and reply['group']==group
        and type(reply['execute_seconds']) is int and reply['execute_seconds']==2100
        and type(reply['total_seconds']) is int and reply['total_seconds']==3600
        and type(reply['roles_ready']) is dict and set(reply['roles_ready'])=={'A','B'}
        and all(reply['roles_ready'][role] is True for role in ('A','B'))
        and visible.finite(reply['received_utc']) and 0 <= utc()-reply['received_utc']
        and (not fresh or utc()-reply['received_utc'] < visible.MAX_AGE),'owner_authorization_stale')
    return ref,reply


def creation_idle(value,bound,item,old,process_checker):
    visible=api()
    predecessor_idle(item,old,process_checker=process_checker)
    root=runtime.regular(value['session_output'],directory=True)
    visible.need({p.name for p in root.iterdir()}=={'owner-workflow-start.json','owner-approval-intent.json'},'resume_device_state_present')
    visible.need(record(visible.reference(root/'owner-workflow-start.json'),root)==dict(
        schema='OT-OWNER-WORKFLOW-START-1',session_nonce=value['session_nonce'],binding=bound),'resume_state_invalid')
    visible.need(not any(Path(value['readiness_root']).iterdir()),'resume_revalidation_used')


def revalidate(binding_path,binding_sha256,*,admission=None,process_checker=no_predecessor_process,
               monotonic=time.monotonic,utc=time.time,sleep=time.sleep,backend_factory=None,binding_admitter=None):
    visible=api()
    admission=visible.native_admission if admission is None else admission
    binding_admitter=visible.admit_binding if binding_admitter is None else binding_admitter
    value,bound=binding_admitter(binding_path,binding_sha256)
    item,old=history(value,bound,admission,monotonic,utc)
    approval_ref,approval=intent(value,bound,item,utc)
    creation_idle(value,bound,item,old,process_checker)
    scope=visible._valid_scope(admission())
    from enrollment_candidate_controller import Clock
    from enrollment_candidate_private_view import _NeutralBackend
    backend=(_NeutralBackend if backend_factory is None else backend_factory)()
    clock=Clock(monotonic)
    start,started_utc=clock.now(),utc()
    deadline=start+visible.NEUTRAL_STEP_SECONDS
    text='\r\n'.join(('TEST ONLY - no device action','Checkpoint: CURRENT READABILITY / READY NOW','',
        '1. Confirm these labels are comfortably readable now.',
        '2. Confirm you are ready for this one approved test.',
        '3. Click Continue; enrollment instructions follow.',
        'Cancel or close if you are not ready.'))
    cleared=False
    try:
        visible.need(backend.open(text) is True,'neutral_view_refused')
        while True:
            visible.need(visible._valid_scope(admission())==scope,'desktop_changed')
            clock.check(deadline)
            event=backend.poll()
            clock.check(deadline)
            if event is not None:
                visible.need(event=='confirm','owner_readiness_refused')
                break
            sleep(0.01)
    finally:
        cleared=backend.clear() is True
    visible.need(cleared,'neutral_clear_unverified')
    clock.check(deadline)
    finished,stamp=clock.now(),utc()
    visible.need(visible._valid_scope(admission())==scope,'desktop_changed')
    history(value,bound,admission,monotonic,utc)
    creation_idle(value,bound,item,old,process_checker)
    visible.need(visible.reference(approval_ref['path'])==approval_ref,'owner_authorization_changed')
    intent(value,bound,item,utc)
    visible.need(0 <= finished-start < visible.NEUTRAL_STEP_SECONDS
        and abs((stamp-started_utc)-(finished-start))<=5,'clock_invalid')
    visible.need(visible._valid_scope(admission())==scope,'desktop_changed')
    receipt=dict(schema=READINESS_SCHEMA,binding=bound,manifest=value['predevice'],approval=approval_ref,
        scope=scope,started_monotonic=start,finished_monotonic=finished,finished_utc=stamp,
        explicit_current_readability=True,ready_now=True,cleared=True,
        hardware_authority=False,enrollment_acceptance=False)
    readiness=visible.save_once(Path(value['readiness_root'])/'readiness.json',receipt)
    successor=item['successor']
    fixed=record(successor['fixed_inputs'],visible.ROOT/'.private')
    auth=dict(schema='OT0238C-V13-OWNER-AUTHORIZATION-1',approved=True,source='fresh_human_reply',
        received_utc=datetime.datetime.fromtimestamp(approval['received_utc'],datetime.timezone.utc).isoformat(),
        case=approval['case'],group=approval['group'],runtime_sha256=fixed['runtime']['sha256'],session_sha256=value['session']['sha256'],
        fixed_inputs_sha256=successor['fixed_inputs']['sha256'],plan_sha256=successor['procedure']['sha256'],
        review_sha256=successor['static_review']['sha256'],execute_seconds=2100,total_seconds=3600,roles_ready=approval['roles_ready'],
        visible_binding_sha256=binding_sha256,session_nonce=value['session_nonce'],visible_readiness=readiness)
    visible.need(visible._valid_scope(admission())==scope,'desktop_changed')
    visible.save_once(Path(value['session_output'])/'owner-authorization.json',auth)
    return readiness


def require(value,bound,*,admission,monotonic,utc,fresh):
    visible=api()
    item,old=history(value,bound,admission,monotonic,utc)
    ref=visible.reference(Path(value['readiness_root'])/'readiness.json')
    receipt=record(ref,Path(value['readiness_root']))
    keys={'schema','binding','manifest','approval','scope','started_monotonic','finished_monotonic','finished_utc',
        'explicit_current_readability','ready_now','cleared','hardware_authority','enrollment_acceptance'}
    visible.need(type(receipt) is dict and set(receipt)==keys and receipt['schema']==READINESS_SCHEMA
        and receipt['binding']==bound and receipt['manifest']==value['predevice']
        and all(receipt[k] is True for k in ('explicit_current_readability','ready_now','cleared'))
        and receipt['hardware_authority'] is False and receipt['enrollment_acceptance'] is False
        and visible._valid_scope(receipt['scope'])==visible._valid_scope(admission()),'readiness_invalid')
    now,stamp=monotonic(),utc()
    visible.need(all(visible.finite(receipt[k]) for k in ('started_monotonic','finished_monotonic','finished_utc'))
        and receipt['started_monotonic'] <= receipt['finished_monotonic'] <= now
        and receipt['finished_utc'] <= stamp
        and receipt['finished_monotonic']-receipt['started_monotonic'] < visible.NEUTRAL_STEP_SECONDS
        and (not fresh or (now-receipt['finished_monotonic'] < visible.MAX_AGE and stamp-receipt['finished_utc'] < visible.MAX_AGE))
        and abs((now-receipt['finished_monotonic'])-(stamp-receipt['finished_utc']))<=5,'readiness_expired')
    approval_ref,reply=intent(value,bound,item,utc,fresh=fresh)
    visible.need(receipt['approval']==visible.reference(Path(value['session_output'])/'owner-approval-intent.json')
        and receipt['approval']==approval_ref
        and 0 <= receipt['finished_utc']-reply['received_utc'] < visible.MAX_AGE,'owner_authorization_stale')
    return value


def anchor(value):
    visible=api()
    return record(visible.reference(Path(value['readiness_root'])/'readiness.json'),Path(value['readiness_root']))


def practice(value):
    item,old=manifest(value)
    return api().require_practice(old,item['predecessor']['binding']['sha256'])
