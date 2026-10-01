# OT-0238b interrupted-enrollment recovery - 2026-09-30

Approved revision 2, host evaluation only. This batch implements the separately
reviewed [candidate recovery contract](../security/ENROLLMENT_CANDIDATE_RECOVERY_V1.md)
and exercises its result through the existing fresh exact-next-epoch session.
It extends the [candidate reset integration](OT-0238b-CANDIDATE-RESET-2026-09-30.md).
OT-0238b remains In Progress; this is not production or physical acceptance.

## Complete result and trust boundary

Recovery starts with a fresh authorized local request and coherent durable
operation records. An archived invitation and both original identity signatures
must verify, including the signature anchored to the device's current persistent
local identity. The exact pair, group, epoch, digest and derived operation must
match the pending journal. Received keys and CRC-valid records alone never
establish trust. The archive is historical proof, never a renewed invitation.

Fresh signed challenges bind the ordered peer states and new local contexts.
Only the verified, current, owner-bound result permits supported public-record
completion. No old traffic key or remembered ready flag is restored. A complete
fresh epoch+1 exchange still requires retained comparison, possession, invitation,
handshake, local transcript confirmation and authenticated activation before
bidirectional status delivery.

The contract defines the exact recoverable state pairs and ownership guards.
Missing proof on both peers, PREPARED records, torn/unreadable stores, unprovable
mixtures, mismatched operations, revocation and reset remain refusals. This
implementation does not erase uncertain evidence to retry, roll back epochs or
claim that every interruption is repairable. Partial recovery or subsequent
cancellation cannot grant traffic permission.

## Validation and limits

Final current-source matrix: **62 suites passed**.
New composed regression: **PASS 60 enrollment candidate recovery groups**.
Focused component checks, independent adversarial review, source-bound lifecycle
review and targeted mutant results are retained in the evidence below. One full
matrix was run on the final implementation; documentation checks are separate.

The fixtures use the actual candidate session, cryptographic operations, durable
coordinator and persistence owners. Physical storage, radio/phone delivery,
trusted display/input, entropy providers and timing remain host simulations.
Passing computer tests does not establish target resource/timing behavior,
real adapters, field reliability or physical recovery acceptance.

The include review covered all 18 firmware targets and found no affected target
or component translation unit. No target rebuild was required for this change.

Exact source hashes, commands, outputs, artifacts and Git state:
`.private/ot0238b-recovery-20260930/closeout.json`; adjacent
`security-matrix-final/result.json`, `focused-final-manifest.json`,
`scope-review.md` and `include-impact.json`. Independent probes:
`.private/ot0238b-recovery-independent/review-manifest.json`.

Next: compose the existing leased display/input adapter into the complete
candidate session. Physical drivers, source-bound target lifecycle/timing,
production selection/binding and separately authorized physical gates remain.
Unsupported recovery states retain their
explicit refusal policy; no automatic repair is inferred.

No devices, cases or batteries were touched. No V1 completion credit or public
website capability changed. Changes remain local/uncommitted; Git publication
is pending. Unrelated OT-0332 work and shared HomeAssistant focus are preserved.
