# OT-0238b trusted review device-port candidate

## Result and limits

A concrete host adapter now produces local review samples from a trusted device
IO boundary. It is not wired to a deployed target. A bounded eight-row layout
preserves all 64 identity digits (or all eight comparison-code digits), the full
64-bit group identifier, role, purpose and button instruction on the existing
21-column panel geometry. Both actual enrollment owners now supply the group.

## Complete ownership flow

| Transition | Required evidence | Failure behavior |
|---|---|---|
| Acquire | Fresh exclusive lease, revision zero, valid boot/request/generation | Poison adapter and attempt release |
| Render | Canonical full frame and exactly next revision; fresh context/lease/time before and after trusted rendering | No review authority on failure or stale readback |
| Observe | Same context, lease and displayed revision, monotonic clock, no reset preemption | Invalid sample closes review |
| Confirm | Stable released input on every page, then a new debounced press and release consumed by the actual review owner | Held entry and page-switch gestures cannot confirm |
| Cancel/preempt | Release or observe preemption; verify old lease is gone | Release failure cannot report safe handoff |
| Return input | Subsequent stable released button after verified release | Reset cannot reuse the enrollment gesture through this handoff |

Debounce is 20 ms. The existing actual review owner governs the one-second hold.
Callbacks are serialized; reentry poisons authority and defers cleanup to the
outer operation. Release is attempted once. IO must outlive the adapter. These
contracts do not establish thread safety or physical display correctness.

## Target preflight and remaining integration

The current target routes GPIO0 through factory-reset input and refreshes normal
status separately. Startup display ownership currently covers PIN/reset overlays,
not enrollment. All writers and input consumers need one actual target arbiter;
this host adapter defines its boundary but does not implement that arbiter.
Protected companion routing currently exposes only an evaluation confirmation
profile, not production enrollment. The candidate identity seed remains unsealed,
and existing factory reset does not cover every new enrollment storage domain.

Before target wiring: settle protected identity storage and reset coverage, bind
product request routing, connect sole display/input ownership, and account for
all new reads/signatures in the complete timing budget. Target build, size and
physical acceptance remain pending. No hardware execution is authorized here.

## Validation

Focused native tests: 36 device-port groups and 21 layout groups passed. They use
the actual fingerprint review consumer; layout fixtures also obtain the actual
activation transcript frame. Controls cover held entry, bounce, page changes,
context/lease/revision loss, reset preemption, clock rollback, render/readback
failure, callback reentry, cancellation, failed release and destructor cleanup.
Malformed domains, purposes, digits and hidden trailing content reject without
changing the output layout. Independent bounded review found no blocking defect.

Final actual-source matrix: **56 suites passed** with source pins unchanged.
Native GCC 16.1.0 / Python 3.14, OPENTRAIL_MSYS2_ROOT=C:/msys64:

`C:/Python314/python.exe -X utf8 -B tests/host/security_current_source_ci.py --output-root C:/lu/OpenTrail/.private/ot177-publication/.private/ot0238b-review-port-final-matrix-v2`

The first matrix stopped because the new test printed a different success prefix;
its assertions passed. Only that output prefix changed before the complete rerun.
The result/commands and hashes are retained privately. Documentation checks and
18 regressions passed; the 11-file publication scan and scanner regressions passed.
Conservative include traversal across 18 targets found no target reaching these
candidate changes; no target build is claimed. No V1 credit, public
website status change, physical rendering claim or completed enrollment claim.
OT-0238b remains In Progress.
