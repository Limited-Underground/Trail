# Product enrollment and rekey - V1 design contract

Owner accepted design, 2026-09-23. OT-0238b implementation remains in progress.
No algorithm, library, wire format, hardware execution or production readiness is selected here.
This proposal preserves [OTSL0/v0](SECURE_LORA_KEY_TRANSPORT_V0.md),
[Decision0035](../decisions/0035-host-tested-secure-lora-key-transport-contract.md)
and the current [factory reset contract](../platform/DEVICE_FACTORY_RESET_V1.md).

## Owner-visible choice

Recommended first release: first-time enrollment happens with both Heltecs physically present. Before trusting a new peer, each person verifies the full device identity fingerprint against the other Heltec's own screen. A phone may help present/transfer the public identity, but phone display, QR parsing and received radio bytes alone never establish trust. The existing matching transcript-code check and local button confirmation remain mandatory afterward. This adds a one-time identity verification step to first enrollment; routine messages do not repeat it. Remote unattended first enrollment is excluded. The owner accepted this workflow on 2026-09-23; implementation and physical usability remain unvalidated.

Why a full fingerprint: the existing short confirmation code is not silently promoted into an independently proven first-root authentication protocol. A future shortcut would need its own reviewed threat model and measured attempt/entropy bounds. Formatting, pagination and reading time must be usability-tested before physical acceptance; partial fingerprint comparison is not sufficient.

## Trust, identity and ownership

Each Heltec owns a persistent device identity and its private signing material. Identity provisioning uses the accepted entropy/persistence boundary; failed or ambiguous identity creation admits no enrollment. The full authoritative fingerprint binds the canonical identity public key, version and algorithm domain under the eventually selected adapter. Names and aliases are presentation only. No arbitrary imported signer becomes a trust root.

The owner opens a bounded first-peer verification session locally on each device. Public peer identity data received through the phone/radio is initially an untrusted candidate. The Heltec renders the complete candidate fingerprint and the current session role, while its own fingerprint can be inspected locally. Users compare each candidate against the physical peer's own display, then confirm locally on each Heltec. A trusted input/display controller binds that physical event to the exact rendered identity, display revision, local device, boot/session generation and purpose. Payload fields and caller Booleans cannot mint such a receipt; stale display, role change, input replay, cancellation or identity change invalidates it.

Pre-invitation possession proofs must be fresh and domain-separated, binding both exact identities, ordered roles, each local preparation/boot generation and fresh challenges. Static signatures and prior preparation receipts cannot satisfy this proof; the later invitation nonce/deadline are not yet available.

Only after physical verification and proof of private-key possession does the crypto adapter establish peer authentication. Verification is scoped to one pair and attempt, not a general certificate authority. Signatures by the presented key before that point prove possession only, not trusted identity. Incoming messages cannot select the inviter role, group or another trusted root. Group creation and inviter/member roles are explicitly requested by each authorized owner and displayed in the later transcript confirmation. First V1 pairing is exactly two members with no relay, server or remote authority.

Identity signing, any identity key-agreement binding, fresh handshake keys and traffic keys remain distinct purposes. A signed binding connects ephemeral/key-agreement contributions to the retained authoritative identity. The selected adapter must validate domain/version, both identities, ordered roles, group, epoch, nonce and deadline. Reusing EnrolledPeerEndpoint peer_a/peer_b as both persistent identity and fresh session identity is prohibited. The implementation must retain separate types for those concepts.

Phone authorization grants permission to request operations on its owned device; it never authenticates a radio peer or exports LoRa private material. Phone disconnect cancels a pending phone-initiated request. Phone owner changes do not silently rekey or alter committed LoRa membership. The destructive factory-reset contract remains controlling.

## Complete first-enrollment flow

