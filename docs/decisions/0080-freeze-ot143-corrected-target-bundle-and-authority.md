# Decision 0080: freeze the OT-143 corrected-target bundle and authority

- Date: 2026-08-26
- Status: Accepted
- Scope: OT-005 / OT-143 host-only executable binding and authority

## Decision

Accept the canonical OT-143 corrected-target build evidence and executable-
bundle preparation as the immutable successor to OT-142. They leave the
accepted OT-142 source and every frozen predecessor unchanged and bind the
exact corrected-target six-file artifact tuple, pinned ESP-IDF commit,
canonical 14-source-input evidence, unchanged OT-135 START/READY runner,
strict 1,014-frame parser/schema, a fresh restoration-safe coordinator and
private-state namespace, one concrete endpoint-bound adapter, and the exact
Trail restoration application.

Only the 149,824-byte corrected-target application BIN may be written, and
only at application offset `0x10000`. The ELF, linker map, bootloader BIN,
partition-table BIN, and generated sdkconfig are immutable provenance evidence,
not writable payloads. Normal ESP32-S3 ROM logging remains unchanged; endpoint
silence from reset is neither required nor claimed.

Accept the separately generated OT-143 authority for exactly one two-node,
application-only attempt. It requires two distinct endpoints, exact installed-
Trail readback and reset of both nodes before journal creation or application
write, benchmark readback before capture, the frozen OT-135 byte-bounded
control/capture contract, and exact restoration/readback/reset of every touched
node. Recovery-only mode remains available without the benchmark artifact until
restoration succeeds. The authority is accepted but is not executed by this
checkpoint and grants no continuing authority, radio use, or selection.

The one-attempt guarantee is workspace-local operational state, not a global,
hardware-backed, signed, or copy-proof consumption mechanism. In this exact
workspace, creation of the fixed private OT-143 journal consumes the attempt
before the first benchmark write; success and abort remain terminal, and an
existing journal blocks another execution. Deleting private state, copying the
public repository or authority, or operating from another workspace is outside
this guarantee and is not authorized by this decision.

## Consequences

- The exact executable binding and fresh explicit non-reusable authority
  required by OT-142 now exist and validate independently.
- The OT-140 authority remains consumed by its accepted fail-closed attempt and
  grants no inherited or replacement attempt.
- The next permitted hardware step is only the single OT-143-authorized
  two-node application-only attempt from this workspace. Every touched node
  must restore exactly to Trail whether execution succeeds or aborts.
- This checkpoint performs no device access, flash, benchmark execution, radio,
  or phone operation and admits no physical result.
- No benchmark result, candidate or suite selection, Phase 2 completion,
  support, compatibility, regulatory, production, secure-LoRa, end-to-end, or
  score claim changes. V1 remains exact 43.75%, displayed 44%; the historical
  baseline remains exact 31.75%, displayed 32%.
- No public website status update is required because no capability, milestone
  completion, score, field-test readiness, support, release, or physical-
  acceptance claim changes.

## Evidence

- [OT-143 host-only evidence](../../tests/hardware/OT-143-2026-08-26.md)
- [Corrected-target build evidence](../../tests/benchmarks/crypto/OT-143-OT005-MONOCYPHER-CORRECTED-TARGET-BUILD-EVIDENCE-V0.json)
- [Executable-bundle preparation](../../tests/benchmarks/crypto/OT-143-OT005-MONOCYPHER-EXECUTABLE-BUNDLE-PREPARATION-V0.json)
- [One-attempt authority](../../tests/benchmarks/crypto/OT-143-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0.json)
- Coordinator: `tools/ot143_monocypher_coordinator.py`
- Hardware adapter: `tools/ot143_monocypher_hardware_adapter.py`
- Execution-authority validator: `tools/ot143_monocypher_execution_authority.py`
