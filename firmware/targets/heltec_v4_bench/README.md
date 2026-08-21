# Heltec V4 bench candidate target

Status: experimentally flashed with bounded USB runtime, BLE service
advertising, one fixed public BLE read, and physically accepted startup/link
status OLED transitions on `OT-DEV-001`; not supported hardware.

OT-085A installed and read-back verified the exact OT-085 image, then accepted
one fixed, privacy-safe public link-information read and a phone-disconnect
return to advertising. OT-085B accepted the independent automatic path: the
phone made no disconnect request, the bound GATT disconnected inside the
frozen timing window, the owner observed `BLE CONNECTED` followed by
`BLE ADVERTISING`, and exactly one compatible service advertiser returned.
No endpoint identity was required, inferred, or retained.
Decision 0028 defers rollback-protected authorization on this hardware; no
secure ownership, trusted phone, protected write, provisioning, or Ready path
is enabled.

OT-106 promotes the accepted compact-footer implementation into the production
UI component and build-links it into this target. BLE startup, advertising,
connected, retry, and terminal-error frames render the exact compact footer
`BAT:--% GPS:-- BLE:<S/A/C/R/E>` with a blank five-column direction field.
Terminal runtime error has compact `BLE:E` precedence. The startup logo and
explicit full-screen `SELF CHECK FAIL` path remain separate.

The target deliberately binds no live battery percentage, GNSS satellite
observation, or LoRa activity owner. Battery and GPS therefore remain at their
fail-closed placeholders and no RX/TX arrow can appear. Two fresh pinned,
cache-isolated computer builds linked the component with zero warnings and
identical artifacts; this is not device, flash, radio, physical-display, live-
telemetry, support, regulatory, readiness, final-configuration, or score
acceptance. See [OT-106 evidence](../../../tests/hardware/OT-106-2026-08-21.md).
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
callback adapter for the exact companion service, protected Protocol Info,
Command, and Stream UUIDs, plus the fixed public link-information UUID suffix
`0x04`. The fixed definition self-check verifies exact bytes, properties,
protected 128-bit key-size requirements, public READ-only access, and the v0.1 Protocol Info capability
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
this increment. OT-085 keeps one connection open for at most 15 seconds so the
fixed public value can be read and `BLE CONNECTED` can be presented. The owner
then waits while the runtime owner requests exact-link termination and requires
its matching disconnect callback within two seconds; failure contains the
runtime, while success enters
the existing bounded advertising restart. Claims and normal commands remain
closed for the lifetime of this composition. OT-085A physically accepted the
phone-driven disconnect path: one Android 13 phone matched the exact READ-only
value, the owner observed the connected-to-advertising OLED lifecycle, and the
same in-memory endpoint was rediscovered. OT-085B then physically accepted the
independent automatic path without a phone disconnect request: the bound GATT
disconnected inside the policy-derived window, the owner observed the same OLED
lifecycle, and exactly one compatible service advertiser returned. The scan
did not require or infer stable address identity.

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
candidate has none of those live adapters. OT-063 replaces placeholder
configuration booleans with a target-linked read-only admission probe. Under the
current configuration it returns `nvs_encryption_not_configured` before any
partition, eFuse, or HMAC read. If a later reviewed configuration enables the
path, it can only check for the named NVS partition and verify a separately
selected HMAC_UP key's purpose, read protection, and one private operational
self-test. It cannot initialize or open NVS, generate keys, program eFuses, or
change GATT admission. Partition presence and a usable key still do not prove
that protected NVS initialized or that the HMAC key is distinct from the future
binding-PRF key.

The pinned ESP-IDF v6.0.2 audit identified the applicable future primitives:
HMAC-protected NVS uses `CONFIG_NVS_ENCRYPTION`, the HMAC security provider,
and verified `nvs_flash_read_security_cfg_v2`/secure initialization; a separate
HMAC_UP eFuse key may serve `esp_hmac_calculate` without exposing raw key
material. These APIs are not called by this target. Ordinary or merely
redundant NVS cannot supply the independent rollback floor required here.

OT-065 adds a backend-neutral two-slot record/floor coordinator plus an exact
inactive candidate layout and provisioning plan. The candidate would split the
final 1 MiB into a 64 KiB encrypted-flagged `ot_auth` NVS partition and a
960 KiB `ot_state` reservation. It is not active: the accepted partition table,
sdkconfig, target contract, and runtime composition are unchanged, and no key,
rollback-floor provider, migration, or physical authority is selected. The
coordinator remains unlinked from this target until protected media, a distinct
binding PRF, an independent floor, migration, and recovery are accepted.
Twelve strict host groups prove the fail-closed ordering. See
[Decision 0010](../../../docs/decisions/0010-reversible-companion-protected-storage-foundation.md)
and [OT-065 evidence](../../../tests/hardware/OT-065-2026-08-17.md).

