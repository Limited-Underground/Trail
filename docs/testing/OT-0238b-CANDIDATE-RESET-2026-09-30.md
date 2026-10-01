# OT-0238b candidate reset integration - 2026-09-30

Approved revision 2; host evaluation only. OT-0238b remains In Progress.
This integrates the existing `DeviceFactoryResetExecutor` with the concrete
candidate session and storage inventory. It extends
[session containment](OT-0238b-CANDIDATE-CONTAINMENT-2026-09-30.md), without
selecting production cryptography, wiring a device or inventing a second reset
state machine. Destructive authorization remains upstream with the accepted
current-owner confirmation or local physical gesture.

## Complete sequence and ownership

`EnrollmentCandidateReset` supplies the integrated session-construction path,
binding the same storage inventory, capacity and reset gate automatically.
The existing ungated session constructor remains a legacy isolated evaluation
path; it is not this integrated reset guarantee. Gate, ports and storage outlive
every referencing session and share exclusive, serialized same-device ownership.

| Boundary | Required effect |
|---|---|
| Startup or reconstruction | Restore the actual executor and freshly observe the marker; unresolved reset/reconciliation, reboot and receipt phases deny session startup |
| Admitted reset | Retire live traffic before destructive work; failed marker commit performs no reset erasure or additional durable mutation beyond ordinary session close |
| Committed intent | Guard each delegated mutation with the exact live marker state/receipt; ambiguous or changed authority refuses continuation |
| Candidate deletion | Erase and verify old identity before related counters; erase external journal/binding/boot and every backend namespace/domain/slot through fixed configured capacity |
| Remaining user data and bonds | Invoke explicit separate ports and verify absence; no candidate-only tombstone can count as whole-device completion |
| Completion and reconstruction | Verify all declared domains before marker completion/receipt consumption; only verified unowned state permits the normal fresh-pairing workflow |

The inventory includes generation zero and unallocated/orphan generations;
corrupt ledger contents cannot reduce the wipe extent. A reset incarnation fence
prevents an old session from regaining authority when counters restart after
reset. Fresh pairing/enrollment still requires its own authorized request.
No reset marker, correlation receipt or Boolean is itself owner authorization.

Ordinary session close retires RX and activation metadata even when the reset
marker cannot be committed. Failed-marker tests compare every candidate slot
with an equivalent ordinary-close baseline, rather than incorrectly requiring
an active traffic lease to survive reset preparation. Identity, membership and
other saved authority are not erased before verified reset intent.

Interrupted erasure retains the reset marker and blocks normal startup. The
existing executor resumes cleanup after reconstruction; a refused or incomplete
readback never becomes successful reset. This explicit reset route does not
implement automatic repair of uncertain enrollment commits or resume old keys.

## Validation and limits

The final current-source matrix passed **61 suites**:
- PASS 32 enrollment candidate reset groups
- PASS 148 enrollment candidate session groups

Focused component regression also covers the unchanged factory-reset executor.
Independent adversarial review and the lifecycle/inventory review are pinned to
the final sources. Their exact cases, commands, initial failures and final
outcomes are retained in the private evidence below. Documentation and diff
checks are recorded separately in the closeout.

Actual candidate component owners and the existing executor run in these tests.
Storage drivers, reset-marker persistence, other user domains, BLE bond storage,
local input and reboot remain simulated. Real-device all-domain inventory,
erase behavior, target timing and physical acceptance are not established.
No target includes the new composition; target-closure evidence is in the scope
review. No firmware target build or physical trial is applicable to this increment.

Evidence: `.private/ot0238b-reset-executor-20260930/closeout.json`, adjacent
`security-matrix-final/result.json`, `focused-final-manifest.json` and
`scope-review.md`; independent probes:
`.private/ot0238b-reset-executor-independent/review-manifest.json`.

Next candidate boundary: reconcile and implement the accepted recovery behavior
for uncertain or one-sided enrollment commits, using fresh authenticated state
comparison. Refusal alone is containment; no old-key resume, epoch rollback or
erase-to-retry may be treated as recovery. Real peer/phone/device adapters,
production selection/final binding and physical validation remain unfinished.

No devices, cases or batteries were touched. No V1 credit or public website
capability changed. Changes are local/uncommitted; publication is pending.
Unrelated OT-0332 work and the shared HomeAssistant focus remain preserved.
