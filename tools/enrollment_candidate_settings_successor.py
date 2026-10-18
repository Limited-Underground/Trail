"""Read-only admission for a fresh current-settings capture successor.

Historical custody and cleanup snapshots remain historical. This module has no
device, process, native UI, grant issuer, SDK or candidate-write capability.
"""
import hashlib
import json
from pathlib import Path
import re

KIND = 'ot0238c-retained-firmware-current-settings'
NAME = 'enrollment_candidate_settings_successor.py'
REQUEST = 'OT-RETAINED-FIRMWARE-CURRENT-NVS-REQUEST-1'
STAGE = 'ot0238c-settings-check-20261011'
LINKAGE = dict(bytes=1083, sha256='e115ab36218f0784a1d6da5eceef7db7ac5f60afd036964bed5580c09a0cc077')
HISTORICAL_LINKAGE_PIN = dict(bytes=1083, sha256='e115ab36218f0784a1d6da5eceef7db7ac5f60afd036964bed5580c09a0cc077')
HISTORICAL_PRIVATE_ROOT = r'C:\lu\OpenTrail\.private\ot177-publication\.private'
DISPOSITION = dict(bytes=11745, sha256='3460aea3cc43b2f8fdca083dc08dbb08372f40737ce9859522ca747f2c561915')
PRESERVATION = dict(bytes=18319, sha256='8f2808026efa55b2d4b8f9d275390aec7ff7b64eb2c20bad4d15b821290ee31a')
CORE = ('enrollment_candidate_runner.py', 'enrollment_candidate_rom_adapter.py', 'enrollment_candidate_original_capture.py',
        'enrollment_candidate_capture_runner.py')

FULL_KIND = 'ot0238c-reviewed-capture-core-full-successor'
FULL_STAGE = 'ot0238c-full-successor-admission-20261012'
FULL_PROFILE_PIN = dict(bytes=3737, sha256='af40e384781d589fc83a8dca04c001acea8934784d52a5e974f48d3256242c9d')
CORE_REVIEW_PIN = dict(bytes=11399, sha256='2052cf6c6e65b536a814f3d7d234c3b3973a3cd321d2fa9b62b1df4bad4e07e0')
REVOKE_KIND = 'ot0238e-reviewed-revoke-successor'
REVOKE_STAGE = 'ot0238e-revoke-admission-20261013'
REVOKE_PROFILE_PIN = {'bytes': 4401, 'sha256': '45c9b57cc731a6b78078bbe353e7264bb1494c2e613ff6342d99ab089fb7006f'}
CANCEL_KIND = 'ot0238c-reviewed-cancel-successor'
CANCEL_STAGE = 'ot0238c-cancel-admission-20261014'
CANCEL_PROFILE_PIN = {'bytes': 4279, 'sha256': 'aa37c66ab057df6d75456fcda394ee8e3fdf98f43e8d65ee87585fae6cf5b4e3'}
RECOVERY_A_KIND = 'ot0238c-reviewed-recovery-A-successor'
RECOVERY_A_STAGE = 'ot0238c-recovery-A-admission-20261014'
RECOVERY_A_PROFILE_PIN = {'bytes': 4322, 'sha256': '439c1f29ce8f71f9fc7d243f467d289bbf4972268dad4d59e86414a1dc48c43a'}
RECOVERY_A_CONTROLLER_PIN = dict(bytes=29536, sha256='6c50e36b18a7dfdb3426418425d8345b328700541fc4a6cdf5c51b9627ce19c4')
CANCEL_CONTROLLER_PIN = dict(bytes=29536, sha256='6c50e36b18a7dfdb3426418425d8345b328700541fc4a6cdf5c51b9627ce19c4')
SELECTION_REVIEW_PIN = dict(bytes=5335, sha256='fba2e1c743c02cd5b99d0d5253907c15066b7c7f719cf949c12957012dacf142')
OBSERVED_KIND = 'ot0238c-reviewed-recovery-A-observed-successor'
OBSERVED_STAGE = 'ot0238c14-recovery-observed-admission-20261014'
OBSERVED_PROFILE_PIN = dict(bytes=1795, sha256='285b11c4eedc503d1b7fc8fef4eadffb0e32e0b6bc8552d23fac9943da697b82')
OBSERVATION_REVIEW_PIN = dict(bytes=10259, sha256='1a51f4421186ccbe93eed4b5b7b896644a2c6e27efc599cd03fd56d4dec8704b')
OBSERVATION_VALIDATION_PIN = dict(bytes=776613, sha256='019b99070cbb8d3e4178e36a11834d93e4aa8d7a765a8033429f82950450188a')
OBSERVATION_DELTA = {"enrollment_candidate_controller.py":{"before":{"bytes":29536,"sha256":"6c50e36b18a7dfdb3426418425d8345b328700541fc4a6cdf5c51b9627ce19c4"},"after":{"bytes":31385,"sha256":"78c015af015acb0162c74d5f529f137ed8decadadc4cacffe1af17e59ab01e71"}},"enrollment_candidate_operator.py":{"before":{"bytes":55897,"sha256":"2a502a49615926db313f059037707bf1f11b94ea3ed2f8d9fd62525168ced1c7"},"after":{"bytes":57373,"sha256":"656d3703fc87de46f47a4c4ec5ec21d9ef6a83856a510f8a8f8fd751d1ba8fae"}},"enrollment_candidate_runner.py":{"before":{"bytes":61076,"sha256":"56bbf37b2fd9a848ad8120a196d8be9cc03ab05895af90e5acf249f3a20079f0"},"after":{"bytes":66698,"sha256":"5222937a7ab6ecdaefd8ef7f061ebd6b6bad2501c2e42c222dd4de6354dff792"}}}
PACKAGE_CALLERS = frozenset((NAME, 'enrollment_candidate_predevice_resume.py',
    'enrollment_candidate_interactive_launch.py', 'enrollment_candidate_owner_workflow.py'))


