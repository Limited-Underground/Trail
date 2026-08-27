# Decision 0089: freeze the Noise XK radio executable bundle

- Date: 2026-08-27
- Status: Accepted
- Scope: OT-005 / OT-153 host-only Noise XK radio execution and restoration preparation

## Decision

Freeze the exact reproducible ESP32-S3 firmware, host runner, simultaneous
two-node coordinator, concrete hardware adapter, and Trail restoration binding
needed to execute the OT-152 benchmark-only Noise XK radio-cost contract later.
The bundle pins ESP-IDF v6.0.2 at commit
`7101770dc6db2667b3c477cc31365dd1acd6db4e`, RadioLib 7.7.1, libsodium
1.0.22, the accepted `OTNXK0/v0` adapter and radio HAL, two byte-identical
fresh cache-disabled build tuples, the exact application-only write at
`0x10000`, and the exact 500,944-byte Trail restoration application with
SHA-256 `f2a58414f82eed585ba90bf7671b06c8ebfaa82e8b23f6ac2093de95143b4e0e`.

The frozen runtime preserves the exact OT-152 boundary: raw 48/48/64-byte
Noise XK messages, both physical role directions, one baseline and one bounded
whole-handshake restart per direction, 14 transmissions, 736 radio-payload
bytes, and 1,447,424 microseconds of theoretical airtime. It binds local
response deadlines, fresh attempt identity, temporary-secret wipe, stale-frame
rejection, strict receipt/counter validation, independent exact readback, and
restoration of every benchmark-touched node on success, failure, or exception.

The canonical preparation has raw SHA-256 `84d7ba8c601f59115fe5d7c74275dc8ccba807a10f87a14ee5afbbe40b3a184b` and
canonical SHA-256 `06de67211443c1432336a0e1d16f4d62be58d870e93cc4b70c2d494c199bcfd9`. Its reproduced build
tuple is `bootloader.bin 22,480 bytes / 96e83ebe4434cd6c9049a59f396b4f8bd06c159b40259da573bdb701c571eca5; partition-table.bin 3,072 bytes / 7f00b6c042a89b15b0cac534f82ed988caf29278ff5700b0c511eb1b5bb7c820; application BIN 296,640 bytes / ed2eef319d5bca22d1d89a0be61e63463ada1a8fb3277238cdf95cf93093cd3c; application ELF 4,734,928 bytes / 197151373b03149e9fbcf6eea4247a04eb87aaec4b0c30d2965f051c11a4424d`.

## Consequences

- OT-153 is host-only preparation. No device, phone, firmware flash, radio
  transmission, cryptographic execution, or physical measurement occurred.
- OT-153 grants no execution authority. OT-154 must separately record fresh,
  explicit owner approval in one non-reusable two-node radio authority before
  any device operation. Success or abort will consume that authority.
- The raw benchmark messages remain exactly the accepted `OTNXK0/v0` payloads.
  The bundle adds no Packet V1 wrapper, OTA1 acknowledgement, production wire
  format, or production interoperability claim.
- No candidate, library, suite, handshake/KDF, or Packet V1 selection is made.
  Phase 2 remains incomplete; no readiness, support, release, regulatory, or
  score claim changes.
- V1 remains exact 43.75%, displayed 44%. This internal preparation changes no
  public status, so no website update is required.

## Evidence

- [OT-153 evidence](../../tests/hardware/OT-153-2026-08-27.md)
- [Canonical preparation](../../tests/benchmarks/crypto/OT-153-OT005-LIBSODIUM-NOISE-XK-RADIO-EXECUTABLE-BUNDLE-PREPARATION-V0.json)
- Bundle validator: `tools/ot153_noise_xk_radio_bundle.py`
- Runner: `tools/ot153_noise_xk_radio_runner.py`
- Restoration-safe coordinator: `tools/ot153_noise_xk_radio_coordinator.py`
- Concrete fail-closed adapter: `tools/ot153_noise_xk_radio_hardware_adapter.py`
- Focused validation: `75/75 passed (firmware 19, runner 21, bundle 12, coordinator 12, adapter 11)`
- Complete Windows Host validation matrix: `passed locally on 2026-08-27`
