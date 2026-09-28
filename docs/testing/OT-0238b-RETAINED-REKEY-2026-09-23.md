# OT-0238b retained enrollment and fresh rekey

## Result and boundary

The host candidate can retain a completed pairing, restart, authenticate matching
records from both actual owners, and complete a fresh exact-next-epoch exchange.
It never restores old traffic keys. Revocation and reset preparation close live
traffic and retain terminal public metadata. Full product enrollment remains in
progress: target adapters and physical validation are not supplied by this result.

This implements the matching-committed-record branch of the owner-accepted
[design](../security/PRODUCT_ENROLLMENT_REKEY_DRAFT_V1.md). Pending, mixed or
uncertain records remain blocked. No automatic reconciliation/repair is added.

## Complete sequence and failure boundaries

| Owner/state | Trigger and required result | Refusal/cleanup |
|---|---|---|
| First enrollment | Existing physical identity review and fresh possession authorize the signed invitation; actual handshake and local transcript gesture precede activation | No caller Boolean or received identity establishes trust |
| Activation possible | Existing durable intent precedes control output; all four actual controls establish peer activation | Missing control cannot commit membership |
| Durable commit | Persist signed invitation, both identity-binding signatures and ordered identity pins, then membership; read all back, recheck authority, and write final committed journal marker | Any partial transition rejects traffic; interrupted banks fail reconstruction or remain reconciliation-only |
| Closed/restarted | Close/wipe old endpoint, reserve a newer durable session generation, use fresh role/counter/activation storage and retain the separate boot ledger | Never reuse blank boot ledgers to manufacture a repeated boot token; generation and boot context are distinct |
| Retained comparison | Actual journal, membership, evidence, proof and identity owners agree; fresh challenges and both contexts are signed by each owner | Different pair/group/epoch/operation/digest/phase, old boot, replay, changed records, revoked state or stale context rejects |
| New attempt | Consume one move-only comparison receipt, obtain fresh dual possession and invitation; require exact epoch+1, fresh keys and nonce | Reject skipped/wrapped epochs, prior nonce/keys, alternate-store receipt or cancellation |
| Rekey pending/activation | Keep the same pair/group, repeat real handshake and trusted transcript gesture; publish pending journal, then activation intent and controls | Old traffic remains closed; no fallback to the previous epoch |
| New active membership | Replace evidence/proof, rotate membership, and commit journal with live authority guards across storage operations | Only exact readback and actual endpoint readiness permit status output |
| Revoke/reset preparation | Close volatile keys first; attempt membership tombstone even if cleanup fails; reset preparation also tombstones evidence/proof | No erase-to-empty or factory-reset claim; failed containment is reported, not treated as success |

The retained comparison signs ordered identities, group, epoch, operation ID,
evidence digest, committed phase, both fresh challenges and boot/generation/request
contexts, and signer role. Old local allocator generations need not match across
peers; each local owner separately requires its new generation to be greater.
The comparison deadline is fixed at start and is not the invitation deadline.
The original signed invitation window remains unchanged.

Retained signatures are cryptographically checked again on read. The stored
binding is not sufficient on its own: membership and the committed journal must
agree. Preparation authority is bound to the exact activation owner's stores.
After local transcript display changes, the guard checks the durable record
without pretending the old display is still current. After journal handoff, live
identity/generation/cancellation and the actual pending/committed owners govern.

A fully written commit whose final sync/readback reports failure may reconstruct
as committed public metadata. The failed live operation still clears keys and
returns no traffic; restart requires a new authenticated comparison and attempt.
This is not a guarantee that a failed call means no bytes were written. An
interruption before complete durable commit cannot reconstruct partial active
state. Checksums do not protect against hostile whole-storage rollback.

## Validation

Focused native GCC tests use actual signing, endpoint, storage, allocator and
membership components; deterministic entropy, storage faults and device ports
remain host fixtures. Results before the final matrix:

- Journal: 4,353 groups across six durable stages, all byte corruption positions,
  nine fault types, reentry, legal transition continuity and overflow refusal.
- Retained signature store: 1,733 groups, including forged provenance with
  recomputed checksums, invitation mismatch, rotation, stale instance and reset.
- Retained comparison: 19 groups using actually activated records, fresh signed
  exchanges, replay, context/display drift and tombstones.
- Existing first-enrollment composition: 26 groups retained.
- Rekey composition: 183 groups, including repeated epochs 1->2->3, eight new
  status deliveries, prior ciphertext refusal, missing controls and interruption
  positions across evidence, binding, membership and journal stores.
- Independent authority regression: 70 groups, including cancellation/reentry
  before/during commit, alternate-store receipts and all 36 storage alias pairs.

Final actual-source matrix: **54 suites passed**, with source pins unchanged
through the run. Command (native GCC16.1.0; OPENTRAIL_MSYS2_ROOT=C:/msys64):

`C:/Python314/python.exe -X utf8 -B tests/host/security_current_source_ci.py --output-root C:/lu/OpenTrail/.private/ot177-publication/.private/ot0238b-retained-final-matrix`

The private result contains exact commands, source pins and binary hashes.
Repository documentation checks and18 documentation regressions passed.
The18-file publication delta scan found no private-data findings; scanner
regressions passed. The18-target include closure has no affected firmware target.

Independent review found and closed two authority boundaries: final commit must
recheck live authority during storage work, and comparison receipts must belong
to the activation owner's actual stores. Full-flow tests also caught two fixture
errors: resetting the boot ledger reused a boot context, and resetting the clock
after endpoint preparation introduced a backwards time sample. The fixture now
retains the boot ledger and establishes the simulated restart clock before any
new endpoint sample; neither failure was bypassed by relaxing a product guard.

No deployed target includes the changed candidate header closure. No firmware
binary change or target rebuild is claimed. No hardware, battery/case work,
public website change, final crypto/wire selection or weighted V1 credit occurred.

## Remaining gate

Compose these candidate owners with protected product request routing and actual
GPIO/display/storage/entropy adapters, then validate target timing and the complete
paired device flow under separately authorized hardware execution. Target storage
confidentiality and rollback defenses remain unresolved. The host identity seed
is unsealed. Serialized owners/backends must outlive references; no arbitrary
concurrent writer or trustworthy atomic behavior from a hostile backend is claimed.

Uncertain/mixed-state automatic repair remains a separately reviewed protocol gate.
OT-0238b remains In Progress; this report does not accept owner completion.
