# Heltec V4 bench candidate target

Status: build-only candidate; not flashed; not supported hardware.

This bounded ESP-IDF target is the first native OpenTrail build surface for the
ESP32-S3 family used by the two assembled Heltec V4 bench clients. It proves
only that a minimal application can be compiled with the pinned toolchain. It
does not establish the exact received board revision or authorize a device
write.

## Allowed behavior

The application may initialize the ESP-IDF runtime, run deterministic
boot-time companion codec and request-coordinator self-checks, emit one fixed
PASS or FAIL line for the combined gate, emit one
fixed startup line after PASS, and emit a recurring USB Serial/JTAG heartbeat.
The self-check exercises exact `OTB0`, `OTC0`, and `OTA0` vectors plus semantic
dispatch, then uses fixed fake snapshot/action authorities to verify exact
snapshot and queued-action responses plus byte-identical duplicate replay
without a second authority call. It opens no transport. Failure suspends the
application before startup or heartbeat. A queued result is only a local queue
disposition; it does not claim radio send or delivery. The only dynamic value
owned and read by the application
is boot-local elapsed milliseconds. The application does not read or print chip
IDs, MAC addresses, serial numbers, coordinates, keys, identities, channel
data, or other device-specific values. ESP-IDF boot/runtime logging shares the
console and remains an unreviewed surface until target build and runtime
evidence exists.

The image also compiles one real ESP-IDF v6.0.2 NimBLE GATT definition for the
exact companion service, Protocol Info, Command, and Stream UUIDs. The fixed
definition self-check verifies their exact bytes, properties, 128-bit key-size
requirement, and Protocol Info value. Protocol Info is encrypted,
authenticated, and authorized Read; Command is encrypted, authenticated, and
authorized Write With Response only; Stream is Notify and Indicate with an
encrypted, authenticated, and authorized CCCD. The registration function
requires injected persistent application authorization and a constructed
device-authority coordinator. Protocol Info retains per-access live link
security and application-authorization checks.

Command access remains unconditionally denied after those checks in this
build-only increment. No code dispatches a request to the coordinator or emits
a Stream indication. That is intentional: the target does not yet own NimBLE
registration events, the exact generated CCCD handle, per-connection subscribe
state, or disconnect cleanup. Inferring a CCCD as `value_handle + 1` is not an
accepted substitute. A later runtime increment must establish those facts
before any coordinator call, so device authority cannot mutate without a
verified response path.

The build-only application deliberately never calls the registration function,
initializes NimBLE/controller state, installs a GAP authorization callback, or
advertises. Thus the service is compiled and link-checked but not discoverable
or usable. This preserves rejection as the default until the pairing,
authorization, exact-board, persistence, CCCD/subscription, and runtime-owner
gates are designed.

## Deliberately absent

- SX1262 or other radio initialization and transmission
- Bluetooth controller/host initialization, advertising, scanning, connection,
  or live GATT registration; only the exact dormant NimBLE definition and
  fail-closed registration/access callbacks are compiled
- GNSS access
- application access to NVS, filesystem, OTA, or other persistence; the
  framework's generated default partition table remains a separate build-review
  surface
- identity, pairing, provisioning, keys, or secrets
- a running Bluetooth stack, registered GATT service, controller session, or
  device transport; the fixed coordinator session and fake authorities are
  local computation only
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