OT-066 adds a host-only production composition for the private bond-reference,
device-secret binding, durable one-phone, and GATT authorization seams. It
proves first claim, reconnect, replacement, lease release, re-pair separation,
and fail-closed private evidence in eight strict groups. The target runtime is
unchanged and continues to inject denied binding and authorization authorities;
no target bond source, private session issuer, physical gesture, GATT admission,
or Ready state is enabled. See
[Decision 0011](../../../docs/decisions/0011-host-trusted-gatt-authority-composition.md)
and [OT-066 evidence](../../../tests/hardware/OT-066-2026-08-17.md).

OT-067 adds a target-neutral exact key/value slot-media adapter for the inactive
`ot_auth` candidate. OT-068 now compiles that common adapter plus a target-local
ESP-IDF NVS backend into the Heltec component, but neither is instantiated by
the runtime. The target adapter accepts only an already-opened handle and owns
neither NVS initialization/open/close nor erase, repair, retry, provisioning,
rollback-floor, bond, or GATT authority. The active partition table, sdkconfig,
runtime behavior, installed image, and physical device remain unchanged. See
[Decision 0012](../../../docs/decisions/0012-protected-authorization-kv-slot-media.md)
and [OT-067 evidence](../../../tests/hardware/OT-067-2026-08-17.md).

Eight strict target-backend groups, the ten-group KV-media regression, nine
target-admission groups, and two reproducible pinned builds pass. The generated
evidence records `BUILD-COMPILED-NOT-RUNTIME-INJECTED`. The new build-only BIN
is 470,928 bytes with SHA-256
`9F5AFB320A015E3BFFD866A9EE31F76198739521FA7519845ACDA12B9B52BAE5`;
it was not flashed. See
[Decision 0013](../../../docs/decisions/0013-inactive-heltec-authorization-nvs-backend.md)
and [OT-068 evidence](../../../tests/hardware/OT-068-2026-08-17.md).

OT-069 adds an inactive target-local owner for opening the exact candidate
protected-NVS context around that backend. It consumes existing configuration
only, zeroes its temporary native configuration, performs one exact open
attempt, and fail-closes ambiguous initialization/open/cleanup and reentry.
Ten strict lifecycle groups, the disabled-configuration zero-I/O check, nine
target-admission groups, and two reproducible target builds pass. The new
build-only BIN is 470,928 bytes with SHA-256
`9D4EBCD8BB68183798BF47267252A1B2A94A114FACD16E8CF975AEBE43314EEF`.
The owner is not runtime-injected; active partitions/sdkconfig/runtime/device
state remain unchanged. See
[Decision 0014](../../../docs/decisions/0014-inactive-heltec-authorization-nvs-context.md)
and [OT-069 evidence](../../../tests/hardware/OT-069-2026-08-17.md).

OT-070 adds a pure host transition guard and an exact design-only
`OTPST0/v0` manifest for splitting the final `OTHP0/v0` state region into the
candidate `ot_auth` plus smaller `ot_state` regions. The guard requires fresh
installed-layout readback, verified blank media or separately verified
migration, exact recovery evidence, no runtime/key/eFuse/other-flash request,
and one exact partition-only operation authority. It performs no I/O.
Thirteen strict C++ groups pass across 100 repeats; five manifest groups, the
existing 9/9 target admission, and the complete host gate pass. The manifest
remains denied and every capability/authority is false. Active partitions,
sdkconfig, target contract, CMake, runtime, installed firmware, and device are
unchanged. See
[Decision 0015](../../../docs/decisions/0015-safe-heltec-protected-storage-partition-transition.md)
and [OT-070 evidence](../../../tests/hardware/OT-070-2026-08-17.md).

OT-071 adds a streaming offline verifier and a separate denied read plan for
the exact installed partition table and complete 1 MiB all-`0xFF` source
region required by OT-070. The verifier emits only a fixed sanitized result;
the denied plan contains no executable hardware reader and requires a future
one-use, exact-unit-bound executor. Eight verifier groups, six
transition-manifest groups, and the existing 9/9 target admission
pass. No device read occurred, the read plan selects no unit and grants no
authority, and the active target/build/runtime/installed-device state remains
unchanged. Source proof alone cannot authorize the transition. See
[Decision 0016](../../../docs/decisions/0016-read-only-protected-storage-transition-evidence.md)
and [OT-071 evidence](../../../tests/hardware/OT-071-2026-08-17.md).

## Deliberately absent

