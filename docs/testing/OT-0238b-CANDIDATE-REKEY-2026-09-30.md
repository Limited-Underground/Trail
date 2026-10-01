# OT-0238b concrete retained-session rekey - 2026-09-30

Approved revision 2, host evaluation only. OT-0238b remains In Progress.
This increment extends the [first-enrollment session](OT-0238b-CANDIDATE-SESSION-2026-09-30.md)
through matching retained membership, fresh comparison and exact-next-epoch rekey.
It does not implement uncertain-state repair or select production cryptography.

## Result and ownership

`EnrollmentCandidateSession::start_rekey` loads the existing identity; it never
provisions a replacement. Read-only preflight verifies the actual committed
journal, membership, signed invitation/binding, local role/group and allocation
record before reserving a fresh session. Public peer input cannot change that pair.
The actual `ProductEnrollmentActivation` supplies the retained comparison owner;
its consumed signed comparison feeds `EnrollmentPreparationOwner`, fresh possession,
invitation, handshake, local transcript confirmation, durable activation and traffic.
Fresh enrollment still requires full fingerprint review.

A caller-owned durable boot store must be supplied from first enrollment and
reused on restart, separate from identity, journal, binding and session storage.
The old per-session boot store cannot provide changing boot tokens across attempts.
The retained route requires the dedicated store and refuses missing, blank,
corrupt or mismatched state. There is no automatic storage migration or erase-to-retry.
The previous live session must close before either peer is reconstructed; stores
are exclusively owned by the serialized composition.

The retained comparison formerly rejected equal boot tokens across peers.
Boot tokens are unique only within each local durable counter, so two independently
restarted devices can legitimately match. That cross-peer inequality was removed;
each peer must still differ from its own prior boot, and signatures bind both
identities, ordered roles, prior operation/digest, fresh contexts and challenges.
Reflected signatures and stale comparison responses remain rejected.

Cleanup first closes the activation endpoint while its dependencies are alive,
then destroys preparation and retained comparison before destroying that endpoint.
This breaks the reference cycle between comparison and endpoint-owned stores.
Failure never resumes old traffic keys or removes ambiguous durable evidence.

## Complete flow

| Boundary | Required outcome | Refusal or interruption |
|---|---|---|
| Closed prior session, fresh authorized request | Load exact existing identity and matching committed records | No provisioning, record repair or old-key resume |
| Valid retained pair/group/role | Reserve fresh generation and durable boot context | Invalid retained state refuses before mutation; allocated attempts are not reused |
| Fresh signed state comparison | Consume proof owned by the actual activation stores | Reflection, replay, changed authority or changed records close the attempt |
| Fresh possession and invitation | Exactly previous epoch + 1, original local deadlines | No skipped epoch, wrap, refreshed preparation time or extended invitation |
| Handshake and trusted local transcript confirmation | Fresh session material and exact current local hold/release | No received Boolean or remembered confirmation grants authority |
| Durable controls and commit | Activation intent before control output; verified local commit before traffic | Uncertain/mixed state stays blocked; no guess that the peer committed |
| Active or closing | Eight protected status deliveries in each tested renewal; release resources on close | Old traffic is rejected; cleanup remains possible after expiry |

The original selected-request limit remains 120 seconds. Retained comparison
keeps its existing 60-second preparation bound and each signed invitation keeps
its independent local window of at most 60 seconds. Clocks are not subtracted
across devices. Host success supplies no physical timing measurement.

## Validation and limits

Final current-source matrix: **60 suites passed**, including **98 session
groups**, the 19-group retained-state suite and 183-group product-rekey suite.
The concrete session tests run first enrollment and renewals at epochs 2 and 3
against actual persistent component owners, including equal peer boot counters,
different local allocation generations after an aborted attempt, eight status
deliveries per renewal, old-message refusal, preflight refusal without writes or
new entropy, replay/reflection, cancellation, deadline and one-sided commit cases.
Existing component interruption coverage remains in the full affected matrix.

Independent source/lifecycle review and an adversarial private mutant test cover
the boot-equality correction. SDK, entropy, clocks, transport and storage drivers
remain simulated; this is not a device or production acceptance result.
No firmware target includes the changed evaluation headers. The applicable
preflight checks are exclusive storage ownership, serialized lifetimes, original
deadlines, failure cleanup and source-bound validation. Board configuration,
target builds, RF settings, flashing, restoration and physical acceptance do not
apply to this host-only increment; real adapters and timing remain required.

Private exact evidence: `.private/ot0238b-session-rekey-20260930/closeout.json`,
its `security-matrix-final/result.json`, focused commands under
`.private/ot0238b-rekey-focused/`, and independent review under
`.private/ot0238b-session-rekey-independent/`.
The closeout records documentation checker, documentation regressions and diff
checks separately; source hashes remain fixed throughout the final matrix.
Use a new output directory for any later matrix run.

## Remaining work

Next host increment: explicit revocation/reset containment through the concrete
session owner. Uncertain-state recovery protocol, all-domain reset integration,
real phone/device/transport adapters, target timing/usability, OT-0237e production
selection, OT-0005i final binding and physical acceptance remain unfinished.
The earlier host read counts remain a timing warning, not a measured rekey cost.

No devices, cases or batteries were touched. No V1 progress credit or public
website capability changed. Existing OT-0332 planning and concurrent HomeAssistant
focus were preserved. Changes are local and uncommitted; Git publication is pending.
