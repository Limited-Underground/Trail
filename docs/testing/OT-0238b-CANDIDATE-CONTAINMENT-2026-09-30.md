# OT-0238b candidate session containment - 2026-09-30

Approved revision 2, host evaluation only. OT-0238b remains In Progress.
This extends the [concrete session and rekey](OT-0238b-CANDIDATE-REKEY-2026-09-30.md)
with trusted local revocation and containment after committed factory-reset intent.
It does not complete factory reset or implement uncertain-state repair.

## Behavior and failure boundary

`revoke_local_membership` closes live traffic, retires volatile identity material,
releases the display/context owner and attempts a durable revoked membership.
It can run after cancellation, expiry or prior close; a stale enrollment request
cannot prevent safety cleanup or cancel a replacement request.

`contain_after_reset_intent` first retires live traffic, then requires the existing
reset marker to remain `intent_committed` with the exact captured receipt.
It independently attempts membership, enrollment-evidence and binding reset
tombstones. A failed domain or display release does not skip the other domains.
The hook only reads the marker. Authorization remains with the trusted local
control path; the existing factory-reset executor owns marker commit, all-domain
erasure, BLE bonds, completion and unowned-startup gating. Receipt zero remains valid for the
existing physical-reset route; it is compared exactly, not treated as invalid.

Terminal operations reconstruct the actual durable component owners after the
live endpoint and its dependent comparison/preparation objects are destroyed.
Every storage operation is guarded by this session's exact durable allocation
generation and, for reset, its unchanged marker. A stale owner cannot modify a
newer allocation. An unreadable, non-active or mismatched allocation generation
refuses mutation; this safety guard does not revalidate every allocator invariant.
Storage and marker adapters remain exclusively owned and serialized; physically
aliased wrappers or a marker supplied for a different device violate that adapter
contract. No live keys are reconstructed for terminal cleanup.

Results separately report volatile clearance, resource release, interrupted
callbacks, reset intent and each durable domain. They provide no overall
factory-reset-complete Boolean. Reentrant calls cannot resume traffic; interrupted
containment can still finish independently verified safety writes.

There is an important limit: a write that fails before the revocation intent is
durable can leave the previous committed public records unchanged. That outcome
is unavailable, not successful revocation. The old live session is closed, but a
later separately authorized fresh rekey may still be possible. Once a membership-
transition intent or tombstone is durably applied, partial or terminal records refuse old membership
reuse. No failure is repaired by erasing records or retrying enrollment in place.

## Lifecycle review

| Before | Action and required result | Failure behavior |
|---|---|---|
| Active, pending, expired or closed session | Stop traffic and retire volatile material | Resource failure reported separately; durable attempts still run |
| Trusted local revoke, exact current allocation | Commit and read back revoked membership | Pre-intent failure can preserve prior committed state; never claim revoked |
| Existing committed reset intent | Pin marker state/receipt and attempt all three tombstones | No pre-intent destructive write; changed marker refuses subsequent mutation |
| Repeated terminal action | Idempotent same state; revoke may escalate to reset | Reset cannot be weakened back to revoked |
| Callback, replacement request or newer generation | Preserve newer request and refuse stale allocation | Interrupted flag is explicit; traffic stays closed |
| Reconstruction after intent/terminal write | Reject incomplete or terminal retained state | No old-key resume, automatic repair or early factory-reset completion |

## Validation

One final current-source matrix passed **60 suites**, including **148 concrete
session groups**, 26 product activation groups and 183 product rekey groups.
Focused tests use actual membership/evidence/binding stores with injected storage
faults. They cover active/closed/expired sessions, retained rekey, pending enrollment,
idempotence, reset escalation, marker changes, stale allocation, replacement
requests, cleanup failures and callbacks. Existing component interruption tests
remain in the same matrix. Independent adversarial probes and lifecycle/source
review were bound to the final source hashes.

Drivers, transport, clocks, reset marker and local input remain host simulations.
No firmware target includes the changed candidate session header: all 18 target
include closures were checked. Host ownership, failure handling, original deadlines
and source pins were reviewed. Board configuration, target builds, RF, flashing,
restoration and physical acceptance do not apply to this host-only increment.
No target timing or hardware compatibility is established.

Private command, artifact and source evidence:
`.private/ot0238b-session-containment-20260930/closeout.json`, the adjacent
`security-matrix-final/result.json`, `focused-final-manifest.json` and
`scope-review.md`, plus `.private/ot0238b-session-containment-independent/`.
Documentation checker, documentation regression and diff-check results are
recorded separately in the closeout. Earlier increments remain unchanged.

## Next bounded work

Compose the existing factory-reset executor with this containment hook and a
concrete candidate-storage erase/absence adapter in host tests. Its complete
inventory must cover identity, journal, binding, boot, retained records and all
configured session tuples, including when the ledger is corrupt. Erase and verify
old identity before resetting related counters. Validate marker
commit before destructive work, interruption/reconstruction, verified absence and
no early pairing or completion. A reconstructed reset owner must gate all session
startup while reset intent or reconciliation remains, even when no tombstone was
saved. Only verified unowned state may permit fresh identity provisioning.
Simulated other domains and bonds must stay labeled.

Real device all-domain inventory/erasure, uncertain-state recovery protocol,
phone/device/transport adapters, target timing, production selection/final binding
and physical acceptance remain. No devices, cases or batteries were touched.
No V1 credit or public website capability changed. Changes are local/uncommitted;
Git publication is pending. Unrelated OT-0332 and concurrent HA work were preserved.
