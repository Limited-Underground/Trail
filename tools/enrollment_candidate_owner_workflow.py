"""One reviewed owner-desktop workflow; maintained entrypoints own all authority.

This parent never opens devices, manufactures human replies, renews grants, or
terminates its launch worker. Exact independent dynamic review precedes publish.
"""
import argparse
import datetime
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
LIMIT = 32768
HEX = re.compile(r'[0-9a-f]{64}')
CLEANUP_AFTER_PINS = {
    'enrollment_candidate_rom_adapter.py': dict(bytes=69318,
        sha256='5a340c902e51afa3e3024712c0517f3a1d3ff291aa542d7ed37dcc33b0fda391'),
    'enrollment_candidate_original_capture.py': dict(bytes=55424,
        sha256='e614b8ac1c02512e90aa60bf1b3efbaa4e935b12bbd6815e85962860eb861c27'),
    'enrollment_candidate_capture_runner.py': dict(bytes=22439,
        sha256='f92cb5929c9994f5e258252179db60d8f3baafefdc9653d57f93c31c14f0d150')}
SETTINGS_NAME = 'enrollment_candidate_settings_successor.py'
FULL_STAGE = 'ot0238c-full-successor-admission-20261012'
FULL_KIND = 'ot0238c-reviewed-capture-core-full-successor'
REVOKE_KIND = 'ot0238e-reviewed-revoke-successor'
REVOKE_STAGE = 'ot0238e-revoke-admission-20261013'
SETTINGS_AFTER_PINS = {'enrollment_candidate_rom_adapter.py': {'bytes': 70957, 'sha256': 'cc207e17bea17355fd9228cd221235c2a7ce5d777d0cbafde6134df8e98c8f4b'}, 'enrollment_candidate_original_capture.py': {'bytes': 56311, 'sha256': '866192e7dd6ebf95e47d5dc519ff9a475bc74ebd50f57a0a7e48bfa0e23f04e1'}, 'enrollment_candidate_capture_runner.py': {'bytes': 23027, 'sha256': 'fbaf3d0fc820210fbac6371cdf7c2ebea1bdb8744738612bfba0480785abbd02'}}
# Reviewed capture-runner bytes require a protected route; they do not select
# the full mode. Runtime is shared by historical routes and is enforced only
# by the explicit full profile/cumulative transition, not by route detection.
REVIEWED_CORE_AFTER_PINS = {
    'enrollment_candidate_capture_runner.py': dict(bytes=23998,
        sha256='6a86517612a6171df25d921639ce796ec947a02674ec6ae8f38249eaec154f3f')}
ACTIONS = ('probe', 'attest', 'practice', 'revalidate', 'baseline', 'route', 'issue_capture',
           'launch', 'status', 'prepare_handoff', 'publish_handoff')
# Keep this fixed vocabulary local: loading a project helper before admit_plan
# verifies its pin would expand the hash-before-load boundary.
REFUSAL_CATEGORIES = frozenset(('action_invalid', 'arguments_invalid', 'admission_invalid',
    'binding_changed', 'binding_invalid', 'clock_invalid', 'continuity_expired', 'continuity_refused',
    'desktop_changed', 'desktop_unavailable', 'display_unavailable', 'host_changed',
    'lock_policy_unavailable', 'mode_invalid', 'neutral_clear_unverified', 'neutral_layout_invalid',
    'neutral_view_refused', 'owner_authorization_changed', 'owner_authorization_stale',
    'owner_readiness_refused', 'power_unavailable', 'practice_required', 'predecessor_process_active',
    'predecessor_process_unresolved', 'readiness_expired', 'readiness_invalid', 'readiness_root_invalid',
    'resume_changed', 'resume_device_state_present', 'resume_evidence_changed', 'resume_invalid',
    'resume_revalidation_used', 'resume_sources_changed', 'resume_sources_invalid', 'resume_state_invalid',
    'resume_terminal_invalid', 'review_required', 'session_invalid', 'session_output_invalid',
    'sources_changed', 'owner_reply_changed', 'handoff_review_changed', 'handoff_review_required',
    'handoff_review_invalid', 'visible_session_refused', 'file_changed', 'record_invalid',
    'assembly_refused', 'output_exists', 'artifact_write_failed',
    'recovery_baseline_unresolved', 'recovery_gate_invalid'))
