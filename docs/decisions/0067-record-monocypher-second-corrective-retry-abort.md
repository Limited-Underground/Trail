# Decision 0067: record the Monocypher second corrective-retry abort

- Date: 2026-08-24
- Status: Accepted
- Scope: OT-005 / OT-128 abort and restoration evidence only

## Decision

Accept the privacy-safe OT-128 abort receipt. Both anonymous nodes passed exact installed-Trail application readback before any benchmark write. Node A then received and readback-verified the exact application-only Monocypher benchmark image, but capture did not validate. Node A was restored, readback-verified, and reset to exact Trail. Node B was never benchmark-written. Every benchmark-touched node was restored. A later non-writing hard reset of both devices succeeded and both USB endpoints returned.

The execution receipt deliberately does not retain raw serial data or a failure detail that could distinguish endpoint re-enumeration, a read exception, partial non-newline data, incomplete frames, or semantic parser rejection. The physical root cause is therefore not confirmed. Source inspection separately identifies three contract gaps that must all be corrected before another attempt: there is no host/device start-ready handshake, the host does not accumulate partial lines across its 250 ms read timeout, and endpoint readiness is represented by a fixed delay instead of verified disappearance and return. These are corrective requirements, not a claim about which one caused OT-128.

There is no Monocypher benchmark result admitted: no frame count, primitive result, timing, resource result, comparative result, or score is admitted. No radio or phone was used. Decision 0066's one-time authority is consumed, creates no continuing authority, and cannot be reused.

## Post-record observation

After this decision and its immutable receipt were published, the owner visually confirmed both Trail displays on. That later restoration observation does not alter the receipt's original false observation flag and admits no benchmark result.

## Consequences

- Phase 2 remains incomplete and the latest admitted benchmark result remains OT-122.
- OT-127, its consumed authority, runner, private journal, and private receipt remain immutable historical evidence.
- No further Monocypher hardware attempt is authorized.
- A future attempt requires a host/device readiness handshake, bounded byte accumulation across partial reads, verified endpoint lifecycle, privacy-safe failure classification, complete hardware-free tests, a new immutable runner/firmware binding, and a fresh one-attempt authority published before device access.
- No candidate or suite is selected.
- No support, compatibility, regulatory, production, secure-LoRa, end-to-end, or score claim changes. V1 remains exact 43.75%, displayed 44%.

## Evidence

- [OT-128 abort receipt](../../tests/benchmarks/crypto/OT-128-OT005-MONOCYPHER-SECOND-CORRECTIVE-RETRY-EXECUTION-ABORT-RECEIPT-V0.json)
- [OT-128 hardware note](../../tests/hardware/OT-128-2026-08-24.md)
