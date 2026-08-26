# Decision 0082: accept the OT-145 one-attempt authority

- Date: 2026-08-26
- Status: Accepted
- Scope: OT-005 / OT-145 authority for the OT-144 executable binding

## Decision

Accept the explicitly owner-approved `OT144MOAA0` authority for the unchanged
OT-144 executable-bundle preparation. The authority binds the corrected
149,824-byte Monocypher comparison application, exact Trail restoration image,
OT-144 coordinator and adapter, frozen OT-135 transport, strict parser/schema,
and every accepted preflight, privacy, and exact-restoration boundary.

The authority permits exactly one two-node, application-only attempt from this
workspace. It is consumed on success or abort, is non-reusable, grants no
continuing authority, permits no radio use or selection, and remains
unexecuted. Both installed Trail applications must pass exact readback and
reset before journal creation or any write. Every touched node must restore,
read back, and hard-reset exactly to Trail whether execution succeeds or
aborts. Recovery-only mode remains available without the benchmark artifact
until restoration succeeds.

The one-attempt guarantee is workspace-local operational state, not a global,
hardware-backed, signed, or copy-proof consumption mechanism. Creation of the
fixed private OT-144 journal consumes the attempt before the first benchmark
write; success and abort are terminal. Deleting private state, copying the
public repository or authority, or operating from another workspace is outside
this guarantee and is not authorized by this decision.

The OT-143 authority remains immutable, unconsumed, non-reusable, and
non-executable. It grants no inherited or replacement attempt.

## Consequences

- The fresh explicit authority required by Decision 0081 now exists and
  validates against the unchanged OT-144 preparation and runtime.
- The next permitted hardware step is only this single OT-145-authorized
  two-node application-only attempt from this workspace, with exact Trail
  restoration of every touched node on success or abort.
- This checkpoint performs no device, endpoint, reset, flash, benchmark,
  radio, or phone operation. No private journal or terminal receipt exists.
- No benchmark result, candidate or suite selection, Phase 2 completion,
  support, compatibility, regulatory, production, secure-LoRa, end-to-end, or
  score claim changes. V1 remains exact 43.75%, displayed 44%; the historical
  baseline remains exact 31.75%, displayed 32%.
- No public website status update is required because no capability, milestone
  completion, score, field-test readiness, support, release, or physical-
  acceptance claim changes.

## Evidence

- [OT-145 host-only evidence](../../tests/hardware/OT-145-2026-08-26.md)
- [OT-144 executable-bundle preparation](../../tests/benchmarks/crypto/OT-144-OT005-MONOCYPHER-EXECUTABLE-BUNDLE-PREPARATION-V0.json)
- [OT-144 one-attempt authority](../../tests/benchmarks/crypto/OT-144-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0.json)
- Coordinator: `tools/ot144_monocypher_coordinator.py`
- Hardware adapter: `tools/ot144_monocypher_hardware_adapter.py`
- Execution-authority validator: `tools/ot144_monocypher_execution_authority.py`
