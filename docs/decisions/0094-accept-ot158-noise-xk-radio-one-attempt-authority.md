# Decision 0094: accept the OT-158 Noise XK radio one-attempt authority

- Date: 2026-08-28
- Status: Accepted
- Scope: OT-005 / OT-158 host-only authority for one later two-node Noise XK radio-cost attempt

## Decision

Accept the owner-approved `OT158NXRA0` authority bound to the exact OT-157
reset-aware executable and restoration bundle. The canonical record is 4,545
bytes with raw SHA-256
`b696598cfdd71d04c2685dc50d74da5cd435ff064a6c1c5ffa985e490f4070f1` and
canonical payload SHA-256
`1cc9b0965cec4ffc5eab87d0045aa5ee5e73e65d75e204ca338cfb2b8ffeac0d`.

The authority permits exactly one later two-node, application-only radio
attempt from this workspace. It binds the exact 296,640-byte benchmark
application; exact 500,944-byte Trail restoration application; OT-156
reset-aware runner and reconnectable runtime; OT-157 restoration-safe
coordinator and concrete adapter; anonymous roles; and frozen US915 profile.
The admitted execution remains A-to-B then B-to-A, with one baseline and one
message-2-withheld forced whole-handshake restart per direction: 14
transmissions, 736 radio-payload bytes, and 1,447,424 microseconds theoretical
airtime.

Both installed Trail applications must pass exact readback and hard reset
before a private one-use journal or first benchmark write. Only the benchmark
application may be written at `0x10000`; bootloader, partition table, NVS, and
other regions remain unwritable. The private journal consumes the authority
before the first write, and either success or abort is terminal. Every touched
node must be restored, read back, and hard-reset exactly to Trail. Recovery is
permitted without the benchmark image and never executes the benchmark or
opens the radio.

## Consequences

- The authority is non-reusable, grants no continuation, and is not copy-proof
  outside the fixed workspace-local one-use state.
- OT-158 performs no device, endpoint, serial, reset, flash, radio, phone,
  cryptographic, or physical operation. No private journal or terminal receipt
  exists yet.
- OT-159 is the sole permitted execution step. Success or abort consumes the
  authority; no retry or replacement authority is implied.
- The raw benchmark messages remain `OTNXK0/v0`; no Packet V1 or OTA1 wrapper
  is admitted.
- No physical OT-155 root cause, candidate, library, suite, handshake/KDF, or
  Packet V1 selection is made. Phase 2 remains incomplete; no readiness,
  support, release, regulatory, or score claim changes.
- V1 remains exact 43.75%, displayed 44%; the historical baseline remains exact
  31.75%, displayed 32%. This internal authority checkpoint does not require a
  website update and remains on the normal ten-task cadence.

## Evidence

- [OT-158 evidence](../../tests/hardware/OT-158-2026-08-28.md)
- [OT-157 canonical preparation](../../tests/benchmarks/crypto/OT-157-OT005-LIBSODIUM-NOISE-XK-RADIO-EXECUTABLE-BUNDLE-PREPARATION-V0.json)
- [OT-158 canonical authority](../../tests/benchmarks/crypto/OT-158-OT005-LIBSODIUM-NOISE-XK-RADIO-ONE-ATTEMPT-AUTHORITY-V0.json)
- Authority validator: `tools/ot158_noise_xk_radio_execution_authority.py`
- Runner: `tools/ot156_noise_xk_radio_runner.py`
- Reconnectable runtime: `tools/ot156_noise_xk_radio_runtime.py`
- Restoration-safe coordinator: `tools/ot157_noise_xk_radio_coordinator.py`
- Concrete fail-closed adapter: `tools/ot157_noise_xk_radio_hardware_adapter.py`
- Focused authority validation: 10/10 passed; the complete OT-153-through-OT-158
  Noise XK chain passed 139/139.
- Raw-byte checkout audit: 254/254 authoritative inputs passed.
- Complete Windows Host validation matrix: passed locally with exit code 0.