- SX1262 or other radio initialization and transmission
- any evidence of Bluetooth connection, GATT exchange, link security, bonding,
  authorization, or Ready state; physical controller/host startup and service
  advertising visibility were observed only through the bounded OT-061 gate
- GNSS access
- application access to NVS, `ot_state`, filesystem, OTA, or other persistence;
  only the fail-closed read-only security probe and in-memory persistence
  self-check exist. The probe does not initialize/open NVS or mutate eFuses.
  `OTHP0/v0` reserves recovery-shaped flash regions but grants no updater, boot-selection, storage, rollback, or recovery authority
- identity, pairing, provisioning, keys, or secrets; no HMAC/eFuse or protected
  NVS operation executes
- a proven Bluetooth controller session, usable secure bond, authorized device
  transport, or normal command path; the observed stack and advertisement do
  not establish any of these
- board GPIO, battery, charger, or general power-control bindings beyond the
  physically accepted target-local OLED Vext/reset/I2C startup/status path
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
accesses hardware; OT-061 physical execution is recorded in
`physical-flash-plan.json`, and OT-064's app-only OLED execution is recorded in
`oled-startup-flash-plan.json` plus their dated hardware evidence.

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
exchange, authorization, Ready state, protected storage, LoRa, GNSS, interactive
display/input, power/endurance, regulatory fit, or support.

OT-064 later updated only the factory app on `OT-DEV-001`. Exact read-only
verification passed before reset; the owner observed the recognizable Trail
logo followed by `BLE ADVERTISING`; boot self-check PASS, four heartbeats, and
one exact-service Android candidate were observed without selection, connection,
pairing, or identifier retention. This proves only the selected unit's
startup/status OLED path. Both write authorizations are consumed; no additional
erase, write, recovery, or unit-2 authority remains. Any later MeshCore recovery
stays owner-operated through the official MeshCore web flasher. See
[OT-061 evidence](../../../tests/hardware/OT-061-2026-08-16.md) and [OT-064 evidence](../../../tests/hardware/OT-064-2026-08-17.md).

OT-075 adds an offline-only generator for the exact `OTPS0/v0` candidate
partition binary and a denied recovery-bundle plan. The pinned ESP-IDF v6.0.2
partition tool produces a 3,072-byte artifact with the fixed encrypted
`ot_auth` and retained `ot_state` rows; generated output remains ignored and
grants no write authority. At OT-075 acceptance, a source-commit rebuild did
not match the exact application then installed by OT-064, so no substitute
recovery image was accepted. That offline increment did not change active
`partitions.csv`, `sdkconfig.defaults`, runtime composition, or device bytes.
See
[Decision 0018](../../../docs/decisions/0018-offline-heltec-protected-storage-recovery-bundle.md)
and [OT-075 evidence](../../../tests/hardware/OT-075-2026-08-17.md).

OT-076 uses one owner-authorized, one-use read-only ROM session to retain the
exact 470,928-byte OT-064 factory application as a private ignored recovery
artifact. After the connection closed, an independent reread matched SHA-256
`A7D8E672CF9169F1D1D4E86EEFF80399C47A145E7D64904C207DD5F1B23F359B`.
The temporary reader/tests/bytecode were deleted. This closes only the missing
application-artifact prerequisite: no restore route, partition transition,
write, erase, protected storage, key/eFuse, rollback floor, bond, GATT, Ready,
LoRa, or GNSS authority follows. See
[Decision 0019](../../../docs/decisions/0019-retain-exact-installed-application-for-recovery.md)
and [OT-076 evidence](../../../tests/hardware/OT-076-2026-08-17.md).

OT-077 accepts only the offline `OTRR0/v0` source-restore contract. It fixes a
no-stub ESP32-S3 ROM route at 115,200 baud with `no-reset` before/after,
unconditionally restores the exact application first and exact source table
last, then requires closed-connection independent readback plus bounded manual
boot evidence. The source-table recipe is deterministic; one private
application copy is retained, while a second independently hashed staged copy
remains a physical gate. Two distinct protected HMAC roles and an independent
monotonic rollback-floor requirement are defined without selecting a provider.
Future physical admission also requires one fresh same-operation/evidence-set
read-only observation proving secure boot, flash encryption, and secure download
mode are disabled; unknown or mismatch denies. After the first write invocation,
any incomplete result closes the connection, stays in ROM without reset or boot
claim, preserves private artifacts plus the minimum private journal, publishes
only `OTRR0/v0/RECOVERY-UNCERTAIN`, and requires fresh authorization to retry.
No device was accessed and no command, write, reset, recovery, key/eFuse, or
transition authority was added. See
[Decision 0020](../../../docs/decisions/0020-offline-exact-rom-recovery-route.md)
and [OT-077 evidence](../../../tests/hardware/OT-077-2026-08-18.md).
OT-078 selects only provider classes: two distinct ESP32-S3 `HMAC_UP` eFuse
blocks for the NVS-encryption and bond-binding PRF roles, and a conditional
custom user-eFuse thermometer field for the independent generation floor. Pure
host evaluators require exact factual admission and fail closed on missing,
unprovisioned, stale, mismatched, noncanonical, exhausted, or uncertain state.
No physical block, field, capacity, key, inventory, provision, runtime injection,
or device authority exists. See
[Decision 0021](../../../docs/decisions/0021-offline-protected-root-provider-selection.md)
and [OT-078 evidence](../../../tests/hardware/OT-078-2026-08-18.md).

