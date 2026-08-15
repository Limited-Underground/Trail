# Heltec V4 bench candidate target

Status: build-only candidate; not flashed; not supported hardware.

This bounded ESP-IDF target is the first native OpenTrail build surface for the
ESP32-S3 family used by the two assembled Heltec V4 bench clients. It proves
only that a minimal application can be compiled with the pinned toolchain. It
does not establish the exact received board revision or authorize a device
write.

## Allowed behavior

The application may initialize the ESP-IDF runtime, emit one fixed startup
line, and emit a recurring USB Serial/JTAG heartbeat. The only dynamic value
owned and read by the application is boot-local elapsed milliseconds. The
application does not read or print chip IDs, MAC addresses, serial numbers,
coordinates, keys, identities, channel data, or other device-specific values.
ESP-IDF boot/runtime logging shares the console and remains an unreviewed
surface until target build and runtime evidence exists.

## Deliberately absent

- SX1262 or other radio initialization and transmission
- Bluetooth or Wi-Fi initialization
- GNSS access
- application access to NVS, filesystem, OTA, or other persistence; the
  framework's generated default partition table remains a separate build-review
  surface
- identity, pairing, provisioning, keys, or secrets
- board GPIO, OLED, battery, charger, or power-control bindings
- the complete `PortableClientComposition`
- device-write, port-selection, erase, or recovery commands

The machine-readable [target contract](target-contract.json) is enforced by a
host admission test. The build helper contains compile commands only.

## Build gate

Use `tools/Build-HeltecV4BenchTarget.ps1` from an already installed and exported
ESP-IDF v6.0.2 environment. The helper fails closed on a different reported
ESP-IDF version, selects only the `esp32s3` compile target, and writes build
outputs beneath the repository's ignored `build/targets` directory. After a
successful build it runs ESP-IDF size analysis and writes byte counts plus
SHA-256 hashes for the application BIN, ELF, and map to an ignored
`build-evidence.json` explicitly marked `NOT-FLASHED`.

ESP-IDF's own successful-build output may print suggested follow-up commands
for flashing. Those informational suggestions are not executed by this helper;
the helper contains no port, erase, write, or flash action.

No hardware action is part of this increment. Before any later write, the
separate bring-up procedure must record exact-unit authority, preserve or
replace the current firmware intentionally, and prove manual ROM recovery on a
sacrificial-first device.