PREDEVICE_CATEGORIES = REFUSAL_CATEGORIES | frozenset(('action_refused', 'action_timeout',
    'output_invalid', 'record_too_large', 'duplicate_key', 'number_invalid', 'pin_changed',
    'pin_invalid', 'path_invalid', 'session_used', 'owner_authorization_missing', 'wait_expired',
    'workflow_refused'))
OWNER_CATEGORIES = PREDEVICE_CATEGORIES | frozenset(('capture_failed', 'grant_invalid',
    'handoff_review_expired', 'handoff_review_refused', 'handoff_review_stale', 'held_incomplete',
    'held_wait_refused', 'launch_refused', 'launch_returned_before_handoff', 'nonce_invalid',
    'plan_changed', 'plan_invalid', 'source_invalid', 'trial_failed_or_unknown', 'trial_result_unknown',
    'worker_cleanup_unresolved'))


class Refused(Exception):
    pass


def refusal_category(error):
    """Recognize only fixed typed errors from already loaded policy modules."""
    known = type(error) is Refused
    for module_name, class_name in (('enrollment_candidate_interactive_launch','AdmissionError'),
                                    ('enrollment_candidate_runtime','RuntimeErrorFixed')):
        module = sys.modules.get(module_name)
        known = known or (module is not None and type(error) is getattr(module,class_name,None))
    category = str(error) if known else 'workflow_refused'
    return category if category in OWNER_CATEGORIES else 'workflow_refused'


def need(condition, category):
    if not condition:
        raise Refused(category)


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode('ascii')


def decode(raw):
    need(len(raw) <= LIMIT, 'record_too_large')
    def unique(pairs):
        value = {}
        for key, item in pairs:
            need(key not in value, 'duplicate_key')
            value[key] = item
        return value
    return json.loads(raw, object_pairs_hook=unique,
                      parse_constant=lambda value: (_ for _ in ()).throw(Refused('number_invalid')))


def regular(path, directory=False):
    path = Path(path)
    need(path.is_absolute(), 'path_invalid')
    for part in (path, *path.parents):
        need(not part.is_symlink() and not part.is_junction(), 'path_invalid')
    need(path.is_dir() if directory else path.is_file(), 'path_invalid')
    return path


def reference(path):
    path = regular(path)
    raw = path.read_bytes()
    return {'path': str(path), 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}


def read_pin(pin):
    need(type(pin) is dict and set(pin) == {'path', 'bytes', 'sha256'}, 'pin_invalid')
    need(type(pin['bytes']) is int and pin['bytes'] >= 0 and
         type(pin['sha256']) is str and HEX.fullmatch(pin['sha256']), 'pin_invalid')
    raw = regular(pin['path']).read_bytes()
    need(len(raw) == pin['bytes'] and hashlib.sha256(raw).hexdigest() == pin['sha256'], 'pin_changed')
    return raw