OT-079 adds only an offline inventory plan and pure supplied-evidence verifier.
A later complete private inventory must cover all six key slots, configured-NVS
conflict state, floor-candidate facts, cleanup, and the disabled security state
required by OT-077. Complete unfavorable facts remain reviewable without
selecting or admitting a provider. At OT-079 acceptance no complete inventory
reader/orchestrator existed; no device was accessed, allocations remain absent, and every physical/read/write/eFuse/runtime authority
remains false. See
[Decision 0022](../../../docs/decisions/0022-read-only-protected-root-inventory-admission.md)
and [OT-079 evidence](../../../tests/hardware/OT-079-2026-08-18.md).
OT-080 rejects host-side Python eFuse inventory because it materializes raw key
blocks, then accepts only an offline contract for a future target-side ESP-IDF
decoded-metadata adapter. At OT-080 acceptance no adapter existed; the rollback-
floor descriptor and
read remain unavailable, no device was accessed, and every deployment/read/
write/eFuse/provisioning/runtime authority remains false. See
[Decision 0023](../../../docs/decisions/0023-offline-protected-root-inventory-reader-route.md)
and [OT-080 evidence](../../../tests/hardware/OT-080-2026-08-18.md).

OT-081 adds the target-local one-use six-slot coarse key-roster adapter. It is
compiled into the target configuration and passes strict synthetic host/static
tests, but no startup/runtime source includes or constructs it and it was not
executed on hardware. It emits no raw data and does not infer provisioning or
reservation, so it cannot produce complete inventory evidence or provider
admission. The complete inventory reader/orchestrator remains absent, operation attempts remain zero, and all
device/read/write/provisioning/runtime authority remains false. See
[Decision 0024](../../../docs/decisions/0024-build-only-target-side-protected-root-key-roster-adapter.md)
and [OT-081 evidence](../../../tests/hardware/OT-081-2026-08-18.md).

OT-082 adds a second build-only target leaf for the default NVS build
configuration and four decoded security-state values. It is one-use,
all-or-none, and re-entry poisoned; strict host tests cover all configuration
and security-value combinations. The source is compiled exactly once and the
build receipt binds its header, source, and object, but no startup/runtime
source includes or constructs it and it was not executed on hardware. The
values expected by the recovery contract remain expectations rather than
device observations. Runtime NVS overrides, configured-key conflict, complete
inventory, provider admission, and the rollback-floor descriptor remain open;
all device/read/write/provisioning/runtime authority remains false. See
[Decision 0025](../../../docs/decisions/0025-build-only-protected-root-configuration-security-adapter.md)
and [OT-082 evidence](../../../tests/hardware/OT-082-2026-08-18.md).

OT-083 rejects the conditional custom USER_DATA eFuse thermometer after pinned
ESP-IDF 6.0.2 source proved its Reed-Solomon coding unit is writable only once.
That medium cannot provide repeated one-bit rollback-floor advances. The review
adds no target source or runtime edge, performs no device/eFuse operation, and
selects no replacement provider, block, range, or capacity. The active target
build and installed application are unchanged. See
[Decision 0026](../../../docs/decisions/0026-reject-esp32s3-user-data-rollback-floor.md)
and [OT-083 evidence](../../../tests/hardware/OT-083-2026-08-18.md).

OT-084 also rejects ESP32-S3 `SECURE_VERSION` as the independent companion-
authorization floor. The field has only 16 irreversible advances and is
already the application-firmware anti-rollback namespace; its native ESP-IDF
model excludes factory/test applications, while this target's accepted layout
and recovery route depend on the factory application. External authenticated
monotonic hardware is a future hardware-revision option only: no part is
selected or present, and no target source, build input, device/eFuse state, or
runtime changed. See
[Decision 0027](../../../docs/decisions/0027-reject-esp32s3-secure-version-authorization-floor.md)
and [OT-084 evidence](../../../tests/hardware/OT-084-2026-08-18.md).