def full_successor_profile(private, kind=FULL_KIND):
    """Read immutable prior evidence only; no future package/source hash cycle."""
    if kind == REVOKE_KIND:
        return revoke_successor_profile(private)
    if kind == CANCEL_KIND:
        return cancel_successor_profile(private)
    if kind == RECOVERY_A_KIND:
        return recovery_a_successor_profile(private)
    if kind == OBSERVED_KIND:
        return observed_successor_profile(private)
    need(kind == FULL_KIND)
    private = Path(private)
    ref = dict(path=str(private / FULL_STAGE / 'successor-admission-profile.json'), **FULL_PROFILE_PIN)
    profile = record(ref, private)
    need(profile['schema'] == 'OT-FULL-SUCCESSOR-ADMISSION-PROFILE-1'
        and profile['kind'] == FULL_KIND and profile['stage'] == FULL_STAGE
        and profile['prestage'] == 'prestage')
    core_ref = dict(path=str(private / 'ot0238c-capture-only-20261011/independent-review.json'), **CORE_REVIEW_PIN)
    need(profile['core_review'] == dict(relative_path='ot0238c-capture-only-20261011/independent-review.json', **CORE_REVIEW_PIN))
    review = record(core_ref, private)
    need(review['schema'] == 'OT0238C-CAPTURE-ONLY-INDEPENDENT-REVIEW-1'
        and review['outcome'] == 'passed_host_only_no_open_findings'
        and review['baseline_verified'] is True and review['source_hashes_verified'] is True
        and review['open_findings'] == [] and review['physical_acceptance'] is False)
    for name, pair in profile['reviewed_core_delta'].items():
        for field, rows in (('before', review['before']), ('after', review['files'])):
            selected = [row for row in rows if row['path'] == 'tools\\' + name]
            need(len(selected) == 1 and {k:selected[0][k] for k in ('bytes', 'sha256')} == pair[field])
    archived = profile['archived_full_binding']
    archive_ref = dict(path=str(private / archived['relative_path']),
        **{k:archived[k] for k in ('bytes', 'sha256')})
    binding = record(archive_ref, private)
    need(binding['schema'] == 'OT-VISIBLE-SESSION-BINDING-2'
        and set(profile['reviewed_core_delta']) == {'enrollment_candidate_capture_runner.py', 'enrollment_candidate_runtime.py'}
        and set(profile['cumulative_sources']) == set(CORE) | {'enrollment_candidate_runtime.py'})
    for name, pair in profile['reviewed_core_delta'].items():
        need(binding['sources'][name] == pair['before']
            and profile['cumulative_sources'][name]['after'] == pair['after'])
    return profile, ref, core_ref, binding


def revoke_successor_profile(private):
    """A separate fixed route inherits reviewed history, never consumed custody."""
    private = Path(private)
    prior, prior_ref, core_ref, archived = full_successor_profile(private)
    ref = dict(path=str(private / REVOKE_STAGE / 'successor-admission-profile.json'), **REVOKE_PROFILE_PIN)
    profile = record(ref, private)
    added = {'prior_full_profile', 'selection_review', 'reviewed_selection_source', 'selection'}
    need(type(profile) is dict and set(profile) == set(prior) | added
        and profile['schema'] == 'OT-REVOKE-SUCCESSOR-ADMISSION-PROFILE-1'
        and profile['kind'] == REVOKE_KIND and profile['stage'] == REVOKE_STAGE
        and profile['prestage'] == 'prestage'
        and all(profile[k] == prior[k] for k in prior if k not in ('schema', 'kind', 'stage'))
        and profile['prior_full_profile'] == dict(relative_path=FULL_STAGE + '/successor-admission-profile.json',
            **{k:prior_ref[k] for k in ('bytes', 'sha256')}))
    selection = profile['selection']
    need(type(selection) is dict and set(selection) == {'case', 'group'}
        and type(selection['case']) is str and selection['case'] == 'revoke'
        and type(selection['group']) is int and selection['group'] == 1)
    review_path = 'ot0238d-revoke-selection-20261013/independent-review.json'
    need(profile['selection_review'] == dict(relative_path=review_path, **SELECTION_REVIEW_PIN))
    review = record(dict(path=str(private / review_path), **SELECTION_REVIEW_PIN), private)
    need(review.get('schema') == 'OT-0238D-INDEPENDENT-REVIEW-1'
        and review.get('outcome') == 'passed_host_only_no_open_findings'
        and review.get('open_findings') == [] and review.get('source_hashes_verified') is True
        and all(review.get(k) is False for k in ('hardware_access', 'live_grants_created',
            'runnable_package_prepared', 'physical_acceptance', 'publication_performed'))
        and profile['reviewed_selection_source'] == dict(
            path='tools/enrollment_candidate_predevice_resume.py',
            sha256=review['source_pins']['tools/enrollment_candidate_predevice_resume.py']))
    return profile, ref, core_ref, archived


def validate_full_successor_provenance(binding, transition, private):
    profile, profile_ref, core_ref, archived = full_successor_profile(private, transition['kind'])
    need(transition.get('admission_profile') == profile_ref and transition.get('core_review') == core_ref
        and transition['sources'] == profile['cumulative_sources']
        and transition['predecessor_assembly'] == profile['predecessor_assembly']
        and transition['predecessor_runtime'] == profile['predecessor_runtime'])
    need(set(binding['sources']) == set(archived['sources']))
    for name, pin in archived['sources'].items():
        if name in PACKAGE_CALLERS:
            # These four final host-policy bytes are closed by the future
            # binding/source review, after implementation; never by this profile.
            continue
        delta = (profile['cumulative_sources'] if transition['kind'] == OBSERVED_KIND
                 else profile['reviewed_core_delta'])
        expected = delta.get(name, {}).get('after', pin)
        need(binding['sources'][name] == expected)
    return profile


