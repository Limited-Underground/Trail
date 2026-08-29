# Decision 0098: accept the OT-162 corrected Noise XK radio one-attempt authority

- Date: 2026-08-28
- Status: Accepted
- Scope: OT-005 / OT-162 host-only authority for one later corrected two-node Noise XK radio-cost attempt

## Decision

Accept the owner-approved `OT162NXRA0` authority bound to the exact OT-161
corrected executable and restoration bundle. The canonical record is 4,545
bytes with raw SHA-256
`58964547c9f38ff2688da14f31421216eb2bc2705916abeee75e202ffa876a58` and
canonical payload SHA-256
`e1b16623d3b9059e6b190a37bc5c7e727479cba872d7f95232336996402d4e79`.

The authority pins the exact 6,113-byte OT-161 preparation with raw SHA-256
`942f7bda82273e8d06901827934eac6dc2c30ac3135ba614c2067eecb8cb171c`
and canonical payload SHA-256
`0364615ab9d1129f4b3d83e0ea34d66da0b6e4a8a3070646d277984881388e9f`.
That preparation binds the exact OT-156 reset-aware runner and reconnectable
runtime, accepted OT-160 corrected coordinator and concrete adapter, unchanged
OT-153 firmware/build lineage, 296,640-byte benchmark application, exact
500,944-byte Trail restoration application, anonymous roles, and frozen US915
profile.

The authority permits exactly one later two-node, application-only radio
attempt from this workspace. The admitted execution remains A-to-B then
B-to-A, with one baseline and one message-2-withheld forced whole-handshake
restart per direction: 14 transmissions, 736 radio-payload bytes, and
1,447,424 microseconds theoretical airtime.

Both installed Trail applications must pass exact readback and hard reset
before the accepted OT-160 private one-use journal or first benchmark write.
Only the benchmark application may be written at `0x10000`; bootloader,
partition table, NVS, and other regions remain unwritable. The private journal
consumes the authority before the first write, and either success or abort is
terminal. Every touched node must be restored, read back, and hard-reset
exactly to Trail. Recovery is permitted without the benchmark image and never
executes the benchmark or opens the radio.

## Consequences

- The authority is non-reusable, grants no continuation, and is not copy-proof
  outside the fixed workspace-local one-use state.
- OT-162 creates no fresh coordinator, adapter, journal, execution receipt,
  recovery receipt, or runtime-private namespace. Future execution retains the
  accepted OT-160 private filenames and `OT160NXJ0` / `OT160NXCR0` schemas.
- OT-162 performs no device, endpoint, serial, reset, flash, radio, phone,
  cryptographic, or physical operation. No private journal or terminal receipt
  exists yet.
- OT-163 is the sole permitted execution step. Success or abort consumes the
  authority; no retry or replacement authority is implied.
- The unused OT-158 authority remains unconsumed historical evidence but is
  permanently bound to defective OT-157 and cannot transfer to OT-161/OT-162.
- The raw benchmark messages remain `OTNXK0/v0`; no Packet V1 or OTA1 wrapper
  is admitted.
- No candidate/library/suite, handshake/KDF, Packet-v1, Phase 2/3 completion,
  readiness, support, release, regulatory, production, or score claim changes.
- V1 remains exact 43.75%, displayed 44%; the historical baseline remains exact
  31.75%, displayed 32%. This internal authority checkpoint does not require a
  website update.

## Validation

- Focused OT-162 authority validation: 10/10 passed.
- Exact OT-153 bundle, OT-157 bundle, OT-160 coordinator/adapter, OT-161 bundle,
  and OT-162 authority chain: 56/56 passed.
- Authoritative raw-byte checkout audit: 274/274 inputs passed.
- Complete Windows Host validation matrix: passed locally with exit code 0.

## Evidence

- [OT-162 evidence](../../tests/hardware/OT-162-2026-08-28.md)
- [OT-161 canonical preparation](../../tests/benchmarks/crypto/OT-161-OT005-LIBSODIUM-NOISE-XK-RADIO-EXECUTABLE-BUNDLE-PREPARATION-V0.json)
- [OT-162 canonical authority](../../tests/benchmarks/crypto/OT-162-OT005-LIBSODIUM-NOISE-XK-RADIO-ONE-ATTEMPT-AUTHORITY-V0.json)
- Authority validator: `tools/ot162_noise_xk_radio_execution_authority.py`
- Runner: `tools/ot156_noise_xk_radio_runner.py`
- Reconnectable runtime: `tools/ot156_noise_xk_radio_runtime.py`
- Corrected coordinator: `tools/ot160_noise_xk_radio_coordinator.py`
- Concrete fail-closed adapter: `tools/ot160_noise_xk_radio_hardware_adapter.py`
