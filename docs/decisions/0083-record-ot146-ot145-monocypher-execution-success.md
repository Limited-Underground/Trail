# Decision 0083: record the OT-146 OT-145-authorized execution success

- Date: 2026-08-26
- Status: Accepted
- Scope: OT-005 / OT-146 bounded Monocypher comparison execution

## Decision

Accept the privacy-safe record of the single OT-145-authorized two-node,
application-only attempt. The non-consuming preflight verified both exact
installed Trail applications, reset both nodes, and received the owner's
visual confirmation that both displays returned. The authorized execution then
validated all 1,014 Monocypher result frames from each anonymous node.

Each node completed the five admitted comparison operations with 100 data-
cache-conditioned and 100 warm samples per operation. Both nodes reported the
same bounded runtime-resource shape: 349,392 starting and 349,372 minimum free
internal 8-bit heap bytes, 20 peak dynamic RAM bytes, 8,192 allocated stack
bytes, 5,048 stack high-water free bytes, 3,144 maximum stack-used bytes, and
zero watchdog resets.

Every benchmark-touched node was restored, read back, and hard-reset exactly
to the 473,152-byte Trail application. The terminal receipt records
`restoration_complete=true`, recovery was not required, and the owner visually
confirmed both displays were back on after execution.

## Consequences

- The OT-145 authority is consumed by this successful attempt, grants no retry
  or continuation, and is not reusable.
- The bounded two-node Monocypher five-of-eight comparison result is admitted.
- Radio remained unused. Monocypher remains structurally nonselectable because
  SHA-256, HKDF-SHA256, and Noise XK are unavailable in its admitted operation
  set.
- This result does not itself complete Phase 2 or select a candidate, library,
  suite, handshake/KDF, or packet-v1 wire format.
- Support, compatibility, regulatory acceptance, production readiness,
  secure-LoRa operation, end-to-end behavior, and score credit remain
  unproven. V1 remains exact 43.75%, displayed 44%; the historical baseline
  remains exact 31.75%, displayed 32%.
- No further Monocypher hardware attempt is authorized. The next gate is an
  independent host-only admission of the completed Phase 2 evidence and an
  explicit suite/library, handshake/KDF, and packet-v1 wire-format selection
  before implementation.
- Public website synchronization is required because this is a newly
  demonstrated bounded two-node benchmark capability, without a percentage or
  field-test-readiness increase.

## Evidence

- [OT-146 execution evidence](../../tests/hardware/OT-146-2026-08-26.md)
- [Sanitized execution receipt](../../tests/benchmarks/crypto/OT-146-OT005-MONOCYPHER-EXECUTION-RECEIPT-V0.json)
- [OT-145 authority](../../tests/benchmarks/crypto/OT-144-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0.json)
- [OT-144 preparation](../../tests/benchmarks/crypto/OT-144-OT005-MONOCYPHER-EXECUTABLE-BUNDLE-PREPARATION-V0.json)
- Coordinator: `tools/ot144_monocypher_coordinator.py`
- Runner: `tools/ot135_monocypher_protocol_runner.py`
- Hardware adapter: `tools/ot144_monocypher_hardware_adapter.py`
