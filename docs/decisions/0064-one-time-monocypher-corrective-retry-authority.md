# Decision 0064: grant one-time Monocypher corrective-retry authority

- Date: 2026-08-24
- Status: Accepted; hardware execution pending
- Scope: OT-005 / OT-125 exact two-node Monocypher comparison retry only

## Decision

Grant exactly one corrective two-node execution attempt for the immutable OT-123 Monocypher 4.0.3 five-of-eight comparison image. This authority is separate from and does not revive Decision 0059, which OT-124 consumed on abort.

The authority binds the OT-124 sanitized abort, immutable OT-123 preparation, exact benchmark and Trail restore images, and corrected OT-125 runner. The runner fixes only the Windows serial-capture boundary: hard reset and process completion precede re-enumeration and fresh serial open; the fresh input is not cleared; no post-open reset line is asserted; and exactly one second fresh cycle is available only before the first accepted frame.

Both devices must enumerate, pass exact installed-Trail readback before the journal and first write, and visibly reboot their displays before the attempt. Writes remain application-only at `0x10000`. Every touched node must be restored, readback-verified, and reset to the exact Trail image after success, failure, or interruption.

## Authority boundary

- Exactly one attempt and two anonymous admitted nodes.
- Monocypher comparison only: five of eight operations, structurally nonselectable.
- No radio operation.
- No broad flash, partition, bootloader, NVS, or OTA-data write.
- Private journal and execution/recovery receipts use new fixed paths; OT-123 and OT-124 private records remain untouched.
- Consumed on success or abort; no continuing or reusable authority.

This decision authorizes measurement only. It does not admit a result in advance and does not authorize candidate or suite selection, Phase 2 completion, support, compatibility, regulatory acceptance, production readiness, secure-LoRa behavior, end-to-end acceptance, or a score change.

## Evidence

- [OT-125 authority](../../tests/benchmarks/crypto/OT-125-OT005-MONOCYPHER-CORRECTIVE-RETRY-AUTHORITY-V0.json)
- [OT-125 authority and runner note](../../tests/hardware/OT-125-2026-08-24.md)
- [OT-124 abort decision](0063-monocypher-comparison-execution-abort.md)
