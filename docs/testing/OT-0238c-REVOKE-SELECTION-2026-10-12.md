# OT-0238d explicit revoke selection: host validation

Execution date: 2026-10-13. The approved report filename retains 2026-10-12;
that filename is not the execution date.

Canonical project: `C:\lu\OpenTrail`.
Active worktree: `C:\lu\OpenTrail\.private\ot177-publication`.

## Diagnosis and bounded change

The predevice resume selection gate admitted only `first` and `retained_rekey`,
although the existing controller already handles the explicit `revoke` case.
OT-0238d adds exactly `revoke` to that gate, with the same strict integer
`group == 1` requirement. Unsupported cases, Boolean groups and other group
values remain refused.

The selected case still must match a fresh owner intent for the exact binding.
Current readiness must pass before deriving the authorization, and the derived
authorization retains the selected `revoke` case and group. Selection and intent
references are rechecked after the readiness confirmation. Existing package
admission, source pins, predecessor evidence, deadlines, consumed-predecessor
refusals and one-use/reissue checks retain their existing implementation.

Only these three files belong to this micro-change:

- `tools/enrollment_candidate_predevice_resume.py`
- `tests/host/enrollment_candidate_predevice_resume_tests.py`
- `docs/testing/OT-0238c-REVOKE-SELECTION-2026-10-12.md`

## Focused host evidence

VERIFIED on 2026-10-13: the following command completed with exit code 0,
50 tests passing in 75.085 seconds, from the active worktree:

```powershell
& 'C:\Python314\python.exe' -X utf8 -B tests/host/enrollment_candidate_predevice_resume_tests.py
```

The [focused output](../../.private/ot0238d-revoke-selection-20261013/focused-resume-tests.log)
contains the individual test results. The fixtures use synthetic provenance,
injected readiness and process checks, and temporary files. They do not invoke
native UI, SDK or device operations.

Coverage includes exact revoke readiness and derived authorization; every
cross-case intent mismatch among `first`, `retained_rekey` and `revoke`;
unsupported cases and invalid group types/values; stale or malformed intents;
changed selection/intent during confirmation; cancel, failed clearing, current
scope loss and expiry; consumed predecessor refusal; and fresh-readiness expiry
without renewing a held anchor or reissuing authorization. Existing historical
provenance and runtime-transition tests also pass in that focused suite.

The root controller owns the required final host matrix and independent review
after the implementation files are frozen. The remaining declared commands are:

```powershell
& 'C:\Python314\python.exe' -X utf8 -B tests/host/enrollment_candidate_owner_workflow_tests.py
& 'C:\Python314\python.exe' -X utf8 -B tests/host/enrollment_candidate_settings_successor_tests.py
& 'C:\Python314\python.exe' -X utf8 -B tools/check_repository_docs.py
& 'C:\Python314\python.exe' -X utf8 -B tests/host/repository_docs_tests.py
```

The root controller reuses the passing focused resume result only after
verifying its source/test pins against the frozen files. The
[final matrix receipt](../../.private/ot0238d-revoke-selection-20261013/validation.json)
and [independent review](../../.private/ot0238d-revoke-selection-20261013/independent-review.json)
own their final outcomes. Pending receipt placeholders are not passing evidence;
the four remaining checks and review are not inferred from this focused run.

## Evidence limits and next gate

This is host validation only; host success is not physical acceptance. No runnable
package, live grant, launcher repoint, SDK/native action, firmware build or device
attempt was prepared or executed by this task. Firmware, controller, runner,
protocol and package authority remain outside the change.

A source change does not admit an existing pinned package. A separately scoped
successor preparation and its own admission/review are required before any later
owner-authorized physical revoke attempt. Exact recovery and restoration gates
remain mandatory for that future attempt.

No V1 completion or public website capability status changes. Git publication
and device acceptance are separate operations and are not established by this
host report.
