# Decision 0081: correct the OT-143 runtime binding before hardware

- Date: 2026-08-26
- Status: Accepted
- Scope: OT-005 / OT-144 host-only executable-runtime correction

## Decision

Record that the accepted OT-143 authority, raw SHA-256
`235e1227ef8ebed75a510335b017f9c0539d0508426a11414546591002f1bce0`,
is unconsumed but non-executable. The OT-143 preparation and authority bind the
149,824-byte corrected OT-142 application
`ot142_monocypher_corrected_bench.bin` with SHA-256
`8e345d41e869cf781e3d6eb3b2269f26882b7c1d9f43b856ff5795c6cc56a034`,
while `tools/ot143_monocypher_coordinator.py` still requires the historical
149,920-byte OT-139 application `ot139_monocypher_quiet_bench.bin` with SHA-256
`29eee8c7294064d772770e2b4591c352eb0a9068b63f5a1fc62d89481ec5f204`.
The accepted adapter invokes that coordinator, so the three accepted layers do
not describe one executable image.

This discovery does not consume the OT-143 authority. No OT-143 private
journal, execution receipt, or recovery receipt exists, and no device,
endpoint, flash, reset, benchmark, radio, or phone operation occurred. Decision
0080, the OT-143 build evidence, preparation, authority, runtime, and tests
remain immutable historical evidence; none is rewritten or silently repaired.

Accept a fresh OT-144 host-only runtime and preparation that bind the corrected
OT-142 application consistently across the coordinator, adapter, preparation,
and real-file cross-layer validation. The runtime preserves the accepted
two-node application-only, exact installed-Trail preflight, benchmark readback,
OT-135 transport/parser, fail-closed capture, privacy, and exact Trail
restoration boundaries. OT-144 does not create or accept execution authority.
A fresh, separately explicit, non-reusable one-attempt authority is required
before any hardware access.

## Consequences

- The OT-143 authority remains unconsumed historical evidence but cannot
  authorize execution because its accepted coordinator pins a different image.
- OT-144 corrects only the successor runtime/preparation binding. It grants no
  inherited, replacement, or continuing hardware authority.
- Cross-layer validation must reject any disagreement in benchmark name,
  length, digest, write offset, restoration identity, or bound runtime digest.
- No hardware, firmware write, benchmark result, radio, selection, Phase 2
  completion, support, compatibility, regulatory, production, secure-LoRa,
  end-to-end, or score claim is added.
- V1 remains exact 43.75%, displayed 44%; the historical baseline remains exact
  31.75%, displayed 32%.
- No public website status update is required because this internal fail-closed
  correction changes no public capability, milestone completion, score,
  field-test readiness, support, release, or physical-acceptance claim.

## Evidence

- [OT-144 host-only evidence](../../tests/hardware/OT-144-2026-08-26.md)
- [Corrected runtime preparation](../../tests/benchmarks/crypto/OT-144-OT005-MONOCYPHER-EXECUTABLE-BUNDLE-PREPARATION-V0.json)
- Coordinator: `tools/ot144_monocypher_coordinator.py`
- Hardware adapter: `tools/ot144_monocypher_hardware_adapter.py`
- Execution-authority validator: `tools/ot144_monocypher_execution_authority.py`
- Focused validation: `39/39 passed`
- Deterministic raw-byte validation: `130/130 passed`

