# Decision 0069: freeze the Monocypher execution bundle and one-attempt authority

- Date: 2026-08-24
- Status: Accepted
- Scope: OT-005 / OT-130 host-only execution preparation and authority

## Decision

Accept the OT-130 immutable execution-bundle preparation as the exact successor
to OT-129. It binds the unchanged five-of-eight Monocypher comparison target,
its complete source inputs, the START/READY transport, the strict 1,014-frame
parser/schema, the restoration-safe two-node coordinator, and the exact Trail
restoration image. Two fresh, initially absent, cache-disabled ESP-IDF v6.0.2
builds reproduced the same application BIN, ELF, map, sdkconfig, bootloader,
and partition-table tuple with zero compiler warnings.

Accept the separately generated OT-130 authority for exactly one two-node,
application-only attempt. The authority is non-reusable and is consumed on
success or abort. It requires distinct endpoints, visible reset/display preflight on both nodes, exact installed-Trail
readback and reset on both nodes before journal or write, benchmark readback
before capture, the OT-129 retrying START/exact READY contract, a fixed
180-second deadline beginning at READY, and exact restoration/readback/reset of
every touched node. Recovery-only mode remains available without the benchmark
artifact until restoration succeeds.

The authority is accepted but not executed by this checkpoint. It grants no
continuing authority.

## Consequences

- The immutable bundle and fresh non-reusable authority required by Decision
  0068 now exist and validate independently.
- The next permitted hardware step is only the one OT-130-authorized two-node
  Monocypher attempt. Both nodes must be restored exactly to Trail whether the
  attempt succeeds or aborts.
- This checkpoint performs no device access, flash, benchmark execution, radio,
  or phone operation.
- No benchmark result, candidate or suite selection, Phase 2 completion,
  support, compatibility, regulatory, production, secure-LoRa, end-to-end, or
  score claim changes. V1 remains exact 43.75%, displayed 44%.

## Evidence

- [OT-130 host-only evidence](../../tests/hardware/OT-130-2026-08-24.md)
- [Immutable execution-bundle preparation](../../tests/benchmarks/crypto/OT-130-OT005-MONOCYPHER-IMMUTABLE-EXECUTION-BUNDLE-PREPARATION-V0.json)
- [One-attempt authority](../../tests/benchmarks/crypto/OT-130-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0.json)
- Coordinator: `tools/ot130_monocypher_coordinator.py`
- Preparation/authority tool: `tools/ot130_monocypher_bundle_authority.py`
