# Enrollment candidate recovery - V1 host contract

OT-0238b revision 2, evaluation implementation contract. This completes a bounded
recovery path under the accepted [enrollment/rekey design](PRODUCT_ENROLLMENT_REKEY_DRAFT_V1.md).
It selects no production cryptography, wire format, device adapter or trust root.
Implementation and validation results belong in the linked task evidence.

## Result and authority

Recover an exact interrupted public commitment, then run the existing complete
fresh epoch+1 enrollment flow before allowing traffic. Recovery never resumes
old session keys, revives the archived invitation or treats public membership as
traffic permission. Both devices require a new authorized local request.

The immutable archive contains the original invitation and both identity-binding
signatures. Verify its canonical encoding, inviter signature and BOTH binding
signatures; the local signer must equal the device's currently loaded persistent
identity. That existing local signature attests the exact peer, ordered roles,
group and invitation. The peer may transport the archive but cannot select a new
root. Journal checksums provide exact continuity and corruption detection, not
authentication. Missing identity must not trigger provisioning during recovery.

The archive must reproduce the locally recorded pair, group, epoch and evidence
digest, with operation ID recomputed as the digest's first 16 bytes. A signed but
different historical operation is insufficient.
An expired archive is historical evidence only: no endpoint receives it as a live
invitation. Existing physical-flash threat limits remain unchanged.

## Recoverable durable states

| Local / peer journal | Required local storage | Permitted result |
|---|---|---|
| ACTIVATION_POSSIBLE / ACTIVE_COMMITTED, either direction | Coherent exact target records, or one of the pending cases below | Fresh authenticated recovery may finish the pending public commitment. |
| ACTIVATION_POSSIBLE / ACTIVATION_POSSIBLE | Same exact operation and a verifiable archive available to both | Both may finish public commitment; neither claims old traffic was active. |
| ACTIVE_COMMITTED / ACTIVE_COMMITTED | Complete coherent exact target records | Idempotent public reconciliation; ordinary fresh rekey remains the traffic path. |
| PREPARED, empty, unavailable or mismatched operation | Any | No recovery commit under this protocol. |

ACTIVATION_POSSIBLE means the exact durable stage before reconstruction maps it
to RECONCILE. A typed read-only observation may expose that stage; no caller can
set it or manufacture a commit receipt. Local allocation generations are local
facts and need not equal the peer's generation.

Pending local records must be one of: all membership/evidence/binding empty at
epoch 1; coherent target archive with exact target membership (or empty membership
at epoch 1); or a coherent, cryptographically verified immediate predecessor
archive with matching active membership, same pair/group and exact next epoch.
The existing verifier's predecessor checks also require new nonce and ephemeral
contributions; an epoch increment alone does not establish a valid transition.
Unprovable mixtures, torn/unreadable stores, arbitrary active membership digests,
revoked/reset-pending membership and unresolved reset markers refuse recovery.
Neither side having the archive is also a refusal. Refusal preserves evidence;
there is no erase-to-empty fallback or invented predecessor.

## End-to-end transitions and ownership

| Boundary | Required effect and guard |
|---|---|
| Local request | Close prior traffic owners; bind locally chosen role/group and exact current authorized request. Observe reset gate, identity and coherent snapshots before recovery mutations. |
| Fresh context | Reserve a new actual allocation and boot context, with fresh nonzero challenge. Never reopen an old generation or copy the peer's clock. |
| Authenticated comparison | Each identity signs a domain/version-separated recovery statement binding both actual durable states, exact operation facts, archive digest, ordered roles, both fresh contexts/challenges and its signer role. |
| Verified comparison | Verify the peer signature against the locally authenticated pinned identity; reread all local guards. Consume that owner-bound comparison once inside `finish()`. No separate receipt type, input structure or Boolean grants durable authority. |
| Public completion | Revalidate before/after delegated calls; persist/read back exact target evidence, binding and active public membership through existing store owners. Finish only the matching stage-2 journal transition, with a final live guard at its irreversible marker. |
| Reconstructed result | Verify coherent committed records. This result has no traffic API and cannot manufacture the existing retained-comparison receipt. |
| Fresh traffic session | Use the actual fresh epoch+1 path under the still-current admitted request and a new endpoint context: retained signed comparison, possession, invitation, handshake, local transcript confirmation, activation controls, durable commit and bidirectional statuses. |
| Failure or interruption | Retire volatile work; report refusal/uncertainty accurately. Reconstruction may repeat fresh comparison for a supported coherent state; torn state remains blocked. Never guess peer commit or report simultaneous activation. |

Recovery comparison uses a separately fresh bounded interval, no longer than the
existing 60-second comparison window or its owning request. It does not extend
any previous preparation or invitation deadline. Clock reversal, context loss,
request replacement/cancellation, allocator change, identity loss, reset or
reentry invalidates the current comparison. Cleanup still runs after expiry.
Continuing to fresh traffic does not restart the admitted request's deadline.

One serialized owner controls all snapshots and mutations. Guard/store/identity
owners outlive their verified comparisons and dependent owners. Exact expected snapshots may
advance only for this owner's verified authorized writes; unrelated late changes
must not disappear into a new snapshot. A committed peer need not remain online
forever after its signed observation; recovery still grants no old traffic.

## Validation and review boundary

Prove real first-enrollment and retained-rekey interruptions in both directions;
supported both-pending cases; archive transfer, public completion, reconstruction
and actual fresh epoch+1 bidirectional traffic. Include missing/forged/substituted
archives, wrong state/role/group/epoch/operation, replay/reflection, stale contexts,
expiry/cancel/reset, partial writes, late mutations, reentry and unchanged rejected
outputs. Verify no identity provisioning, old-key traffic, epoch rollback or
uncertain-record erasure. Unsupported torn/mixed cases remain explicit limits.

2026-09-30: independent scope and protocol review agreed this candidate sequence
fits the already approved OT-0238b host scope. The accepted design requires a
separate recovery review; this record is that implementation-design review, not
owner acceptance of completed work. Final source/runtime review and the affected
validation matrix remain required. Production selection/binding and physical
acceptance remain their existing gates.
