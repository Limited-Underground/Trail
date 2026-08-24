# Decision 0071: accept the host-only Monocypher opaque-preamble correction

- Date: 2026-08-24
- Status: Accepted
- Scope: OT-005 / OT-132 computer-only capture-boundary correction

## Decision

Accept the new OT-132 successor capture runner as the computer-only correction
for the bounded OT-131 `capture_failed` / `preamble_invalid` boundary. Complete
pre-`READY` records are treated as opaque and ignored only within the existing
eight-record and 512-byte limits. This fixes a host-side compatibility gap
without retaining, reconstructing, or inferring the unpublished OT-131
preamble.

Only the exact complete `OTCBXCTL1 READY\n` line starts capture. A result-frame
prefix before `READY` still fails closed, an exact duplicate `READY` after the
transition remains invalid, and any non-frame record after `READY` remains
malformed. Partial bytes still survive empty reads, the control deadline is not
extended by chatter, and the unchanged strict 1,014-frame parser remains the
only result admission boundary. A fragmented exact `READY` prefix may complete
after the opaque-chatter budget is full; any mismatching byte fails closed.

OT-129 and every OT-130/OT-131 binding remain byte-for-byte unchanged. OT-132
is a new host-only successor with no hardware CLI, serial backend, artifact
writer, execution authority, or radio surface.

## Consequences

- Fourteen adversarial OT-132 groups pass, including a fabricated 512-byte
  opaque record, fragmented exact `READY`, byte and record overflow, exact
  control matching, frame-before-`READY`, duplicate `READY`, post-`READY`
  strictness, privacy-safe diagnostics, and the real 1,014-frame parser.
- The frozen OT-129 through OT-131 regression suites remain accepted and
  unchanged.
- This decision does not authorize device access, flashing, benchmark
  execution, radio use, or another Monocypher attempt.
- Any future device attempt requires a new immutable executable binding and
  fresh explicit non-reusable one-attempt authority.
- No benchmark result, candidate selection, Phase 2 completion, support,
  compatibility, regulatory, production, secure-LoRa, end-to-end, or score
  claim changes. V1 remains exact 43.75%, displayed 44%.

## Evidence

- [OT-132 host-only evidence](../../tests/hardware/OT-132-2026-08-24.md)
- Successor capture runner: `tools/ot132_monocypher_protocol_runner.py`
- Adversarial tests: `tests/host/ot132_monocypher_protocol_runner_tests.py`
