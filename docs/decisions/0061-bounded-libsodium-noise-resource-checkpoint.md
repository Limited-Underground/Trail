# Decision 0061: Bounded libsodium Noise XK and runtime-resource checkpoint

Status: Accepted

## Decision

Record the OT-122 continuation under the still-bounded Decision 0059 Phase 2 session as a second checkpoint only. Two independently admitted anonymous experimental nodes each passed all eight Espressif libsodium operations, including one complete benchmark-only Noise XK handshake, with 100 data-cache-conditioned and 100 warm samples per operation. Benchmark readback, capture validation, exact Trail restoration, restore readback, and reset passed on both nodes.

Both nodes reported the same runtime-resource result: 334,504 bytes of internal 8-bit heap were free at both the start and local minimum, so measured peak dynamic heap use was zero; the 8,192-byte worker retained a 3,880-byte high-water free margin, so maximum stack use was 4,312 bytes; and uninterrupted terminal completion reported zero watchdog resets. Zero peak dynamic heap is an observed result, not an absent value.

This checkpoint still does not complete Phase 2 or admit Phase 3. Measurements for the other candidates, conditional radio execution, the exact matched linked-flash delta and static-RAM admission, cross-candidate comparison, independent admission, and explicit suite/library, handshake/KDF, and packet-v1 wire selection remain open. No support, compatibility, regulatory, production, secure-LoRa, end-to-end, or score claim is added.

## Evidence

- [OT-122 public execution receipt](../../tests/benchmarks/crypto/OT-122-OT005-LIBSODIUM-NOISE-RESOURCE-EXECUTION-RECEIPT-V0.json)
- [OT-122 public hardware evidence](../../tests/hardware/OT-122-2026-08-23.md)
- [Decision 0059 execution authority](0059-one-time-phase-two-benchmark-execution-authority.md)
- [Decision 0060 first checkpoint](0060-bounded-libsodium-local-primitives-checkpoint.md)

The immutable authority and OT-121 evidence remain unchanged. Phase 2 is still partial and no selection is made.