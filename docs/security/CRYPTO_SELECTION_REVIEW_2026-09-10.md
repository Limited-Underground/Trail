# Crypto selection review after the two-device radio benchmark

## Result

Retain Espressif libsodium 1.0.22 as the evidence-backed engineering direction.
It is the only candidate with the complete eight-operation surface required by
the frozen benchmark plan. This review does not select a production library,
suite, handshake/KDF or packet-v1 wire format: the complete admission criteria
in [Decision 0003](../decisions/0003-crypto-benchmark-gate.md) and
[Decision 0084](../decisions/0084-reconcile-incomplete-phase-two-before-selection.md)
have not yet passed. The successful radio trial closes a real measurement gap;
it does not close the remaining security and comparison evidence gaps.

The additive [corpus reconciliation](../../tests/benchmarks/crypto/OT-163-CRYPTO-CORPUS-RECONCILIATION-2026-09-10.json)
pins the reviewed inputs and dispositions. Earlier plans, receipts, aborts and
the OT-148 reconciliation remain immutable historical evidence. This successor
assessment is not a completed OTCBXR1 Phase 3 admission.

## Candidate comparison

| Candidate and exact pinned version | Accepted evidence | Selection implication |
|---|---|---|
| Espressif libsodium 1.0.22 | Both nodes exercised all eight admitted operations, 100 cold and 100 warm samples per operation, with timing and runtime resource evidence. The separate Noise XK radio trial now passed. | Only structurally eligible candidate; final selection gates remain open. |
| Monocypher 4.0.3 | Both nodes exercised all five admitted comparison operations with 100 cold and 100 warm samples and runtime resources. | Comparison only: required SHA256, HKDF-SHA256 and Noise composition are absent from the admitted surface. |
| ESP-IDF mbedTLS/PSA 4.1.0 | Five-operation comparison API and matched host resource accounting are admitted. The later hardware capture aborted. | No accepted two-node timing result; pinned configuration lacks required Ed25519 signing/verification and Noise composition. |

These are conclusions about exact admitted configurations, not claims that other
versions or configurations cannot supply those operations. The resource reports
must not be ranked as a complete matched three-library comparison: OT-150's
signed/zero-capable result covers mbedTLS, not the whole corpus. Ordinary
application sizes from different benchmark harnesses are not library deltas.
Likewise the recorded cold sample class does not substitute for device cold-power
or brownout entropy/uniqueness acceptance.

## What the radio result admits

[Attempt 5](../testing/OT-163-RECEIPT-CONSOLE-RUN-2026-09-10.md) passed 14 frames
and 736 wire bytes: two baseline and two bounded-retry handshakes, all four final
successes, two intentional withheld-message timeouts and zero reported loss,
duplicates, corruption or unexpected packets. The two role cycles reverse the
initiator/responder direction. Both original applications and protected regions
were independently restored and verified.

The exact benchmark profile is 915 MHz, SF7, 125 kHz bandwidth and 2 dBm command
setpoint. Its summed TX command windows are not pure RF airtime. This evidence
does not establish independent Noise implementation interoperability, authenticated
product group lifecycle, packet-v1 framing, field range or phone-to-phone delivery.
The previous physical timeout cause remains unproven.

## Consolidated next work

The [integration and admission plan](../testing/OT-163-CRYPTO-INTEGRATION-GATES-2026-09-10.md)
groups the remaining work into coherent batches with exact reusable components,
accepted evidence and exit conditions. The immediate batch is to finish the
comparison/resource/custody evidence, while preparing the target security proofs
in parallel. Hardware attempts must be proposed with current identities, exact
artifacts and recovery; no consumed grant can be reused.

After all required evidence is reconciled, complete the distinct host-only Phase 3
admission and make the explicit selection decision. Then implement the selected
production adapter and authenticated radio path through both protected BLE
endpoints, followed by one coherent two-phone/two-device acceptance run.

The existing V1 physical-access limitation remains in force; this review adds no
secure-element, irreversible eFuse or production secure-boot requirement. Deferred
cold-power work stays visible and is not replaced with warm-restart evidence.
No numeric completion credit, firmware change, device action, website update,
publication or deployment is performed by this review.
