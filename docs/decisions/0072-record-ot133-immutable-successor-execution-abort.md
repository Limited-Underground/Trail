# Decision 0072: record the OT-133 immutable successor execution abort

- Date: 2026-08-24
- Status: Accepted
- Scope: OT-005 / OT-133 one-attempt Monocypher successor execution

## Decision

Accept the OT-133 immutable successor bundle, its fresh non-reusable authority,
and the sanitized record of the authority's single execution. The bundle binds
the OT-132 runner, a fresh restoration-safe coordinator and private state,
the exact benchmark application, the unchanged strict 1,014-frame parser and
schema, and the exact Trail restoration application.

The one authorized application-only attempt failed closed on Node A as
`capture_failed` / `preamble_invalid`. One 512-byte read contained nine
complete pre-`READY` records, so `preamble_lines_ignored=9` exceeded the frozen
eight-record limit before any result frame was accepted. This is a precise
bounded host/device compatibility observation; no unpublished record contents
are retained or inferred.

Node A passed benchmark readback, then restored, read back, and reset exactly
to Trail. Node B was never benchmark-written and remained on Trail. The
restoration record is `restoration_complete=true`, and the owner visually
confirmed both Trail logos after the abort.

## Consequences

- The OT-133 authority is consumed by this abort, grants no retry, and is not
  reusable.
- No Monocypher result frame, timing, resource result, candidate selection,
  Phase 2 completion, radio evidence, support, regulatory acceptance,
  production readiness, end-to-end proof, or score credit is admitted.
- The latest admitted benchmark result remains OT-122.
- No further Monocypher hardware attempt is authorized. The next work is
  host-only investigation and adversarial testing of the nine-record boundary
  while preserving the 512-byte/time bounds, exact `READY`, frame-before-
  `READY` rejection, post-`READY` strictness, privacy-safe diagnostics, and the
  real 1,014-frame parser.
- Any later device access requires a new immutable executable successor and
  fresh explicit non-reusable one-attempt authority.
- V1 remains exact 43.75%, displayed 44%.

## Evidence

- [OT-133 execution evidence](../../tests/hardware/OT-133-2026-08-24.md)
- [Executable-bundle preparation](../../tests/benchmarks/crypto/OT-133-OT005-MONOCYPHER-EXECUTABLE-BUNDLE-PREPARATION-V0.json)
- [One-attempt authority](../../tests/benchmarks/crypto/OT-133-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0.json)
- [Sanitized abort receipt](../../tests/benchmarks/crypto/OT-133-OT005-MONOCYPHER-EXECUTION-ABORT-RECEIPT-V0.json)
- Coordinator: `tools/ot133_monocypher_coordinator.py`
- Runner: `tools/ot132_monocypher_protocol_runner.py`
- Hardware adapter: `tools/ot133_monocypher_hardware_adapter.py`
- Authority validator: `tools/ot133_monocypher_execution_authority.py`

