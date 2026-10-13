"""Explicit package-bound capture-only selection; no issuer or device authority.

The familiar CMD retains its full owner workflow. Its optional capture-only
branch stays closed until a separately prepared binding has a reviewed hash.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time

import enrollment_candidate_runtime as runtime

ROOT = Path(__file__).resolve().parents[1]
BINDING_RELATIVE = '.private/ot0238c-capture-selection-20261011/selection-binding.json'
UNPREPARED = 'UNPREPARED'
CORE_FILES = ('tools/enrollment_candidate_capture_runner.py', 'tools/enrollment_candidate_runtime.py',
              'tests/host/enrollment_candidate_capture_runner_tests.py',
              'tests/host/enrollment_candidate_runtime_tests.py')


class SelectionRefused(RuntimeError):
    pass


def need(condition, category):
    if not condition:
        raise SelectionRefused(category)


def read_ref(value, *, private=None):
    need(type(value) is dict and set(value) == {'path', 'bytes', 'sha256'},
         'selection_binding_invalid')
    return runtime.read_pin(value['path'], {k: value[k] for k in ('bytes', 'sha256')}, private=private)


def admit_core_review(value, sources):
    """Consume the prior closed-core approval, separate from selection review."""
    private = runtime.regular(ROOT / '.private', directory=True)
    review = runtime.decode(read_ref(value, private=private))
    need(type(review) is dict and review.get('schema') == 'OT0238C-CAPTURE-ONLY-INDEPENDENT-REVIEW-1'
         and review.get('outcome') == 'passed_host_only_no_open_findings'
         and review.get('open_findings') == [] and review.get('physical_acceptance') is False
         and review.get('baseline_verified') is True and review.get('source_hashes_verified') is True,
         'selection_core_review_required')
    need(type(review.get('worktree')) is str and Path(review['worktree']).resolve() == ROOT.resolve(),
         'selection_core_review_required')
    files = review.get('files')
    need(type(files) is list and len(files) == len(CORE_FILES), 'selection_core_review_required')
    checked = {}
    for row in files:
        need(type(row) is dict and set(row) == {'path', 'bytes', 'sha256'}
             and type(row['path']) is str, 'selection_core_review_required')
        relative = row['path'].replace('\\', '/')
        need(relative in CORE_FILES and relative not in checked, 'selection_core_review_required')
        pin = {k: row[k] for k in ('bytes', 'sha256')}
        runtime.read_pin(ROOT / relative, pin)
        if relative.startswith('tools/'):
            need(sources.get(relative) == pin, 'selection_core_review_required')
        checked[relative] = pin
    need(set(checked) == set(CORE_FILES), 'selection_core_review_required')
    return review


def admit_binding(path, sha256):
    """Read-only selection binding; maintained package preflight owns admission."""
    need(type(sha256) is str and runtime.HEX64.fullmatch(sha256), 'selection_unprepared')
    private = runtime.regular(ROOT / '.private', directory=True)
    expected = ROOT / BINDING_RELATIVE
    need(Path(path) == expected, 'selection_binding_invalid')
    path = runtime.regular(path)
    raw = path.read_bytes()
    need(len(raw) <= 32768 and hashlib.sha256(raw).hexdigest() == sha256,
         'selection_binding_changed')
    binding = runtime.decode(raw)
    need(type(binding) is dict and set(binding) == {'schema', 'worktree', 'workflow',
         'selector', 'sources', 'assembly', 'package', 'request', 'grant', 'core_review', 'review'}
         and binding['schema'] == 'OT-CAPTURE-SELECTION-BINDING-1'
         and binding['worktree'] == str(ROOT), 'selection_binding_invalid')
    need(binding['workflow'] == 'capture-only', 'selection_mode_invalid')
    need(binding['selector']['path'] == str(Path(__file__).resolve()), 'selection_source_changed')
    read_ref(binding['selector'])
    sources = binding['sources']
    need(type(sources) is dict and set(sources) == {'tools/' + n for n in runtime.SOURCE_NAMES},
         'selection_source_changed')
    for relative, pin in sources.items():
        runtime.read_pin(ROOT / relative, pin)
        # Parent-side admission uses these loaded source files, while the child
        # runs the isolated assembly. Both closures must match the review.
        runtime.read_pin(Path(__file__).parent / Path(relative).name, pin)
    admit_core_review(binding['core_review'], sources)
    review = runtime.decode(read_ref(binding['review'], private=private))
    core = {k: v for k, v in binding.items() if k != 'review'}
    need(type(review) is dict and set(review) == {'schema', 'binding_core_sha256',
         'outcome', 'findings', 'hardware_access', 'live_grants_created'}
         and review['schema'] == 'OT-CAPTURE-SELECTION-REVIEW-1'
         and review['binding_core_sha256'] == hashlib.sha256(runtime.canonical(core)).hexdigest()
         and review['outcome'] == 'no_unresolved_findings' and review['findings'] == []
         and review['hardware_access'] is False and review['live_grants_created'] is False,
         'selection_review_required')
    values = {k: read_ref(binding[k], private=private) for k in ('assembly', 'package', 'request', 'grant')}
    sidecar = runtime.decode(values['assembly'])
    need(sidecar.get('sources') == sources and sidecar.get('source_root') == str(ROOT),
         'selection_source_changed')
    package = runtime.decode(values['package'])
    need(package.get('operation') == 'capture-only' and package.get('handoff') is None,
         'selection_mode_invalid')
    need(all(package.get(k) == binding[k] for k in ('assembly', 'request', 'grant')),
         'selection_binding_invalid')
    from enrollment_candidate_original_capture import CURRENT_NVS_REQUEST
    need(runtime.decode(values['request']).get('schema') == CURRENT_NVS_REQUEST,
         'selection_mode_invalid')
    # Capture-only reuses the existing capture grant. It has no candidate grant.
    need(runtime.decode(values['grant']).get('operation') == 'capture', 'selection_mode_invalid')
    return binding


def launch_capture(path, sha256, *, utc=time.time, monotonic=time.monotonic,
                   assembly_verifier=runtime.verify_assembly, subprocess_run=None):
    binding = admit_binding(path, sha256)
    from enrollment_candidate_capture_runner import preflight
    prepared = preflight(binding['package']['path'], binding['package']['sha256'],
                         utc=utc, monotonic=monotonic, assembly_verifier=assembly_verifier)
    need(prepared.operation == 'capture-only' and prepared.handoff_path is None,
         'selection_mode_invalid')
    need(prepared.assembly.assembly_path == Path(binding['assembly']['path'])
         and prepared.assembly.assembly_sha256 == binding['assembly']['sha256']
         and prepared.assembly.worktree == ROOT, 'selection_binding_invalid')
    # Recheck the reviewed selection after preflight; runtime.launch performs
    # its own package and assembly admission immediately before child dispatch.
    need(admit_binding(path, sha256) == binding, 'selection_binding_changed')
    kwargs = dict(utc=utc, monotonic=monotonic, assembly_verifier=assembly_verifier)
    if subprocess_run is not None:
        kwargs['subprocess_run'] = subprocess_run
    return runtime.launch(binding['assembly']['path'], binding['assembly']['sha256'],
                          'capture-only', binding['package']['path'], binding['package']['sha256'], **kwargs)


def main(argv=None):
    class Parser(argparse.ArgumentParser):
        def error(self, message):
            raise SelectionRefused('selection_arguments_invalid')
    parser = Parser(description='Select retained/rekey or reviewed current-NVS capture-only.', allow_abbrev=False)
    parser.add_argument('--workflow', choices=('retained_rekey', 'capture-only'), default='retained_rekey')
    parser.add_argument('--plan')
    parser.add_argument('--plan-sha256')
    parser.add_argument('--binding')
    parser.add_argument('--binding-sha256')
    try:
        args = parser.parse_args(argv)
        if args.workflow == 'retained_rekey':
            need(args.plan is not None and args.plan_sha256 is not None
                 and args.binding is None and args.binding_sha256 is None, 'selection_arguments_invalid')
            import enrollment_candidate_owner_workflow as owner
            return owner.main(['--plan', args.plan, '--plan-sha256', args.plan_sha256])
        need(args.plan is None and args.plan_sha256 is None and args.binding is not None
             and args.binding_sha256 is not None, 'selection_arguments_invalid')
        result = launch_capture(args.binding, args.binding_sha256)
        print(json.dumps(result, separators=(',', ':')))
        return 0 if result['outcome'] == 'released' and result['category'] is None else 1
    except SystemExit:
        raise
    except BaseException as error:
        category = str(error) if type(error) is SelectionRefused else 'selection_runtime_refused'
        print(json.dumps({'schema': 'OT-CAPTURE-SELECTION-RESULT-1', 'outcome': 'refused',
                          'category': category}, separators=(',', ':')))
        return 1


if __name__ == '__main__':
    sys.exit(main())
