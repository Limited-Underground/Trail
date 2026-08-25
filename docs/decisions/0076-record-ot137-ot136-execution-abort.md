# Decision 0076: record the OT-137 OT-136 execution abort

- Date: 2026-08-25
- Status: Accepted
- Scope: OT-005 / OT-137 one-attempt Monocypher execution

## Decision

Accept the sanitized record of the single OT-136-authorized application-only
attempt. The non-consuming preflight first verified both exact installed Trail
applications, reset both nodes, and received the owner's visual confirmation
that both displays returned to the Trail logo.

The sole authorized attempt then reached Node A benchmark readback and failed
closed as `capture_failed` / `preamble_invalid` before any result frame was
accepted. Two 512-byte reads produced 1,024 observed bytes. Eleven complete
opaque pre-`READY` records were counted while accepted complete-record bytes
remained within the cumulative 512-byte allowance; the remaining partial
pre-`READY` data was not an exact `READY` prefix and caused the bounded
`preamble_bytes + len(partial)` state to exceed 512 bytes. No raw record or
partial content is retained or inferred.

Node A restored, read back, and reset exactly to Trail. Node B was never
benchmark-written and remained on Trail. The terminal receipt records
`restoration_complete=true`, and the owner visually confirmed both Trail logos
after the abort.

## Consequences

- The OT-136 authority is consumed by this abort, grants no retry, and is not
  reusable.
- No Monocypher result frame, timing, primitive result, resource result,
  candidate selection, Phase 2 completion, radio evidence, support, regulatory
  acceptance, production readiness, end-to-end proof, or score credit is
  admitted.
- The latest admitted benchmark result remains OT-122.
- No further Monocypher hardware attempt is authorized. The next work must be
  host-only investigation of the bounded pre-`READY` partial-data boundary and
  the live boot/control compatibility before proposing another executable
  successor.
- Any later device access requires a new immutable executable binding and fresh
  explicit non-reusable one-attempt authority.
- V1 remains exact 43.75%, displayed 44%; the historical baseline remains exact
  31.75%, displayed 32%.
- Public website synchronization is required because the canonical latest
  increment, evidence, and next gate changed, even though no milestone
  percentage or physical-acceptance claim changed.

## Evidence

- [OT-137 execution evidence](../../tests/hardware/OT-137-2026-08-25.md)
- [Executable-bundle preparation](../../tests/benchmarks/crypto/OT-136-OT005-MONOCYPHER-EXECUTABLE-BUNDLE-PREPARATION-V0.json)
- [One-attempt authority](../../tests/benchmarks/crypto/OT-136-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0.json)
- [Sanitized abort receipt](../../tests/benchmarks/crypto/OT-137-OT005-MONOCYPHER-EXECUTION-ABORT-RECEIPT-V0.json)
- Coordinator: `tools/ot136_monocypher_coordinator.py`
- Runner: `tools/ot135_monocypher_protocol_runner.py`
- Hardware adapter: `tools/ot136_monocypher_hardware_adapter.py`
- Authority validator: `tools/ot136_monocypher_execution_authority.py`
