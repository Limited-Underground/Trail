# Decision 0068: accept the host-only Monocypher start-ready protocol correction

- Date: 2026-08-24
- Status: Accepted
- Scope: OT-005 / OT-129 computer-only protocol correction

## Decision

Accept the isolated OT-129 successor firmware and capture transport as the
computer-only correction for the gaps recorded by OT-128. The host opens a fresh
endpoint only after either observed disappearance/return or three stable-present
polls, then retransmits the fixed `OTCBXCTL1 START\n` command every 250 ms until
the device returns exact `OTCBXCTL1 READY\n`. The device accepts a complete START
independently of USB read boundaries, treats repeated START commands idempotently,
drains READY, and only then emits the unchanged 1,014 `OTCBXRF2` result frames.

The host retains partial bytes across empty 250 ms reads, bounds printable startup
chatter, starts the benchmark deadline only after READY, never resets or reopens
after START transmission begins, and classifies failures with closed public-safe
codes and bounded counters. Raw ports, device identities, traffic, exception text,
and exception contexts are not retained in a public result.

The frozen OT-123 firmware, shared frame writer, parser, and OT-123/125/127 runners
remain unchanged. OT-129 is a successor, not a rewrite of historical evidence.

## Consequences

- The four source-level corrective requirements recorded by OT-128 are satisfied
  in computer-only implementation and adversarial tests.
- This decision does not authorize device access, flashing, benchmark execution,
  radio use, or a further Monocypher attempt.
- Before hardware use, the successor target and transport still require an exact
  immutable build/runner binding, integration with the restoration-safe two-node
  coordinator, and a fresh non-reusable execution authority.
- No benchmark result, candidate selection, Phase 2 completion, support,
  compatibility, regulatory, production, secure-LoRa, or score claim changes.
  V1 remains exact 43.75%, displayed 44%.

## Evidence

- [OT-129 host-only evidence](../../tests/hardware/OT-129-2026-08-24.md)
- Successor firmware target:
  `tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129`
- Successor capture transport: `tools/ot129_monocypher_protocol_runner.py`