def cancel_successor_profile(private):
    """Fixed early preactivation cancellation; no invented selection review.

    The unchanged controller is anchored by the immutable prior full binding.
    Replacement package callers still require their future source review.
    """
    private = Path(private)
    prior, prior_ref, core_ref, archived = full_successor_profile(private)
    ref = dict(path=str(private / CANCEL_STAGE / 'successor-admission-profile.json'), **CANCEL_PROFILE_PIN)
    profile = record(ref, private)
    need(type(profile) is dict and set(profile) == set(prior) | {
        'prior_full_profile', 'cancellation_controller', 'selection', 'controller_scope'}
        and profile['schema'] == 'OT-CANCEL-SUCCESSOR-ADMISSION-PROFILE-1'
        and profile['kind'] == CANCEL_KIND and profile['stage'] == CANCEL_STAGE
        and profile['prestage'] == 'prestage'
        and all(profile[k] == prior[k] for k in prior if k not in ('schema', 'kind', 'stage'))
        and profile['prior_full_profile'] == dict(relative_path=FULL_STAGE + '/successor-admission-profile.json',
            **{k:prior_ref[k] for k in ('bytes', 'sha256')})
        and profile['cancellation_controller'] == dict(path='tools/enrollment_candidate_controller.py',
            **CANCEL_CONTROLLER_PIN)
        and archived['sources']['enrollment_candidate_controller.py'] == CANCEL_CONTROLLER_PIN
        and profile['controller_scope'] == 'mode0_begin_both_cancel_A_close_B_before_activation')
    selection = profile['selection']
    need(type(selection) is dict and set(selection) == {'case', 'group'}
        and type(selection['case']) is str and selection['case'] == 'cancel'
        and type(selection['group']) is int and selection['group'] == 1)
    return profile, ref, core_ref, archived


def recovery_a_successor_profile(private):
    """Fixed A-only interrupted commitment; no invented selection review.

    The unchanged controller is anchored by the immutable prior full binding.
    Replacement package callers still require their future source review.
    """
    private = Path(private)
    prior, prior_ref, core_ref, archived = full_successor_profile(private)
    ref = dict(path=str(private / RECOVERY_A_STAGE / 'successor-admission-profile.json'), **RECOVERY_A_PROFILE_PIN)
    profile = record(ref, private)
    need(type(profile) is dict and set(profile) == set(prior) | {
        'prior_full_profile', 'recovery_controller', 'selection', 'controller_scope'}
        and profile['schema'] == 'OT-RECOVERY-A-SUCCESSOR-ADMISSION-PROFILE-1'
        and profile['kind'] == RECOVERY_A_KIND and profile['stage'] == RECOVERY_A_STAGE
        and profile['prestage'] == 'prestage'
        and all(profile[k] == prior[k] for k in prior if k not in ('schema', 'kind', 'stage'))
        and profile['prior_full_profile'] == dict(relative_path=FULL_STAGE + '/successor-admission-profile.json',
            **{k:prior_ref[k] for k in ('bytes', 'sha256')})
        and profile['recovery_controller'] == dict(path='tools/enrollment_candidate_controller.py',
            **RECOVERY_A_CONTROLLER_PIN)
        and archived['sources']['enrollment_candidate_controller.py'] == RECOVERY_A_CONTROLLER_PIN
        and profile['controller_scope'] == 'mode0_commit_A_restart_mode2_recovery_fresh_activation_eight_statuses')
    selection = profile['selection']
    need(type(selection) is dict and set(selection) == {'case', 'group'}
        and type(selection['case']) is str and selection['case'] == 'recovery_after_A_commit'
        and type(selection['group']) is int and selection['group'] == 1)
    return profile, ref, core_ref, archived


def successor_review(ref, schema, hash_field, core, private):
    """Close a new-route host review before SDK inspection or owner markers."""
    review = record(ref, private)
    digest = hashlib.sha256(json.dumps(core, sort_keys=True, separators=(',', ':'),
        allow_nan=False).encode('ascii')).hexdigest()
    need(type(review) is dict and set(review) == {'schema', hash_field, 'outcome', 'findings',
        'hardware_access', 'live_grants_created'} and review['schema'] == schema
        and review[hash_field] == digest and review['outcome'] == 'no_unresolved_findings'
        and review['findings'] == [] and review['hardware_access'] is False
        and review['live_grants_created'] is False)