def require_cleanup_recovery(binding_ref):
    """Check the pinned cleanup guard before any owner workflow marker.

    Compile the exact binding-pinned helper bytes under a non-main name. Old
    schemas remain inert here; no source lookup or arbitrary module is accepted.
    """
    binding = decode(read_pin(binding_ref))
    # The new package's fixed parent also remains blocked if its transition is
    # removed, renamed or swapped for historical metadata.
    cleanup_parent = Path(binding.get('session', {}).get('path', '')).parent == (
        ROOT / '.private/ot0238c-launcher-recovery-20261011/prestage')
    sources = binding.get('sources')
    settings_parent = Path(binding.get('session', {}).get('path', '')).parent == (
        ROOT / '.private/ot0238c-settings-check-20261011/prestage-v2')
    full_parent = Path(binding.get('session', {}).get('path', '')).parent in (
        ROOT / '.private' / FULL_STAGE / 'prestage',
        ROOT / '.private' / REVOKE_STAGE / 'prestage')
    settings_sources = type(sources) is dict and (SETTINGS_NAME in sources or any(
        sources.get(name) == pin for name, pin in SETTINGS_AFTER_PINS.items()) or any(
            sources.get(name) == pin for name, pin in REVIEWED_CORE_AFTER_PINS.items()))
    cleanup_sources = type(sources) is dict and any(sources.get(name) == pin
        for name, pin in CLEANUP_AFTER_PINS.items())
    if binding.get('schema') != 'OT-VISIBLE-SESSION-BINDING-2':
        need(not cleanup_parent and not cleanup_sources and not settings_parent and not settings_sources and not full_parent, 'recovery_gate_invalid')
        return
    if 'predevice' not in binding:
        need(not cleanup_parent and not cleanup_sources and not settings_parent and not settings_sources and not full_parent, 'recovery_gate_invalid')
        return
    item = decode(read_pin(binding['predevice']))
    transition = item.get('runtime_transition') if type(item) is dict else None
    cleanup_kind = type(transition) is dict and transition.get('kind') == 'ot0238c-capture-mismatch-cleanup'
    settings_kind = type(transition) is dict and transition.get('kind') == 'ot0238c-retained-firmware-current-settings'
    full_kind = type(transition) is dict and transition.get('kind') in (FULL_KIND, REVOKE_KIND)
    if not full_kind and not full_parent and not cleanup_kind and not cleanup_parent and not cleanup_sources and not settings_kind and not settings_parent and not settings_sources:
        return
    try:
        path = ROOT / 'tools/enrollment_candidate_predevice_resume.py'
        pin = binding['sources']['enrollment_candidate_predevice_resume.py']
        raw = read_pin(dict(path=str(path), **pin))
        scope = {'__name__': '_ot_bound_predevice_recovery', '__file__': str(path)}
        exec(compile(raw, str(path), 'exec'), scope)
        guard = scope.get('require_recovery_gate')
        need(callable(guard), 'recovery_gate_invalid')
        result = guard(binding)
        if settings_kind or full_kind:
            need((full_parent if full_kind else settings_parent) and result is True, 'recovery_gate_invalid')
            return
    except Exception as error:
        raise Refused('recovery_baseline_unresolved' if str(error) == 'recovery_baseline_unresolved'
            else 'recovery_gate_invalid') from None
    raise Refused('recovery_gate_invalid')


def admit_plan(path, sha256):
    path = regular(path)
    private = regular(ROOT / '.private', directory=True)
    need(path.resolve().is_relative_to(private.resolve()), 'plan_invalid')
    raw = path.read_bytes()
    need(hashlib.sha256(raw).hexdigest() == sha256, 'plan_changed')
    value = decode(raw)
    need(type(value) is dict and set(value) == {'schema', 'worktree', 'host', 'entrypoint',
         'module', 'binding', 'review', 'session_nonce', 'readiness_root', 'session_output'}
         and value['schema'] == 'OT-OWNER-WORKFLOW-PLAN-1'
         and Path(value['worktree']).resolve() == ROOT.resolve(), 'plan_invalid')
    for key in ('host', 'entrypoint', 'module', 'binding'):
        read_pin(value[key])
    need(Path(value['host']['path']).resolve() == Path(sys.executable).resolve()
         and sys.dont_write_bytecode and not sys.flags.optimize, 'host_changed')
    need(Path(value['entrypoint']['path']) == ROOT / 'tools/enrollment_candidate_visible_session.py'
         and Path(value['module']['path']) == Path(__file__), 'source_invalid')
    binding = decode(read_pin(value['binding']))
    need(binding['worktree'] == value['worktree'] and binding['host'] == value['host']
         and all(binding[key] == value[key] for key in
                 ('session_nonce', 'readiness_root', 'session_output')), 'binding_invalid')
    need(type(value['session_nonce']) is str and re.fullmatch('[0-9a-f]{32}', value['session_nonce']),
         'nonce_invalid')
    for key in ('readiness_root', 'session_output'):
        directory = regular(value[key], directory=True)
        need(directory.resolve().is_relative_to(private.resolve()), 'path_invalid')
    review = decode(read_pin(value['review']))
    core = {key: item for key, item in value.items() if key != 'review'}
    need(type(review) is dict and set(review) == {'schema', 'plan_core_sha256', 'outcome',
         'findings', 'hardware_access', 'live_grants_created'}
         and review['schema'] == 'OT-OWNER-WORKFLOW-REVIEW-1'
         and review['plan_core_sha256'] == hashlib.sha256(canonical(core)).hexdigest()
         and review['outcome'] == 'no_unresolved_findings' and review['findings'] == []
         and review['hardware_access'] is False and review['live_grants_created'] is False,
         'review_required')
    require_cleanup_recovery(value['binding'])
    return value


