# Heltec V4 bench candidate target

Status: experimentally flashed, public-region post-write verified, and bounded USB runtime plus BLE service advertising observed on `OT-DEV-001`; not supported hardware.

This bounded ESP-IDF target is the first native OpenTrail build surface for the
ESP32-S3 family used by the two assembled Heltec V4 bench clients. OT-059 binds
the build configuration to the recorded `OT-DEV-001` evidence: observed
ESP32-S3 revision v0.2, external 16 MB quad-capable flash, and embedded 2 MB
PSRAM. The `ESP32-S3R2` family-profile part plus QIO/80 MHz flash and quad/80 MHz
PSRAM are build selections, not verified physical clock/interface behavior. It
does not establish the exact received minor board
revision or RF variant, transfer this profile to `OT-DEV-002`, claim support,
or authorize a device write.

## Allowed behavior

The application is coded to initialize the ESP-IDF and NimBLE runtimes after
all deterministic
boot-time companion codec and request-coordinator self-checks, emit one fixed
PASS or FAIL line for the combined gate, emit one
fixed startup line after PASS, and emit a recurring USB Serial/JTAG heartbeat.
The self-check exercises exact `OTB0`, `OTC0`, and `OTA0` vectors plus semantic
dispatch, then uses fixed fake snapshot/action authorities to verify exact
snapshot and queued-action responses plus byte-identical duplicate replay
without a second authority call. A second fixed in-memory sink checks the
one-connection GATT lifecycle's exact handle registration, secure/subscribed
session opening, maximum-size response reservation before action mutation,
indication submission, exact completion token, and duplicate no-reapply path.
An additional fixed in-memory check exercises the restricted authorization
lifecycle: exact 20-byte `OTB0` v0.1 claim-capability evidence, bonded and
encrypted provisional admission, exact 44-byte Pending and 48-byte Accepted
records, distinct delivery tokens, no authority call before Pending
confirmation, terminal reservation before the fixed authority call, and
promotion only after exact terminal confirmation. A fourth fixed in-memory
check composes the real callback adapter with fixed trusted-binding and
indication-port seams, proves the exact v0.1/Pending/Accepted vectors through
that adapter, then proves one normal action response is admitted only after
promotion. A fifth fixed in-memory check encodes one exact protected-store
owner record, reconstructs the persistence adapter to model reboot, verifies
the owner, commits an advancing unowned tombstone, reconstructs again, and
verifies the tombstone and stable private bond binding. A sixth in-memory
check exercises exact runtime-owner startup ordering, one connection, cleanup,
bounded delayed re-advertising, and permanently closed claim/normal-command
status. These checks open no storage or transport.
Failure suspends the
application before startup or heartbeat. A queued result is only a local queue
disposition; it does not claim radio send or delivery. The only dynamic value
owned and read by the application
is boot-local elapsed milliseconds. The application does not read or print chip
IDs, MAC addresses, serial numbers, coordinates, keys, identities, channel
data, or other device-specific values. ESP-IDF boot/runtime logging shares the
console and remains an unreviewed surface until target build and runtime
evidence exists.

The image also compiles one real ESP-IDF v6.0.2 NimBLE GATT definition and
callback adapter for the exact companion service, Protocol Info, Command, and
Stream UUIDs. The fixed definition self-check verifies their exact bytes,
properties, 128-bit key-size requirement, and v0.1 Protocol Info capability
shape. Protocol Info is encrypted, authenticated, and authorized Read;
Command is encrypted, authenticated, and authorized Write With Response only;
Stream is Indicate-only with encrypted, authenticated, and authorized access.
Every protected access re-reads current NimBLE encryption, authentication,
bond, key-size, and ATT-MTU evidence before the target-neutral lifecycle may
proceed. A successful exact 20-byte Protocol Info read is therefore the
device-enforced provisional-path evidence; bond state by itself is not enough.

The target registration path owns NimBLE registration, connection,
security/MTU, subscription, authorization, indication-completion, timeout, and
disconnect callbacks. It discovers and binds the real generated Stream CCCD
handle through `ble_gatts_find_dsc`; it never infers `value_handle + 1`.
Before application authorization, the adapter admits only Protocol Info and
the authorization claim kinds. Normal snapshot/action requests remain denied
until the exact Pending response has completed, a fresh trusted private bond
binding has been resolved at the authority-decision point, and an exact
Accepted/Replaced terminal indication has been confirmed. Response capacity is
reserved before any authority mutation.