| State/owner | Trigger | Required effect and evidence | Forbidden effect / failure action |
|---|---|---|---|
| Unenrolled, device identity owner | Authorized workflow request and explicit local preparation | Load/read back device identity; reserve a fresh session generation; open bounded fingerprint verification | No signing invitation or traffic on ambiguous identity/storage |
| Untrusted candidate, display/input controller | Public peer identity arrives | Bound parse; display full candidate fingerprint; freeze display/session revision | Received identity alone gives no trust; changed candidate clears confirmation |
| Locally verified peer, device owner | Both local fingerprint confirmations plus possession proofs | Consume one-shot receipts; bind exact persistent peer identities and role pair | Missing/wrong proof, stale receipt or mismatch aborts; clear attempt material |
| Invitation issuer, inviter Heltec | Both endpoints ready for the invitation exchange | Device generates/signs one secret-free invitation: exact pair/group/epoch/roles, fresh nonce, original bounded deadline | Phone cannot supply root or signed authority; no long-lived secret in QR/export |
| Invitation admitted, endpoint owner | Exact validated invitation | Consume invitation attempt durably before handshake output; reject reuse, malformed/reentrant/out-of-phase attempts | No restart reopening; possible consumption failure blocks attempt |
| Handshake in progress, crypto adapter | Versioned peer records | Authenticate retained identities and fresh contributions; derive bound transcript using volatile handshake scratch only | No staged candidate membership/key authority or traffic before both local confirmations; mismatch/expiry/entropy fault wipes volatile scratch |
| Awaiting local confirmation | Both Heltecs display matching transcript code and group/role summary | Each trusted local input confirms exact current transcript before original deadline | A phone click or host token cannot replace local confirmation |
| Pending membership, durable coordinator | Both authenticated confirmations | Commit/read back complete pending public evidence and fresh key-domain state in isolated storage | No active-state publication; uncertain write enters reconciliation |
| Activation, both endpoint owners | Exact authenticated peer activation controls | Bind attempt, epoch, both identities and durable candidate digest; persist activation intent before sending activation | Never infer peer activation from TX completion or generic ACK |
| Active, traffic owner | Local durable commit/readback plus required exact peer activation evidence | Admit only the new current epoch/direction/counters; return truthful ready status | A packet queued or one endpoint ready is not proof both endpoints can send |
| Closed or failed | Cancel, deadline, fault, loss of context | Stop radio work; consume attempt; wipe volatile secrets; retain minimal failure category and durable uncertainty state | No automatic retry, root substitution, old-key fallback or invite-window extension |

Physical fingerprint review precedes invitation issuance, so reading two full fingerprints cannot consume or extend the signed invitation deadline. It has a separate proposed two-minute local preparation limit; timeout discards its receipts and requires a new deliberate preparation session. Once issued, the invitation window remains the existing at-most60 seconds; all authority-granting activation must complete within it. Cancellation, zeroization, durable containment and resource release still run after expiry; expiry never prevents cleanup or revives authority. Fresh pre-invitation identity/proof checks must not let an expired preparation session issue a new invitation.

Local monotonic clocks are independent. Existing versioned clock/boot binding is reused only where source review proves it fits this flow; never subtract different-device clocks. Every command cost, durable read, identity guard, human hold, radio TX completion and RX rearm enters the final paired timing model. If the flow does not fit, report failure; do not enlarge the invitation window to pass.

## Rekey, restart and reconciliation

A routine rekey keeps the same exact pair of authoritative identities and group. It permits exactly current epoch+1, rejects zero/skips/wrap, reserves a fresh durable session generation and derives distinct handshake/traffic material. Prior public membership authorizes considering a retained peer, never resuming traffic keys. Both endpoints stop old routine traffic while the replacement is unresolved. New invitation, identity possession, full transcript binding and local transcript confirmation still apply; full fingerprint verification need not repeat for an unchanged, durably verified pair.

Durable coordinator states are EMPTY, ACTIVE_PUBLIC, PREPARED, ACTIVATION_POSSIBLE, ACTIVE_COMMITTED, RECONCILE and REVOKED/RESET_PENDING. These are semantic states, not a new frozen storage schema. Each transition binds pair/group, exact epoch, session generation, public evidence digest and operation ID. Identity, public membership/evidence, transient pending state, activation and counter authority have distinct owners/namespaces. No partial record can be interpreted as active.