def action_result(raw, action, *, returncode=0):
    """Operational actions print bounded JSONL before the final typed result."""
    need(type(raw) is bytes and 0 < len(raw) <= LIMIT and raw.endswith(b'\n'), 'output_invalid')
    try:
        rows = [decode(line) for line in raw.splitlines() if line.strip()]
    except Exception:
        raise Refused('output_invalid') from None
    need(rows and all(type(row) is dict for row in rows), 'output_invalid')
    result = rows[-1]
    expected = {'schema', 'action', 'outcome', 'category'}
    failed = result.get('outcome') == 'failed'
    if not failed and action in ('probe', 'attest', 'practice', 'revalidate'):
        expected.add('receipt')
    need(set(result) == expected and result.get('schema') == 'OT-VISIBLE-SESSION-RESULT-1'
         and result.get('action') == action, 'output_invalid')
    need(sum(row.get('schema') == 'OT-VISIBLE-SESSION-RESULT-1' for row in rows) == 1,
         'output_invalid')
    if failed:
        need(returncode != 0 and type(result['category']) is str
             and result['category'] in REFUSAL_CATEGORIES, 'action_refused')
        raise Refused(result['category'])
    need(returncode == 0 and result.get('outcome') == 'completed'
         and result.get('category') is None, 'action_refused')
    return result


def review_release(plan, path, prepared_utc, now):
    """Only exact independently supplied release; preclock review is separate."""
    root = Path(plan['session_output'])
    release = decode(regular(path).read_bytes())
    keys = {'schema', 'source', 'outcome', 'findings', 'session_nonce', 'binding',
            'handoff_admission', 'candidate_package', 'capture_grant', 'candidate_grant', 'received_utc'}
    need(type(release) is dict and set(release) == keys
         and release['schema'] == 'OT-OWNER-HANDOFF-REVIEW-RELEASE-1'
         and release['source'] == 'independent_current_handoff_review'
         and release['outcome'] == 'no_unresolved_findings' and release['findings'] == []
         and release['session_nonce'] == plan['session_nonce']
         and release['binding'] == plan['binding'], 'handoff_review_refused')
    stamp = release['received_utc']
    need(type(stamp) in (int, float) and math.isfinite(stamp)
         and prepared_utc <= stamp <= now, 'handoff_review_stale')
    for key, filename in (('handoff_admission', 'handoff-admission.json'),
                          ('candidate_package', 'candidate-package.json'),
                          ('capture_grant', 'capture-grant.json'),
                          ('candidate_grant', 'candidate-grant.json')):
        need(release[key] == reference(root / filename), 'handoff_review_changed')
    admission = decode(read_pin(release['handoff_admission']))
    need(admission['candidate_package'] == release['candidate_package'], 'handoff_review_changed')
    capture = decode(read_pin(release['capture_grant']))
    candidate = decode(read_pin(release['candidate_grant']))
    need(capture['capture_expires_utc'] - capture['issued_utc'] == 2100
         and capture['cleanup_expires_utc'] - capture['issued_utc'] == 3600
         and candidate['execute_expires_utc'] == capture['capture_expires_utc']
         and candidate['restore_expires_utc'] == capture['cleanup_expires_utc']
         and candidate['execute_expires_utc'] - now >= 1400, 'handoff_review_expired')
    return reference(path)


