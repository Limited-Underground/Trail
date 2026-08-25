# Decision 0074: accept the host-only Monocypher byte-bounded preamble correction

- Date: 2026-08-25
- Status: Accepted
- Scope: OT-005 / OT-135 computer-only capture-boundary correction

## Decision

Accept the new OT-135 successor capture runner as the computer-only correction
for the bounded OT-133 `capture_failed` / `preamble_invalid` boundary. Complete
pre-`READY` records remain opaque and are admitted only while their cumulative
complete-record bytes remain at or below 512 and the unchanged control deadline
has not expired. The independent eight-record ceiling is removed: each complete
record consumes at least its newline byte, so the 512-byte budget already
provides a finite record bound without overfitting the observed nine-record boot.

Only the exact complete `OTCBXCTL1 READY\n` line starts capture. A result-frame
prefix before `READY` still fails closed, an exact duplicate `READY` after the
transition remains invalid, and any non-frame record after `READY` remains
malformed. Partial bytes still survive empty reads, chatter never extends the
control deadline, and the unchanged strict 1,014-frame parser remains the only
result-admission boundary. A fragmented exact `READY` prefix may finish after
the 512-byte complete-record budget is full; any mismatching byte fails closed.

OT-129 through OT-133 remain byte-for-byte immutable. OT-135 is a host-only
successor with no hardware CLI, serial backend, artifact writer, execution
authority, or radio surface. All test preambles are fabricated; no private raw
capture is retained, reconstructed, inferred, or published.

## Consequences

- Fourteen adversarial OT-135 groups pass. They admit nine complete fabricated
  records totaling exactly 512 bytes and more than nine short records within
  the same byte budget; reject byte 513 and frames before `READY`; preserve
  exact/duplicate/post-`READY` behavior, deadlines, privacy-safe diagnostics,
  and the real 1,014-frame parser.
- The frozen OT-132 14-group suite still passes unchanged, including its
  historical eight-record rejection, and all OT-133 evidence remains consumed.
- The deterministic checkout audit now covers 71 authoritative raw-byte inputs,
  including the LF-pinned OT-135 runner and tests.
- This decision does not authorize device access, flashing, benchmark
  execution, radio use, or another Monocypher attempt.
- Any future device attempt requires a new immutable executable binding and
  fresh explicit non-reusable one-attempt authority.
- No benchmark result, candidate selection, Phase 2 completion, support,
  compatibility, regulatory, production, secure-LoRa, end-to-end, or score
  claim changes. V1 remains exact 43.75%, displayed 44%.
- No public website status update is required because capability and milestone
  status do not change.

## Evidence

- [OT-135 host-only evidence](../../tests/hardware/OT-135-2026-08-25.md)
- Successor capture runner: `tools/ot135_monocypher_protocol_runner.py`
- Adversarial tests: `tests/host/ot135_monocypher_protocol_runner_tests.py`
