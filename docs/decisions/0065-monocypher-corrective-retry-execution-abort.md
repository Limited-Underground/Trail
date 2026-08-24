# Decision 0065: record the Monocypher corrective-retry execution abort

- Date: 2026-08-24
- Status: Accepted
- Scope: OT-005 / OT-126 abort, root cause, and restoration evidence only

## Decision

Accept the privacy-safe OT-126 abort receipt. Both anonymous nodes passed exact installed-Trail application readback. Node A then received and readback-verified the exact application-only Monocypher benchmark image, but no frame was accepted. Node A was restored, readback-verified, and reset to exact Trail. Node B was never rewritten. A final non-writing hard reset returned both nodes to normal runtime, both USB endpoints returned, and the owner observed both Trail displays on.

The capture defect is confirmed from the immutable firmware and runner contracts. The firmware suppresses startup logs and waits 3,000 ms before its first benchmark frame, in addition to normal boot and USB startup. The OT-125 runner abandons a fresh cycle after eight 250 ms empty reads, at most 2,000 ms. It can therefore close the fresh endpoint before the first frame is possible. Its preflight readbacks also leave both devices in download mode and its abort path resets only benchmark-touched nodes.

There is no Monocypher benchmark result admitted: no frame, primitive result, timing, resource result, comparative result, or score is admitted. No radio or phone was used. Decision 0064's one-time authority is consumed, creates no continuing authority, and cannot be reused.

## Consequences

- Phase 2 remains incomplete and the latest admitted benchmark result remains OT-122.
- OT-125 and its consumed runner remain immutable historical evidence.
- A future attempt requires a new hash-bound successor runner and fresh one-attempt authority.
- The successor must use a fixed capture deadline, preserve at least a 10-second first-frame grace on each fresh endpoint, never reopen after an accepted frame, and reset all preflight devices on success or failure before any journal/write.
- No candidate or suite is selected.
- No support, compatibility, regulatory, production, secure-LoRa, end-to-end, or score claim changes. V1 remains exact 43.75%, displayed 44%.

## Evidence

- [OT-126 abort receipt](../../tests/benchmarks/crypto/OT-126-OT005-MONOCYPHER-CORRECTIVE-RETRY-EXECUTION-ABORT-RECEIPT-V0.json)
- [OT-126 hardware note](../../tests/hardware/OT-126-2026-08-24.md)