def observed_successor_profile(private):
    """Exact c13 observation delta; earlier A recovery remains immutable.

    This is file-only source admission. It grants no current custody, readiness,
    fresh NVS, dispatch, or physical acceptance.
    """
    private = Path(private)
    prior, _, core_ref, archived = recovery_a_successor_profile(private)
    ref = dict(path=str(private / OBSERVED_STAGE / 'successor-admission-profile.json'),
        **OBSERVED_PROFILE_PIN)
    selected = record(ref, private)
    need(type(selected) is dict and set(selected) == {'schema', 'kind', 'stage',
        'prestage', 'prior_recovery_a_stage', 'selection', 'observation_review',
        'observation_validation', 'reviewed_observation_delta'}
        and selected['schema'] == 'OT-RECOVERY-A-OBSERVED-SUCCESSOR-ADMISSION-PROFILE-1'
        and selected['kind'] == OBSERVED_KIND and selected['stage'] == OBSERVED_STAGE
        and selected['prestage'] == 'prestage'
        and selected['prior_recovery_a_stage'] == RECOVERY_A_STAGE
        and selected['selection'] == prior['selection']
        and type(selected['selection']['group']) is int
        and selected['reviewed_observation_delta'] == OBSERVATION_DELTA)
    review_ref = dict(path=str(private / 'ot0238c13-recovery-observation-20261014/independent-review.json'),
        **OBSERVATION_REVIEW_PIN)
    validation_ref = dict(path=str(private / 'ot0238c13-recovery-observation-20261014/validation.json'),
        **OBSERVATION_VALIDATION_PIN)
    for key, value in (('observation_review', review_ref), ('observation_validation', validation_ref)):
        need(selected[key] == dict(relative_path=Path(value['path']).relative_to(private).as_posix(),
            **{k:value[k] for k in ('bytes', 'sha256')}))
    review = record(review_ref, private)
    validation = decode(read(validation_ref, private), limit=1048576)
    need(review['schema'] == 'OT0238C13-INDEPENDENT-REVIEW-1'
        and review['outcome'] == 'review_complete_no_open_findings' and review['findings'] == []
        and {k:review['reviewed_validation'][k] for k in ('bytes', 'sha256')} == OBSERVATION_VALIDATION_PIN
        and review['builder_validation_review']['final_matrix_passed'] is True
        and review['builder_validation_review']['total_tests'] == 188
        and review['acceptance']['host_observation_review_passed'] is True
        and review['acceptance']['physical_acceptance'] is False
        and validation['final_matrix_passed'] is True and validation['final_test_total'] == 188
        and validation['acceptance']['physical_acceptance'] is False)
    cumulative = {name:dict(pair) for name, pair in prior['cumulative_sources'].items()}
    for name, pair in OBSERVATION_DELTA.items():
        expected_before = prior['cumulative_sources'].get(name, {}).get('after', archived['sources'][name])
        need(pair['before'] == expected_before
            and validation['baseline']['files']['tools/' + name] == pair['before']['sha256']
            and validation['builder_source_files']['tools/' + name] == pair['after'])
        rows = [row for row in review['reviewed_code_and_test'] if Path(row['path']).name == name]
        need(len(rows) == 1 and {k:rows[0][k] for k in ('bytes', 'sha256')} == pair['after'])
        read(dict(path=str(private.parent / 'tools' / name), **pair['after']), private.parent)
        cumulative[name] = dict(before=cumulative.get(name, {}).get('before', pair['before']), after=pair['after'])
    profile = dict(prior, schema=selected['schema'], kind=OBSERVED_KIND, stage=OBSERVED_STAGE,
        cumulative_sources=cumulative, recovery_controller=dict(path='tools/enrollment_candidate_controller.py',
            **OBSERVATION_DELTA['enrollment_candidate_controller.py']['after']),
        reviewed_observation_delta=OBSERVATION_DELTA, observation_review=review_ref,
        observation_validation=validation_ref)
    return profile, ref, core_ref, archived


def revoke_selection(binding, transition, profile, private, *, stage=REVOKE_STAGE):
    """Bind selection to the actual succession record, before any dispatch."""
    item = record(binding['predevice'], private)
    need(type(item) is dict and item.get('schema') == 'OT-PREDEVICE-SUCCESSION-2'
        and item.get('runtime_transition') == transition)
    successor = item['successor']
    need(type(successor) is dict and all(successor.get(k) == binding[k] for k in
        ('session_nonce', 'readiness_root', 'session_output', 'session', 'assembly', 'sources')))
    successor_review(item.get('review'), 'OT-PREDEVICE-SUCCESSION-REVIEW-1', 'manifest_core_sha256',
        {k:v for k,v in item.items() if k != 'review'}, private)
    successor_review(binding.get('review'), 'OT-VISIBLE-SESSION-REVIEW-1', 'binding_core_sha256',
        {k:v for k,v in binding.items() if k != 'review'}, private)
    ref = successor['fixed_inputs']
    need(transition.get('fixed_inputs') == ref
        and Path(ref['path']) == Path(private) / stage / 'prestage/fixed-inputs.json')
    fixed = record(ref, private)
    need(type(fixed) is dict and type(fixed.get('case')) is str and fixed['case'] == profile['selection']['case']
        and type(fixed.get('group')) is int and fixed['group'] == profile['selection']['group']
        and fixed.get('runtime') == transition['successor_runtime']
        and type(fixed.get('proposed_execute_seconds')) is int and fixed['proposed_execute_seconds'] == 2100
        and type(fixed.get('proposed_restore_seconds')) is int and fixed['proposed_restore_seconds'] == 3600)
    return ref


class SettingsSuccessorError(RuntimeError):
    pass


def need(value):
    if not value:
        raise SettingsSuccessorError('recovery_gate_invalid')


def regular(path, private, directory=False):
    path, private = Path(path), Path(private)
    need(path.is_absolute() and path.resolve().is_relative_to(private.resolve())
         and path.resolve() != private.resolve())
    for parent in (path, *path.parents):
        need(not parent.is_symlink() and not parent.is_junction())
    need(path.is_dir() if directory else path.is_file())
    return path


def read(ref, private):
    need(type(ref) is dict and set(ref) == {'path', 'bytes', 'sha256'}
         and type(ref['bytes']) is int and 0 <= ref['bytes'] <= 33554432
         and type(ref['sha256']) is str and re.fullmatch('[0-9a-f]{64}', ref['sha256']))
    raw = regular(ref['path'], private).read_bytes()
    need(len(raw) == ref['bytes'] and hashlib.sha256(raw).hexdigest() == ref['sha256'])
    return raw


