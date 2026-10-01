# OT-0238b concrete first-enrollment candidate session

Host evaluation only, under approved checklist revision 2. This continues the
[guarded request handoff](OT-0238b-CANDIDATE-HANDOFF-2026-09-30.md) and the
[accepted enrollment design](../security/PRODUCT_ENROLLMENT_REKEY_DRAFT_V1.md).
OT-0238b remains In Progress. No production cryptography selection, firmware
installation, Git publication or V1 completion credit is supplied by this work.

## Integration boundary

The earlier handoff tests supplied local intent and fabricated boot/session
values. They established the handoff's guards, but did not establish a usable
continuation into the actual activation endpoint. The new
`EnrollmentCandidateSession` owns the first-enrollment operation instead of
leaving that orchestration in a test fixture.

The serialized owner captures the exact selected phone request and a deliberate
local role/group choice. It composes the actual identity store, durable generation
allocator, generation-specific storage views, fingerprint review, possession
proofs, invitation binding and `ProductEnrollmentActivation`. Endpoint preparation
supplies the real boot context and ephemeral contribution used throughout the
exchange. Peer input never selects the local role or group.

Public candidate, possession offer and clock-mark values are bounded in-memory
evaluation inputs. They are not a BLE/radio codec, production protocol selection
or evidence of transport delivery. The current Android app and device targets
do not call this owner. The device-port context binding is a trusted local I/O
adapter seam; physical input/display, entropy and storage drivers remain simulated.

## Lifecycle and failure handling

| Transition | Required effect | Failure boundary |
| --- | --- | --- |
| Live request to local start | Capture original request/role/group; reserve a real durable generation | Invalid authority, retained state or ambiguous allocation refuses; no fresh deadline |
| Candidate to local review | Freeze one peer identity; derive boot/session context from the real endpoint | Self identity, incompatible value or replacement candidate cannot continue |
| Review to possession | Consume the full-fingerprint local receipt and exchange fresh signed challenges | Received bytes and phone Booleans cannot establish peer trust |
| Possession to invitation | Bind actual contributions, roles, group and separately observed local clocks | Peer timestamps never replace local time; signed local windows stay at 60 seconds |
| Handshake to activation | Authenticate endpoint exchange and require local transcript confirmation plus peer activation controls | Pending request checks surround operations and run inside final journal commit |
| Durable commit to traffic | Read back committed membership; consume only the exact pending request | No successful completion or traffic on failed/ambiguous commit; later phone disconnect does not remove committed membership |
| Close or restart | Clear live secrets and release display ownership; preserve durable evidence | No erase-to-success or resumption of old traffic keys; retained recovery is a separate route |

Pending work stays within both the original 120-second request deadline and the
signed local invitation window. A late invitation does not extend preparation.
Cleanup still runs after cancellation or expiry. The serialized backing stores
must be physically isolated; wrapper types cannot detect deliberately aliased
backend namespaces.

The optional activation lifecycle callback carries the session's live request
guard into the irreversible journal transition. Cancellation observed before a
write refuses it. A cancellation occurring during an already executed write can
leave durable public state; failure does not promise rollback or erase it. The
fresh-session owner refuses retained state and must not be presented as a repair
path. Tests assert the actual durable state after each injected fault.

## Validation

The focused suite passes 61 groups. Two independent simulated devices use actual
component code for fingerprint review, dual possession proofs, invitation, handshake,
local transcript confirmation, durable activation and eight authenticated status
deliveries. Their clocks differ by 50 seconds. Post-commit phone disconnect keeps
the radio session usable. Late invitation preserves the original request limit.
The suite also covers malformed/substituted inputs, failed reads, retained-state
refusal, request replacement, reentry, cancellation, expiry and cleanup failures.

Independent review reproduced a final-save ordering defect: cancellation from a
late generation-ledger read occurred after the first lifecycle sample, allowing
the completion marker to be written. A terminal lifecycle check now runs after
the delegated generation/endpoint reads. The discriminating regression fails
before the correction and passes afterward. Known cancellation before the marker
prevents its write; cancellation inside the marker's already executed storage
call can retain committed public evidence, but returns failure, clears live keys
and prevents this fresh-only owner from restarting against it. Incomplete journal
headers can refuse initialization altogether; no readable RECONCILE state is
invented. One out-of-range byte index in a new negative test was also corrected.

Two early matrix attempts were stopped after review changed test inputs;
they are retained as interrupted evidence, not passes. The next run reached a
test-output integration error: the new suite exited successfully but did not emit
the runner's required initial `PASS` marker. The suite's reporting was corrected;
no test assertion or runner gate was relaxed. The final fresh current-source
matrix passes all 60 suites, including the 61 session groups, with unchanged
source hashes throughout. GCC/G++ 16.1.0 rebuilt the admitted signing dependency
and test executables. SDK, entropy, clocks, transport and storage drivers remain
simulated. Independent source and scope reviews have no remaining blocking finding.

The repository documentation checker, 18 documentation regressions and
`git diff --check` pass. Exact commands, artifacts,
source hashes and interrupted/negative results remain in the private closeout
bundle. The current-source run used:

```powershell
$env:OPENTRAIL_MSYS2_ROOT='C:\msys64'
& C:\Python314\python.exe -X utf8 -B tests/host/security_current_source_ci.py --output-root C:\lu\OpenTrail\.private\ot177-publication\.private\ot0238b-session-20260930\security-matrix-final-r4
& C:\Python314\python.exe -X utf8 -B tools/check_repository_docs.py
& C:\Python314\python.exe -X utf8 -B tests/host/repository_docs_tests.py
git diff --check
```

The matrix directory is immutable evidence; a later rerun must use a new empty
output path. Changes remain local and uncommitted; publication is pending.

## Storage cost and target preflight

A bounded private diagnostic counts the actual storage-read callbacks through
the complete passing host operation. It observes 78,418 reads for both devices
through enrollment, one Ready observation each, eight delivered statuses and
cleanup. The two durable commits account for 53,960 reads. One active send uses
673 reads and its receive uses 663. These are host callback counts, not measured
NVS reads or elapsed time. They include no driver cost, transport scheduling or
human reading time. Exact target cost/window budgeting must precede a hardware
trial; this result does not establish physical timing or justify longer windows.

Independent reverse-include review covers 467 firmware source/header files and
all 18 targets. None includes the five changed/new evaluation headers. Target
builds and reproducibility checks therefore do not apply to this host increment.
The applicable porting checks are serialized ownership, storage-ownership boundaries,
deadline/failure handling, source-bound validation and byte-preserving edits.
Board identity, RF configuration, offsets, physical entropy, USB custody,
restoration and physical acceptance are unexecuted because no target or device
is changed. The production identity startup remains load-only.

## Remaining boundaries

This owner covers first enrollment only. Retained comparison/rekey owners already
have separate component evidence, but their orchestration with current request,
authority, storage and lifecycle owners still needs integration. Uncertain-state
recovery, revoke/reset composition, real Android/device/transport adapters,
physical timing/usability and final production binding remain explicit work.
Final production selection stays under OT-0237e and binding under OT-0005i.

No public website capability or weighted V1 progress changed. Devices, cases and
batteries were untouched. Existing unrelated OT-0332 planning is preserved.
