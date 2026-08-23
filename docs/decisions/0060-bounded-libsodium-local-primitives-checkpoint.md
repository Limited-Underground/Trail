# Decision 0060: Bounded libsodium local-primitives checkpoint

Status: Accepted

## Decision

Record the OT-121 `OT121LPER1` result as a bounded execution checkpoint only. On two independently admitted anonymous experimental nodes, all seven libsodium local-primitive operations passed. Each node recorded 700 data-cache-conditioned (cold-labeled) and 700 warm samples across those operations, exactly 100 per operation, after two prerequisite gates passed. The captured benchmark was validated and each node was restored exactly to its pre-execution image and reset.

This checkpoint does not complete Phase 2 and does not admit Phase 3. Noise XK, radio, cross-candidate comparison, linked-flash/static-RAM/peak-dynamic-RAM/max-stack/watchdog measurements, suite/library/handshake/KDF/wire selection, product support, compatibility, regulatory readiness, production readiness, secure-LoRa behavior, two-pair end-to-end acceptance, and score changes remain outside the result.

## Evidence

- [OT-121 public execution receipt](../../tests/benchmarks/crypto/OT-121-OT005-LIBSODIUM-LOCAL-PRIMITIVE-EXECUTION-RECEIPT-V1.json)
- [OT-121 public hardware evidence](../../tests/hardware/OT-121-2026-08-23.md)
- [Decision 0059 execution authority](0059-one-time-phase-two-benchmark-execution-authority.md)

The immutable authority artifact remains unchanged. Its bounded execution session is only partially exercised; the remaining Phase 2 candidate, Noise, and radio measurements still require completion before any separate Phase 3 admission or explicit selection.
