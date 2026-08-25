# Decision 0075: freeze the OT-136 immutable successor binding and authority

- Date: 2026-08-25
- Status: Accepted
- Scope: OT-005 / OT-136 host-only executable binding and authority

## Decision

Accept the OT-136 executable-bundle preparation as the immutable successor to
OT-135. It leaves OT-129 through OT-135 unchanged and binds the exact OT-135
byte-bounded START/READY runner, the unchanged strict 1,014-frame parser and
schema, a fresh restoration-safe coordinator and private-state namespace, one
concrete endpoint-bound adapter, the exact Monocypher benchmark application,
and the exact Trail restoration application.

Accept the separately generated OT-136 authority for exactly one two-node,
application-only attempt. It requires two distinct endpoints, exact installed-
Trail readback and reset of both nodes before journal creation or application
write, benchmark readback before capture, the frozen OT-135 byte-bounded
control/capture contract, and exact restoration/readback/reset of every touched
node. Recovery-only mode remains available without the benchmark artifact until
restoration succeeds. The authority is accepted but is not executed by this
checkpoint and grants no continuing authority.

The one-attempt guarantee is workspace-local operational state, not a global,
hardware-backed, signed, or copy-proof consumption mechanism. In this exact
workspace, creating the fixed private OT-136 journal consumes the attempt before
the first benchmark write; success and abort remain terminal, and an existing
journal blocks a second execution. Copying the public repository or authority,
deleting private state, or operating from another workspace is outside this
guarantee and is not authorized by this decision.

## Consequences

- The immutable executable binding and fresh non-reusable authority required by
  Decision 0074 now exist and validate independently.
- OT-133 remains consumed and grants no inherited or replacement attempt.
- The next permitted hardware step is only the single OT-136-authorized
  two-node application-only attempt from this workspace. Every touched node
  must restore exactly to Trail whether execution succeeds or aborts.
- This checkpoint performs no device access, flash, benchmark execution, radio,
  or phone operation and admits no physical result.
- No benchmark result, candidate or suite selection, Phase 2 completion,
  support, compatibility, regulatory, production, secure-LoRa, end-to-end, or
  score claim changes. V1 remains exact 43.75%, displayed 44%; the historical
  baseline remains exact 31.75%, displayed 32%.
- No public website status update is required because no capability, milestone
  completion, score, or physical-acceptance claim changes.

## Evidence

- [OT-136 host-only evidence](../../tests/hardware/OT-136-2026-08-25.md)
- [Executable-bundle preparation](../../tests/benchmarks/crypto/OT-136-OT005-MONOCYPHER-EXECUTABLE-BUNDLE-PREPARATION-V0.json)
- [One-attempt authority](../../tests/benchmarks/crypto/OT-136-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0.json)
- Coordinator: `tools/ot136_monocypher_coordinator.py`
- Hardware adapter: `tools/ot136_monocypher_hardware_adapter.py`
- Execution-authority validator: `tools/ot136_monocypher_execution_authority.py`