Each real indication retains its immutable submission-era connection,
transport generation, session, exchange, value handle, and delivery token.
The pinned ESP-IDF v6.0.2 source-order admission proves active indication
failure is delivered before the application disconnect callback, preventing a
late old completion from being relabeled after connection-handle reuse. Wrong
or stale completion, timeout, security loss, unsubscribe, or disconnect fails
closed.

The target also build-links the target-neutral lifecycle used by that
in-memory boot check. It owns one exact registered Command/Stream/CCCD tuple,
one connection, negotiated MTU, link/application authorization state, one
indication subscription, one response reservation, one outstanding indication,
opaque delivery-token correlation, timeout containment, and disconnect cleanup.
The callback adapter supplies those event bindings. The deterministic boot
self-check uses fixed in-memory evidence instead of calling NimBLE; only after
that check passes is the real runtime startup path reached.

The restricted authorization lifecycle and real callback composition are
build-linked and exercised only by deterministic in-memory boot checks. If a
future application installs this exact path, both a new secure bond and a
secure reconnect by an existing owner use protected v0.1 Protocol Info and
Claim Start; there is no v0.0 reconnect shortcut. The injected trusted binding,
authorization/persistence authority, physical-presence decision source, and
timer owner remain outside this increment, so no live claim path exists yet.

If executed, the application is coded to initialize NimBLE, install the exact
GATT/GAP callbacks, register the protected service, and advertise only the
stable public 128-bit service UUID and standard discoverability flags using a
privacy-capable own-address policy. Its advertising-data payload supplies no
device name, manufacturer data, address field, or device/user/group identifier.
All host callbacks are copied
into a fixed eight-entry queue and consumed by the single `app_main` runtime
owner; queue overflow, startup timeout, host reset, or restart exhaustion
contains the stack without an unbounded retry loop.

Protected authorization storage remains denied by the exact current preflight.
The configured Secure Connections, MITM, bonding, no-input/no-output, and
no-store settings therefore do not provide a usable pairing or secure bond in
this increment. Every successful connection is immediately terminated. An
already-disconnected result releases the exact adapter state and schedules the
bounded advertising restart; any other termination-initiation error contains
the runtime so an unauthenticated peer cannot monopolize the sole connection.
Claims and normal commands remain closed for the lifetime of this composition.

The image also build-links the target-neutral durable authorization adapter and
a target-local security admission preflight. The fixed 32-byte `OAP0/v0`
record carries only state, generation, an opaque 128-bit owner token, reserved
zeroes, and a corruption-detection CRC. The CRC is not authentication or
rollback protection. The injected protected-store contract must compare fresh
state, atomically commit the complete owner or unowned tombstone together with
an independently rollback-resistant generation floor, and return exact
readback. A pre-write failure guarantees no durable change; any possible
post-write failure is uncertain and latches authorization closed.

Private bond binding accepts only a protected bond-store reference and bond
generation, never a public address, peer value, raw bond key, or logged value.
A separately provisioned device-secret PRF must produce the opaque owner token;
re-pairing must allocate a new private reference or generation. The current
candidate has none of those live adapters. Its compile-time preflight reports
NVS encryption not configured. Even a future configuration selection will not
count as runtime proof that protected NVS initialized or that an HMAC eFuse key
is provisioned, read-protected, usable, and distinct from the binding PRF key.

The pinned ESP-IDF v6.0.2 audit identified the applicable future primitives:
HMAC-protected NVS uses `CONFIG_NVS_ENCRYPTION`, the HMAC security provider,
and verified `nvs_flash_read_security_cfg_v2`/secure initialization; a separate
HMAC_UP eFuse key may serve `esp_hmac_calculate` without exposing raw key
material. These APIs are not called by this target. Ordinary or merely
redundant NVS cannot supply the independent rollback floor required here.

## Deliberately absent

- SX1262 or other radio initialization and transmission
- any evidence of Bluetooth connection, GATT exchange, link security, bonding,
  authorization, or Ready state; physical controller/host startup and service
  advertising visibility were observed only through the bounded OT-061 gate
