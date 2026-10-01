# OT-0238b candidate sequencing and guarded review handoff

2026-09-30. Host evaluation increment; full enrollment remains unfinished.

## Result and approved sequence

The owner authorized the remaining base V1 firmware tasks and this sequencing
correction. The hosted checklist now separates an isolated enrollment candidate
from the later selected production implementation. Six current revisions retain
approval: OT-0238b, OT-0005i, OT-0237b/d/e revision 2 and OT-0238a revision 4.
OT-0238b is In Progress. No task completion was accepted by this change.

This supersedes the unresolved sequencing proposal in the
[September 29 execution-gate review](OT-0238b-EXECUTION-GATE-2026-09-29.md).
The explicit dependency graph is unchanged:

1. OT-0238b completes the isolated candidate using actual components.
2. Existing lifecycle, entropy, persistence, retirement and reset tasks gather
   the missing evidence against its exact frozen composition.
3. OT-0237e retains all eight admission gates, historical Monocypher custody
   disposition, source/license/corpus binding and explicit production selection.
4. OT-0005i explicitly retains final production enrollment binding alongside
   selected secure transport, including revalidation wherever changed production
   inputs invalidate candidate evidence. OT-0238a cannot complete before it.

Approval to perform a security review is not the review's selection outcome.
No security gate, required device evidence or final product requirement was
removed. The [remaining task plan](../../tasks/V1_REMAINING_PLAN.md) records the
revised scopes; the [backlog](../../tasks/BACKLOG.md) retains the existing IDs.

## Implemented boundary

`enrollment_candidate_preparation.hpp` composes the existing selected request,
identity store and fingerprint review. The public candidate is a bounded,
versioned in-memory evaluation value, not a new BLE or radio wire format. Its
producer exports the public identity through the actual identity store. Version,
profile, nonzero identity and self-identity checks do not establish peer trust.

An exact-request-bound local authority supplies role, group, allocated review
context and clock-domain identity. This trusted application/device seam is
simulated in the host tests; a production adapter does not exist yet. Candidate
input cannot choose role/group or mint physical confirmation. Both endpoints
still need the full local fingerprint review and fresh possession proofs.

| Transition | Required effect | Rejection and containment |
| --- | --- | --- |
| Pending selected request to candidate | Capture exact authority, connection, delivery token, exchange, admission time and original deadline | Missing/replaced request, invalid local intent or clock domain refuses |
| Candidate to local display review | Obtain own identity from the store, bind untrusted peer and local role/group; begin within original request deadline | No new 120-second budget; malformed profile, self identity, changed context or late display refuses |
| Display review to local receipt | Existing full-fingerprint page and hold/release rules produce one consumed receipt | Page/context/revision change, input replay, cancellation or expiry cannot confirm |
| Receipt to preparation | Receipt retains its originating guarded port; actual preparation, fresh possession and signed invitation binding use that port | Substituting a raw port consumes/refuses the receipt; disconnect, replacement and identity retirement suppress output |
| Failure or destruction | Retire this attempt and cancel only its exact captured request | A stale attempt cannot cancel a newer request; no membership or traffic authority is created |

The request and review clocks must be the same device monotonic clock domain.
Samples are bracketed by current authority observations and checked for rollback,
expiry and context drift. The inherited deadline is also carried in the opaque
review receipt. Existing standalone review callers retain their bounded API.
Serialized dependencies and the guarded port must outlive preparation. Terminal
observations are read-only; the new refusal tests additionally cover observable
callback cancellation, reentry and identity retirement.

## Independent review and validation

Independent review found two defects in the first implementation and required
actual failing regressions before correction:

- A review receipt could be passed to preparation with an unguarded raw port.
  The receipt now retains its originating port across moves and refuses a
  different one, including reuse after failed consumption.
- Identity retirement during the final authority observation could publish a
  challenge. A final nondelegating identity-failure check now rejects the
  operation and preserves output. The independent before/after reproduction
  observed success/output change before the fix and refusal/unchanged output
  afterward.

Focused current-source tests passed 45 candidate-preparation groups and 38
fingerprint-review groups, warning-free. They exercise two actual identity
stores, full local review receipts, dual possession proofs and signed invitation
authorization, plus malformed versions/profiles, self identity, request reuse,
role/group changes, late handoff, expiry, disconnect, rollback and callback faults.
The new suite is registered in the maintained current-source security matrix.

The final fresh current-source security matrix passed all **59 suites** from
unchanged final inputs, including the 45/38 groups above. MSYS2 UCRT64 GCC/G++
16.1.0 rebuilt the admitted scalar/signing dependency and all affected test
executables. SDK, entropy, storage and physical-I/O seams remain simulated.
The repository documentation checker, all 18 documentation regressions and
`git diff --check` pass. Independent source and scope reviews found no remaining
blocking issue after the two corrections.

Validation commands from the active checkout:

```powershell
$env:OPENTRAIL_MSYS2_ROOT='C:\msys64'
& C:\Python314\python.exe -X utf8 -B tests/host/security_current_source_ci.py --output-root C:\lu\OpenTrail\.private\ot177-publication\.private\ot0238b-sequencing-20260930\security-matrix-final
& C:\Python314\python.exe -X utf8 -B tools/check_repository_docs.py
& C:\Python314\python.exe -X utf8 -B tests/host/repository_docs_tests.py
git diff --check
```

The matrix output directory is immutable evidence; a rerun must use a new
initially absent directory. Exact commands, source/artifact hashes, initial
fixture failures and discriminating before/after regression results remain in
the private task evidence bundle. Changes are local and uncommitted.

## Target preflight and limits

Independent transitive include review inspected all 18 firmware targets and 466
firmware source/header files, including shared source definitions and existing
standard/confirmation dependency captures. None includes the three changed
evaluation headers. Firmware target/build/reproducibility checks are therefore
not applicable to this host-only increment; this is not a target build claim.

The applicable porting checks are serialized ownership, original deadline,
callback/failure containment, source-bound host validation and byte-preserving
edits. Board identity, region, offsets, physical entropy, UART/USB custody,
restoration and hardware acceptance checks were not executed because no target
or device is changed. Existing target identity startup remains load-only.

No Android command, BLE/radio candidate encoding, target identity provisioning,
physical display/input observation, invitation transport, product activation or
complete retained recovery is delivered here. The current Android app cannot
use this in-memory handoff. This increment ends at host preparation/trusted
invitation binding and cannot complete OT-0238b or earn V1 credit.

Next: implement and host-test the candidate's concrete local intent and peer
exchange producer using the accepted workflow, then compose the retained-state,
activation and reset owners. Keep the selected production adapter and contract
under OT-0005i after admission. Before any later trial, prepare a new exact image
and restoration extent; the prior restoration span is not reusable by assumption.
Cases and batteries remain untouched. No Git publication, physical execution or
public website capability change is part of this increment.
