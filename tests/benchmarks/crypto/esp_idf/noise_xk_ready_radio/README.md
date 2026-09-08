# Solicited readiness benchmark target

## Implementation preflight (2026-09-08)

This successor adds only a read-only serial readiness query to the hash-pinned
OT-153 benchmark. The original source remains unchanged. The generated source
is a build artifact, not an independently maintained application copy.

- Target boundary: existing Heltec V4.2 benchmark pins, ESP32-S3, native USB
  serial/JTAG, QIO selection, DIO image header, 80 MHz / 16 MB flash, US915 fixed 915 MHz radio profile and
  2 dBm command setpoint are reused. No new hardware compatibility is inferred.
- Reproducibility: two initially absent build/config directories passed with
  ESP-IDF 6.0.2, RadioLib 7.7.1 and libsodium 1.0.22. Generated source,
  ELF/BIN/map/config/bootloader/partition and compiler identity files are byte
  identical; the shared dependency lock remained unchanged. The compiler
  launchers fix SOURCE_DATE_EPOCH=0 for RadioLib date/time macros, and a prefix
  map makes the generated source's file macro independent of build directory.
  These settings change build metadata, not the firmware display clock.
- Lifecycle: host queries after opening the final serial handle. The query must
  echo a fresh 32-character lowercase hexadecimal challenge and report actual
  startup self-test/current idle state. It neither resets nor arms transmission.
- Concurrency: the existing CLI radio mutex protects the complete response.
  The self-test result is stored before creating the CLI task. Dirty state is
  rejected without wiping attempts, counters, permits or ledger history.
- Persistence: no persistence changes; existing restoration and application-only
  write gates remain mandatory. Firmware is not installed by the build process.
- Validation: generator tampering/anchor tests and compiled real-handler behavior
  precede composed host validation and two clean target builds.
- Hardware: no access is authorized by this target or generator. Fresh identity,
  exact current per-role recovery spans, untouched-region preservation and a
  separate one-use execution grant remain gates for root execution. BLE, display,
  GNSS, battery, cold-power and field-range changes/tests are skipped because this
  derivative changes none of those behaviors.

The new command is `ready <32 lowercase hexadecimal characters>`. Success emits
`OT153 READY schema=OTNXREADY1 challenge=<echo> accepted=yes stale_selftest=yes radio_ready=yes idle=yes tx=no`,
then the existing PROFILE and STATUS records. Failed readiness emits the ordinary
REJECT response and does not manufacture boot receipts.

Generate directly with:

```text
python tools/noise_xk_ready_firmware_source.py --source tests/benchmarks/crypto/esp_idf/ot153_noise_xk_radio_cost/main/app_main.cpp --output <build-directory>/ready_app_main.cpp
```

CMake invokes the same generator and links the existing Noise adapter/radio HAL.
Only a separately verified application image may be installed; the generated
partition table must never replace the installed Trail table.

## Accepted build validation (2026-09-08)

Six generator and compiled-handler tests passed without failures or skips.
Both clean target builds exited successfully. The application is 297,152 bytes,
version `nxk-ready-v1`, SHA-256
`3c3913ee4c2a60bd4168e0bb60eca6a0f2a641ba8f283f99f656c04c07b301c7`.
Esptool accepted its checksum and validation hash. The local libsodium copy
matches all 733 files in the established source tree by relative path and SHA-256.

[Build evidence](../../OT-163-SOLICITED-READINESS-BUILD-2026-09-08.json)
records exact source/artifact hashes, pins, configuration and comparisons.
ESP-IDF deliberately maps QIO configuration to DIO image/flash arguments; no
flash-mode override or partition-table installation was performed. Determinism
is demonstrated with this pinned Windows toolchain. Hardware readiness and
restoration execution remain separate gates.