- GNSS access
- application access to NVS, `ot_state`, filesystem, OTA, or other persistence;
  only the fail-closed security preflight and in-memory persistence self-check
  exist. `OTHP0/v0` reserves recovery-shaped flash regions but grants no updater,
  boot-selection, storage, rollback, or recovery authority
- identity, pairing, provisioning, keys, or secrets; no HMAC/eFuse or protected
  NVS operation executes
- a proven Bluetooth controller session, usable secure bond, authorized device
  transport, or normal command path; the observed stack and advertisement do
  not establish any of these
- board GPIO, OLED, battery, charger, or power-control bindings
- the complete `PortableClientComposition`
- device-write, port-selection, erase, or recovery commands

The versioned `OTHP0/v0` [partition layout](partitions.csv) is an explicit
bench recovery boundary for the recorded 16 MB flash. It provides a 5,177,344-byte factory fallback slot, two equal 5,242,880-byte
OTA-capable application slots, and a final 1,048,576-byte application-defined
type `0x40` `ot_state` reservation ending exactly at the 16 MB boundary. It
contains no ordinary NVS or PHY-data partition. No updater, OTA selection,
checkpoint store, target storage owner, rollback, or recovery executor is
implemented, and no code opens `ot_state`. No NVS partition exists in this
layout, and the reservation cannot satisfy the denied OT-054 security preflight.

The machine-readable [target contract](target-contract.json) is enforced by a
host admission test. The build helper contains compile commands only.

## Build gate

Use `tools/Build-HeltecV4BenchTarget.ps1` from an already installed and exported
ESP-IDF v6.0.2 environment. The helper fails closed on a different reported
ESP-IDF version, selects only the `esp32s3` compile target, and writes build
outputs beneath the repository's ignored `build/targets` directory. After a successful build it fails closed unless generated configuration selects
the exact 16 MB QIO/80 MHz flash selection, 2 MB quad/80 MHz PSRAM selection,
and `OTHP0/v0` custom partition profile. ESP-IDF v6.0.2 intentionally leaves
the image bootstrap header in DIO mode and enables quad flash during boot; the
helper checks that expected DIO header rather than misreporting it as QIO. It
checks the application image header directly,
decodes and verifies the generated partition binary with the pinned ESP-IDF
tool, runs size analysis, and writes exact byte counts plus SHA-256 hashes for
the application BIN, ELF, map, partition table, generated configuration, and
source partition CSV to ignored `build-evidence.json`. That helper-generated
receipt remains explicitly marked `NOT-FLASHED` because the helper itself never
accesses hardware; OT-061 physical execution is recorded separately in
`physical-flash-plan.json` and the dated hardware evidence.

ESP-IDF's own successful-build output may print suggested follow-up commands
for flashing. Those informational suggestions are not executed by this helper;
the helper contains no port, erase, write, or flash action.

OT-061 selected only `OT-DEV-001`, kept `OT-DEV-002` disconnected, and
rehearsed manual ESP32-S3 ROM entry plus return to the unchanged public MeshCore
runtime. The owner then authorized one full-chip erase and one write of the
four frozen OpenTrail regions. Every input hash matched, the single write
completed, a separate public-region verification passed, and the board stayed
in ROM until verification succeeded. After one manual reset, a privacy-safe
twelve-second observation found at least two five-second heartbeat records and
no self-check, runtime, panic, abort, or assertion failure.

That result proves the boot self-check gate completed and the NimBLE runtime
startup call returned on this experimental unit. One owner-observed Android
scan using the exact accepted Trail APK found one compatible OpenTrail service
candidate. It did not select, connect, or pair, and no address or identifier was
retained. This proves physical BLE service advertising visibility, not GATT
exchange, authorization, Ready state, protected storage, LoRa, GNSS, display,
GPIO, power/endurance, regulatory fit, or support. The OLED is expected to
remain blank. The exact result and consumed one-attempt authority are recorded
in `physical-flash-plan.json` and
`tests/hardware/OT-061-2026-08-16.md`. No additional erase, write, recovery, or
unit-2 authority remains. Any later MeshCore recovery stays owner-operated
through the official MeshCore web flasher.
