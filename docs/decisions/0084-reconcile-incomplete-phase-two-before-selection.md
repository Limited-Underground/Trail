# Decision 0084: Reconcile incomplete Phase 2 before selection

Date: 2026-08-26

## Decision

Accept OT-148 as a host-only, fail-closed reconciliation of the bounded OT-005
Phase 2 evidence corpus. The frozen OT-116 plan remains authoritative. OT-122
and OT-146 are valid partial results, but both explicitly retain
`phase_two_complete=false` and `radio_used=false`; they do not permit Phase 3
admission or cryptographic/wire selection.

Espressif libsodium 1.0.22 is recorded as the evidence-backed recommendation
because it is the only candidate with an admitted eight-of-eight operation
surface and structural selection eligibility. This recommendation is not a
selection. No library, suite, handshake, KDF, or Packet-v1 wire format is
accepted by OT-148.

## Reconciled evidence

- Libsodium: two anonymous nodes passed all eight admitted operations with
  timing and runtime heap/stack/watchdog evidence and exact Trail restoration.
- Monocypher: two anonymous nodes passed its five admitted comparison
  operations with timing and runtime heap/stack/watchdog evidence and exact
  Trail restoration. It remains structurally nonselectable.
- mbedTLS/PSA: five operations are admitted for comparison, but no Phase 2
  target execution result exists.
- The OT-123 matched-resource contract remains unexecuted, and its current
  validator cannot admit signed or zero deltas.
- OT-114 proves the accepted direct-radio profile and ceiling; it is not the
  required Noise XK handshake radio-cost measurement.

The exact reconciliation is frozen in
`OT-148-OT005-PHASE-TWO-CORPUS-RECONCILIATION-V0.json` and checked against the
raw and canonical bytes of all controlling inputs.

## Remaining completion gates

Phase 2 remains incomplete until all of the following are present and bound:

1. the two-node mbedTLS/PSA five-operation timing and runtime result;
2. matched linked-flash and static-RAM results under a signed/zero-capable
   successor admission;
3. Noise XK handshake wire-byte, fragment, measured-airtime, and bounded-retry
   results at the frozen radio profile;
4. independent reconciliation of all eight OT-116 named gates; and
5. verifiable custody of the required private raw traces.

After those items, a separate host-only Phase 3 admission must bind the complete
corpus before a separate explicit selection decision.

## Correction of the prior forward-looking sentence

Decision 0083 remains immutable evidence of the successful OT-146 Monocypher
attempt. Its forward-looking phrase “completed Phase 2 evidence” was premature
and is superseded by this decision. The accepted OT-146 result itself is not
changed.

## Authority and publication

OT-148 authorizes no device access, flash, radio transmission, benchmark
execution, candidate selection, secure-LoRa implementation, Packet-v1
implementation, or score credit. It changes no public capability or percentage,
so the public website remains on the normal batched update cadence.
