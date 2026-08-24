# Decision 0070: record the OT-131 Monocypher execution abort

- Date: 2026-08-24
- Status: Accepted
- Scope: OT-005 / OT-131 executable boundary, abort, and restoration evidence

## Decision

Accept the OT-131 executable adapter and replacement one-attempt authority as the missing concrete composition around the unchanged OT-130 coordinator. The adapter binds one backend instance to application-only write/readback, hard reset, endpoint presence, and serial open; provides a non-consuming exact-Trail readback/reset preflight; requires explicit owner visual acknowledgement before execution; and exposes only fixed privacy-safe CLI output. The OT-131 authority supersedes the unexecuted OT-130 authority without inheriting its attempt.

Accept only the sanitized OT-131 abort receipt. The one authorized application-only attempt reached Node A benchmark readback, then capture failed closed as `capture_failed` / `preamble_invalid`. The bounded diagnostics record lifecycle `stable_continuous`, one reset, one open, one START write, 512 observed bytes, one complete line, and zero buffered frame lines. They establish the closed failure boundary but neither disclose the preamble nor confirm a physical root cause.

Node A admitted no capture and was restored, readback-verified, and reset to exact Trail. Node B was never benchmark-flashed and remained on Trail. Restoration completed, and the owner visually confirmed both Trail logos after the abort. The private journal and receipt remain unchanged and unpublished.

The OT-131 one-attempt authority is consumed by this abort, creates no continuing authority, and cannot be reused. No Monocypher frame, primitive, timing, resource, or comparative result is admitted. No radio was used.

## Consequences

- Phase 2 remains incomplete and the latest admitted benchmark result remains OT-122.
- No further Monocypher hardware attempt is authorized.
- Any future attempt requires host-only investigation and correction of the bounded preamble failure, complete tests, a new immutable executable binding, and fresh explicit one-attempt authority before device access.
- No candidate or suite is selected.
- No support, compatibility, regulatory, production, secure-LoRa, end-to-end, or score claim changes. V1 remains exact 43.75%, displayed 44%.

## Evidence

- [OT-131 sanitized abort receipt](../../tests/benchmarks/crypto/OT-131-OT005-MONOCYPHER-EXECUTION-ABORT-RECEIPT-V0.json)
- [OT-131 hardware note](../../tests/hardware/OT-131-2026-08-24.md)
- [OT-131 executable preparation](../../tests/benchmarks/crypto/OT-131-OT005-MONOCYPHER-EXECUTABLE-BUNDLE-PREPARATION-V0.json)
- [OT-131 one-attempt authority](../../tests/benchmarks/crypto/OT-131-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0.json)
