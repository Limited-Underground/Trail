# Decision 0096: accept the OT-160 Noise XK hash-helper host correction

- **Status:** Accepted
- **Date:** 2026-08-28
- **Scope:** OT-005 / OT-160 host-only successor to the blocked OT-157/158 composition

## Context

OT-159 recorded that both physical anonymous nodes independently matched the
exact installed Trail application and completed hard reset, but both official
OT-157-bound preflight attempts failed before the private journal, benchmark
write, radio path, receipt, or authority-consumption boundary.

The deterministic host cause is an exported-name collision. The inherited
OT-153 adapter passes exact application readback bytes to
`coordinator._sha256(readback)`. OT-157 substitutes its successor coordinator,
whose local `_sha256` instead accepts a `Path` and calls `path.read_bytes()`.
That local file helper shadows the inherited byte helper and rejects an
otherwise exact readback before the attempt can be consumed.

## Decision

1. Preserve every OT-157, OT-158, and OT-159 source, preparation, authority,
   decision, and evidence byte as immutable history.
2. Add a fresh OT-160 coordinator with `_source_sha256(Path)` for immutable
   source verification and no local `_sha256`. Attribute fallback therefore
   exposes the frozen OT-153 `_sha256(bytes)` helper unchanged.
3. Give the successor a separate private namespace using `ot160-*` journal and
   receipt filenames plus schemas `OT160NXJ0` and `OT160NXCR0`.
4. Add a fresh adapter that reuses the immutable OT-153 application
   write/readback/reset implementation and OT-156 reconnectable runtime, while
   rebinding both inherited coordinator references to the corrected OT-160
   coordinator.
5. Prove the real composed preflight path in deterministic host tests. Mock
   only preparation and subprocess transport: exact bytes must issue two
   application reads at `0x10000` and two resets for anonymous roles A and B;
   corrupt and short first readbacks must fail as generic `preflight_failed`
   after still reading and resetting both roles.
6. Require every tested preflight outcome to create no journal or receipt and
   perform no application write or radio-open operation.
7. Treat OT-160 as host-only correction evidence. It grants no device access,
   bundle acceptance, execution authority, radio attempt, selection, Phase 2
   completion, readiness, or continuation.

## Exact accepted source identities

- OT-160 coordinator: 7,264 bytes, SHA-256
  `444528fd341b3d55f3a5b3224b217620e1b37e3c7960d224aefbe01d9953a02d`.
- OT-160 adapter: 3,906 bytes, SHA-256
  `5cfb1706900e12be2a7fe03a4b558e1a75637af5c03d4da5116a12b1cae82f28`.
- OT-160 coordinator tests: 19,421 bytes, SHA-256
  `4979348106afec502bc436adb45c755d5ae04dc8d2ffa22000427f21d3eee788`.
- OT-160 adapter tests: 10,320 bytes, SHA-256
  `6f6095ef6161bde0d3fa9902560b4620e4dc92bc1f535abdbe5ac0e3a94e828d`.

Focused validation passes 13/13: coordinator behavior 7/7 and composed adapter
behavior 6/6.

## Alternatives rejected

- Editing OT-157 in place: rejected because OT-157 and OT-158 are immutable
  hash-bound historical inputs.
- Rebinding OT-158 to corrected code: rejected because its accepted authority
  cannot be transferred to different coordinator or adapter bytes.
- Teaching the local file helper to accept both paths and bytes: rejected
  because separate helpers make the contract explicit and prevent another
  ambiguous shadow.
- Treating mocked roles as new physical observations: rejected because OT-160
  uses no hardware; its read/reset evidence is deterministic composed-host
  coverage only.

## Consequences

OT-160 removes the deterministic helper-shadow blockage without changing
firmware, benchmark or restoration images, radio protocol, or accepted physical
history. No device, phone, private endpoint, physical read, physical reset,
flash, application write, radio operation, journal, receipt, bundle, or
execution authority was used or created by this correction.

OT-161 must separately freeze the exact corrected executable/restoration
bundle. OT-162 must later accept fresh explicit non-reusable authority before
any corrected hardware attempt. Neither later gate is implied by OT-160.

No milestone completion changes. V1 remains exact 43.75% and displayed 44%;
the historical baseline remains exact 31.75% and displayed 32%. This internal
host correction requires no public website status update.

## Evidence

- [OT-160 host-only evidence](../../tests/hardware/OT-160-2026-08-28.md)
- Coordinator: `tools/ot160_noise_xk_radio_coordinator.py`
- Adapter: `tools/ot160_noise_xk_radio_hardware_adapter.py`
- Coordinator tests: `tests/host/ot160_noise_xk_radio_coordinator_tests.py`
- Adapter tests: `tests/host/ot160_noise_xk_radio_hardware_adapter_tests.py`