def decode(raw, *, limit=131072):
    need(len(raw) <= limit)
    def unique(pairs):
        value = {}
        for key, item in pairs:
            need(key not in value)
            value[key] = item
        return value
    return json.loads(raw, object_pairs_hook=unique,
        parse_constant=lambda value: (_ for _ in ()).throw(SettingsSuccessorError('recovery_gate_invalid')))


def record(ref, private):
    return decode(read(ref, private))


class _HistoricalClosure:
    """Physical E reads for one hash-authenticated, immutable linkage closure."""

    def __init__(self, private, relocated):
        self.private = Path(private)
        self.relocated = relocated
        self.logical_by_physical = {}
        self.pins_by_logical = {}

    def physical_path(self, value):
        need(type(value) is str)
        if not self.relocated:
            path = Path(value)
            need(path.is_absolute() and path.is_relative_to(self.private) and path != self.private
                and path.resolve().is_relative_to(self.private.resolve())
                and path.resolve() != self.private.resolve())
        else:
            if value.startswith(HISTORICAL_PRIVATE_ROOT + '\\'):
                suffix = value[len(HISTORICAL_PRIVATE_ROOT) + 1:]
            else:
                current = str(self.private)
                need(value.startswith(current + '\\'))
                suffix = value[len(current) + 1:]
            parts = suffix.split('\\')
            need(parts and all(part not in ('', '.', '..') and ':' not in part
                and not part.endswith((' ', '.')) for part in parts)
                and str(Path(value)) == value)
            path = self.private.joinpath(*parts)
        # Case aliases can name the same Windows file even when their strings
        # differ; a second logical identity may not reuse one physical leaf.
        key = str(path.resolve() if self.relocated else path).casefold()
        need(self.logical_by_physical.get(key, value) == value)
        self.logical_by_physical[key] = value
        return path

    def read(self, ref):
        need(type(ref) is dict and set(ref) == {'path', 'bytes', 'sha256'})
        path = self.physical_path(ref['path'])
        pin = (ref['bytes'], ref['sha256'])
        need(self.pins_by_logical.get(ref['path'], pin) == pin)
        self.pins_by_logical[ref['path']] = pin
        return read(dict(ref, path=str(path)), self.private)

    def record(self, ref):
        return decode(self.read(ref))

    def directory(self, value):
        return regular(self.physical_path(value), self.private, True)


def settled_diagnostic(linkage_ref, private):
    """Authenticate frozen history while retaining its original logical refs."""
    return _settled_diagnostic(linkage_ref, private)[:3]


