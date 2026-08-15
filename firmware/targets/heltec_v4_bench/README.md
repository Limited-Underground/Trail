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
verifies the tombstone and stable private bond binding. It opens no storage or
transport.
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

The dormant registration path owns NimBLE registration, connection,
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

The target now also build-links the target-neutral lifecycle used by that
in-memory boot check. It owns one exact registered Command/Stream/CCCD tuple,
one connection, negotiated MTU, link/application authorization state, one
indication subscription, one response reservation, one outstanding indication,
opaque delivery-token correlation, timeout containment, and disconnect cleanup.
The callback adapter now supplies those event bindings, but only in dormant
code: `app_main` never installs or starts it. The deterministic self-check uses
fixed in-memory evidence instead of calling NimBLE.

The restricted authorization lifecycle and real callback composition are
build-linked and exercised only by deterministic in-memory boot checks. If a
future application installs this exact path, both a new secure bond and a
secure reconnect by an existing owner use protected v0.1 Protocol Info and
Claim Start; there is no v0.0 reconnect shortcut. The injected trusted binding,
authorization/persistence authority, physical-presence decision source, and
timer owner remain outside this increment, so no live claim path exists yet.

The build-only application deliberately never calls the registration function,
initializes NimBLE/controller state, installs the GATT/GAP callbacks, or
advertises. Thus the service is compiled and link-checked but not discoverable
or usable. This preserves rejection as the default until the pairing,
authorization, exact-board, persistence, CCCD/subscription, and runtime-owner
gates are designed.

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
- Bluetooth controller/host initialization, advertising, scanning, connection,
  or live GATT registration; only the exact dormant NimBLE definition and
  fail-closed registration/access callbacks are compiled
- GNSS access
- application access to NVS, filesystem, OTA, or other persistence; only the
  fail-closed security preflight and in-memory persistence self-check exist. The
  framework's generated default partition table remains a separate build-review
  surface
- identity, pairing, provisioning, keys, or secrets; no HMAC/eFuse or protected
  NVS operation executes
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
