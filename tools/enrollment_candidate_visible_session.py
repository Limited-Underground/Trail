"""One fixed owner-terminal entrypoint. Never accepts commands or device data.

Probe/attest/practice are computer-only. Session actions require a fresh pinned
readiness record before importing the exact independently reviewed successor
template. The template retains separate fresh owner hardware authorization.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import sys

import enrollment_candidate_interactive_launch as visible

ACTIONS = ('probe', 'attest', 'practice', 'revalidate', 'baseline', 'route', 'issue_capture', 'launch',
           'prepare_handoff', 'publish_handoff', 'status')
OPERATIONAL = frozenset(('baseline', 'route', 'issue_capture', 'launch', 'prepare_handoff', 'publish_handoff'))
CONTINUATION = frozenset(('prepare_handoff', 'publish_handoff'))
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


def dispatch(binding_path, binding_sha256, action, owner_ref=None, *,
             readiness_checker=None, binding_admitter=visible.admit_binding,
             session_loader=None, handoff_review_ref=None):
    visible.need(action in ACTIONS, 'action_invalid')
    visible.need((owner_ref is not None) == (action == 'attest'), 'action_invalid')
    visible.need((handoff_review_ref is not None) == (action == 'publish_handoff'), 'handoff_review_required')
    if action == 'probe':
        return visible.probe(binding_path, binding_sha256)
    if action == 'attest':
        return visible.attest(binding_path, binding_sha256, owner_ref)
    if action == 'practice':
        return visible.practice(binding_path, binding_sha256)
    if action == 'revalidate':
        bound, _ = binding_admitter(binding_path, binding_sha256)
        visible.need(bound.get('schema') == 'OT-VISIBLE-SESSION-BINDING-2', 'action_invalid')
        from enrollment_candidate_predevice_resume import revalidate
        return revalidate(binding_path, binding_sha256)
    # Always check current calling process desktop, same logon, source closure
    # and freshness before importing any operational code, including baseline.
    if readiness_checker is None:
        readiness_checker = visible.require_status if action == 'status' else (
            visible.require_continuity if action in CONTINUATION else visible.require_ready)
    bound = readiness_checker(binding_path, binding_sha256)
    if handoff_review_ref is not None:
        visible.need(type(handoff_review_ref) is dict and handoff_review_ref.get('path') == str(Path(bound['session_output']) / 'handoff-review-release.json'),
                     'handoff_review_invalid')
        visible.read_reference(handoff_review_ref, private=Path(bound['session_output']))
    if action in OPERATIONAL:
        visible.require_practice(bound, binding_sha256)
    if session_loader is None:
        session_ref = bound['session']
        raw = visible.read_reference(session_ref, private=visible.ROOT / '.private')
        # Compile the exact admitted bytes rather than reopening a changed file
        # through an import loader between verification and execution.
        spec = importlib.util.spec_from_loader('_ot_reviewed_visible_session', loader=None)
        module = importlib.util.module_from_spec(spec)
        module.__file__ = session_ref['path']
        module.VISIBLE_BINDING = str(binding_path)
        module.VISIBLE_BINDING_SHA256 = binding_sha256
        module.VISIBLE_SESSION_OUTPUT = bound['session_output']
        module.VISIBLE_CONTINUATION = action in CONTINUATION
        module.VISIBLE_HANDOFF_REVIEW = handoff_review_ref
        exec(compile(raw, session_ref['path'], 'exec'), module.__dict__)
    else:
        module = session_loader(bound)
        module.VISIBLE_HANDOFF_REVIEW = handoff_review_ref
    readiness_checker(binding_path, binding_sha256)
    visible.need(callable(getattr(module, action, None)), 'session_invalid')
    return getattr(module, action)()


def main(argv=None):
    args = None
    class Parser(argparse.ArgumentParser):
        def error(self, message):
            raise visible.AdmissionError('arguments_invalid')
    parser = Parser(description='Reviewed owner-terminal readiness and fixed successor session; no grant issuer here.')
    parser.add_argument('--binding', required=True)
    parser.add_argument('--binding-sha256', required=True)
    parser.add_argument('--action', choices=ACTIONS, required=True)
    parser.add_argument('--owner-reply')
    parser.add_argument('--owner-reply-sha256')
    parser.add_argument('--handoff-review')
    parser.add_argument('--handoff-review-sha256')
    try:
        args = parser.parse_args(argv)
        visible.need((args.owner_reply is None) == (args.owner_reply_sha256 is None), 'arguments_invalid')
        visible.need((args.handoff_review is None) == (args.handoff_review_sha256 is None), 'arguments_invalid')
        visible.need((args.handoff_review is not None) == (args.action == 'publish_handoff'), 'handoff_review_required')
        owner = None
        if args.owner_reply is not None:
            owner = visible.reference(args.owner_reply)
            visible.need(owner['sha256'] == args.owner_reply_sha256, 'owner_reply_changed')
        handoff_review = None
        if args.handoff_review is not None:
            handoff_review = visible.reference(args.handoff_review)
            visible.need(handoff_review['sha256'] == args.handoff_review_sha256, 'handoff_review_changed')
        result = dispatch(args.binding, args.binding_sha256, args.action, owner,
                          handoff_review_ref=handoff_review)
        answer = {'schema': 'OT-VISIBLE-SESSION-RESULT-1', 'action': args.action,
                  'outcome': 'completed', 'category': None}
        if args.action in ('probe', 'attest', 'practice', 'revalidate'):
            answer['receipt'] = result
        print(json.dumps(answer, separators=(',', ':')))
        return 0
    except SystemExit:
        raise
    except BaseException as error:
        category = str(error) if type(error) in (visible.AdmissionError,visible.runtime.RuntimeErrorFixed) else 'visible_session_refused'
        if category not in REFUSAL_CATEGORIES:
            category = 'visible_session_refused'
        print(json.dumps({'schema': 'OT-VISIBLE-SESSION-RESULT-1',
                          'action': args.action if args is not None else None, 'outcome': 'failed',
                          'category': category}, separators=(',', ':')))
        return 1


if __name__ == '__main__':
    sys.exit(main())
