# OT-0238b first-enrollment host composition

Date: 2026-09-23. VERIFIED host candidate under the
[accepted design](../security/PRODUCT_ENROLLMENT_REKEY_DRAFT_V1.md).
OT-0238b remains in progress; this is not production or hardware acceptance.

## Implemented flow

1. A dedicated identity owner durably writes provisioning intent before entropy,
   commits/readbacks its seed, and retains the same signing identity after restart.
   Partial provisioning refuses rather than regenerating over uncertain records.
2. The full fingerprint review produces a consumed, move-only local receipt.
   Real stored identity signatures prove possession of both pinned keys over fresh
   challenges, ordered roles and both boot/generation/request contexts.
3. A preparation owner checks the actual session allocator and identity store,
   issues one invitation, verifies both bindings and hands a single-use capability
   to its matching activation owner. Received bytes and caller Booleans cannot
   produce that capability. Cancellation or changed generation invalidates it.
4. The actual independent endpoint consumes the invitation and executes the
   handshake. Its transcript is rendered through the trusted device-port boundary;
   release baseline, fresh 1000-3000ms hold and release are required. Wrong display,
   request, context, time, stale/held button or cancellation refuses confirmation.
5. Local transcript confirmation precedes journal preparation. Durable activation
   intent precedes control output. Actual authenticated peer activation plus local
   membership commit/readback precede status traffic. Context/identity/generation
   checks continue before and after operations. Failure closes volatile keys.

The successful two-owner host case runs three handshake transfers, four
activation controls and eight status deliveries. The port, entropy source and
storage faults are host fixtures; cryptography, signing, endpoint, persistence,
allocator and membership logic are the real candidate components.

## Validation

Final actual-source matrix: **50 suites passed**. Command:

`C:/Python314/python.exe -X utf8 -B tests/host/security_current_source_ci.py --output-root C:/lu/OpenTrail/.private/ot177-publication/.private/ot0238b-first-enrollment-final-matrix`

Environment: `OPENTRAIL_MSYS2_ROOT=C:/msys64`; native GCC16.1.0. Private output
contains exact commands, logs, source pins and binary hashes. Source is frozen
through the run. Three new suites are included in the maintained CI matrix.
Focused checks passed 895 identity-store groups, the possession-proof mutation
suite and 26 integrated activation groups. Independent reviews found no remaining
blocking defect for this documented first-enrollment boundary after consumed
receipt and continued-generation corrections.

Tests include persisted-byte corruption and storage fault positions, incomplete
provisioning restart, invalid signatures/roles/contexts, consumed receipts,
cancellation, bad button gestures, activation/membership readback failures,
stale generation, unchanged rejected outputs and restart traffic refusal.

No deployed firmware target includes these new headers. Therefore this increment
changes no firmware binary and requires no target rebuild. Existing radio target
builds and accepted physical evidence remain separately recorded.

## Limits and exact remaining work

- This is first enrollment only. The current journal retains activation uncertainty
  after restart, even after a successful host run. Restart refusal prevents old-key
  traffic; it does not implement usable restart/rekey recovery.
- Next implement explicit committed public state and retained signed invitation,
  then authenticated comparison of records read from their actual owners. Matching
  committed records may authorize a fresh exact-next-epoch attempt. Mixed/pending
  records remain blocked; no automatic repair is authorized by this checkpoint.
- Connect retained rekey, revocation and reset containment and test interrupts at
  all transitions. Reuse existing evidence, membership and reset owners.
- Target GPIO/display, protected request routing, identity backend confidentiality,
  rollback protection, entropy and complete paired timing remain acceptance gates.
  The host identity seed is unsealed. Checksums do not prevent malicious rollback.
- Trusted dependencies are serialized and outlive their owners. Terminal input
  samples must be read-only; reentry checks do not claim arbitrary concurrency safety.
- Candidate crypto/encoding remain unselected. No hardware, case/battery work,
  website synchronization or weighted V1 progress change occurred.

No owner action is currently required for the remaining approved host work.
Publication of this checkpoint does not accept OT-0238b completion.

Publication hygiene: after the matrix, added source lines were normalized to LF
for the repository whitespace gate. All non-whitespace bytes are unchanged; exact
before/after hashes are in `.private/ot0238b-first-enrollment-whitespace.json`.
Documentation checker and 18 document regressions passed.