def closure_report(root, attempt, worker_raw):
    """Dispatch completion and actual trial outcome are separate evidence."""
    action_result(worker_raw, 'launch')
    rows = [decode(line) for line in worker_raw.splitlines() if line.strip()]
    actual = [row for row in rows if row.get('schema') == 'OT-ORIGINAL-CAPTURE-LAUNCH-1']
    need(len(actual) == 1 and set(actual[0]) == {'schema', 'mode', 'outcome', 'category'}
         and actual[0]['mode'] == 'capture' and actual[0]['outcome'] in ('passed', 'failed', 'held'),
         'trial_result_unknown')
    receipt = decode(regular(root / ('enrollment-original-runner-' + attempt + '.json')).read_bytes())
    need(receipt.get('schema') == 'OT-ORIGINAL-CAPTURE-RUNNER-RESULT-1'
         and receipt.get('attempt') == attempt and receipt.get('operation') == 'capture'
         and receipt.get('outcome') == actual[0]['outcome'], 'trial_result_unknown')
    released = (receipt.get('lease_released') is True and receipt.get('capture_custody_released') is True
                and all(receipt.get(key) is None for key in
                        ('cleanup_failure', 'candidate_cleanup_failure', 'capture_cleanup_failure'))
                and not (root / 'enrollment-original-active.json').exists()
                and not (root / 'enrollment-candidate-active.json').exists())
    return {'trial_outcome': actual[0]['outcome'], 'trial_category': actual[0]['category'],
            'candidate_outcome': receipt.get('candidate_outcome'), 'first_failure': receipt.get('first_failure'),
            'custody_release_verified': released, 'restoration_acceptance': 'requires_independent_review'}


