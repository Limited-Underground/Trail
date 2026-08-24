# Decision 0063: record the Monocypher comparison execution abort

- Date: 2026-08-24
- Status: Accepted
- Scope: OT-005 / OT-124 abort and restoration evidence only

## Decision

Accept the privacy-safe OT-124 abort receipt. Both anonymous nodes passed exact installed-Trail application readback before any write. Node A then received and readback-verified the exact application-only OT-123 benchmark image, but the bounded serial capture ended without an accepted frame. Node A was restored, readback-verified, and reset to the exact Trail image. Node B remained untouched after preflight.

No Monocypher frame, primitive result, timing, resource result, comparative result, or score is admitted. The observed boundary is a capture timeout after benchmark readback; the public record does not claim a confirmed root cause. No radio or phone was used.

Decision 0059's one-time Phase 2 authority is consumed by this abort, creates no continuing authority, and cannot be reused. Decision 0059 and OT-123 remain immutable. Any retry requires fresh explicit one-attempt authority and a separately hash-bound corrected successor runner.

## Consequences

- Phase 2 remains incomplete.
- The latest admitted benchmark result remains OT-122.
- No candidate or suite is selected.
- No support, compatibility, regulatory, production, secure-LoRa, end-to-end, or score claim changes.
- V1 remains exact 43.75%, displayed 44%.

## Evidence

- [OT-124 abort receipt](../../tests/benchmarks/crypto/OT-124-OT005-MONOCYPHER-COMPARISON-EXECUTION-ABORT-RECEIPT-V0.json)
- [OT-124 hardware note](../../tests/hardware/OT-124-2026-08-24.md)