- Known no-mutation abort before activation intent may retain the old public membership. It does not revive volatile traffic keys after restart.
- Before the first activation message can leave either endpoint, persist/read back ACTIVATION_POSSIBLE. From that point, uncertainty has no old-epoch fallback.
- Local active commit and authenticated peer evidence are both required to enable traffic. Lost controls may leave one endpoint locally committed while the other reconciles; bounded protocol handling must represent that honestly. No finite exchange is described as guaranteeing simultaneous activation.
- Restart never restores old traffic keys from public evidence or a remembered ready flag. It abandons the old session allocation. PREPARED/ACTIVATION_POSSIBLE/uncertain records reconstruct as RECONCILE with no traffic. They stay blocked until the recovery protocol is separately reviewed and implemented; this draft authorizes no automatic reconciliation or production launch with unspecified recovery.
- Reconciliation exchanges fresh authenticated challenges with the retained exact identities and compares exact durable operation/epoch/digests. It may finish the recorded operation only with a newly reviewed fresh-session protocol, or remain blocked. It never guesses peer commit, decrements epoch, revives an invitation, or erases evidence to retry. Implementing that recovery protocol is a required follow-on acceptance gate, not an invented automatic repair in this design increment.
- Revocation/leave stops admission and retires ordinary access to old keys before reporting completion. Revoked peers cannot rekey using old credentials. With the V1 pair reduced to one device, there is no routine peer traffic. Joining a different identity is new enrollment, not ordinary rekey.
- Full factory reset uses the existing all-domain erasure coordinator, including identity/membership/evidence/session/counter/bond/user data. No added partial reset or battery-disconnection requirement. Logical retirement does not claim physical flash secure erasure.

## Evidence, tests and implementation order

1. Independently review/accept this first-peer workflow and identity/rekey contract. Keep cryptographic selection under its existing separate gate. Do not change the evaluation target just to make it look like product firmware.
2. Define the typed identity, trusted display/input receipt and authenticated-adapter boundaries, including concrete producers and persistence owners. No placeholder ProductAuthority interface can count as completed trust bootstrap. Pin canonical encodings only after review.
3. Implement retained identity to fresh-session binding, exact epoch+1 and durable pending/activation coordinator using existing SessionGenerationAllocator, isolated NVS views and membership/evidence primitives where compatible. Preserve old passing evaluation tests as evaluation evidence.
4. Bind the product workflow to actual protected request/local input paths. Separate Android UI wiring and production transport selection remain their existing task gates; exercise actual components in the host composition rather than granting authority through test Booleans.
5. Test first-join success and substituted roots, name/alias spoofing, truncated fingerprints, stale/replayed input receipts, wrong group/role, swapped endpoints, invalid signatures, changed identity/session binding, invite replay/expiry, epoch skip/wrap, revoked identity, context loss and entropy failure. On every rejected operation, outputs stay unchanged and no traffic authority leaks.
6. Interrupt every durable write/sync/readback before and after possible mutation, reconstruct both endpoints from those real stores, and prove no uncommitted membership/old traffic keys are exposed. Test one-sided activation, lost/duplicate controls, restart during each state, tombstone failure and no erase fallback.
7. Review the full timing/resource lifecycle; run focused discrimination tests, then the affected matrix and target builds once on final inputs. Physical first-join, retained restart/rekey/revoke and recovery need separately prepared authorization. Cases/batteries remain untouched unless separately approved.

Log only bounded stage/fault categories, counts and timings. Do not log fingerprints, device IDs, invitation material, human codes, secrets or plaintext. Public documentation describes capability only after accepted evidence; this draft earns no V1 completion credit.

## Design acceptance

Accept or revise the recommended in-person first-peer fingerprint verification followed by the existing transcript confirmation, the separate bounded preparation period, persistent identity/exact+1 rekey, and fail-closed interrupted-operation policy. Acceptance authorizes preparing the concrete implementation under OT-0238b; it does not select cryptography, authorize hardware or declare the complete recovery protocol implemented. Recovery protocol details must be reviewed before that implementation stage can claim lifecycle completion.

## Review record

2026-09-23: independent source/contract review found no remaining objection to bounded owner review of this design prerequisite after fresh pre-invitation proof binding, post-expiry cleanup and pre-confirmation scratch/staging clarifications. Review does not establish cryptographic protocol security, product implementation or complete recovery acceptance. Repository documentation checker and18 document regressions pass. Existing dirty work is preserved; no code, build, device or publication operation was performed.

2026-09-23: owner replied "proceed" to the design-approval request. This accepts the workflow and authorizes its scoped host implementation; it does not accept task completion, select cryptography, or authorize hardware/publication. Independent implementation review constrains the first recovery slice to authenticated state comparison: matching committed records can permit a new exact-next-epoch attempt; uncertain or mixed records remain RECONCILE with no traffic. No automatic repair is granted by that comparison.