def _settled_diagnostic(linkage_ref, private):
    """Finite read-only closure of the accepted diagnostic linkage contract.

    Mirrors consume_receipt's normative evidence boundary. Historical review
    narratives are authenticated as immutable leaf files; they are never
    recursively interpreted as current workspace, process or SDK authority.
    """
    private = Path(private)
    need(linkage_ref == dict(path=str(private / 'ot0238c-inspection-inventory-correction-20261011'
        / 'inspection/diagnostic-linkage.json'), **LINKAGE))
    # The current-root linkage is read and hash-checked before the frozen C-drive
    # references in its finite closure can acquire an E-drive physical location.
    linkage = record(linkage_ref, private)
    historical = _HistoricalClosure(private, LINKAGE == HISTORICAL_LINKAGE_PIN)
    need(linkage.get('schema') == 'OT-RECOVERY-DIAGNOSTIC-LINKAGE-1'
        and linkage.get('status') == 'fresh_diagnostic_custody_settled'
        and linkage.get('automatic_legacy_custody_released') is False
        and linkage.get('candidate_eligible') is False
        and linkage.get('historical_byte_equality') == 'unknown')
    receipt_ref, owner_ref, review_ref = (linkage[k] for k in
        ('diagnostic_receipt', 'owner_observation', 'independent_review'))
    receipt, owner, review = (historical.record(ref) for ref in (receipt_ref, owner_ref, review_ref))
    need(receipt.get('schema') == 'OT-RECOVERY-DIAGNOSTIC-RECEIPT-1'
        and receipt.get('origin') == 'live_authorized_controller'
        and receipt.get('candidate_eligible') is False
        and receipt.get('automatic_legacy_custody_released') is False
        and receipt.get('capture_complete') is True and receipt.get('lease_closed') is True
        and receipt.get('cleanup_failure') is None and set(receipt.get('roles', {})) == {'A', 'B'})
    package = historical.record(receipt['package'])
    output = historical.directory(package['evidence_root'])
    need(Path(receipt_ref['path']) == Path(package['evidence_root']) / 'diagnostic-receipt.json'
        and package.get('schema') == 'OT-RETAINED-RECOVERY-DIAGNOSTIC-PACKAGE-1'
        and receipt['controller_sha256'] == package['controller_sha256']
        and receipt['source_pins'] == package['source_pins'])
    historical.read(receipt['source_pins'])
    auth, grant, comparator, request = (historical.record(ref) for ref in
        (receipt['authorization'], package['capture_grant'], package['comparator_request'], package['capture_request']))
    for key in ('legacy_disposition', 'legacy_preservation'):
        need(receipt[key] == package[key]); historical.read(receipt[key])
    historical.read(package['legacy_gate_module'])
    need(auth.get('schema') == 'OT-RECOVERY-DIAGNOSTIC-AUTHORIZATION-1'
        and auth.get('scope') == 'retained_freshness_diagnostic_and_one_guarded_original_boot_per_role'
        and auth.get('owner_approved') is True and auth.get('one_use') is True
        and auth.get('candidate_operations_authorized') is False
        and auth.get('fresh_identity_profiles_reviewed') is True
        and auth.get('rom_entry_and_guarded_original_boot_authorized') is True
        and auth.get('package_sha256') == receipt['package']['sha256']
        and auth.get('controller_sha256') == receipt['controller_sha256']
        and auth.get('capture_grant_sha256') == package['capture_grant']['sha256']
        and auth.get('capture_attempt') == receipt['capture_attempt']
        and auth.get('legacy_disposition_sha256') == package['legacy_disposition']['sha256']
        and auth.get('identities_sha256') == package['identities']['sha256']
        and auth.get('profiles_sha256') == package['profiles']['sha256'])
    need(grant.get('schema') == 'OT-ORIGINAL-CAPTURE-GRANT-1' and grant.get('operation') == 'capture'
        and grant.get('origin_attempt') is None and grant.get('attempt') == receipt['capture_attempt']
        and grant.get('attempt_count') == 1 and grant.get('actions') == auth.get('actions')
        == ['guard_original', 'read_six_spans', 'reset_original']
        and all(type(auth.get(k)) is int and auth[k] == grant.get(k)
            for k in ('issued_utc', 'capture_expires_utc', 'cleanup_expires_utc'))
        and 0 < auth['capture_expires_utc'] - auth['issued_utc'] <= 2100
        and auth['capture_expires_utc'] <= auth['cleanup_expires_utc']
        and auth['cleanup_expires_utc'] - auth['issued_utc'] <= 3600)
    journal_ref = receipt['diagnostic_journal']
    need(Path(journal_ref['path']) == Path(package['evidence_root']) / ('enrollment-original-' + receipt['capture_attempt'] + '.json')
        and not any((output / n).exists() for n in ('enrollment-original-active.json',
            'enrollment-candidate-active.json', Path(journal_ref['path']).with_suffix('.pending').name)))
    journal = historical.record(journal_ref)
    need(journal.get('phase') == 'released' and journal.get('capture_complete') is True
        and journal.get('candidate') is None and journal.get('cleanup_failure') is None
        and journal.get('roles') == receipt['roles'] and journal.get('request') == request
        and journal.get('grant_sha256') == package['capture_grant']['sha256']
        and journal.get('attempt') == receipt['capture_attempt'])
    request_sha = hashlib.sha256(json.dumps(request, sort_keys=True, separators=(',', ':'),
        allow_nan=False).encode('ascii')).hexdigest()
    need(grant.get('request_sha256') == request_sha
        and grant.get('runtime_sha256') == request.get('runtime_sha256') == comparator.get('runtime_sha256')
        and receipt.get('reset_attempted') == ['A', 'B'])
    drift_map = receipt.get('current_nvs_drift')
    need(type(drift_map) is dict and set(drift_map) <= {'A', 'B'})
    immutable = {'application', 'bootloader', 'partition', 'otadata', 'ota0_prefix'}
    for role in ('A', 'B'):
        row = receipt['roles'][role]
        need(comparator['roles'][role]['device_binding'] == request['roles'][role]['device_binding']
            and row.get('owner') == 'capture' and row.get('entered') is True
            and row.get('original_boot_allowed') is True and row.get('handles_closed') is True
            and row.get('pins') == comparator['roles'][role]['originals'])
        resets = [p for p in receipt['proofs'] if p.get('role') == role and p.get('operation') == 'reset_original']
        need(len(resets) == 1 and resets[0].get('validated_success') is True
            and resets[0].get('factory_original_layout') is True
            and resets[0].get('safe_current_nvs_guarded') is True
            and resets[0].get('immutable_five') == {n:row['pins'][n] for n in sorted(immutable)})
        current, baseline = resets[0].get('reset_current_nvs'), row['pins']['nvs']
        if current == baseline:
            need(role not in drift_map and 'release_nvs_evidence' not in row)
        else:
            drift = drift_map.get(role)
            need(type(drift) is dict and set(drift) == {'data', 'evidence'})
            raw, evidence = historical.read(drift['data']), historical.record(drift['evidence'])
            need(len(raw) == 12288 and current == {k:drift['data'][k] for k in ('bytes', 'sha256')}
                and evidence == dict(schema='OT-ORIGINAL-NVS-DRIFT-EVIDENCE-1',
                    attempt=receipt['capture_attempt'], role=role, device_binding=request['roles'][role]['device_binding'],
                    request_sha256=request_sha, grant_sha256=package['capture_grant']['sha256'],
                    runtime_sha256=request['runtime_sha256'], retained_nvs=baseline, current_nvs=current,
                    purpose='cleanup_only_not_retained_baseline')
                and row.get('release_nvs_evidence') == {k:drift['evidence'][k] for k in ('bytes', 'sha256')})
    need(owner.get('schema') == 'OT-RECOVERY-DIAGNOSTIC-OWNER-OBSERVATION-1'
        and owner.get('diagnostic_receipt_sha256') == receipt_ref['sha256']
        and owner.get('roles') == {'A':'usual_screen_owner_confirmed', 'B':'usual_screen_owner_confirmed'})
    need(review.get('schema') == 'OT-RECOVERY-DIAGNOSTIC-INDEPENDENT-REVIEW-1'
        and review.get('accepted') is True and review.get('diagnostic_receipt_sha256') == receipt_ref['sha256']
        and review.get('owner_observation_sha256') == owner_ref['sha256']
        and review.get('controller_sha256') == receipt['controller_sha256']
        and review.get('source_pins_sha256') == receipt['source_pins']['sha256']
        and review.get('scope') == 'new_diagnostic_evidence_linkage_only')
    preserved = historical.record(receipt['legacy_preservation'])
    disposition = historical.record(receipt['legacy_disposition'])
    legacy = historical.directory(preserved['session'])
    members = tuple(legacy.iterdir())
    need(preserved.get('schema') == 'OT-LEGACY-SESSION-PRESERVATION-1'
        and preserved.get('before_equals_after') is True and len(preserved['files']) == 39
        and all(p.is_file() and not p.is_symlink() and not p.is_junction() for p in members)
        and {str(historical.physical_path(path)) for path in preserved['files']}
            == {str(p) for p in members}
        and disposition.get('schema') == 'OT-MANUAL-RECOVERY-DISPOSITION-1'
        and disposition.get('status') == 'manual_recovery_observed_baseline_unresolved'
        and disposition.get('candidate_eligible') is False
        and disposition.get('automatic_custody_released') is False
        and disposition.get('predecessor_observed_dead') is False
        and disposition.get('predecessor_pid') is None)
    for path, ref in preserved['files'].items():
        need(ref['path'] == path and Path(path).parent == Path(preserved['session']))
        historical.read(ref)
    for path in members:
        need(not path.name.endswith(('.pending', '.interrupted', '.transfer'))
            and path.name != 'enrollment-candidate-active.json'
            and not re.fullmatch('enrollment-candidate-[0-9a-f]{32}\\.json', path.name))
    for ref in disposition['references'].values():
        historical.read(ref)
    return linkage, receipt, package, historical


