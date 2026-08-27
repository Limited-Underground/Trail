# Decision 0090: accept the OT-154 Noise XK radio one-attempt authority

- Date: 2026-08-27
- Status: Accepted
- Scope: OT-005 / OT-154 host-only authority for one later two-node Noise XK radio-cost attempt

## Decision

Accept the explicitly owner-approved `OT154NXRA0` authority bound to the
unchanged OT-153 executable and restoration bundle. The canonical 3,330-byte
record has raw SHA-256
`d792bd6814d03a196f74fdda5a675386e74b02e7e65335892a57f51a7dc5beb0`
and canonical payload SHA-256
`490abc8717f5593a137e87d6e359e4372a4c120b782f10bacd557298cd4d61e6`.

The authority permits exactly one later two-node, application-only radio
attempt from this workspace. It binds the exact 296,640-byte benchmark
application, the exact 500,944-byte Trail restoration application, the strict
runner, simultaneous restoration-safe coordinator, concrete endpoint-bound
adapter, anonymous roles, and frozen US915 profile. The admitted execution is
A-to-B then B-to-A, with one baseline and one message-2-withheld forced
whole-handshake restart per direction: 14 transmissions, 736 radio-payload
bytes, and 1,447,424 microseconds theoretical airtime.

Both installed Trail applications must pass exact readback and hard reset
before a private one-use journal or first benchmark write. Only the benchmark
application may be written at `0x10000`; bootloader, partition table, NVS, and
other regions remain unwritable. The private journal consumes the authority
before the first write, and either success or abort is terminal. Every touched
node must be restored, read back, and hard-reset exactly to Trail. Recovery is
permitted without the benchmark image and never executes the benchmark or
opens the radio.

## Consequences

- The authority is non-reusable, grants no continuation, and is not
  copy-proof outside the fixed workspace-local one-use state.
- OT-154 performs no device, endpoint, reset, flash, radio, phone, or physical
  operation. No private journal or terminal receipt exists yet.
- OT-155 is the sole permitted execution step. Success or abort consumes the
  authority; no retry or replacement authority is implied.
- The raw benchmark messages remain `OTNXK0/v0`; no Packet V1 or OTA1 wrapper
  is admitted.
- No candidate, library, suite, handshake/KDF, or Packet V1 selection is made.
  Phase 2 remains incomplete; no readiness, support, release, regulatory, or
  score claim changes.
- V1 remains exact 43.75%, displayed 44%; the historical baseline remains
  exact 31.75%, displayed 32%. This internal authority checkpoint does not
  require a website update and remains on the normal ten-task cadence.

## Evidence

- [OT-154 evidence](../../tests/hardware/OT-154-2026-08-27.md)
- [OT-153 canonical preparation](../../tests/benchmarks/crypto/OT-153-OT005-LIBSODIUM-NOISE-XK-RADIO-EXECUTABLE-BUNDLE-PREPARATION-V0.json)
- [OT-154 canonical authority](../../tests/benchmarks/crypto/OT-154-OT005-LIBSODIUM-NOISE-XK-RADIO-ONE-ATTEMPT-AUTHORITY-V0.json)
- Authority validator: `tools/ot153_noise_xk_radio_execution_authority.py`
- Runner: `tools/ot153_noise_xk_radio_runner.py`
- Restoration-safe coordinator: `tools/ot153_noise_xk_radio_coordinator.py`
- Concrete fail-closed adapter: `tools/ot153_noise_xk_radio_hardware_adapter.py`
- Focused validation: `52/52 passed (authority 8, adapter 11, coordinator 12, runner 21)`
- Raw-byte checkout audit: `230/230 passed`
- Complete Windows Host validation matrix: `passed locally on 2026-08-27`
