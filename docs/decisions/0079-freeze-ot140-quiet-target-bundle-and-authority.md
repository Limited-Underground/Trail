# Decision 0079: freeze the OT-140 quiet-target bundle and authority

- Date: 2026-08-25
- Status: Accepted
- Scope: OT-005 / OT-140 host-only executable binding and authority

## Decision

Accept the OT-140 executable-bundle preparation as the immutable successor to
OT-139. It leaves the accepted quiet target and every frozen predecessor
unchanged and binds the exact OT-139 six-file artifact tuple, pinned ESP-IDF
commit, canonical 14-source-input build evidence, unchanged OT-135 START/READY
runner, strict 1,014-frame parser/schema, a fresh restoration-safe coordinator
and private-state namespace, one concrete endpoint-bound adapter, and the exact
Trail restoration application.

Only the 149,920-byte OT-139 application BIN may be written, and only at
application offset `0x10000`. The ELF, linker map, bootloader BIN,
partition-table BIN, and generated sdkconfig are immutable provenance evidence,
not writable payloads. Normal ESP32-S3 ROM logging remains unchanged; endpoint
silence from reset is neither required nor claimed.

Accept the separately generated OT-140 authority for exactly one two-node,
application-only attempt. It requires two distinct endpoints, exact installed-
Trail readback and reset of both nodes before journal creation or application
write, benchmark readback before capture, the frozen OT-135 byte-bounded
control/capture contract, and exact restoration/readback/reset of every touched
node. Recovery-only mode remains available without the benchmark artifact until
restoration succeeds. The authority is accepted but is not executed by this
checkpoint and grants no continuing authority.

The one-attempt guarantee is workspace-local operational state, not a global,
hardware-backed, signed, or copy-proof consumption mechanism. In this exact
workspace, creation of the fixed private OT-140 journal consumes the attempt
before the first benchmark write; success and abort remain terminal, and an
existing journal blocks another execution. Deleting private state, copying the
public repository or authority, or operating from another workspace is outside
this guarantee and is not authorized by this decision.

## Consequences

- The exact executable binding and fresh explicit non-reusable authority
  required by Decision 0078 now exist and validate independently.
- The OT-136 authority remains consumed by the accepted OT-137 abort and grants
  no inherited or replacement attempt.
- The next permitted hardware step is only the single OT-140-authorized
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

- [OT-140 host-only evidence](../../tests/hardware/OT-140-2026-08-25.md)
- [Executable-bundle preparation](../../tests/benchmarks/crypto/OT-140-OT005-MONOCYPHER-EXECUTABLE-BUNDLE-PREPARATION-V0.json)
- [One-attempt authority](../../tests/benchmarks/crypto/OT-140-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0.json)
- Coordinator: `tools/ot140_monocypher_coordinator.py`
- Hardware adapter: `tools/ot140_monocypher_hardware_adapter.py`
- Execution-authority validator: `tools/ot140_monocypher_execution_authority.py`
