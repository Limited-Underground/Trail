# Decision 0066: one-time Monocypher second corrective-retry authority

- Date: 2026-08-24
- Status: Accepted
- Scope: OT-005 / OT-127 one bounded application-only two-node attempt

## Decision

Grant exactly one fresh two-node execution attempt for the five-of-eight Monocypher comparison target. This authority replaces nothing and cannot reuse Decision 0064: OT-126 consumed that authority on abort. The new authority is bound to the immutable OT-125 authority, the privacy-safe OT-126 abort receipt, the OT-123 preparation, the exact benchmark and Trail application images, and the exact OT-127 successor runner.

The successor removes the operator-set capture timeout and fixes the capture contract at 180 seconds. Each fresh serial handle receives a 10-second first-frame grace, the overall deadline never restarts, and no reset or reopen is permitted after any accepted frame. Both devices must pass exact installed-Trail readback and then be hard-reset before the private journal or first write; both resets are attempted even if a readback or reset fails. Only the application slot at `0x10000` is writable. Every benchmark-touched node must be restored, readback-verified, and hard-reset to exact Trail on success or abort.

The owner's standing authorization for reversible test-device work covers this one temporary application-only attempt. It makes no permanent firmware decision. Both USB endpoints and both visible Trail displays already passed the required preflight after OT-126 restoration.

## Consequences

- The authority is consumed by either success or abort, creates no continuing authority, and is never reusable.
- The prior private journals and receipts remain unchanged and retained.
- No radio use, candidate or suite selection, support, compatibility, regulatory, production, secure-LoRa, end-to-end, or score claim is authorized.
- Until a validated execution receipt exists, Phase 2 remains incomplete and the latest admitted benchmark result remains OT-122.
- V1 remains exact 43.75%, displayed 44%.

## Evidence

- [OT-127 authority](../../tests/benchmarks/crypto/OT-127-OT005-MONOCYPHER-CORRECTIVE-RETRY-AUTHORITY-V0.json)
- [OT-127 hardware note](../../tests/hardware/OT-127-2026-08-24.md)
- [OT-126 abort receipt](../../tests/benchmarks/crypto/OT-126-OT005-MONOCYPHER-CORRECTIVE-RETRY-EXECUTION-ABORT-RECEIPT-V0.json)