def validate_settings_successor(binding, transition, private):
    private = Path(private)
    revoke = transition.get('kind') == REVOKE_KIND
    cancel = transition.get('kind') == CANCEL_KIND
    observed = transition.get('kind') == OBSERVED_KIND
    recovery_a = transition.get('kind') == RECOVERY_A_KIND or observed
    if cancel or recovery_a:
        need(set(transition) == {'schema', 'kind', 'review', 'predecessor_assembly',
            'successor_assembly', 'predecessor_runtime', 'successor_runtime', 'sources',
            'settings_gate', 'settings_successor', 'admission_profile', 'core_review', 'fixed_inputs'}
            and transition.get('schema') == 'OT-PREDEVICE-RUNTIME-TRANSITION-1')
    selected = revoke or cancel or recovery_a
    full = selected or transition.get('kind') == FULL_KIND
    root = private / (OBSERVED_STAGE if observed else RECOVERY_A_STAGE if recovery_a else CANCEL_STAGE if cancel else REVOKE_STAGE if revoke else FULL_STAGE if full else STAGE)
    prestage = 'prestage' if full else 'prestage-v2'
    profile = validate_full_successor_provenance(binding, transition, private) if full else None
    need(binding.get('schema') == 'OT-VISIBLE-SESSION-BINDING-2'
         and Path(binding.get('worktree', '')).resolve() == private.parent.resolve()
         and transition.get('kind') == (OBSERVED_KIND if observed else RECOVERY_A_KIND if recovery_a else CANCEL_KIND if cancel else REVOKE_KIND if revoke else FULL_KIND if full else KIND))
    fixed_ref = revoke_selection(binding, transition, profile, private,
        stage=OBSERVED_STAGE if observed else RECOVERY_A_STAGE if recovery_a else CANCEL_STAGE if cancel else REVOKE_STAGE) if selected else None
    resolution = record(transition['settings_successor'], private)
    keys = {'schema', 'scope', 'legacy_disposition', 'legacy_preservation',
        'settled_diagnostic_linkage', 'capture_request', 'source_review', 'successor_runtime',
        'successor_session', 'successor_root', 'historical_custody_released',
        'historical_baseline_replaced', 'diagnostic_nvs_adopted', 'candidate_eligible'}
    if selected:
        keys.add('fixed_inputs')
    need(type(resolution) is dict and set(resolution) == keys
         and resolution['schema'] == ('OT-CURRENT-SETTINGS-SUCCESSOR-7' if observed else 'OT-CURRENT-SETTINGS-SUCCESSOR-6' if recovery_a else 'OT-CURRENT-SETTINGS-SUCCESSOR-5' if cancel else 'OT-CURRENT-SETTINGS-SUCCESSOR-4' if revoke else 'OT-CURRENT-SETTINGS-SUCCESSOR-3' if full else 'OT-CURRENT-SETTINGS-SUCCESSOR-2')
         and resolution['scope'] == ('fresh_current_nvs_capture_for_observed_recovery_after_A_commit' if observed else 'fresh_current_nvs_capture_for_recovery_after_A_commit' if recovery_a else 'fresh_current_nvs_capture_for_early_cancel_enrollment' if cancel else 'fresh_current_nvs_capture_for_revoke_enrollment' if revoke else 'fresh_current_nvs_capture_for_full_enrollment' if full else 'fresh_current_nvs_capture_only')
         and (not selected or resolution['fixed_inputs'] == fixed_ref)
         and all(resolution[k] is False for k in ('historical_custody_released',
             'historical_baseline_replaced', 'diagnostic_nvs_adopted', 'candidate_eligible'))
         and Path(transition['settings_successor']['path']) == root / ('settings-successor.json' if full else 'settings-successor-v2.json')
         and resolution['successor_session'] == binding['session']
         and resolution['successor_runtime'] == transition['successor_runtime']
         and resolution['successor_root'] == binding['session_output'])
    session = regular(binding['session']['path'], private)
    output = regular(binding['session_output'], private, True)
    need(session == root / prestage / 'session.template.py'
         and output == root / 'sessions' / binding['session_nonce'])
    read(binding['session'], private)
    gate = dict(path=str(private.parent / 'tools' / NAME), **binding['sources'][NAME])
    need(transition['settings_gate'] == gate)
    # The gate source is outside .private but its exact bytes were verified by
    # the loader before compilation and are also bound by the source review.
    linkage_ref = dict(path=str(private / 'ot0238c-inspection-inventory-correction-20261011'
        / 'inspection/diagnostic-linkage.json'), **LINKAGE)
    need(resolution['settled_diagnostic_linkage'] == linkage_ref)
    linkage, receipt, package, history = _settled_diagnostic(linkage_ref, private)
    legacy_root = private / 'ot0238c-launcher-recovery-20261011/recovery'
    disposition_ref = dict(path=str(legacy_root / 'manual-recovery-observed-baseline-unresolved.json'), **DISPOSITION)
    preservation_ref = dict(path=str(legacy_root / 'legacy-session-preservation.json'), **PRESERVATION)
    for key, expected in (('legacy_disposition', disposition_ref), ('legacy_preservation', preservation_ref)):
        # Fresh successor records name current physical files. Frozen receipt
        # references keep their authenticated logical identity and exact pins.
        need(resolution[key] == expected
            and dict(receipt[key], path=str(history.physical_path(receipt[key]['path']))) == expected)
    disposition = history.record(receipt['legacy_disposition'])
    preserved = history.record(receipt['legacy_preservation'])
    legacy_session = history.directory(preserved['session'])
    need(disposition['candidate_eligible'] is False and disposition['automatic_custody_released'] is False
         and disposition['predecessor_observed_dead'] is False and disposition['predecessor_pid'] is None
         and preserved['before_equals_after'] is True and len(preserved['files']) == 39
         and {str(history.physical_path(path)) for path in preserved['files']}
             == {str(p) for p in legacy_session.iterdir() if p.is_file()}
         and not output.is_relative_to(legacy_session) and not legacy_session.is_relative_to(output))
    for path, ref in preserved['files'].items():
        need(ref['path'] == path and Path(path).parent == Path(preserved['session']))
        history.read(ref)
    for ref in disposition['references'].values():
        history.read(ref)
    historical = history.record(package['capture_request'])
    request = record(resolution['capture_request'], private)
    need(Path(resolution['capture_request']['path']) == root / prestage / 'capture-request.json'
         and request == dict(historical, schema=REQUEST,
             runtime_sha256=transition['successor_runtime']['sha256']))
    # Equality to the fixed reviewed request preserves all seven accepted
    # provenance refs and exact A/B opaque device/profile bindings. NVS bytes
    # from the diagnostic never enter this fresh request or its originals.
    # Only the current normative runtime manifest has this larger bound.
    # Generic control records retain their 128 KiB limit.
    manifest = decode(read(resolution['successor_runtime'], private), limit=1048576)
    assembly = record(transition['successor_assembly'], private)
    pins = record(assembly['source_pins'], private)
    need(assembly['runtime'] == resolution['successor_runtime']
         and assembly['source_root'] == str(private.parent)
         and manifest['root'] == assembly['root'] and manifest['worktree'] == str(private.parent)
         and pins['files'] == assembly['sources'])
    policy_root = regular(Path(manifest['root']) / 'policy', private, True)
    policy = {n:p for n,p in manifest['files'].items() if n.startswith('policy/')}
    need(len(policy) == 36 and set(policy) == {'policy/' + p.name for p in policy_root.iterdir() if p.is_file()}
         and all(p.is_file() and not p.is_symlink() and not p.is_junction() for p in policy_root.iterdir()))
    for name, pin in policy.items():
        read(dict(path=str(Path(manifest['root']) / name), **pin), private)
    for name, pin in assembly['sources'].items():
        need(pin == binding['sources'][Path(name).name] == policy['policy/' + Path(name).name])
    review = record(resolution['source_review'], private)
    need(Path(resolution['source_review']['path']) == root / ('independent-source-review.json' if full else 'independent-source-review-v2.json')
         and review.get('schema') == ('OT-RECOVERY-A-OBSERVED-SUCCESSOR-SOURCE-REVIEW-1' if observed else 'OT-RECOVERY-A-SUCCESSOR-SOURCE-REVIEW-1' if recovery_a else 'OT-CANCEL-SUCCESSOR-SOURCE-REVIEW-1' if cancel else 'OT-REVOKE-SUCCESSOR-SOURCE-REVIEW-1' if revoke else 'OT-FULL-SUCCESSOR-SOURCE-REVIEW-1' if full else 'OT-CURRENT-SETTINGS-SOURCE-REVIEW-1')
         and (not selected or review.get('fixed_inputs') == fixed_ref)
         and review.get('outcome') == 'approved_host_source_only' and review.get('findings') == []
         and review.get('hardware_access') is False and review.get('live_grants_created') is False
         and review.get('capture_request') == resolution['capture_request']
         and review.get('successor_runtime') == resolution['successor_runtime']
         and review.get('validator_source') == gate
         and review.get('binding_sources') == binding['sources']
         and review.get('sources') == transition['sources']
         and transition.get('review') == resolution['source_review']
         and set(transition['sources']) == ((set(CORE) | {'enrollment_candidate_runtime.py'}
             | set(OBSERVATION_DELTA)) if observed else set(CORE) | {'enrollment_candidate_runtime.py'} if full else set(CORE)))
    if full:
        need(review.get('admission_profile') == transition['admission_profile']
            and review.get('core_review') == transition['core_review']
            and review.get('reviewed_core_delta') == profile['reviewed_core_delta'])
    if observed:
        need(review.get('reviewed_observation_delta') == profile['reviewed_observation_delta']
            and review.get('observation_review') == profile['observation_review']
            and review.get('observation_validation') == profile['observation_validation'])
    for name, pair in transition['sources'].items():
        need(set(pair) == {'before', 'after'}
             and pair['after'] == binding['sources'][name]
             and pair['after'] == assembly['sources']['tools/' + name])
    # Current mutable session state is checked separately by the one-shot
    # owner workflow; its markers must never be mistaken for historical proof.
    return True