class Workflow:
    def __init__(self, plan, *, run=subprocess.run, popen=subprocess.Popen,
                 monotonic=time.monotonic, utc=time.time, sleep=time.sleep, notify=print):
        self.plan, self.run, self.popen = plan, run, popen
        self.clock, self.utc, self.sleep, self.notify = monotonic, utc, sleep, notify
        self.root = Path(plan['session_output'])
        self.ready = Path(plan['readiness_root'])
        self.child = None
        self.closure = None
        self.stage = 'session_admission'
        self.predevice = True
        self.predevice_receipt = None

    def args(self, action, owner=None, handoff_review=None):
        need(action in ACTIONS, 'action_invalid')
        # Recheck exact frozen parent/entrypoint/binding before every dispatch.
        for key in ('host', 'entrypoint', 'module', 'binding'):
            read_pin(self.plan[key])
        args = [self.plan['host']['path'], '-X', 'utf8', '-B', self.plan['entrypoint']['path'],
                '--binding', self.plan['binding']['path'], '--binding-sha256',
                self.plan['binding']['sha256'], '--action', action]
        if owner is not None:
            need(action == 'attest', 'action_invalid')
            args += ['--owner-reply', owner['path'], '--owner-reply-sha256', owner['sha256']]
        need((handoff_review is not None) == (action == 'publish_handoff'), 'handoff_review_required')
        if handoff_review is not None:
            args += ['--handoff-review', handoff_review['path'], '--handoff-review-sha256', handoff_review['sha256']]
        return args

    def action(self, action, owner=None, handoff_review=None):
        self.stage = action
        self.notify('Starting ' + action + '.')
        try:
            result = self.run(self.args(action, owner, handoff_review), capture_output=True,
                              timeout=720 if action == 'probe' else 600 if action == 'practice' else 330)
        except subprocess.TimeoutExpired:
            raise Refused('action_timeout') from None
        need(type(result.stderr) is bytes
             and len(result.stderr) <= LIMIT, 'action_refused')
        return action_result(result.stdout, action, returncode=result.returncode)

    def wait_file(self, path, ceiling):
        while True:
            need(self.clock() < ceiling, 'wait_expired')
            if self.child is not None:
                need(self.child.poll() is None, 'launch_returned_before_handoff')
            if path.exists():
                return regular(path)
            self.sleep(min(0.25, max(0, ceiling - self.clock())))

    def held(self, ceiling, attempt):
        path = self.root / ('enrollment-original-' + attempt + '.json')
        while True:
            need(self.clock() < ceiling and self.child.poll() is None, 'held_wait_refused')
            if path.exists() and not path.with_suffix('.pending').exists():
                state = decode(regular(path).read_bytes())
                need(state['attempt'] == attempt and state['first_failure'] is None
                     and state['cleanup_failure'] is None, 'capture_failed')
                if state['phase'] == 'held' and state['capture_complete'] is True:
                    need(all(state['roles'][role]['entered'] is True
                             and state['roles'][role]['handles_closed'] is True
                             and state['roles'][role]['owner'] == 'capture' for role in ('A', 'B')),
                         'held_incomplete')
                    return
            self.sleep(0.25)

    def execute(self):
        """A failed sibling suppresses publish; existing worker retains cleanup."""
        require_cleanup_recovery(self.plan['binding'])
        marker = self.root / 'owner-workflow-start.json'
        with marker.open('xb') as handle:
            handle.write(canonical({'schema': 'OT-OWNER-WORKFLOW-START-1',
                         'session_nonce': self.plan['session_nonce'], 'binding': self.plan['binding']}) + b'\n')
        try:
            return self._execute()
        except BaseException as error:
            if self.predevice and self.predevice_receipt != 'saved':
                self.record_predevice_failure(error)
            raise

    def record_predevice_failure(self, error):
        """Terminal-only diagnostic; never modifies the two-file creation gate."""
        category = refusal_category(error)
        if category not in PREDEVICE_CATEGORIES:
            category = 'workflow_refused'
        try:
            stamp = self.utc()
            if type(stamp) not in (int,float) or not math.isfinite(stamp):
                stamp = None
        except Exception:
            stamp = None
        value = dict(schema='OT-OWNER-PREDEVICE-RESULT-2', outcome='refused',
            stage=self.stage, category=category, binding=self.plan['binding'],
            session_nonce=self.plan['session_nonce'], observed_utc=stamp, device_attempt_started=False)
        try:
            raw = canonical(value) + b'\n'
            with (self.root / 'owner-workflow-predevice-result.json').open('xb') as handle:
                need(handle.write(raw) == len(raw), 'workflow_refused')
            self.predevice_receipt = 'saved'
        except Exception:
            self.predevice_receipt = 'write_refused'

    def _execute(self):
        owner = self.ready / 'owner-reply.json'
        auth = self.root / 'owner-authorization.json'
        release = self.root / 'handoff-review-release.json'
        need(not owner.exists() and not auth.exists() and not release.exists()
             and not (self.root / 'capture-grant.json').exists(), 'session_used')
        binding = decode(read_pin(self.plan['binding']))
        if binding.get('schema') == 'OT-VISIBLE-SESSION-BINDING-2':
            from enrollment_candidate_predevice_resume import manifest, predecessor_idle, no_predecessor_process
            self.stage = 'predecessor_manifest'
            item, old = manifest(binding)
            self.stage = 'predecessor_idle'
            predecessor_idle(item, old, process_checker=no_predecessor_process)
            self.notify('Waiting for current approval before the readiness popup.')
            approval = self.root / 'owner-approval-intent.json'
            self.stage = 'waiting_current_approval'
            # This is parked computer coordination, not a popup, readiness or
            # device clock. Nothing operational starts while approval is absent.
            while not approval.exists():
                need({p.name for p in self.root.iterdir()} == {'owner-workflow-start.json'}, 'session_used')
                self.sleep(0.25)
            self.action('revalidate')
            need(auth.exists(), 'owner_authorization_missing')
        else:
            self.action('probe')
            self.notify('Send the actual word, changed row/column and readability in chat; click Continue immediately.')
            self.stage = 'waiting_owner_reply'
            self.wait_file(owner, self.clock() + 300)
            self.action('attest', reference(owner))
            self.action('practice')
            self.notify('Waiting for fresh exact trial authorization recorded from chat.')
            self.stage = 'waiting_trial_authorization'
            try:
                self.wait_file(auth, self.clock() + 300)
            except Refused as error:
                if str(error) == 'wait_expired' and {p.name for p in self.root.iterdir()} == {'owner-workflow-start.json'}:
                    with (self.root / 'owner-workflow-predevice-result.json').open('xb') as handle:
                        handle.write(canonical(dict(schema='OT-OWNER-PREDEVICE-RESULT-1', outcome='wait_expired',
                            phase='waiting_trial_authorization', binding=self.plan['binding'],
                            session_nonce=self.plan['session_nonce'], observed_utc=self.utc(),device_attempt_started=False)) + b'\n')
                    self.predevice_receipt = 'saved'
                raise
        self.predevice = False
        for action in ('baseline', 'route', 'issue_capture'):
            self.action(action)
        grant = decode(regular(self.root / 'capture-grant.json').read_bytes())
        need(type(grant['attempt']) is str and re.fullmatch('[0-9a-f]{32}', grant['attempt'])
             and grant['capture_expires_utc'] - grant['issued_utc'] == 2100
             and grant['cleanup_expires_utc'] - grant['issued_utc'] == 3600, 'grant_invalid')
        execute_ceiling = self.clock() + grant['capture_expires_utc'] - self.utc()
        restoration_ceiling = self.clock() + grant['cleanup_expires_utc'] - self.utc()
        worker_log = self.root / 'owner-workflow-launch.jsonl'
        screen_request = None
        with worker_log.open('xb', buffering=0) as log:
            self.child = self.popen(self.args('launch'), stdin=subprocess.DEVNULL,
                                    stdout=log, stderr=subprocess.STDOUT)
            try:
                self.held(execute_ceiling - 1400, grant['attempt'])
                self.action('status')
                prepared_utc = self.utc()
                self.action('prepare_handoff')
                candidate = decode(regular(self.root / 'candidate-grant.json').read_bytes())
                need(type(candidate['attempt']) is str and re.fullmatch('[0-9a-f]{32}', candidate['attempt']),
                     'grant_invalid')
                screen_request = self.root / ('enrollment-candidate-original-screen-' + candidate['attempt'] + '-request.json')
                self.notify('Handoff prepared; waiting for independent current review. Keep this window open.')
                self.wait_file(release, execute_ceiling - 1400)
                accepted = review_release(self.plan, release, prepared_utc, self.utc())
                need(reference(release) == accepted, 'handoff_review_changed')
                self.action('publish_handoff', handoff_review=accepted)
            finally:
                # No terminate/kill/retry/timeout: the maintained worker owns its
                # original restoration ceiling and must reach its own cleanup.
                announced = False
                while self.child.poll() is None:
                    need(self.clock() < restoration_ceiling, 'worker_cleanup_unresolved')
                    if not announced and screen_request is not None and screen_request.exists():
                        self.notify('Confirm in chat when both original usual screens are back.')
                        announced = True
                    self.sleep(0.25)
                self.child.wait()
                try:
                    self.closure = closure_report(self.root, grant['attempt'], regular(worker_log).read_bytes())
                except Exception:
                    self.closure = {'trial_outcome': 'unknown', 'custody_release_verified': False,
                                    'restoration_acceptance': 'requires_independent_review'}
        need(self.child.returncode == 0, 'launch_refused')
        action_result(regular(worker_log).read_bytes(), 'launch')
        need(self.closure['trial_outcome'] == 'passed', 'trial_failed_or_unknown')
        self.notify('Trial returned. Restoration evidence and owner confirmation still require review.')


def main(argv=None):
    parser = argparse.ArgumentParser(description='Reviewed fixed owner workflow; no arbitrary command arguments.')
    parser.add_argument('--plan', required=True)
    parser.add_argument('--plan-sha256', required=True)
    args = parser.parse_args(argv)
    workflow = None
    try:
        workflow = Workflow(admit_plan(args.plan, args.plan_sha256))
        workflow.execute()
        print(json.dumps({'schema': 'OT-OWNER-WORKFLOW-RESULT-1', 'outcome': 'completed', 'closure': workflow.closure}))
        return 0
    except BaseException as error:
        category = refusal_category(error)
        print(json.dumps({'schema': 'OT-OWNER-WORKFLOW-RESULT-1', 'outcome': 'refused', 'category': category,
                          'predevice_receipt': workflow.predevice_receipt if workflow is not None else None,
                          'closure': workflow.closure if workflow is not None else None}))
        return 1


if __name__ == '__main__':
    sys.exit(main())
