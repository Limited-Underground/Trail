# OpenTrail Project Status, Assumptions, and Open Questions

Status date: 2026-08-20

## Conceptual goals

- Offline group communication and location awareness using ESP32 and LoRa
- Portable, vehicle, repeater, and larger touchscreen configurations
- Priority emergency/status messages, store-forward where useful, and graceful disconnection
- Offline local maps and a normalized OpenGauge critical-alert input

OT-093 freezes the deterministic pre-selection build baseline required before
the later OT-005 ESP32-S3 cryptographic comparison. Two independent, initially
absent and cache-disabled builds of the unsupported Heltec V4 bench candidate
used stable project version `ot093-precrypto-v0`, exact source-index/raw-byte,
configuration, ESP-IDF, tool-executable, and isolated-Python locks. Both exited
zero with zero warnings and produced identical ordered application BIN, ELF,
map, bootloader, partition-table, sdkconfig, and partition-CSV tuples. Individual
run receipts remained reconciliation-pending; only aggregate validation derived
equality and accepted
`BUILD-BASELINE-FROZEN; OTCB0-EXECUTION-BLOCKED`.

This is build-only host evidence. The `OTCB0/v0` plan remains `draft_blocked`,
execution authority false, and final candidate-ready target/toolchain/sdkconfig
applicability unresolved. No candidate or secure-LoRa adapter was imported or
executed; no suite/library, handshake/KDF, packet-v1 wire, radio profile, key or
entropy operation, device access, implementation, physical result, or score is
accepted. Android remains 60%; V1 remains exact 43.75%/displayed 44%; the
historical baseline remains exact 31.75%/displayed 32%; V1.5 remains
unmeasured. See [Decision 0037](decisions/0037-pre-crypto-build-baseline.md),
the [benchmark evidence boundary](security/CRYPTO_BENCHMARK_EVIDENCE_V0.md),
and [OT-093 evidence](../tests/hardware/OT-093-2026-08-20.md).

OT-094 now freezes the separate strict `OTCBR0/v0` host-only
candidate-readiness contract with canonical SHA-256
`705b30693196e2f46d8bda7c17acb1e04d7b9092c4a3817286c14d189001b9d3`
and result
`CANDIDATE-READINESS-CONTRACT-FROZEN-HOST-ONLY; OTCB0-EXECUTION-BLOCKED`.
It binds the unchanged historical OT-005 plan and accepted OT-093 baseline,
while keeping six target, final-configuration, dependency-lock, and direct-radio
requirements blocked. Exact received revision/RF identity, final common and
candidate-overlay sdkconfig applicability, project-owned libsodium/Monocypher
locks, mbedTLS/PSA lock/API/config eligibility, and region/MTU/full-PHY remain
unresolved.

A caller-declared legacy `ready` plan is structural only: without an
independently accepted, fully resolved readiness artifact it cannot create a
result template or yield pass. The accepted-ready trust-anchor set is empty.
All dependency-acquisition, import, build, execution, device, radio, key,
selection, packet-v1, support, physical-evidence, and score authority remains
false. This changes no percentage: Android remains 60%; V1 exact
43.75%/displayed 44%; the historical baseline exact 31.75%/displayed 32%; and
V1.5 and V2 remain unmeasured. See
[Decision 0038](decisions/0038-host-only-ot005-candidate-readiness-contract.md)
and [OT-094 evidence](../tests/hardware/OT-094-2026-08-20.md).

OT-095 now freezes the strict `OTCSL0/v0` host-only candidate source-lock
admission contract with canonical and policy SHA-256
`c0bd923782d0977f8b375cbd2fe8cde5ff132a26b8b6a7ea34a62111bd101f1f`
and result
`SOURCE-LOCK-ADMISSION-CONTRACT-FROZEN-HOST-ONLY; ZERO-SOURCES-ACQUIRED-OR-IMPORTED; OTCBR0-READINESS-BLOCKED`.
It distinguishes acquisition-receipt, immutable-source-tree, project-lock,
API/configuration, candidate-import, and benchmark-execution evidence layers,
with admission defined for the first five and benchmark-execution admission
still undefined and blocked. Separate candidate-specific source/API-config/import
trust anchors and accepted anchor registries remain empty: no source lock is
accepted, no source was
acquired or imported, and the installed mbedTLS/PSA observation is not a project
lock or API/Ed25519/final-configuration eligibility result. All six OTCBR0
requirements remain blocked, OTCB0 remains `draft_blocked`, and every authority,
readiness, execution, selection, support, physical-evidence, and score claim
remains false. See [Decision 0039](decisions/0039-host-only-candidate-source-lock-admission-contract.md)
and [OT-095 evidence](../tests/hardware/OT-095-2026-08-20.md).

OT-091 freezes and host-tests `OTSL0/v0`, the algorithm-neutral Decision 0033
secure-LoRa lifecycle/admission contract for V1's exact two-node pairwise-
unicast path. One secret-free authenticated invitation admits one candidate and
one attempt. Mutual device authentication, complete transcript binding,
matching local confirmation, exact candidate commit/readback, and exact peer
activation precede traffic. Epoch replacement advances by exactly one with
fresh material for retained identities, blocks routine traffic while unresolved,
and never falls back to an old epoch after possible/new activation.

The contract binds both full identities, ordered direction, group/epoch, and
purpose before using the existing traffic-context, durable-counter, and nonce-
composition obligations. Packet v0/plaintext fallback is denied. Authentication
precedes replay mutation; durable cryptographic replay and receive admission
precede plaintext release and protected acknowledgement. Exact retries reuse
the same sealed bytes. A positive LoRa acknowledgement means only peer-device
durable admission, not phone display or user read.

This is deterministic host-contract evidence only. Decision 0003 remains in
force: the OT-005 public benchmark plan is blocked, and no suite/library,
handshake/KDF, packet-v1 wire, target storage, key operation, radio traffic,
provisioning, epoch replacement, replay protection, acknowledgement, delivery,
or physical result is implemented or accepted. Android remains 60%; V1 remains
exact 43.75%/displayed 44%; the historical baseline remains exact 31.75%/
displayed 32%; V1.5 remains unmeasured. See
[Decision 0035](decisions/0035-host-tested-secure-lora-key-transport-contract.md),
the [OTSL0/v0 contract](security/SECURE_LORA_KEY_TRANSPORT_V0.md), and
[OT-091 evidence](../tests/hardware/OT-091-2026-08-19.md).

OT-090 freezes and host-tests `OTBP0/v0`, the exact Decision 0033 practical
physical-presence BLE pairing, saved-bond reconnect, and confirmed phone-
replacement contract. Pairing is normally closed. Holding the designated
target-neutral local input for at least 3000 ms and releasing it opens one exact
30-second, one-attempt, single-candidate window; no GPIO/button mapping is
selected. Each admitted window receives one fresh uniformly sampled, locally
displayed six-decimal-digit passkey. Pairing is Bluetooth LE Secure
Connections-only, MITM passkey-authenticated and bonded with an exact
16-byte/128-bit key;
legacy pairing, `Just Works`, and static/debug passkeys are denied.
Reconnect rechecks the saved current bond, link security, and separate
application authorization without rewriting ownership. Replacement stays
distinct: after the candidate secure bond, a second qualifying hold/release
must complete before the original deadline. Candidate commit and exact readback
precede old-authorization invalidation; verified old-bond removal precedes new-
controller publication. Abort, expiry, interruption, or known pre-mutation
failure preserves the exact prior owner only after candidate-bond removal and
verified absence. Ambiguous commit, readback, candidate cleanup, or old-bond
cleanup publishes neither controller.
Timeout, mismatch, authentication/bond failure, disconnect, stale/replayed
events, clock failure/rollback, restart, and incoherent persistence fail closed.

This is deterministic host-contract evidence only. No target or Android
implementation, storage binding, physical gesture/display, Bluetooth operation,
PIN display/entry, pairing, bond, reconnect, replacement, protected GATT,
`Ready`, secure LoRa, phone installation, signed release, supported hardware,
or coherent V1 physical result is claimed. Android remains 60%; V1 remains exact
43.75%/displayed 44%; V1.5 remains unmeasured. See
[Decision 0034](decisions/0034-host-tested-ble-pairing-replacement-contract.md),
the [OTBP0/v0 contract](platform/BLE_PAIRING_REPLACEMENT_V0.md), and
[OT-090 evidence](../tests/hardware/OT-090-2026-08-19.md).

OT-089 permanently adopts the owner-approved V1 scope and security boundary.
V1 now requires exactly two supported Heltec LoRa devices and two approved
Android phones, one current phone per Heltec, with one coherent bidirectional
Phone A ⇄ BLE ⇄ Heltec A ⇄ direct LoRa ⇄ Heltec B ⇄ BLE ⇄ Phone B acceptance.
At OT-089 acceptance, practical authorization still required an exact state
contract. OT-090 later freezes and host-tests that contract without
implementation or score credit. Factory reset,
reflashing, invasive access, or old-flash restore may reset or roll back
ownership; V1 requires no secure element or independent monotonic floor and
makes no rollback-proof physical-attacker claim. OT-091 later freezes the
algorithm-neutral LoRa lifecycle/admission semantics without implementation or
score credit. OT-093 later freezes only the deterministic pre-crypto build
baseline. Final candidate readiness, exact OT-005 candidate benchmarking,
suite/wire selection, target implementation, and physical secure-LoRa
acceptance remain open.

V1.5 is a separate unmeasured four-supported-node interoperability milestone.
Mixed hardware is allowed and preferred but not required; four identical
supported nodes remain eligible, four phones are unnecessary, and a relay claim
requires a physical three-radio path. OT-089 is planning only: no pairing,
protected control, LoRa transmission, signed Android release, hardware support,
or V1/V1.5 completion is claimed. Android remains 60%; V1 remains exact
43.75%/displayed 44%. See
[Decision 0033](decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md),
the [canonical scope](testing/V1_V1_5_ACCEPTANCE_SCOPE_V0.md), and
[OT-089 evidence](../tests/hardware/OT-089-2026-08-19.md).

At OT-088 acceptance, OpenTrail froze one private-pilot operational policy revision. Privacy and data
safety are offline, account-free, transient, backup/transfer-excluded, and
subject to later physical inspection. First-release rollback means explicit
disconnect/service stop and uninstall with verified app-data removal, no
downgrade, and retry only with the same accepted artifact/digest/signer.
Support begins only after a complete OTAR pass and is best-effort/no-SLA for
the then-planned four-phone private matrix, since superseded by Decision 0033's
two-phone scope. This satisfies three more prerequisites;
five of eight are satisfied, while physical matrix, release identity, and
signer/custody remain blocked. The result remains
`PLAN-ACCEPTED-EXECUTION-BLOCKED`, the release gate is `NOT-EVALUATED`, and
execution authority remains false. No phone, installation, signing, account,
upload, or distribution operation occurred. Android remains 60%, and V1
Companion remains exact 43.75%/displayed 44%. See
[Decision 0032](decisions/0032-android-private-pilot-operational-policy-freeze.md),
the [private-pilot policy](platform/ANDROID_PRIVATE_PILOT_OPERATIONAL_POLICY_V0.md),
and [OT-088 evidence](../tests/hardware/OT-088-2026-08-19.md).

OT-087 freezes Android version code/name `1` / `1.0.0`, configures an explicit
non-debuggable release build type without signing, and accepts bounded packaged
inspection of one disposable unsigned local APK. Debug and release unit tests,
both warning-as-error lint variants, debug/instrumentation/release assembly,
exact manifest/permission/backup checks, DEX checksums, debug/test-surface
absence, and unsigned-state verification pass. This satisfies only the
production-variant and stable-version OTAR prerequisites. Six remain: physical
acceptance matrix, privacy/data-safety, release identity, rollback policy,
signer/custody, and support policy. The artifact was not installed, signed,
uploaded, or distributed; no phone, hardware, key, certificate, account, or
store was accessed. Android remains 60%, and V1 Companion remains exact
43.75%/displayed 44%. See
[Decision 0031](decisions/0031-android-unsigned-release-build-foundation.md)
and [OT-087 evidence](../tests/hardware/OT-087-2026-08-19.md).

At OT-086 acceptance, OpenTrail accepted the plan-only `OTAR0/v0` Android
operational-release admission contract, one canonical plan, a fail-closed
validator, and deterministic denial cases. The only current distribution scope is
`private-sideload-v1-pilot`, meaning controlled private installation on the
four V1 pilot phones that must each be approved and frozen before execution; it
is not public or store distribution. A future Play scope requires a separate
current target-API policy recheck and any required Android toolchain/target-SDK
update. The sole successful planning outcome is
`PLAN-ACCEPTED-EXECUTION-BLOCKED`: the plan is bounded, but no production
variant, signer/custody policy, immutable artifact, supported-device matrix,
lifecycle/endurance/privacy/support result, installation, distribution, or
release pass existed. OT-087 later satisfied the production-variant and
stable-version prerequisites; OT-088 later satisfied the three policy
prerequisites without executing them. Android remains 60%, and V1 Companion remains exact
43.75%/displayed 44%. See
[Decision 0030](decisions/0030-android-operational-release-admission.md),
[the operational-release contract](platform/ANDROID_OPERATIONAL_RELEASE_ACCEPTANCE_V0.md),
and [OT-086 evidence](../tests/hardware/OT-086-2026-08-19.md).

OT-085B physically accepts the independent automatic public-BLE lifecycle on
the exact 471,456-byte OT-085 factory image already installed and read-back
verified on `OT-DEV-001`. One Android 13 phone found exactly one compatible
service advertiser, connected, required the UUID-suffix-`0x04` characteristic
to be READ-only, and matched the exact fixed 16-byte zero-capability `OTB0/v0`
value. The phone made no disconnect request. The bound GATT disconnected inside
the policy-derived window around the frozen 15-second target limit; the owner
observed `BLE CONNECTED` followed by `BLE ADVERTISING`, and exactly one
compatible service advertiser returned. No stable address or endpoint identity
was required or inferred, and no name, coordinate, key, or private binding was
retained. See
[OT-085B](../tests/hardware/OT-085B-2026-08-19.md),
[OT-085A](../tests/hardware/OT-085A-2026-08-19.md),
[OT-085](../tests/hardware/OT-085-2026-08-18.md), and
[Decision 0029](decisions/0029-bounded-read-only-ble-link-status.md).

Decision 0028 historically deferred rollback-protected companion authorization
beyond the current Heltec V1 rather than weakening it. Decision 0033 now
supersedes only the independent-floor requirement for V1 and adopts practical
authorization under the disclosed physical-reflash rollback limit. The
build-tested stronger foundations remain historical. OT-090 later freezes and
host-tests the replacement practical state contract; target/app implementation,
storage binding, physical acceptance, and Ready remain open.
OT-085A's accepted third Android evidence gate raised V1 Companion to exact
43.75%/displayed 44%. OT-085B closes its automatic-termination sub-gate without
an additional score; the Heltec milestone description advances but its
completion remains unchanged because no separate scoring increment is defined.
See
[Decision 0028](decisions/0028-defer-rollback-protected-companion-authorization-beyond-current-heltec-v1.md).

OT-084 rejects ESP32-S3 `SECURE_VERSION` as the independent companion-
authorization floor. Pinned ESP-IDF 6.0.2 sources prove it is a 16-step
application-firmware anti-rollback field whose native model requires OTA slots
without a factory application. That role conflicts with OpenTrail's accepted
factory layout and exact factory-restore route, and sharing it would consume
the firmware version budget rather than provide an independent authorization
domain. The strict source-bound evaluator and six plan groups pass. No external
part is selected or present on the current target; no device, eFuse, target
build input, or runtime changed. At OT-084 acceptance, V1 was exact
39.75%/displayed 40%. See
[OT-084](../tests/hardware/OT-084-2026-08-18.md) and
[Decision 0027](decisions/0027-reject-esp32s3-secure-version-authorization-floor.md).

OT-083 rejects the conditional ESP32-S3 custom USER_DATA eFuse thermometer
from Decision 0021. Pinned ESP-IDF 6.0.2 sources prove the USER_DATA block uses
Reed-Solomon coding and an already nonempty coding unit cannot be written
again, which is incompatible with repeated independent one-bit rollback-floor
advances. The pure viability evaluator, the updated provider admission, six
descriptor-plan groups, existing protected-root plan regressions, eleven target-
admission groups, and the complete 146-executable host gate pass. No provider,
block, bit range, or capacity is selected; no target source, device/eFuse state,
or runtime changed. The two protected HMAC roles remain selected by type only.
At OT-083 acceptance, V1 was exact 39.75%/displayed 40%. See
[OT-083](../tests/hardware/OT-083-2026-08-18.md) and
[Decision 0026](decisions/0026-reject-esp32s3-user-data-rollback-floor.md).

OT-082 adds a Heltec-local, one-use, all-or-none source for the default build's
normalized NVS protection configuration and four decoded security-state values.
It keeps secure boot, flash encryption, secure download, and download-mode
disablement distinct, and fails closed on invalid build configuration, re-entry,
or reuse. Strict host tests, eleven target-admission groups, the complete
145-executable host gate, and two identical pinned target builds pass. The
source is build-compiled but has no runtime call path and was not executed on a
device. It proves no current security state, runtime NVS scheme, configured-key
conflict, complete inventory, provider suitability, or physical allocation.
All device/read/reset/provisioning/write/runtime authority remains false and
milestone completion is unchanged. See
[OT-082](../tests/hardware/OT-082-2026-08-18.md) and
[Decision 0025](decisions/0025-build-only-protected-root-configuration-security-adapter.md).

OT-081 implements the OT-080 target-side boundary as a Heltec-local, one-use
coarse key-roster adapter. It calls only the five admitted decoded ESP-IDF
6.0.2 APIs for six logical key slots, publishes only after all calls and
invariants pass, and fails closed on invalid purpose, contradictory unused
evidence, re-entry, or reuse. Strict host tests, ten target-admission groups,
the complete 144-executable host gate, and two identical pinned target builds
pass. The compiled source has no runtime call path and was not executed on a
device. A false unused result is not treated as proof of provisioning, and
reservation is not inferred, so the adapter cannot produce complete OT-079
inventory evidence or select a provider. Device/eFuse read, key access,
allocation, provisioning, protection, write, runtime, GATT, and Ready authority
remain false. Milestone completion is unchanged. See
[OT-081](../tests/hardware/OT-081-2026-08-18.md) and
[Decision 0024](decisions/0024-build-only-target-side-protected-root-key-roster-adapter.md).

OT-080 rejects host-side Python eFuse inventory because the reviewed path
materializes raw key blocks in host memory. It accepts only a pure offline
contract for a then-future audited target-side ESP-IDF 6.0.2 metadata adapter using
five decoded key-purpose, protection, and unused-state APIs. Raw key/block
reads, HMAC operations, writes, burns, protection changes, and every unlisted
API remain denied. At OT-080 acceptance no adapter existed; the rollback-floor descriptor and
physical read remain unavailable, no hardware was accessed, and every
deployment, device-read, provisioning, provider, and runtime authority remains
false. Milestone completion is unchanged. See
[OT-080](../tests/hardware/OT-080-2026-08-18.md) and
[Decision 0023](decisions/0023-offline-protected-root-inventory-reader-route.md).

OT-079 accepts an offline-only protected-root inventory plan and pure supplied-
evidence verifier. A future inventory must supply one fresh, privately bound,
complete six-slot key roster, configured-NVS conflict state, complete candidate
floor map, and the exact disabled secure-boot, flash-encryption, and secure-
download state expected by OT-077. Missing, contradictory, stale, mixed, or
secret-bearing evidence denies. A complete but unfavorable inventory remains
reviewable; it does not select or admit a provider. At OT-079 acceptance no
complete inventory reader/orchestrator existed; no device was accessed, all allocations remain absent, every physical/write/eFuse/runtime
authority remains false, and milestone completion is unchanged. See
[OT-079](../tests/hardware/OT-079-2026-08-18.md) and
[Decision 0022](decisions/0022-read-only-protected-root-inventory-admission.md).

OT-078 selects the protected-root provider classes offline. The NVS-encryption
and private bond-binding roles each require a distinct ESP32-S3 `HMAC_UP` eFuse
block; a future admission must prove exact provisioning, purpose, read
protection, operational self-test, freshness, and one shared evidence binding.
The independent floor conditionally uses a dedicated custom user-eFuse
thermometer field with one-bit advance, exact reread, permanent exhaustion, and
fail-closed uncertainty. No physical block, counter field, capacity, key, or
provisioning sequence is selected. No hardware was accessed and every physical,
write, runtime, and transition authority remains false. At OT-078 acceptance,
V1 Companion was exact 39.75%/displayed 40%. See
[OT-078](../tests/hardware/OT-078-2026-08-18.md)
and [Decision 0021](decisions/0021-offline-protected-root-provider-selection.md).

OT-077 accepts the exact `OTRR0/v0/heltec-v4-ot064-source-restore` route as an
offline contract. It fixes ESP32-S3 ROM serial tooling at esptool 5.3.1,
115,200 baud, `no-reset` before/after, and no RAM stub; restores the exact
application unconditionally before writing the exact source partition table;
and requires closed-connection independent readback before a bounded manual
reset observation of boot self-check PASS, Trail logo, `BLE ADVERTISING`, and
at least two expected heartbeats within 12 seconds. The public source-table
recipe is deterministic. One private application copy is proved retained, but
a second independently hashed staged copy is required before any physical
operation. Two distinct protected HMAC roles and an independent monotonic
rollback-floor requirement are defined without selecting or provisioning a
provider. Future admission also requires a fresh same-operation/evidence-set read-only
observation proving secure boot, flash encryption, and secure download mode are
disabled; unknown or mismatch denies. Any failure after the first write closes
the connection, remains in ROM as `RECOVERY-UNCERTAIN`, preserves private
artifacts and a minimum private journal before transient cleanup, makes no boot-
success claim, never auto-retries, and requires fresh owner authorization.
No hardware was accessed; every physical recovery, write, key/eFuse,
rollback, and transition authority remains false. At OT-077 acceptance, V1
Companion was exact 39.75%/displayed 40%. See
[OT-077](../tests/hardware/OT-077-2026-08-18.md) and
[Decision 0020](decisions/0020-offline-exact-rom-recovery-route.md).

OT-076 closes the missing exact installed-application artifact prerequisite.
One owner-authorized, one-use read-only operation captured the 470,928-byte
OT-064 factory application from `OT-DEV-001`, closed the connection, and
independently reread the staged artifact before accepting SHA-256
`A7D8E672CF9169F1D1D4E86EEFF80399C47A145E7D64904C207DD5F1B23F359B`.
The binary remains a private ignored recovery artifact; the temporary reader,
tests, and bytecode were deleted. No port, device identifier, private operation
identity, or detailed transport output is retained publicly. This grants no
restore or write authority. OT-077 later accepts the exact route offline and
defines the key-role/floor requirements; fresh same-operation evidence,
redundant custody, concrete providers, exact-unit physical validation, and
separate physical-write authorization remain open. After manual RST, the
owner observed the Trail logo followed by `BLE ADVERTISING`. The transition
stays denied; at OT-076 acceptance, V1 Companion was exact
39.75%/displayed 40%. See
[OT-076](../tests/hardware/OT-076-2026-08-17.md) and
[Decision 0019](decisions/0019-retain-exact-installed-application-for-recovery.md).

OT-075 freezes the exact offline `OTPS0/v0` candidate partition artifact. The
pinned ESP-IDF v6.0.2 generator produces one 3,072-byte table whose decoded
rows preserve boot and OTA regions, add encrypted 64 KiB `ot_auth`, retain
960 KiB `ot_state`, and end at exactly 16 MiB. At OT-075 acceptance, the exact
application installed by OT-064 was not available as a recovery artifact;
OT-076 later captured it exactly. A clean rebuild from its
recorded source commit completed but did not match the installed digest, so it
was rejected and not retained. The recovery bundle and transition therefore
remain denied. Active partitions, configuration, runtime, and device bytes are
unchanged. OT-076 and OT-077 later closed the artifact and offline-route
prerequisites. A future operation still requires redundant custody, concrete
key/floor providers, fresh unified evidence, and separate physical authority.
At OT-075 acceptance, V1 Companion was exact 39.75%/displayed 40%. See
[OT-075](../tests/hardware/OT-075-2026-08-17.md) and
[Decision 0018](decisions/0018-offline-heltec-protected-storage-recovery-bundle.md).

OT-074 accepts the exact protected-storage source prerequisite on
`OT-DEV-001`. One bounded read-only operation matched the installed 3,072-byte
partition table and verified that the complete 1 MiB source region was all
`0xFF`, returning only `OTPSTE1/v0` / `SOURCE-PROOF-SATISFIED-ONLY`. No raw
bytes, paths, port, device identifier, private operation/evidence identity, or
detailed transport output was retained, and the temporary executor and
bytecode were deleted with cleanup verified. After manual RST, the owner
observed the Trail logo followed by `BLE ADVERTISING`. This satisfies only the
OT-070 source prerequisite; it does not authorize a partition transition or
prove protected NVS, key roles, an independent rollback floor, bond
persistence, GATT authorization, Ready, LoRa, or GNSS. At OT-074 acceptance,
historical standalone progress was exact 31.75%/displayed 32% and V1 Companion
was exact 39.75%/displayed 40%. See
[OT-074](../tests/hardware/OT-074-2026-08-17.md).

OT-073 records one owner-authorized, one-use read-only source-proof attempt on
`OT-DEV-001`. The audited executor used esptool 5.3.1, one no-reset ROM
connection attempt, no RAM stub, and only the two OT-071 allowlisted ranges.
It returned only `OTPSTE0/v0` / `DENY-READ-FAILURE`; the authorization was
consumed and no retry occurred. No raw bytes, paths, port, identifier, private
operation identity, or detailed transport output was retained, and both
ephemeral executor files plus bytecode were deleted. The owner then pressed RST
with PRG released and observed the Trail logo followed by `BLE ADVERTISING`.
Source proof remains absent, so no partition transition, protected storage,
key, rollback-floor, bond, GATT, Ready, LoRa, or GNSS authority or evidence is
added. At OT-073 acceptance, historical standalone progress was exact
31.75%/displayed 32% and V1 Companion was exact 39.75%/displayed 40%. See
[OT-073](../tests/hardware/OT-073-2026-08-17.md).

OT-072 established the first canonical V1 Companion release measurement.
The six weights total 100: architecture/safety 15 at 85%, core firmware 20 at
65%, loader 15 at 15%, Heltec/Trail Essential target 15 at 25%, Android
Companion 20 at 40%, and four-person Companion field proof 15 at 0%. At that
acceptance point, the exact weighted result was 39.75% and public display
rounded to 40%. Android's 40% recorded two of five equal accepted gates: tested application plus bounded
physical install/lifecycle/artwork observations, and exact-service discovery.
Physical GATT connection/negotiation, protected one-phone authorization and
Ready, operational release acceptance, and Companion field proof were open.
The historical standalone baseline was exact 31.75%/displayed 32%; V2
Integrated was unmeasured. No firmware, app package, target, device, or
field evidence changed. See [Decision 0017](decisions/0017-v1-companion-release-measurement.md)
and [OT-072](../tests/hardware/OT-072-2026-08-17.md).

OT-071 adds a streaming offline verifier and a separate denied read-only plan
for the exact OT-070 source-evidence prerequisite. Eight focused verifier
groups, six transition-manifest groups, and the existing 9/9 Heltec admission
pass. The verifier accepts only the exact 3,072-byte installed partition table
and complete 1 MiB all-`0xFF` source region, bound to nonzero operation and
evidence-set identities. It emits only a fixed schema and sanitized outcome;
raw bytes, paths, ports, device identifiers, identities, nonblank digests, and
nonblank locations are not retained. The read plan is design-only, selects no
unit, contains no command or executable hardware reader, and grants no
authority. No hardware read occurred;
active target/build/runtime/installed-device state is unchanged. A later
physical read requires a new exact owner authorization, and success can satisfy
only the source-proof prerequisite, never the partition transition. Historical
V1 remains 31.75% and displays as 32%; release tracks remain unmeasured. See
[Decision 0016](decisions/0016-read-only-protected-storage-transition-evidence.md)
and [OT-071](../tests/hardware/OT-071-2026-08-17.md).

OT-070 adds a pure target-neutral admission guard and exact design-only Heltec
manifest for the `OTHP0/v0` to `OTPS0/v0` partition split. Thirteen strict C++
groups pass with warnings treated as errors and across 100 repeated runs; five
manifest groups, the existing 9/9 Heltec admission, and the complete host gate
also pass. The guard requires exact installed-layout readback, blank source
media or a separately implemented and verified semantic migration, exact
recovery artifacts and ROM route, no runtime/key/eFuse/other-flash operation,
and one operation-scoped partition-only authority. It performs no I/O.
At OT-070, the target manifest remained denied because source evidence had not been captured;
all capabilities and authorities are false, and the active partition table,
sdkconfig, target contract, CMake, runtime, installed image, and physical device
are unchanged. No transition, migration, rollback floor, key, eFuse, pairing,
GATT authorization, or Ready state is accepted. Historical V1 remains 31.75%
and displays as 32%; current release tracks remain unmeasured. See
[Decision 0015](decisions/0015-safe-heltec-protected-storage-partition-transition.md)
and [OT-070](../tests/hardware/OT-070-2026-08-17.md).

OT-069 adds one inactive target-local owner for the exact candidate encrypted
NVS context. Ten strict lifecycle groups plus a separate disabled-configuration
executable prove exact partition/config/open order, single-attempt behavior,
temporary security-configuration zeroing, reverse cleanup, destructor cleanup,
native ambiguity containment, reentry containment, and zero native I/O under
the current configuration. Heltec target admission passes 9/9. Two pinned
ESP-IDF v6.0.2 builds reproduce a 470,928-byte BIN with SHA-256
`9D4EBCD8BB68183798BF47267252A1B2A94A114FACD16E8CF975AEBE43314EEF`.
The owner and backend are build-compiled but no runtime source includes,
constructs, opens, or injects them. The active partition table still has no
`ot_auth`, encryption/key selection remains absent, and storage admission stays
denied before native I/O. No provisioning, eFuse operation, rollback-floor
selection, migration, bond persistence, pairing, GATT authorization, Ready,
firmware write, or physical device change occurred. Historical V1 remains
31.75% and displays as 32%; current release tracks remain unmeasured. See
[Decision 0014](decisions/0014-inactive-heltec-authorization-nvs-context.md)
and [OT-069](../tests/hardware/OT-069-2026-08-17.md).

OT-068 adds one inactive target-local ESP-IDF NVS implementation of OT-067's
exact protected-KV backend. It accepts only an already-opened `nvs_handle_t`,
revalidates `ot_auth` / `ot_owner` / `oap_slot_a|b` and exact 32-byte values,
and exposes only size-query/read, set, and commit. Eight strict backend groups,
the unchanged ten-group slot-media regression, and nine target-admission groups
pass. Two pinned ESP-IDF v6.0.2 builds reproduce a 470,928-byte BIN with SHA-256
`9F5AFB320A015E3BFFD866A9EE31F76198739521FA7519845ACDA12B9B52BAE5`.
Both inactive backend objects are build-compiled, but no runtime source includes
or constructs the adapter. The active partition table still has no `ot_auth`,
NVS encryption remains disabled, and storage admission stays denied before any
backend operation. No initialization/open, key provisioning, eFuse operation,
rollback-floor selection, bond persistence, pairing, GATT authorization, Ready,
firmware write, or physical device change occurred. Historical V1 stays 31.75%
and displays as 32%; V1 Companion and V2 Integrated remain unmeasured. See
[Decision 0013](decisions/0013-inactive-heltec-authorization-nvs-backend.md)
and [OT-068](../tests/hardware/OT-068-2026-08-17.md).

OT-067 adds the reversible key/value media layer beneath the accepted two-slot
authorization coordinator. Ten strict groups prove exact `ot_auth` partition,
`ot_owner` namespace, and `oap_slot_a|b` key binding; exact 32-byte values;
missing-key handling; typed read/write failures; explicit durable commit;
post-write uncertainty; reentry containment; alternating-slot rotation; reboot
restore; and refusal of rolled-back, prepared-ahead, and ambiguous media. The
backend remains injected and target-neutral. No active partition, sdkconfig,
Heltec runtime, image, device, storage initialization, key, rollback authority,
bond, pairing, authorization, or Ready state changed. Historical V1 stays
31.75% and displays as 32%; V1 Companion and V2 Integrated remain unmeasured.
See [OT-067](../tests/hardware/OT-067-2026-08-17.md).

OT-066 adds the missing host production composition between private bond
evidence, device-secret owner derivation, durable one-phone ownership, and the
existing GATT claim authority. Eight strict groups prove exact connection-
generation binding, cached refresh, changed-reference and stale-generation
denial, re-pair identity separation, physical-gated first claim, owner reconnect
without durable rewrite, wrong-phone denial, explicit replacement, exact lease
release, reentry, malformed private sessions, and persistence uncertainty. The
Heltec target remains wired to its denied binding and authorization authorities;
no target storage, keys, pairing, GATT exchange, physical control, Ready state,
image, or device changes. Historical V1 stays 31.75% and displays as 32%; V1
Companion and V2 Integrated remain unmeasured. See
[OT-066](../tests/hardware/OT-066-2026-08-17.md).

OT-065 adds a concrete two-slot protected authorization-store coordinator over
injected record media and an independent monotonic-generation authority.
Twelve strict host groups prove empty boot, exact-current restore, inactive-slot
rotation, prepare-before-floor ordering, exact readback, reboot reconciliation,
and fail-closed prepared-ahead, stale-only, duplicate-current, corrupt,
missing, conflicting, and ambiguous states. An exact `OTPS0/v0` Heltec
candidate layout reserves a 64 KiB `ot_auth` NVS partition while retaining the
remaining 960 KiB as `ot_state`; the accompanying plan selects no keys or
rollback provider and grants no authority. The active partition table,
sdkconfig, target contract, runtime, and physical device are unchanged. No NVS
initialization, persistent write, eFuse operation, bonding, GATT authorization,
or Ready state is added. Target encryption, two distinct protected key roles,
private bond-reference lifecycle, an independent physical rollback floor,
migration, power-loss, and recovery evidence remain open. Historical V1 stays
31.75% and displays as 32%; V1 Companion and V2 Integrated remain unmeasured.
See [OT-065](../tests/hardware/OT-065-2026-08-17.md).

OT-064 adds the first physically accepted target-local peripheral binding on
only `OT-DEV-001`. A 128 x 64 one-bit display owner, fail-contained
SSD1315-compatible Heltec adapter, and deterministic Limited Underground Trail
bitmap passed three focused host groups, seven final target-only admission
groups, and two byte-identical pinned ESP-IDF v6.0.2 builds. One bounded
factory-app-only update wrote the exact 470,928-byte image (SHA-256
`A7D8E672CF9169F1D1D4E86EEFF80399C47A145E7D64904C207DD5F1B23F359B`)
at `0x010000`; no full erase or other partition write occurred. Read-only
verification passed before reset. The owner observed the recognizable Trail
logo followed by `BLE ADVERTISING`; boot self-check PASS, four USB heartbeats,
and no failure/panic marker were observed. Android found one exact-service
candidate without selection, connection, pairing, or identifier retention.
This proves only the selected unit's startup/status OLED path. Exact controller
silicon/revision, protected storage, GATT/Ready, LoRa, GNSS, interactive UI/input,
power/endurance, recovery, support, and field behavior remain open. The Heltec
target milestone advances from 20% to 25%; exact historical V1 is 31.75% and
displays as 32%. Current V1 Companion and V2 Integrated remain unmeasured. See
[OT-064](../tests/hardware/OT-064-2026-08-17.md).

OT-063 adds a target-linked, read-only protected-storage admission probe without
changing the accepted OT-061 device image. It observes only coarse
configuration, named NVS-partition presence, selected HMAC_UP purpose, key read
protection, and one private operational HMAC self-test. The current target
configuration still returns `nvs_encryption_not_configured` before any
partition, eFuse, or HMAC read. Six strict host groups, the deterministic boot
self-check at 100/100, five target-only static groups, and two reproducible
pinned builds pass; the 440,240-byte BIN has SHA-256
`0D064045D44D7F4D1120D164912CEAE9E1103ECE159E226B1CEEE2B11489B650`.
The build is not flashed. The probe cannot initialize/open/write NVS, generate
keys, program eFuses, resolve bonds, or enable GATT. Protected NVS, private bond
storage, a distinct binding-PRF key, atomic record/floor storage, an independent
rollback floor, authorization, and Ready remain absent. Historical V1 stays
31%; current V1 Companion and V2 Integrated remain unmeasured. See
[OT-063](../tests/hardware/OT-063-2026-08-16.md).

OT-062 adds the exact owner-supplied Limited Underground Trail artwork to the
real Android Compose entry surface with no timed splash or artificial delay.
The isolated gate passes 136 JVM tests across thirteen suites, warning-as-error
lint, manifest inspection, and debug assembly. The 12,236,702-byte APK has
SHA-256
`0E3A9C91E4AB68F0D6C45FB1D5A613CED7EE33154155AB2D0E76CE453F52918E`;
its packaged 2,559,044-byte artwork remains byte-identical to the approved
source. That APK replaced the prior debug build in place on the authorized
physical Android 13/API 33 phone, cold-launched successfully, and the owner
visually accepted the artwork and framing. Home/background and reopen also
succeeded. No screenshot or device identifier was retained. This is Android
entry-artwork evidence—not an Android system splash, clean install, broad
accessibility acceptance, secure GATT/Ready, Heltec OLED, touchscreen, release,
endurance, or field evidence. OT-061 and the historical 31% calculation remain
unchanged; current V1 Companion and V2 Integrated scores remain unmeasured. See
[OT-062](../tests/hardware/OT-062-2026-08-16.md).
OT-061 closes the first bounded physical OpenTrail target gate on only
`OT-DEV-001`; `OT-DEV-002` remained disconnected and untouched. Manual
ESP32-S3 ROM entry/exit returned to the unchanged public MeshCore runtime before
the owner authorized one full-chip erase and one write of the four frozen
OpenTrail regions. Exact input rehash, the single write, and post-write
`verify-flash` all passed. A manual reset then reached the deterministic boot
self-checks, NimBLE runtime, and at least two five-second USB heartbeat records
without a self-check/runtime/panic/assertion failure. The exact accepted Android
APK subsequently reported one compatible OpenTrail service advertisement on one
physical Android 13/API 33 phone; no candidate was selected, connected, paired,
or identified. This is experimental target and BLE-advertisement visibility
evidence, not GATT, application authorization, Ready, protected storage, LoRa,
GNSS, display, GPIO, power, recovery-after-loss, regulatory, support, or field
evidence. The historical phone-independent evidence calculation advances to
31%; current V1 Companion and V2 Integrated release scores remain unmeasured.
See [OT-061](../tests/hardware/OT-061-2026-08-16.md).
OT-060 adds foreground-only screen retention and the first physical Android
install evidence for the Trail debug app. One owner-authorized Android 16/API 36
handset installed and launched the exact 9,677,165-byte APK with SHA-256
`9CE206EEEAE2B13FC5C1092CEF41C226607FD3A9905A5797D4EBE31F3DC7F01C`.
With the original USB stay-awake setting restored and the device's 30-second
timeout active, the visible untouched Activity retained its screen-on flag and
normal active brightness for 40 seconds; backgrounding released focus and
reopening succeeded. Local test remained visibly fake and started neither a
Bluetooth permission prompt nor the real BLE service. This does not prove live
BLE, OpenTrail target firmware, LoRa, authorization, Ready, release, endurance,
or field behavior. At OT-060 acceptance, V1 remained 30%.
The close-range MeshCore path now has bounded transport, experimental OpenTrail packet-v0, and three-node MeshCore repeater hardware evidence including a software-forced route with a repeat-off negative control. A privacy-safe USB pass also proved that both Heltec companion builds detect/activate their connected GNSS hardware and emit GPS telemetry, while the SenseCAP repeater obtained a live fix and subsequent checks increased through four, seven, and eight satellites. Two strengthened role-reversed physical cycles carried 2/2 exact `OGA0` alerts and 2/2 correlated `OGK0` ACKs with zero loss/duplicates/errors and exact aggregate +4 SenseCAP flood RX/TX; each returned ACK then passed real OpenGauge peer authorization, session binding, replay/correlation ingress, and completed its exact reconstructed outbox entry. Fixed-capacity C++ radio, codec, identity lifecycle, group-access policy, non-secret configuration persistence, acknowledgement/retry/expiry, duplicate suppression plus canonical `OTD0` checkpoint serialization and the `ODS0` two-slot host storage boundary, controlled forwarding, priority admission, GPS fix validation/age handling, compact position encoding, LoRa airtime calculation, redacted diagnostics, the OpenGauge critical-alert ingress, mirrored `OGK0` acknowledgement codec, final-ingress-to-ACK responder, and commit-last ACK boot-session allocator have deterministic host tests. Cryptographic joining, target/physical/rollback-aware duplicate-checkpoint storage, persistent secret/group/message-counter state, authenticated acknowledgement/priority transport composition, on-device authenticated alert transport, physical field repeater behavior, complete-client GPS binding/performance, position scheduling/hardware transmission, maps, store-forward behavior, a direct SX1262 binding, rendered UI, and field performance remain unvalidated.

An owner-reported Wio Tracker L1 Pro has now completed a privacy-safe first
USB/runtime/configuration pass. Windows exposed public model `Seeed Wio Tracker
L1`; four read-only MeshCore cycles kept USB Companion
`v1.17.0-727fc05`, the 910.525 MHz/BW 62.5 kHz/SF7/CR5/22 dBm profile,
and zero error/traffic counters stable while uptime increased. GNSS was detected
but inactive with no GPS telemetry, and a non-transmitting Heltec comparison
reduced private data to match/distinct booleans only. This is OT-020
`partial`/`experimented` evidence, not over-air compatibility, exact hardware,
regulatory, recovery, or support evidence.

OT-020A now provides a host-only lifecycle/recovery coordinator contract for a
future supervised GNSS pass. Fifteen synthetic injected-adapter groups pass,
and the complete `tools/Test-Host.ps1` gate exits 0 with publication safety, all
host matrices, and existing read-only loader acceptance. The coordinator requires
the exact target, location-safe policy, GPS-off state, zero pending work, and no
existing recovery journal before any enable request; journal recovery authority
is created first. Enable acknowledgement is resolved by readback, telemetry is
reduced to presence only, and every post-journal path must restore/read back GPS
off, revalidate the target binding, update/delete the journal against the
expected record, and pass settled counters with the same session-scoped
continuity token, zero pending work, and no transmission.
An existing journal permits recovery-only handling that can never enable GPS.
No live adapter or device I/O occurred. The entire live GNSS phase remains open,
OT-020 stays `partial`, the unit stays `experimented`, and the missing shipping/
pre-write state remains explicit. See
`tests/hardware/OT-020A-2026-08-14.md`.

OT-020B now supplies the missing Windows host authority below any future Wio
discovery or command adapter. Fifteen authority groups cover a bounded,
no-overwrite DPAPI CurrentUser 32-byte HMAC key store; domain-separated target
and session-scoped continuity tokens; nonblocking per-target mutex ownership;
and two reduced counter observations across a requested five-second quiet
interval. The continuity token is not a device boot identity: transport
generation and plausible monotonic uptime remain independently checked. Busy,
abandoned, raced, release/close-uncertain, private-error, pending, traffic,
generation, uptime, and timing paths fail closed. A prior-window gap of exactly
180 seconds passes; 180.001 seconds fails. The separate OT-020A lifecycle suite
remains at 15 groups, and the hardened full host gate exits 0 with publication
safety. OT-020B has no serial, PnP, BLE, MeshCore, discovery, live-device, or
mutable device-command surface. The aggregate gate's pre-existing privacy-safe
read-only loader precheck separately observed one Heltec, one SenseCAP, and one
Wio; that is not OT-020B device evidence. OT-020 remains `partial`, the Wio
remains `experimented`, and the entire live GNSS phase remains open. See
`tests/hardware/OT-020B-2026-08-14.md`.

The current-tree C# and Python loaders now recognize that Wio family. The
warning-free 59-group C# suite and three consecutive built-in production
refreshes pass with one Heltec, one SenseCAP, and one Wio runtime-identified
and zero ready. A new failure-to-recovery gate found and fixed stale assertive
content on the collapsed error peer; hidden state now clears its text, ID, help,
and live setting. The replacement 464-file source-free package passed exact
manifest/hash/extraction/launch checks plus three native external UI Automation
selection/Refresh/live-event/heading cycles against the exact one-Heltec, one-
SenseCAP, one-Wio public roster. A non-remediating Defender archive scan found
no threats. This remains local engineering evidence, not a public release.

OT-030 now tracks a laptop-only dual virtual-LCD simulator. Historical OT-030A
proved the first isolated two-window WPF shell, but its generic Home/Messages/
Compose/Alerts workflow was not the firmware model and is retained only as
historical host evidence. OT-030B replaced it with the renderer-neutral shared
C++ `PortableUiShell`, canonical `UiFrame`, and fixed 466 x 466 logical render
plan. Each WPF LCD draws only the native host's offered primitives and returns
an exact action slot; Windows device/connection/evidence controls remain host
chrome outside the portable circle. Two-phase offer/present/commit prevents a
failed render or stale generation/revision input from advancing shared state or
emitting a typed request.

OT-030B also added a bounded companion bridge. Passive VID/PID discovery lists
only public candidate labels and never opens or queries hardware. A USB session
requires explicit selection, exact private binding/runtime recheck, exclusive
one-client assignment, and generation-bound cleanup. The SenseCAP repeater is
excluded. Child-process input/output, cancellation, errors, stderr, teardown,
and restart are bounded. The live USB application admits only fixed quick-
status and critical-alert requests; acknowledgement, template/arbitrary chat,
archive, and position requests fail closed. Its unauthenticated `OTS0` helper
protocol can decode an inbound correlated `OTS0:A` observation, but that can
advance only an already-outbound matching critical alert. The accepted warning-
free OT-030B focused gate was 32 Core, 23 Windows bridge, 15 private helper, and
11 WPF groups. No hardware or serial port was accessed for that acceptance.

OT-030C adds the shared message surface without expanding the compact 24-byte
`UiFrame`. A shell-owned `UiPresentationSidecar` carries the bounded text and
message metadata for the exact offered frame, preserving the legacy embedded
ABI and result-object memory budgets. The surface uses a 12-message snapshot
boundary, 96-byte printable-ASCII copied text, explicit truncated/unavailable
presentation, two-row Inbox/Outbox pages, detail read only after successful
presentation, eight fixed C++ Compose templates, bridge-local delivery evidence,
and held acknowledgement of an exact active inbound critical alert. Protocol v2
is newline-delimited, 4096-byte-command/8192-byte-reply bounded, exact-field,
and uppercase-hex. Generation, revision, request, bridge-session epoch, template
or message identity, and applied message sequence must all agree. Version 1,
NUL, partial EOF, oversized input, malformed exceptional text, stale input,
replayed evidence, and contradictory completion reject without applying state.

The accepted warning-free focused gate passes strict C++ shell/render
tests, 33 Core groups, 23 Windows bridge groups, 15 private helper groups, ten
native-protocol groups, and 11 integrated WPF groups. It includes exact-once
typed request handling and two native LCD sessions crossing one fixed-template
message over local loopback. Representative Home, Compose, post-send Client A,
and inbound-detail Client B renders passed visual review. The complete expanded
112-executable `tools/Test-Host.ps1` matrix and both publication-safety layers
pass; remote publication verification remains pending. OT-030C is `done` for
its bounded host/shared-model increment; parent OT-030 remains `partial`.

Queued-local, bridge-accepted, bridge-observed, and bridge-acknowledgement-
observed remain separate local evidence. None proves a production OpenTrail
packet, MeshCore application command, LoRa transmission, authenticated peer,
physical receipt, operator response, target firmware, physical LCD/input, or
supported hardware. Real monitor/theme/assistive-technology acceptance,
packaging, installer, signing, clean-machine operation, two-device USB use, and
physical retest remain open. OT-030 is `partial`, and V1 remains 29%.

The accepted product direction keeps two future presentation tracks in this
repository over shared versioned behavior: an affordable Android companion
using a separately approved mesh device, and the original self-contained
touchscreen client. The simulator is their shared behavior reference, not a
production dependency. Android platform/device, signing, accessibility, and
distribution evidence remain open; iPhone/store direction is undecided. The
existing self-contained V1 definition remains unchanged.

OT-033 now fixes the first production-facing Android/device seam without adding
an Android project or BLE target. A brand-neutral three-characteristic GATT v0
contract separates Protocol Info, Write-With-Response Command, and indicated/
notified Stream traffic. Exact fixed-capacity `OTB0/v0` capability records and
`OTC0/v0` fragments are bounded to 16 and 148 bytes; unknown versions, roles,
capabilities, kinds, reserves, lengths, sessions, and fragments fail closed.
The one-controller guard admits only an opaque private binding backed by an
encrypted link, authenticated bond, and separate application authorization.
Session nonces and request IDs increase without wrap; duplicates cannot reapply
an action, stale/wrong-controller/wrong-session/server-direction/fragmented
requests reject, and public status omits the controller binding. Fifteen focused
host groups, 100/100 repeats, and the complete 113-executable host/publication-
safety gate pass warning-free. This is codec/session-admission evidence only:
OT-033 itself includes no BLE stack, pairing/OOB workflow, Android application,
semantic snapshot/action payload, reassembly/result cache, radio/GNSS/persistence
binding, target build, or physical-device result. No hardware was accessed or
written, and V1 is unchanged.

OT-035 now supplies the first production-neutral semantic payloads above that
framing. Exact fixed-capacity `OTX0/v0` snapshot request, `OTN0/v0` typed status,
`OTA0/v0` user intent, and `OTR0/v0` action result records occupy 8, 32, 20,
and 20 bytes and fit one v0 fragment. The snapshot exposes only device-owned
typed radio/GNSS/power/position-sharing state, a queue count, a nonzero revision,
and an optional exact critical-alert ID. Actions are limited to the four
canonical quick-status IDs, acknowledgement of that exact alert ID, and
distinct position-sharing Start/Stop. A strict dispatcher rejects every
payload under the wrong `OTC0` kind. Results distinguish local admission,
device-owned queue admission, and typed rejection; queued never means sent or
delivered. Thirteen warning-free focused groups and 100/100 repeats cover exact
vectors, closed enums, strict lengths/version/reserves, action/result coherence,
atomic failure, and envelope-kind binding. The complete 114-executable host/
publication-safety gate passes. There is still no BLE stack, target runtime,
request fingerprint/result cache, Android binding, coordinates, message
history, radio/GNSS integration, or physical result. No hardware was accessed
or written, and V1 remains 30%.

OT-036 adds the first buildable native Android foundation without claiming a
functional device connection. A pure Kotlin module strictly encodes/decodes
`OTB0/v0` protocol info and `OTC0/v0` fragments against the shared C++ golden
bytes. A Jetpack Compose shell exposes Disconnected, Selecting, Connecting,
Connected, and Failed states over a deterministic fake-only transport with two
local choices and one active fake connection. Six protocol tests, four
application-state tests, warning-as-error lint, debug assembly, and an
independent APK/manifest audit pass. The debug artifact uses stable technical
package `io.github.nbjelanovic.otclient`, displays the current working product
name, and requests no Bluetooth, nearby-device, location, internet, storage, or
management permission. The activity-owned controller is not lifecycle-safe and
the fake adapter's free-form errors are not a production boundary. Live BLE,
typed adapter errors, configuration/process recovery, accessibility/rendered
acceptance, signing, installation, distribution, and physical one-phone/
one-device evidence remain open. No emulator, phone, serial port, or LoRa device
was accessed; V1 remains 30%.

OT-037 links the accepted C++ companion envelope and semantic payload sources
into the generic build-only Heltec candidate. Its boot-path self-check compares
exact fixed `OTB0`, `OTA0`, and combined `OTC0` plus `OTA0` vectors, then
decodes and semantically dispatches the action request before allowing the
heartbeat path. Static admission passes 3/3 and 100/100 repeats. ESP-IDF v6.0.2
produces a hash-stable 145,657-byte application image, 3,692 bytes above the
OT-034 baseline, and the link map contains both companion objects. The evidence
is explicitly `BUILD-LINKED-NOT-RUN`, `NOT-FLASHED`, and
`UNREVIEWED-RUNTIME`; no hardware was accessed. The image still has the generic
2 MB/DIO/80 MHz profile, and no BLE stack, GATT/session owner, radio, GNSS,
storage, recovery, physical runtime, or supported-hardware evidence exists.
OT-034 remains `partial`, and V1 remains 30%.

OT-038 extends the Android foundation with exact Kotlin semantic payload parity
and a typed fake-only workflow. Nine shared C++ golden rows cover status,
actions, and results; strict Kotlin validation preserves the exact `OTC0` kind,
closed enums, reserves, bounds, and result coherence. The fake UI exposes four
quick statuses, exact pending-alert acknowledgement, and position-sharing
Start/Stop with monotonic correlated test envelopes. Six base protocol, ten
semantic, and eleven app-state tests plus lint/APK inspection pass. All visible
outcomes remain explicitly fake/test state. No Bluetooth permission, Android
Bluetooth facade, device, emulator, install, or radio evidence exists.

OT-039 adds the target-neutral fixed-memory request coordinator needed behind a
future GATT adapter. It preflights exact response capacity and bytes, uses a
pure prepare plus atomic action commit, and caches only the last completed
request/response. An exact duplicate replays byte-identically without another
authority call; a conflicting, stale, old-session, exhausted, or terminally
failed request cannot apply. Sixteen strict groups and 100/100 repeats pass,
raising the complete host matrix to 115 C++ executables. The authority adapters,
BLE/GATT runtime, persistence, radio path, and physical evidence remain absent.
OT-038 and OT-039 are bounded `done` increments; V1 remains 30%.

OT-040 links the accepted request coordinator into the generic build-only
Heltec candidate and adds one target-local deterministic boot self-check. Exact
action/result and snapshot/status requests pass through full `OTC0` framing;
one prepared action commits once, and an exact duplicate replays the identical
response without another authority call. Strict native checks, 100/100 repeats,
static admission, the complete 116-executable host matrix, and a reproducible
ESP-IDF v6.0.2 build pass. The 148,949-byte application remains generic 2 MB,
`BUILD-LINKED-NOT-RUN`, `NOT-FLASHED`, and `UNREVIEWED-RUNTIME`; fixed fake
authorities are not a real adapter, GATT service, device runtime, or delivery
result. Exact evidence is recorded in
[OT-040](../tests/hardware/OT-040-2026-08-14.md).

OT-041 adds the lifecycle-safe Android BLE runtime boundary below the still
fake-only visible application. One owner coordinates scan, GATT, reconnect,
security/application-authorization evidence, MTU and Protocol Info negotiation,
indication-only Stream subscription, authoritative snapshot opening, exact
session/action correlation, bounded phase/result timeouts, owner-thread callback
handling, stale-generation rejection, observer re-entrancy containment, and
complete stop/destroy cleanup through an injected facade. Six base-protocol,
ten semantic, 17 BLE-runtime, and 11 controller tests pass with clean lint and
debug APK assembly. At OT-041 acceptance the manifest requested no Bluetooth,
nearby-device, location, internet, or storage permission, no Android Bluetooth
facade was implemented or wired, and no phone/device/emulator/ADB/BLE access
occurred.
OT-040 and OT-041 are bounded `done` increments; OT-034 remains `partial` and
V1 remains 30%.

OT-042 adds the exact BLE Companion GATT v0 service and three characteristic
definitions to the generic build-only target. Its NimBLE configuration allows
one peripheral/GATT-server connection, Secure Connections only, authenticated
and encrypted characteristic access, and a 16-byte minimum key. The current
application does not inject authorization/coordinator authority, register the
service, initialize NimBLE/controller state, or advertise. Command writes are
denied before mutation until registered CCCD and per-connection indication-
subscription ownership exists. Static admission passes 100/100, and two pinned
ESP-IDF v6.0.2 builds reproduce a 155,061-byte generic image and exact hashes.
Evidence is [OT-042](../tests/hardware/OT-042-2026-08-15.md) and remains
`BUILD-LINKED-NOT-RUN`, `NOT-FLASHED`, controller `NOT-STARTED`, advertising
`NOT-IMPLEMENTED`, and authorization `NOT-INJECTED`.

OT-043 adds a concrete but unwired Android 12+ Bluetooth facade behind OT-041.
It enforces exact service/characteristic/indication profiles, bounded scans and
opaque tokens, API 31/32 compatibility, main-thread callback ownership,
operation-specific Scan/Connect permission checks, post-revocation cleanup,
and typed privacy-safe failure. Six protocol, ten semantic, six Android-policy,
17 BLE-runtime, and 11 controller tests pass with clean lint. The 9,656,378-byte
debug APK has SHA-256
`CAAC3922EBC2BD011F12EE4A334DA98FBA1AE9467228C23174ED658F3F650AFE`.
Its manifest declares Scan with `neverForLocation` and Connect only, plus the
generated same-app receiver permission. At OT-043 acceptance the shipped
activity remained fake-only with no Nearby Devices permission UX or facade
wiring; no phone/device/emulator/ADB/BLE access occurred. OT-042 and OT-043 are bounded `done` increments;
OT-034 remains `partial` and V1 remains 30%.

OT-044 adds the response-safe GATT lifecycle missing from OT-042. One fixed-
memory owner binds exact registered Command, Stream, and CCCD handles, one
connection, ATT MTU, encrypted/authenticated/application-authorized evidence,
indication subscription, coordinator session, and one outstanding response
token. The complete maximum response is reserved before coordinator mutation.
Congestion and guarded callback re-entry reject without mutation or state
advance; wrong/stale completion preserves the exact pending response.
Unsubscribe, security loss, submit failure, negative exact completion, or
timeout contain and block until exact disconnect. Fifteen strict groups and 100/100
repeats pass. Target boot self-check, static admission, and two identical ESP-
IDF v6.0.2 builds pass; the 157,957-byte generic image and exact artifacts are
recorded in [OT-044](../tests/hardware/OT-044-2026-08-15.md). The owner is only
build-linked/self-checked: the controller is not started, advertising is absent,
authorization is not injected, real NimBLE Command dispatch remains denied,
and no device was accessed or flashed.

OT-045 replaces the fake-only Android entry point with an explicit Local test
or Bluetooth-device mode choice. Bluetooth mode owns Android 12+ Nearby Devices
permission request/settings recovery, explicit scan/select/connect/disconnect,
typed public errors, and the accepted actions through one lifecycle-safe
controller/runtime/facade composition. There is no silent fake fallback. Nine
new controller/lifecycle tests cover stop/resume, mode switching while scanning,
connecting, or Ready, late permission callbacks, revocation, observer re-entry,
off-owner rejection, and exactly-once cleanup. The combined Android gate passes
59 tests and lint; the 9,595,057-byte debug APK has SHA-256
`0CCD4DECAAAE712A587DB97BC744B515E97008532FAA834BBF3D7BE4C715D76C`.
At OT-045 acceptance the injected production application-security authority
remained deny-all, so no
successful live Ready state, phone/device/emulator/ADB/BLE access, install,
signing, or field evidence exists. OT-044 and OT-045 are bounded `done`
increments; OT-034 remains `partial` and V1 remains 30%.

OT-046 supplies the host-tested one-phone authorization policy that the live
GATT path still lacks. A trusted bond authority must provide a stable opaque
128-bit token that is never an address, public/client identifier, client value,
or key. One externally serialized authority instance per boot binds encrypted,
authenticated claims to exact boot, monotonic session, and private controller
challenges; admits one active controller; and requires an explicit 30-second
physical claim, revoke, replace, or reset window. Its injected persistence
backend must atomically compare, commit, advance a trusted generation floor,
and read back the exact record. Failure, uncertainty, conflict, rollback,
exhaustion, replay, clock rollback, invalid actions, and callback re-entry fail
closed. Sixteen strict groups, 100/100 repeats, and the complete 118-executable
host matrix pass. This authority is not build-linked and has no BLE bond-store,
persistence, physical-input, target, or device evidence.

OT-047 adds the device-authoritative phone-claim presentation without enabling
live claims. Android Bluetooth mode explicitly offers authorize-this-phone and
replace-lost-phone, instructs physical device action within 30 seconds, and
accepts only bounded device-issued Pending/Accepted/Denied/Replaced events with
exact token, purpose, and generation correlation. Timeout, malformed, or lost
results display unknown device authority and require reconnect/resync; the app
never invents rollback. Lifecycle, permission, mode-switch, callback re-entry,
queue, timer, and cleanup races fail closed. The combined Android gate passes
66 tests and lint; the 9,611,441-byte debug APK has SHA-256
`3EB3986BD17F3DFC918936CF8978E44A36089E38D9CDCD877336BB9A16024C44`.
The production claim client is disabled; no live bond, phone/device/emulator/
ADB/BLE access, install, signing, or field evidence exists. OT-046 and OT-047
are bounded `done` increments; OT-034 remains `partial` and V1 remains 30%.

OT-048 freezes fixed brand-neutral authorization payloads and their client-side
response tracker. Exact 8-byte `OTL0/v0` Claim Start, 24-byte `OTP0/v0`
Pending, and 28-byte `OTF0/v0` terminal records bind to dedicated `OTC0/v0`
kinds `0x03`, `0x84`, and `0x85`. The device issues one nonzero opaque 128-bit
correlation privately bound to the exact provisional session, exchange, and
authorize-or-replace purpose. It is not an identity, address, key, secret,
physical token, or value that may be displayed, logged, or persisted. The
fixed tracker requires explicit future negotiated support plus encrypted and
authenticated bond evidence, enforces Pending before one terminal result,
rejects mismatched or replayed context, and makes exact transport close the
sole release of a connection generation when called by its owner. Fourteen strict groups, ten shared
vectors, 100/100 repeats, and the complete 119-executable C++ host matrix pass.

The generic target already links the shared protocol, semantic-dispatcher, and
coordinator sources touched by OT-048. Two pinned ESP-IDF v6.0.2 rebuilds
reproduced a 157,957-byte image and 158,080-byte BIN; those sources remain in
the link map while the new authorization wire/tracker source is absent. Exact
artifacts are in [OT-048](../tests/hardware/OT-048-2026-08-15.md). This is
build-linked-not-run kind recognition and normal-path rejection only, not a
provisional authorization transport or runtime.

OT-049 mirrors the three records, dedicated envelope kinds, shared vectors,
closed enums/coherence, and exact-close tracker behavior in pure Kotlin. The
combined Android gate passes 77 JVM tests across eight suites (protocol suites
6, 10, and 10; application suites 6, 17, 11, 1, and 16) and warning-as-error lint. The isolated
debug APK is 9,627,825 bytes with SHA-256
`967FCD7A032ECED63789378F5B3C0F6AC86D06CE9CF3B6B16205E7C49B8093A3`.
OT-049 added no activity, BLE-runtime, or GATT-transport wiring, and its
production claim client remained disabled. At that increment's acceptance,
`OTB0/v0` had no claim-support bit and GATT required application authorization
before Protocol Info and session negotiation, so it provided no live
provisional session or negotiated claim capability. OT-048 and OT-049 remain
bounded `done` codec/tracker increments; OT-034 remains `partial` and V1 remains
30%.

OT-050 adds the restricted device-side provisional authorization lifecycle and
explicit negotiated capability missing above. Its exact 20-byte `OTB0/v0.1`
Protocol Info carries claim capability `0x10`, the existing bounds, and one
device-issued provisional session nonce; claim-capable payload capacity is
strictly 28 through 128 bytes. Protocol Info is available only with trusted
encrypted/authenticated-bond evidence. Claim admission additionally binds the
exact connection/generation/session/exchange/purpose/correlation and registered
Command/Stream/CCCD handles, requires indication subscription and MTU at least
51, confirms Pending before later physical authority resolution, reserves each
response before authority work, and promotes only after exact Accepted/Replaced
terminal indication confirmation. Normal traffic still requires MTU 151 and an
explicit Snapshot Request. Failure, timeout, unsubscribe, security loss,
disconnect, or stale callbacks fail closed without inventing a denial.

Twenty strict groups and 100/100 repeats, the complete 120-executable host
matrix, the deterministic target self-check at 100/100, and static admission
3/3 pass. Two pinned ESP-IDF v6.0.2 builds reproduce a 165,349-byte image and
165,472-byte BIN with SHA-256
`E2ACF6672925D2FF298BD58E7C7BCBA564D46F1B7A6853D67865CE62F09D12B9`;
the link map retains the authorization wire and restricted lifecycle. Exact
evidence is in [OT-050](../tests/hardware/OT-050-2026-08-15.md).

OT-051 mirrors the v0.1 record and provisional operation order in Android. The
runtime-backed client reads Protocol Info before requesting its advertised MTU,
enables exact Stream indications, writes one Claim Start, requires correlated
Pending and terminal frames, and permits the explicit normal Snapshot Request
only after Accepted/Replaced. Exact lease and generation checks contain denial,
timeout, permission loss, disconnect, malformed/stale input, and lifecycle
release without automatic retry. The combined gate passes 90 JVM tests across
ten suites (protocol 6, 10, 10, and 3; application 7, 9, 17, 11, 1, and 16),
lint reports no issues, and the 9,644,209-byte debug APK has SHA-256
`28ED3014ACE420F8C531625211D26BD3FB9D522F1349BACA0878F94726534D8A`.

At OT-050/051 acceptance both increments remained non-live. The real target
NimBLE AUTHOR path was baseline v0.0 and denied,
controller/service/advertising startup was absent, no trusted target bond,
physical-input, persistence, or device authority was injected, and
`MainActivity` still constructed the disabled claim client. No phone, emulator,
peripheral, ADB, install, signing, successful authorization, target runtime, or
physical BLE evidence existed. OT-050 and OT-051 remain bounded `done`
increments.

OT-052 now build-links the real ESP-IDF NimBLE callback adapter for the frozen
restricted lifecycle. It binds exact registered characteristic handles and the
separately discovered Stream CCCD; refreshes encryption, authentication, bond,
16-byte key, MTU, and private trusted-binding evidence on protected access and
again at physical authority resolution; permits only protected Protocol Info
plus authorization claims before promotion; reserves response storage before
mutation; and accepts indication completion only for the immutable connection,
transport-generation, session, exchange, Stream-handle, and token tuple. A
successful exact protected 20-byte v0.1 Protocol Info read is device-enforced
security evidence; Android bond state alone is not.

Ten strict callback-adapter groups pass at 100/100, the composed target boot
self-check passes 100/100, static admission passes 3/3, the pinned NimBLE
teardown-order gate passes, and the full native matrix passes 121 enabled
executables. Two pinned ESP-IDF v6.0.2 builds reproduce a 170,313-byte image and
170,432-byte BIN with SHA-256
`22CAE43F7AEA9D980602C41E1ACEB49CA1174315EE87598D15E6717A27A1E4D4`.
Exact evidence is in [OT-052](../tests/hardware/OT-052-2026-08-15.md).

OT-053 wires the frozen protected-read flow into the Android production
composition selected by explicit Bluetooth mode, with no fake fallback. The
client uses successful protected Protocol Info access—not bond state alone—as
the device-enforced security-path evidence, then performs advertised MTU,
Stream indication subscription, Claim Start, correlated Pending/terminal, and
explicit post-promotion Snapshot Request. The Android gate passes 101 JVM tests
across ten suites (protocol 29; application 8 + 15 + 17 + 11 + 1 + 20), lint
reports no issues, and the 9,644,209-byte debug APK has SHA-256
`BE385FEB8966210C4C09027388C3F560745F6A075B9CBB1ABF25DC0893C0033C`.

OT-054 adds the fixed 32-byte durable owner/tombstone record, protected-store
compare/commit/readback contract, independently rollback-resistant floor
requirement, and private bond-reference/device-secret PRF boundary. Seventeen
strict persistence and composed-authority groups pass at 100/100; target
self-check passes 100/100; static admission passes 3/3; and all 122 enabled
native executables pass. Two pinned ESP-IDF v6.0.2 builds reproduce a
175,701-byte image and 175,824-byte BIN with SHA-256
`D39430096B7BEDD0F69D9ECCDE2424EDCD635C0BEA904EB2E4FCA3EEED307080`.
The target preflight remains denied: NVS encryption is not configured, and no
protected-NVS runtime proof, usable protected key, private bond store, separate
PRF key, atomic protected record/floor backend, or independent rollback floor
exists. No target production path calls NVS or eFuse/HMAC APIs. Exact evidence
is in [OT-054](../tests/hardware/OT-054-2026-08-15.md).

OT-055 gives the production Android BLE graph one explicit user-started,
non-exported `connectedDevice` foreground-service owner. It uses
`START_NOT_STICKY`, starts foreground disclosure before constructing BLE,
contains no boot receiver/background auto-start, separates Local test ownership,
and never automatically retries a claim. Manifest admission is limited to
Bluetooth Scan/Connect, base and connected-device foreground service, Android
13+ notifications, and AndroidX's generated same-app permission; no network,
location, or storage permission is present. The gate passes 124 JVM tests
across twelve suites (protocol 29; application 95), clean warning-as-error lint,
manifest inspection, and debug assembly. The 9,660,781-byte APK has SHA-256
`33174B72792E2AFC0D03AB52DFAC6613BAE48618BF268C3197D7E04105897722`.

OT-056 codes the smallest one-connection NimBLE boot/runtime owner. After all
deterministic self-checks and the exact denied OT-054 preflight, `app_main` is
coded to initialize the host/controller, register the protected service, and
advertise standard flags plus the public service UUID under a privacy-capable
address policy. Its advertising-data payload contains no name, manufacturer
data, address field, or device/user/group identifier. Host callbacks cross a
fixed eight-entry queue into one owner context; disconnect cleanup precedes a
bounded tokenized restart, and startup/queue/reset/retry/stop errors contain.
Because protected storage/private bond/PRF/floor admission remains denied,
configured SC/MITM/bonding is not usable-bond proof, every connection is
immediately terminated, and claim plus normal-command authority remains closed.
Thirteen strict groups pass at 100/100, target self-check passes 100/100, all
123 native entries and full Test-Host pass, static admission passes 3/3, pinned
NimBLE teardown/stop ordering passes, and two builds reproduce a 433,104-byte
BIN with SHA-256
`8A25508B50B29FE2A09CF3390AE53473BBA0BF04F60AE9A6366B930D516FCE2A`.
Exact evidence is in [OT-056](../tests/hardware/OT-056-2026-08-15.md).

OT-057 adds a renderer-neutral Android Group / Location presentation model.
Real Bluetooth cards report coordinates unavailable because no accepted BLE
coordinate source exists; Local mode uses separately labeled deterministic
synthetic cards. It adds no phone GPS, map, tiles, network, location, or storage
permission. The gate passes 134 JVM tests across thirteen suites (protocol 29;
application 105), clean lint, manifest inspection, and debug assembly. The
9,677,165-byte APK has SHA-256
`697D73A6E48F1850A2756FB0886A8201C653804FB5A2B9628DD26790C8EC65B1`.

OT-058 bounds and coalesces simulator native-session notifications without
weakening the five-second failure timeout. Focused validation passes 33 Core,
23 Windows bridge, 15 private helper, 10 native-protocol, and 13 integrated WPF
groups; full Test-Host exits 0.

OT-059 freezes the first evidence-bound Heltec memory and recovery-layout build
profile for OT-DEV-001. The pinned ESP-IDF v6.0.2 configuration selects 16 MiB
flash, QIO/80 MHz operation, embedded 2 MiB quad PSRAM at 80 MHz with boot
initialization and memory testing, and explicit allocation through capability
APIs. Its exact partition table contains otadata, a 5,177,344-byte factory
slot, two 5,242,880-byte OTA slots, and a 1 MiB application-owned state row;
the last row ends exactly at the 16 MiB boundary. No updater, OTA workflow,
state storage, recovery authority, or physical write is implemented.

Four target-only admission groups pass. Two pinned builds reproduce the exact
configuration, partition binary, and artifacts; the 437,552-byte BIN has
SHA-256
`F0E81310C62CA0C17CA2531AF9B0D5BD5E6E115E1649F84C97514F72D51D6A3A`.
The image header truthfully remains DIO bootstrap while the generated
configuration selects QIO. No device, port, or flash was accessed. Exact
physical memory behavior, recovery, runtime, radio/GNSS/GPIO use, and support
remain open. Exact evidence is in
[OT-059](../tests/hardware/OT-059-2026-08-15.md).

At OT-059 acceptance these increments remained non-live: the target runtime was
`CODED-BUILD-LINKED-NOT-RUN`, nothing was flashed, and no protected storage or
bond authority was admitted. OT-061 supersedes only that no-flash/no-runtime
boundary with one experimental target write, verified boot/heartbeat, and one
service-advertisement observation. Protected storage, pairing, authorization,
Ready, normal commands, LoRa, GNSS, display, and support remain open.

At OT-034 acceptance, the first repository-native ESP-IDF target surface was a
strictly build-only `heltec_v4_bench` candidate. Its exact `OTTB0` contract
pinned ESP-IDF v6.0.2 and the `esp32s3` compiler target while marking the
received revision unknown, supported hardware false, and device writing
unauthorized. The application emitted one fixed startup line plus a recurring
heartbeat over the USB Serial/JTAG console. Its only application-owned dynamic
value was boot-local elapsed milliseconds, and it read no device-specific
identifier. The application did not initialize, access, or bind radio, BLE,
Wi-Fi, GNSS, storage, identity, secrets, GPIO, OLED, battery/power, or
`PortableClientComposition`.

That historical gate passed three host admission groups plus
PowerShell/Python/JSON parsing. The pinned build reported 141,965 application
bytes, 86% of its application partition free, a 142,080-byte BIN, and exact
artifact hashes with a hash-stable incremental rerun, recorded in
`tests/hardware/OT-034-2026-08-14.md`. Its generic 2 MiB/DIO/80 MHz header and
default NVS/PHY/factory partition table were not an authoritative received-
Heltec profile. OT-059 supersedes those build-profile, generated-configuration,
partition, size, and artifact facts; it does not supersede OT-034's physical
boundaries. Exact-board authority, sacrificial-first recovery, physical
runtime/log review, and every physical capability remain open. At that historical checkpoint the two Heltec units remained bench candidates,
no hardware support claim followed, and V1 was 30%. OT-061 later supplied the
first experimental physical target evidence without creating a support claim.

The owner has also accepted the provisional working-name hierarchy in Decision
0008. `Limited Underground` is the parent; `Limited Underground Trail` is the
Android application and umbrella family; Essential is the screenless,
phone-required LoRa companion; Gold is the one-touchscreen client; Platinum is
the two-display client; and `Limited Underground Trail Repeater` is the
optional repeater. The shared desktop tool is `Limited Underground Firmware
Loader`; it remains visibly Preview and inspection-only until real writing and
recovery pass. These names remain pending professional clearance and do not
change OpenTrail repository/folder/namespace names, `OT-*` records, protocol or
GATT identifiers, schemas, cryptographic domains, compatibility/board IDs, or
device IDs.

A deterministic group-load model now accounts for the planned four-client
standalone, four-plus-repeater, and eight-plus-repeater phases using exact LoRa
airtime plus explicit source and forwarding transmissions. It is host planning
evidence, not a field-capacity, collision, delivery, range, or regulatory result.

The first-release capacity policy now states one public boundary: at most eight
active clients in one group plus one optional authorized repeater. Four
standalone clients must pass first, followed by four-plus-repeater and then
eight-plus-repeater evidence on frozen hardware/firmware. The base client may
not require the repeater, server, internet, phone, laptop, or vehicle. No phase
has passed yet, so this remains a release target rather than a support claim.

The product boundary map now defines one self-contained base client and five
optional role families. Repeater, opt-in archive, OpenGauge vehicle input,
offline-map/large-display hardware, and post-session management may add value
but may not become requirements for base radio/group operation. Their allowed
data and failure behavior are explicit; concrete hosting operations, private
location/participant data, credentials, and provider choices remain outside the
public repository.

The first generic quick-status catalog now has one compact host-tested payload.
Exact 12-byte `OTQ0/v0` carries only one of four fixed meanings: I'm OK, Need
assistance, Anyone online?, or Available to help. It contains no participant,
device, group, location, timestamp, message ID, acknowledgement, free text,
credential, or routing data. Ten groups plus 100/100 focused repeats cover
canonical vectors, all entries, strict version/reserve/length/status handling,
no-mutation encode failures, and corruption at every byte. This is payload
evidence only: authenticated packet-v1, priority/replay/expiry/ACK policy,
outbound admission, menu/confirmation UX, renderer, target binding, radio
delivery, and four-device evidence remain absent. `Need assistance` is not a
guaranteed rescue request and does not replace the held critical-alert path.
See the [quick-status payload contract](protocol/QUICK_STATUS_PAYLOAD_V0.md).

Local selection now has a separate revision-safe owner. Two canonical
four-action pages fit the four-choice catalog while preserving Back on every
page. Exact Next/Previous transitions consume one newer revision; a deferred
display retries the pending page without polling a second input. A valid choice
returns one typed `QuickStatusKind` and the minimum newer parent revision, then
the menu becomes inactive. Ten menu groups plus the thirteen-group checked
local-interface suite pass 100/100 focused repeats. The coordinator holds no
radio, queue, delivery, identity, storage, GPS, archive, server, or critical-
alert reference, so selected does not mean queued/sent/received/delivered.
Parent-shell restoration, authenticated outbound composition, outcome UX,
renderer/physical controls, target binding, and device evidence remain. See the
[quick-status menu contract](platform/QUICK_STATUS_MENU_COORDINATOR_V0.md).

A narrow parent page now owns exact entry and restoration around that chooser.
Its status frame contains only Quick status plus Back. Nested Back restores the
parent without a choice; a typed choice is withheld until the parent is
successfully restored at the menu's returned newer revision. If restoration is
temporarily unavailable, the pending revision/choice remains private and no
second input is polled. Ten groups plus 100/100 repeats pass. This closes the
parent/menu UI handoff only: no queue/radio/identity/storage/GPS/archive/server/
alert reference exists, selected is not sent/delivered, and the complete home/
messages/critical/position/archive/recovery shell remains. See the
[quick-status parent-page contract](platform/QUICK_STATUS_PARENT_PAGE_COORDINATOR_V0.md).

The update path now has separate fail-closed bundle admission and board/install
preflight policies, a bounded firmware-bundle candidate inspector, and
read-only Windows USB/runtime inspection adapters.
Bundle admission requires externally verified canonical manifest and image
digests, signature, trusted exact signer ID, hardware/processor/target-role and
revision binding, minimum bootloader schema, exact image length/capacity, and a
non-rollback release generation. Twelve groups pass. Admission means only that
the candidate may reach the board preflight. The Windows adapter now enforces
an exact three-entry `.fwbundle`, canonical bounded manifest, complete image
length, matching image SHA-256, and RSA-PSS-3072/SHA-256 verification when a
signer public key is pinned. Its packaged catalog is deliberately empty, and it
supplies no production signer trust, protected revocation/generation policy,
release admission, signing key, or writer.
One final pure composition now requires bundle admission, board/install
preflight, and exact cross-gate equality for hardware profile, processor,
target role, revision range, minimum bootloader schema, maximum image size, and
signed/candidate length before `ready_to_write`. Eight groups pass. This closes
the host decision topology only; no result consumer, one-use invalidation,
writer, target, or physical evidence exists.
The first loader-facing presentation model now consumes the redacted live
runtime snapshot and produces fixed, privacy-safe screen data. Its connected
bench result is `3 found · 3 inspected · 0 ready to flash`, with one SenseCAP
Solar repeater, one Heltec V4 OLED companion, and one Wio Tracker L1 companion.
Refresh/Inspect are enabled;
all firmware selection/write/recovery actions are disabled. Four groups pass.
A real .NET 8 WPF development shell now consumes that document and renders
candidate cards. The application and 59 independent C#
document/identity/accessibility/production-window-refresh/selection/failure-recovery/forward-and-reverse-keyboard/automation-peer/automation-scroll/automation-heading/high-DPI/resize/contrast-theme/snapshot-binding/device-match/process-boundary/USB-runtime-probe/
firmware-bundle-candidate/packaged-inspection scenarios build warning-free;
malformed
counts, any ready-to-flash candidate, enabled global Flash authority, and
private local-port disclosure fail closed. Device-card accessibility copy is
derived only from validated public fields, omits the internal candidate
reference, explains every Flash blocker, and rejects empty, oversized, or
control-bearing blocker copy. F5 invokes the same bounded refresh path,
keyboard focus is visible, and summary/error fields are automation live
regions. Only the newest active refresh may
publish, and window close invalidates pending output. Helper stdout/stderr are
bounded; cancellation, timeout, or invalid output triggers best-effort
termination of the inspection process tree, and raw stderr remains hidden. The
shell now has one local candidate-bundle picker and a bounded structural/image-
digest/signature inspector. It never displays or retains the selected path.

Three consecutive current-tree built-in refreshes republish the expected one-
Heltec, one-SenseCAP, one-Wio roster, keep all three runtime-identified and zero
ready, and clear selection/action state. A failure-to-recovery acceptance group
found that a collapsed error peer could retain stale assertive content after a
successful refresh. Hidden error state now clears text, Automation ID, and help
and sets its live setting Off; only an actual current visible failure is
assertive. The replacement source-free package now passes independent
manifest/hash/extraction/launch verification and three external UI Automation
cycles against the exact current public roster.
Injected packaged failure/recovery, Narrator, and clean-machine behavior remain
separate gates.
An independent OT-019AI source-free probe now passes against the exact retained
package and public Heltec/SenseCAP/Wio roster. At 96 DPI it resized the packaged
window to exactly 900 x 620 and invoked the real Wio-card `ScrollItem`, moving
that unchanged 342 x 635 card from offscreen at x=425/y=1085 to onscreen at
x=425/y=254. Refresh focus, empty selection, disabled bundle/Flash authority,
enabled Refresh, and the exact three-found/three-runtime-identified/zero-ready
summary held; `inspection-error` was absent before and after, and cleanup left
zero owned processes/temp directories. A first integrated combined assertion
then exposed a verifier timing race while post-refresh UI state was still
settling; it did not establish a product defect. A complete settled-state wait
fixed the verifier. Three consecutive hardened runs—a two-repeat batch and one
reviewer-hardened run—then each passed exact package/roster verification, three
Refresh cycles, minimum-window `ScrollItem`, initial/selected/post-refresh no-
error checks, privacy, manifest/hash/launch, and zero-residue cleanup. The
complete `tools/Test-Host.ps1` gate and Windows PowerShell 5.1 parse/
publication-safety checks pass. OT-019AI is accepted.
Every candidate result is bound to the current connected-device snapshot. A
device refresh immediately discards the prior display, invalidates an
in-flight result, and blocks selection until a new validated snapshot is
published; window close invalidates both authorities.
The connected-card surface is now a keyboard-accessible single-selection list.
Only a reduced generic candidate ordinal from the current snapshot may become
selected. Refresh/close clears it, changing it invalidates prior bundle state,
and the UI says visibly that selection is not Flash permission. No local port,
hardware identifier, serial number, device identity, or pairing data becomes
selection state.
Production-window focus traversal now enters that list once between Refresh and
the enabled bounded bundle action, skips both disabled Flash actions, and cycles
back to Refresh. The acceptance run found and removed noninteractive blocker
content from the Tab sequence. Physical key injection, focus appearance across
live themes, Narrator, and assistive-technology review remain unverified. Routed production-
window navigation now covers wide Right/Left, wrapped Down/Up, and Home/End.
Native End failed on the wrapped layout because it resolved the first row edge;
the list now handles only Home/End and keeps exact first/last selection, focus,
scroll access, selection status, and bounded bundle availability synchronized.
Routed F5 reaches the same production command and restores the safe unselected
three-card state after refresh.
Reverse traversal is now independently accepted at 900×620. With a current
selection it moves from Refresh to the enabled bundle action, the exact same
selected card, then back to Refresh. With no selection it skips the disabled
bundle action, enters and leaves the list without implicitly selecting a
device, and returns to Refresh. All three card Flash controls and the footer
Flash action remain disabled. This is WPF focus-manager evidence, not physical
Shift+Tab injection or keyboard-layout acceptance.
When F5 begins on a focused card, the refresh destroys that item container.
The window now records that keyboard context and returns focus to Refresh after
either a successful or failed refresh without stealing focus on initial load;
the next Tab can enter the newly generated device list after success.
The actual production UI Automation peers now expose the current public device
summary and blocker help on each selectable list item, single optional
selection on the list, Invoke on Refresh, and named/helpful disabled Flash
controls. Summary, safety, selection, bundle, and error peers report their
current visible message rather than a fixed label. UI Automation selection
reaches the same bounded selection path; it cannot enable a writer. Narrator
speech, announcement timing/verbosity, Braille, and other assistive-technology
behavior remain separate gates.
The minimum-window production peer is now functionally exercised through its
`ScrollItem` provider, not merely inspected for pattern presence. With the
third wrapped card initially outside the outer viewport, `ScrollIntoView`
moves the real content scroll offset and changes the item from offscreen to
onscreen while Refresh retains focus, device selection stays empty, public
name/help stays unchanged, and bundle selection plus the footer Flash action
remain disabled. This is in-process WPF provider evidence, not packaged
out-of-process scrolling or an
assistive technology's end-user behavior.
The production peers now also expose an ordered heading hierarchy: the full
utility title at level 1; current inspection, bundle, and safe-mode status at
level 2; and each public device name at level 3. Exact names and levels survive
selection and refresh without exposing internal candidate or transport data.
The same sequence passes against the independently extracted package in three
external UI Automation cycles. Narrator/Braille heading navigation, spoken
order, verbosity, and other assistive-technology behavior remain untested.
The same shown minimum-size production window now also survives an in-window
classic-to-deterministic-high-contrast-to-classic resource transition with the
same wrapped card selected and keyboard-focused, the same scroll offset and
accessible item name, bundle selection enabled, Flash disabled, and the
zero-ready summary intact. An explicit list-item focus visual is assigned. The
first transition render used yellow for both focus and selection; focus is now
white while the selected outline remains yellow. This is deterministic WPF
state and render evidence, not proof of an actual Windows theme-setting change
or focus appearance under every built-in or customized contrast theme.
A separate shown-window resize transition requests 1120×760, moves to the
900×620 production minimum, and returns to its effective shown size. Each host
must expose room for at least two cards; geometry must match the realized
capacity before and after the material resize. On the local 1920×1080 display,
the exact 1120×760 → 900×620 → 1120×760 run moves all three cards from one row
to a two-plus-one wrap and back while the same last card and generated container
remain selected and keyboard-focused. After
one initial `BringIntoView`, the first stricter run found minimum-size reflow
could leave that card below the viewport and drop its focus. The window now
defers until resized layout settles, verifies the selection is still current,
brings it into view, and restores its keyboard focus. Both transitions then
keep the selected card intersecting the viewport, scroll state finite and
bounded, its public automation name and visible selection status current,
bounded bundle inspection enabled, and Flash disabled. This is deterministic
WPF layout/state evidence, not physical resize input, a real monitor
transition, or per-monitor DPI-change acceptance.
One pure selected-device matcher now compares hardware-profile ID, processor,
product role, board-revision range, minimum bootloader schema, and image-size
capacity from the inspected manifest against a separate authoritative
received-unit profile. Runtime labels and vendor-family candidates cannot
supply that profile, so all three live cards remain visibly unmatched. A future
exact match still cannot grant release admission or Flash permission.
The packaged catalog contains no production signer, so inspection also states
that trust is not configured and admission remains blocked. It contains no signing private key, protected revocation/generation
state, writer, erase/reset/DFU/recovery adapter, or mutation path. If the development source
tree is absent, a built-in Windows
adapter uses SetupAPI rather than the laptop's access-denied CIM/PnP inventory
path. It privately allowlists exact USB families, holds COM names only long
enough for fixed MeshCore identity requests, and publishes no local transport,
raw response, BLE PIN, hardware-instance, pairing, identity, key, or coordinate
data. The companion path permits only app-start/device-info; the repeater path
permits only `board`, `ver`, and `get role`. The live C# path reports three
runtime-identified candidates—one Heltec V4 OLED companion, one SenseCAP Solar
repeater, and one Wio Tracker L1 companion—and zero ready to flash without
Python or MeshCLI. These are
installed-runtime identities, not authoritative received-board profiles.
Each recognized card now includes a candidate-only hardware-profile panel. It
separates the allowlisted USB/runtime observation from the published vendor-
family baseline, states that the result cannot authorize Flash, and names the
deliberate maintenance restart or DFU/bootloader step plus received-revision
confirmation still required. Unknown USB devices receive no restart offer.
The live three-device result remains entirely runtime-only; no reset occurred.
The future maintenance-profiler boundary is now explicitly fail-closed: a
recognized device permits at most one operator-confirmed attempt per session,
and a failed attempt blocks every retry until ordinary USB/runtime recovery has
been verified. Both inspection producers expose that caution, while unknown
hardware has zero attempt authority. This is pure policy and warning evidence;
the application still exposes no maintenance action, reset, line toggle,
esptool, DFU, erase, write, reboot, or recovery adapter.
One bounded physical attempt then exercised that rule on `OT-DEV-002`.
Automatic ROM entry failed before chip connection, normal MeshCore runtime
temporarily became unavailable, and no retry occurred. After USB reconnection/
re-enumeration, the unit returned with unchanged firmware and radio settings,
zero errors, and an empty queue. This is manual recovery evidence only; the
low-level profile remains unresolved.
The first local `win-x64` self-contained engineering package now builds from a
runtime-specific restore with no external NuGet source. Its manifest records
exact payload lengths and SHA-256 hashes, prohibits source, firmware,
writer/recovery tools, keys, and repository engineering names, and declares
only device inspection plus bounded local bundle-candidate inspection. A
separate verifier extracted the retained
ZIP, matched every manifest record, launched `DeviceUtility.exe` from that
fresh source-free directory, and removed its owned process and temporary tree.
The newest verified 464-file archive is 72,103,538 bytes with SHA-256
`133A4E133A78D0CE789873B6E43226EAA455B59EDFF81D8DCF4369C172DED2C5`.
Native Windows UI Automation then inspected that exact independently extracted
executable across the process boundary. Stable privacy-safe IDs exposed the
window, Refresh, dynamic summary/selection/bundle regions, device list,
bounded bundle action, and disabled Flash action. The three list items exposed
selection and scroll-item patterns, public summary/blocker help, and positions
1/3, 2/3, and 3/3 without revealing candidate, COM, or MAC identifiers. Three
external selection/Refresh cycles each enabled only bounded bundle inspection,
then cleared selection, republished all three devices, restored zero-ready
status, and kept Flash disabled. The verifier independently required the exact
public display-name multiset `Heltec V4 OLED`, `SenseCAP Solar`, and
`Wio Tracker L1`. A non-remediating Windows Defender scan inspected the exact
ZIP with exclusions ignored and found no threats. This is native automation-
client evidence, not Narrator speech, physical input, or a clean-machine result.
The same source-free check requires the exact public level 1/2/3 heading
sequence before selection, after selection, and after each of the three
Refresh cycles.
The same native client now also receives the ordered LiveRegionChanged stream
for all three selection/Refresh cycles: current bundle and selection status,
then selection cleared, bundle waiting, inspection busy, bundle reset, final
zero-ready summary, and the selection prompt. Names match the settled visible
properties and remain privacy-safe. This proves event delivery, not Narrator's
spoken wording, timing, verbosity, or interruption behavior.
This is local package evidence only: no installer lifecycle, clean-machine,
code-signing, distribution, or public-release claim exists.
Deterministic production-XAML renders have been reviewed at 1600×900 and the
900×620 minimum, including a scrolled minimum view. That review fixed a
transparent content root and horizontal card clipping; all three cards remain
reachable and disabled Flash labels remain readable. Physical input/Narrator,
live system-theme switching, visible physical-input refresh,
installer/clean-machine, and real assistive-technology acceptance remain
unverified. The packaged Windows adapter passes three consecutive reads through
the production window with all three bench candidates and zero ready.
The packaged executable now embeds an explicit `PerMonitorV2` application
manifest with a legacy per-monitor fallback. Deterministic minimum-window
production renders at 125%, 150%, and 200% preserve their logical layout,
expected scaled dimensions/DPI, nonblank pixels, usable scroll viewport, and
measurable Refresh and firmware-selection controls. Pixel review found the
selected-device state, bundle blocker, safe-mode boundary, and disabled Flash
label readable at every profile. Real monitor movement, alternate system or
high-contrast themes, keyboard/Narrator, and clean-machine operation remain
separate gates.
The loader's custom Windows 95 presentation now consumes semantic dynamic
brushes rather than hard-coded control colors. Classic mode retains the same
gray/navy palette. When `SystemParameters.HighContrast` is active, the runtime
owner maps window/text, control/text, highlight/highlight-text, and gray-text
pairs from current WPF `SystemColors`; accessibility, color, visual-style,
window, and general preference notifications reapply those values on the UI
dispatcher. A deterministic black/white/yellow profile passes at least 7:1
text and disabled-label contrast through the real production window, with its
selected card, three-card state, scroll viewport, safety boundaries, and
disabled Flash copy intact. A stricter shown-window matrix found that local
button color values could override the shared disabled-state trigger even
while the resource-level check passed. Those values were removed so one
semantic template owns enabled and disabled colors. The matrix now enumerates
every disabled production button and measures its resolved text against the
actual rendered `ButtonBorder` surface in ready/unselected, ready/selected,
and held busy-refresh states. Classic requires at least 4.5:1; deterministic
high contrast requires at least 7:1. This is resolved WPF control/template
evidence, not final pixel-antialiasing or live system-theme evidence;
live switching through every Windows contrast theme remains unverified. A
focused classic-mode check also found and fixed a 3.79:1 disabled-button pairing;
the production pairing is now above 5.5:1 and every disabled instance is
regression-gated at 4.5:1.
The production window also passes three controlled refresh/selection cycles
against a validated read-only source. Each cycle clears the previous card and
bundle state, restores Refresh, republishes all three candidates, and requires
a new current selection before bounded bundle inspection becomes available.
This fixed stale post-inspection footer wording but does not prove real Windows
input or repeated live USB refresh. The validation workflow now builds every WPF and
test intermediate/output tree under one unique, verified system-temporary
directory and removes that exact directory afterward. This prevents stale or
protected development artifacts from blocking the warning-free app build and
45-scenario run; it adds no device-write or firmware-admission authority.
The visible app title/header is now composed from one replaceable working
identity: `Limited Underground Firmware Loader — Preview`. The UI also states
`Inspection only` and attorney review pending; Preview/inspection-only cannot be
removed until physical firmware writing and recovery pass. Presentation JSON
stays non-authoritative for branding, and automated safeguards reject
standalone/prohibited LU structures, TLU/LUT/LUTrail, and `®`. Internal
repository names, namespaces, script paths, schemas, and `OT-*` records remain
stable. This is not legal clearance or irreversible adoption.
The owner-requested loader presentation now follows a Windows 95-style utility
direction with gray square surfaces, navy headings, classic typography, and an
application-owned beveled button template. That template fixes the observed
near-white-on-white disabled labels by explicitly retaining dark disabled
copy. Deterministic desktop/minimum rendered layout and vertical access to a
wrapped third card and deterministic 125%/150%/200% scaled renders are accepted
on the current host; interactive resize, live Windows contrast-theme switching, keyboard,
and assistive-technology acceptance remain
open.
Read-only inspection and flash permission are distinct outcomes: a connected
board can remain inspectable while incomplete or conflicting processor,
flash/PSRAM, exact profile/revision, bootloader schema, or image-size evidence
blocks Flash. Firmware target role must also agree: bench client, complete
client, and packaged repeater are not interchangeable. Clean install requires
explicit destructive-erase confirmation; recovery additionally requires
separate physical authorization. Thirteen
preflight groups pass. The USB adapter adds four privacy/fail-closed discovery
groups, five strict runtime-reduction groups, and a dated 2026-08-12 live
snapshot. It
found two Espressif application runtimes plus one Seeed TinyUSB runtime, then
identified two Heltec V4 OLED MeshCore companions and one Seeed SenseCAP Solar
MeshCore repeater without emitting raw replies, pairing fields, local ports, or
persistent identity. Installed runtime role remains non-authoritative for the
unresolved OpenTrail target role, so all three stayed blocked from Flash. No
low-level probe, approved production signer/admission composition, approved board profile,
erase/write/reboot capability, accepted interactive UI, or physical recovery
evidence exists. See the
[bundle candidate format](update/FIRMWARE_BUNDLE_CANDIDATE_FORMAT_V0.md),
[firmware-bundle admission](update/FIRMWARE_BUNDLE_ADMISSION_V0.md),
[final write admission](update/FIRMWARE_WRITE_ADMISSION_V0.md),
[firmware-install preflight](update/FIRMWARE_INSTALL_PREFLIGHT_V0.md), and
[Windows loader desktop shell](update/WINDOWS_LOADER_DESKTOP_SHELL_V0.md).

The optional archive now has a host-tested client-side session boundary rather
than only a concept. Explicit start/stop controls the existing current-fix-only
scheduler; one exact 56-byte `OTBA/v0` record carries only an opaque nonzero
session ID, accepted-record sequence, boot-local capture time, the canonical
16-byte current-position payload, reserves, and CRC-32. Session IDs must
increase during one object lifetime, and sequence advances only after local
transport acceptance, so retryable pressure retains the same record number.
Ten groups and 100/100 focused repeats pass. This does not implement a server,
remote acknowledgement/durability, identity/authorization, cryptography,
retention/export/deletion, target binding, or physical capture. Base radio
operation remains outside and independent of the archive object.

That session now composes with a bounded 16-record RAM outbox and cooperative
uploader. Exact `OTBA/v0` records remain FIFO; full capacity never overwrites,
and only an injected `durable_ack` permits exact-head removal. Not-ready,
remote rejection, and failure retain the head; a post-ACK local commit mismatch
latches upload closed instead of risking a blind duplicate or false success.
Ordering history keeps only opaque session/sequence metadata after a record
leaves, not a second full coordinate-bearing copy. Ten groups plus 100/100
focused repeats pass. The queue is volatile, and the fake durable-ACK result is
not evidence of a server, endpoint, protected persistence, authentication,
account/access, retention/export/deletion, target, physical interruption, or
lost-device recovery.

The outbox uploader now has a separate checked-time retry owner. It attempts
the first head immediately after one valid guarded clock sample, doubles a
nonzero retry delay after transient not-ready/failure outcomes up to a fixed
maximum, and makes no remote call before the exact deadline. Durable
acknowledgement plus exact local commit resets the delay for the next head.
Temporary clock not-ready defers; rollback/source failure, remote rejection,
deadline overflow, or uploader ambiguity retain the queue and latch this
optional boot composition closed. Ten groups plus 100/100 focused repeats pass.
This is not a target task, connectivity detector, server adapter, TLS/auth
boundary, receipt proof, persistent retry schedule, power result, or physical
upload. Base radio operation remains independent.

The optional path now has a privacy-safe semantic presentation adapter. It
validates copied session/outbox/retry relationships and reduces them to stopped,
active, queued, waiting, full, or failed notices plus a bounded 0-through-16
queue count. Every frame is action-free, includes no breadcrumb bytes,
coordinates, endpoint, credentials, identities, deadlines, or receipt details,
and never turns archive failure into a base system-fault claim. Incoherent
state fails visibly and impossible queue counts are redacted. Ten groups plus
100/100 focused repeats pass. This is host semantic evidence, not an atomic
target snapshot, renderer, physical display, server, recovery workflow, or
authorization to capture, upload, discard, export, or delete.

That presentation now has one target-facing status-source contract rather than
requiring three unrelated owner reads. One 200-byte host tuple carries only
session, outbox, and retry status; common code performs exactly one source call
per capture. Temporary not-ready returns no frame, failed or unknown source
state ignores partial output and emits a generic warning with queue count zero,
and revision zero is rejected before source access. Ten groups plus 100/100
focused repeats pass. This defines the serialization obligation but does not
implement an ESP-IDF task/lock, concurrent runtime proof, renderer, physical
display, or archive execution authority.

That source now has a target-shaped common-code adapter over the three concrete
archive owners and one injected nonblocking lock. A ready result requires one
acquire, all three status copies inside the held boundary, and one successful
release before the tuple is published. Contention returns temporary not-ready;
lock/unlock failure or unknown state redacts output and latches snapshotting
closed. Ten groups plus 100/100 focused repeats pass. The real ESP-IDF primitive,
shared writer discipline, concurrent stress, target resource measurements, and
physical failure behavior remain unproved.

A private target-shaped archive runtime owner now removes direct mutable access
to the concrete capture session, outbox, uploader, and retry coordinator. Start,
stop, capture, retry-controlled upload, and status snapshot calls share the one
injected lock. Contention attempts no operation; component rejection remains
typed and distinct; lock/unlock uncertainty latches the optional runtime, with
post-operation unlock failure marked outcome-uncertain. Ten groups plus 100/100
focused repeats pass. Target-exclusive local workflow composition, ESP-IDF
binding, concurrent target stress, physical network/storage durability, and
on-device measurements remain.

Archive execution now has a revision-bound local-only consent boundary. The
canonical Start confirmation requires a hold and exact active frame revision;
Stop is immediate and clock-independent from its own exact confirmation frame.
Start alone reads checked time and consumes a nonzero session ID inside an
explicit caller-supplied inclusive range. Temporary clock/lock unavailability
defers without mutation, while uncertain post-operation state consumes the ID
so it cannot be reused; the final lease ID permanently exhausts that
controller. Stale, cancel, wrong-screen, unsupported, and failed-input paths
make no archive call. Eleven groups plus 100/100 focused repeats pass. Rendered
consent, parent navigation, physical input, selected-target bootstrap binding,
ESP-IDF composition, and on-device evidence remain.

One complete local workflow now composes that consent boundary with the private
serialized runtime and a snapshot-backed archive controls screen. A coherent
stopped state offers Start; active state offers Stop; unknown, failed, or
incoherent state can offer only Stop. Control input is polled only after a fresh
snapshot confirms the displayed semantics. Start remains hold-only, Stop
remains immediate, Cancel mutates no runtime state, and each completed action
must publish a newer truthful control frame. Failed post-Start refresh and
revision exhaustion attempt privacy-safe Stop and latch. This remains host
common code. Re-entry requires an exact-parent-revision local action and keeps
the same cursor inside the precommitted range, but complete parent navigation,
renderer, physical controls, selected-target bootstrap binding, ESP-IDF
binding, concurrent target stress, and device evidence are absent. See the
[complete archive workflow](location/BREADCRUMB_ARCHIVE_WORKFLOW_COORDINATOR_V0.md).

A restart-safe non-identifying archive session lease store now reserves an
entire inclusive ID range before the workflow may use its first value. Exact
64-byte `OTBL/v1` records alternate across two commit-last slots in a separate
`breadcrumb_archive_state` / `ot_archive` persistence domain. A later boot
starts after the last committed final ID, abandoning unused or
committed-but-uncertain values rather than reusing them. Nine store groups,
five real key/value-composition groups, consent range exhaustion, and 100/100
focused repeats pass. The record carries no participant/device/group/location/
endpoint/account identity. Secure blank-state entropy, ESP-IDF/NVS binding,
authenticated integrity, rollback resistance, recovery UX, target boot
composition, physical interruption, and on-device durability remain absent.
See the [archive session lease store](persistence/BREADCRUMB_ARCHIVE_SESSION_LEASE_STORE_V0.md).

A fixed-memory bootstrap now owns that store-to-workflow boundary. Explicit
initialization commits and reads back the lease before the non-copyable workflow
is constructed; dormant service/entry touches no storage, runtime, input, or
display. Same-boot reinitialization is idempotent. Any invalid, failed, or
uncertain allocation latches the optional path without retry/reset or workflow
construction, while a fresh boot skips a range whose final commit became
durable despite reporting failure. Eight groups plus 100/100 focused repeats
pass. The bootstrap has no base-radio or automatic-Start authority. Exact
ESP-IDF/NVS and secure-seed binding, parent navigation, recovery UX, concurrent
target stress, renderer/physical input, reset/brownout/endurance, and on-device
evidence remain. See the
[durable archive workflow bootstrap](location/BREADCRUMB_ARCHIVE_WORKFLOW_BOOTSTRAP_V0.md).

An exact-revision navigation handoff now connects an external parent shell to
that bootstrap without creating a second archive control path. First entry
requires a resolved `open_archive_controls` action matching the active parent
frame, derives the next workflow revision, and commits the lease before
controls presentation. Cancel returns a minimum newer parent revision; later
exact-parent entry reuses the same boot workflow/lease with no new storage
allocation. Invalid, stale, already-active, or exhausted entry rejects before
storage; lease failure latches before runtime/display access. Eight groups plus
100/100 repeats pass. This remains a handoff—not a complete home/menu owner,
renderer, physical input, ESP-IDF target, or device result. See the
[archive navigation coordinator](location/BREADCRUMB_ARCHIVE_NAVIGATION_COORDINATOR_V0.md).

A narrow optional archive parent page now owns an actual semantic frame around
that handoff without claiming the complete client UI. Explicit activation
presents exactly Archive controls plus Back and performs no archive storage or
runtime call. Open enters the nested navigation; nested Cancel restores this
page at the returned newer revision; Back exits to the broader future shell.
Display-not-ready restoration retries the same pending revision without
re-entering navigation or allocating another lease. Nine groups plus 100/100
repeats pass. This is not the home/message/quick-status/critical/position shell,
renderer, physical input, ESP-IDF target, or device evidence. See the
[archive parent page](location/BREADCRUMB_ARCHIVE_PARENT_PAGE_COORDINATOR_V0.md).

A host-only archive UI coordinator now owns display revisions around that
single-read source. Every valid cooperative service call takes exactly one new
snapshot. Unchanged semantics redraw nothing and consume no revision;
temporary snapshot/display unavailability and permanent display errors retain
the last successfully presented frame for a later fresh-snapshot retry. Failed,
unknown, or incoherent status remains a redacted action-free archive warning,
and revision exhaustion rejects only a changed presentation. Ten groups plus
100/100 focused repeats pass. The coordinator has no capture, outbox, upload,
storage, input, or base-radio reference. Target task/locking, renderer,
physical display, resource measurement, and on-device failure recovery remain.

A fixed-memory position scheduler now adds explicit start/stop, current-fix-only
output, delayed-service coalescing, and separate cadence/retry timing around the
existing 16-byte payload. A host-only sink now carries its exact attempt time,
revalidates the current payload, obtains injected ephemeral packet-v0 metadata,
encodes one exact 38-byte frame, and admits it only as background position
traffic. Rate/capacity pressure feeds typed scheduler retry. This is
unauthenticated packet-v0 component evidence: real coordinates remain
prohibited, and no final cadence, authenticated packet/priority composition,
direct-radio/GPS binding, or physical position-sharing result exists.

A fixed-memory, single-owner handoff now peeks the priority/FIFO head and
removes it only after `DeliveryController` accepts the copy. Delivery capacity,
duplicate-ID, and frame-policy rejection retain the original entry; remaining
priority lifetime bounds delivery expiry and cannot be extended. Ten groups
plus 100 repeats include an exact scheduler-to-packet-to-priority-to-delivery
fake-radio flow. This is host composition evidence only, not authentication,
real-coordinate permission, direct-radio binding, or physical delivery.

A cooperative outbound coordinator now obtains one successful checked
monotonic sample and reuses it for location, scheduling, handoff, delivery, and
opaque-radio service in a fixed order. Sharing stopped means no GPS read;
temporary clock not-ready means no downstream call; rollback/source failure
stops sharing and latches the boot composition closed. Ten groups plus 100
repeats cover exact ordering, failure isolation, pressure recovery, and
same-cycle packet delivery to a fake peer. This is not ESP-IDF task/concurrency,
inbound, authenticated-packet, target-adapter, or physical-radio evidence.

A runtime-aware position safety overlay now prevents a permanent outbound clock
fault from appearing as ordinary stopped sharing. Coherent source-failure or
rollback status produces a critical no-action system-fault frame, incoherent
status fails closed, and a Start resolved from an older healthy frame is
rejected without scheduler access. Stop remains safe/idempotent. Ten groups
plus 100 repeats pass. Exact renderer, revision/task ownership, reboot behavior,
and physical input evidence remain absent.

The live coordinator now also owns target-facing position commands. Start reads
one checked sample at action application, defers without scheduler access when
time is temporarily unavailable, and shares the permanent rollback/source-
failure latch with outbound service. Stop applies immediately without reading
the clock. Ten command groups plus 100 repeats, and 100 repeats of the updated
safety suite, pass. Target task synchronization, concrete clock binding,
rendered retry behavior, reboot policy, and physical input remain absent.

A cooperative position-sharing UI coordinator now owns revision allocation,
initial/current publication, one checked input poll, live command application,
and post-action refresh. Temporary Start deferral retains the current truthful
frame. A failed result-frame write or exhausted revision space stops sharing and
latches input closed. Ten groups plus 100 repeats pass. This is not an ESP-IDF
task/lock, renderer, physical input, or concurrency result; exact target
serialization remains absent.

That owner now observes current outbound/scheduler presentation before polling
input. User-visible GPS wait/recovery, sink deferral, and permanent clock-fault
changes publish a higher revision first, making queued older input stale;
nonvisible runtime counters and timestamps do not churn the screen. Failed
observed-state publication stops sharing and latches UI input closed. Ten
additional groups plus 100 repeats pass. Exact target serialization, atomic
snapshots, renderer behavior, and physical timing remain absent.

One validated position UI result can now become a versioned 32-bit `OTPD0/v0`
diagnostic and fixed public logger message. It retains only coarse event,
outcome, displayed position notice, reason, and safety flags; idle polls are
suppressed and identifier/location/runtime detail is structurally absent. Ten
groups plus 100 repeats pass. Target log binding, retention/export/clear policy,
persistence, and physical service capture remain absent. A separate host-only
CLI accepts exactly the canonical uppercase logger record, revalidates the
complete word shape, and emits stable operator category names. It does not
read a device, log, file, or network service and does not establish an export
workflow.

The two strict diagnostic parsers now also sit behind one host-only
`opentrail_diagnostic_cli` entry point. Exact `OTPD0` and `OTRD0` prefixes
dispatch to their original validators and emit a leading stable record type;
malformed supported records and unsupported prefixes fail without echoing the
input. The unified tool adds no file, log, device, network, export, or execution
authority and changes no wire or logger format. Both canonical records plus
malformed/unsupported smoke cases pass in the complete local host gate and in
public GitHub Actions run `31502841481`.

Offline maps now have a dated architecture gate rather than an implied
provider or renderer selection. Public OpenStreetMap tile servers are excluded
from offline package creation; source rights and visible attribution are
mandatory. MBTiles 1.3, PMTiles v3, and a pre-rendered indexed-raster reference
remain candidates for an exact-target comparison. Packages are prepared off-
device, staged and fully verified, activated read-only with prior-good
recovery, and allowed to fail to a mapless UI without stopping communications.
No provider, package, target renderer, storage path, or on-device map result is
claimed.

That gate now has a strict `OTMP0/v0` host manifest and exact-byte verifier.
Canonical rights/attribution, experimental container/encoding/scheme, Web
Mercator coverage, byte/tile counts, SHA-256, reader/firmware, storage, and
scratch requirements fail closed across seven groups. The tool reads only a
manifest and optional local package, streams its digest, writes nothing, and
does not echo rejected paths. This proves metadata and byte identity only—not
source authenticity, a lawful real package, staging/activation, rendering, or
target compatibility.

A separate bounded C++ activation guard now makes the fail-safe lifecycle
testable without selecting a storage implementation. Missing/ambiguous boot
selectors start mapless; staging leaves the current map untouched; trial begins
only after exact external selector-commit evidence; the prior package remains
until the policy's complete-read threshold passes; and read, deadline, clock,
or media failures require exact verified fallback or visible mapless state.
Ten groups and 100/100 focused repeats pass. No selector codec/storage adapter,
package authentication, filesystem integration, renderer, or target result is
claimed, and the guard has no communications authority.

Restart state is now explicit through fixed 64-byte `OTM0/v0`. The canonical
CRC-protected record contains only active/prior slots and generations,
stable/trial/fallback state, bounded health/deadline/boot policy, and record
generation. Trial restore discards volatile reads/time, increments the boot
count, retains the exact verified prior, and requires full health again; boot-
limit, policy/evidence mismatch, corruption, or missing prior fail to fallback
or mapless. Ten checkpoint groups and 100/100 focused repeats pass. CRC is not
authentication or anti-rollback, and no physical selector store or target
result is claimed.

`OTM0` now has an abstract recoverable two-slot store. A complete prepared
record has a zero commit byte and is therefore undecodable; byte 59 is committed
last, followed by exact full readback. The store alternates away from the newest
valid slot, selects only unique newer generations, repairs known invalid/
uncommitted peers, rejects unreadable media and equal-generation conflict, and
can enforce an external minimum generation. Read-only exact live-checkpoint
verification plus exact post-erase empty readback brings the store to fourteen
groups and 100/100 repeats. A typed
boot coordinator now holds restored guard state private, persists
and verifies every resumed-trial increment and boot-limit fallback before
release, exposes stable/fallback state without an unnecessary rewrite, and
publishes only a fresh mapless guard on storage, conflict, rollback, package,
or save failure. Ten groups and 100/100 repeats pass. No physical backend,
atomicity/endurance/power-loss evidence, protected generation, authentication,
target task, or on-device result exists. A runtime transition coordinator now
requires exact stored/live/policy/generation agreement and persists trial
promotion/failure, deadline/clock fallback, fallback completion, and prior
cleanup before publishing a private attempted guard. Invalid fallback evidence
verified-clears only selector records and stays mapless. Thirteen transition
groups and 100/100 repeats pass; physical clearing and target behavior remain
unproved.

A replacement-map candidate coordinator now binds those pieces without
granting live authority early. It accepts only typed evidence for an externally
staged alternate-slot package while the live guard is stably active, verifies
the exact live checkpoint/policy/generation/floor, applies candidate state to a
private guard, rechecks the preflight generation at save time, and publishes
trial state only after commit-last exact readback. Invalid candidate evidence
leaves the current map active; persistence or generation uncertainty fails
mapless. Eleven groups and all four affected suites pass 100/100 repeats. This
does not establish the first map baseline, provide locking, stage physical
bytes, select a renderer, or prove target behavior.

First installation now has its own coordinator rather than a no-fallback trial.
It requires a clean `no_selector` mapless guard, exact policy, fully evidenced
package, two readable empty selector slots, and zero trusted history. Stable
record generation 1 is constructed privately, committed last, and read back
exactly before the map becomes available. Existing/dirty/unreadable selectors,
nonzero history, races, and write/readback uncertainty remain mapless and
cannot be reset or reseeded through this API. Ten groups and all six affected
suites pass 100/100 repeats.

A separate service-reseed coordinator now handles dirty or previously used
selector state without weakening first use or healthy replacement. A fresh,
exact-operation service permit, an already-mapless owner, exact policy,
valid package evidence, and exclusive store ownership are required. Only the
two selector records are erased; both must read back empty, the new record must
advance beyond observable/trusted history, and exact commit-last readback must
finish before map exposure. Twelve coordinator groups and fourteen store
groups cover conflict, dirty state, partial/dishonest erase, exhaustion,
persistence uncertainty, restart restore, and a post-clear selector race.
The permit now comes from a separate host-tested authorizer: an injected backend
must verify and atomically consume a short-lived local-service grant bound to
the exact boot, scope, transport, policy, package, trusted floor, five service
confirmations, and local-confirmation revision. The non-copyable permit's boot
and expiry are rechecked when it is burned before selector access; remote
radio, mismatch, expiry, and replay fail
closed. Ten authorization groups and twelve reseed groups pass. Concrete
credentials/challenges, backend replay persistence, protected trusted history,
physical package and selector adapters, locking, audit, renderer, and target
evidence remain open.

A separate protected trusted-generation prerequisite now replaces the implied
idea that any caller-supplied selector floor is itself rollback protection. The
boot-local, non-copyable common enforcer reads an injected backend, requires an
atomic exact-expected compare-and-advance, and accepts an advance only after
exact readback. Source rollback, stale conflict, and nonincreasing requests do
not write. Any reported advance failure or post-write ambiguity latches all
later source I/O closed until fresh-boot reconciliation. Ten groups pass. No
protected backend exists yet. The boot path now has a separate trusted-source
composition: it derives the floor internally, restores and persists selector
state on a private guard, rechecks or advances trust with exact readback, and
publishes only after both generations match. Empty selector media with nonzero
trusted history, selector rollback, failed trust reads, uncertain advances,
and final-value conflicts cannot expose a map. Ten trusted-boot groups pass.
Runtime transitions now have their own protected-source composition. It derives
both generation values internally, runs selector mutation on a private guard,
and requires a final exact trust recheck or selector-save-before-trust-advance
ordering before publication. Failed or changed trust contains a currently
visible map; invalid-fallback selector clearing retains protected history and
routes to service reconciliation. Eleven trusted-transition groups pass.
Candidate replacement now has its own protected-source composition. It derives
both generation values before selector access, saves trial state privately,
advances and exactly reads back trust, and publishes only after exact agreement.
Rejected candidates recheck trust before the active map remains available;
post-save protected conflict or uncertainty stays mapless for reconciliation.
Eleven trusted-candidate groups pass. Initial baseline creation now has its own
protected-source composition. It permits only an exact clean `no_selector`
owner with zero protected history, saves stable selector generation 1 on a
private guard, then advances protected history from 0 to 1 with exact readback
before publishing the map. Retryable initial trust failure preserves clean
first use; nonzero history blocks selector access; and any post-save trust
conflict or uncertainty remains ambiguous-mapless for fresh-boot
reconciliation. Eleven trusted-baseline groups pass. The service-reseed
coordinator now has its own protected-source composition. It derives the
reviewed floor before selector access, requires the single-use permit to match
that exact value, verified-clears and saves the replacement on a private guard,
then advances and exactly reads back protected history before publishing the
recovered map. Initial source failure touches no selector storage; selector
failure never advances trust; and post-save trust uncertainty remains
ambiguous-mapless for fresh-boot reconciliation. Twelve trusted-reseed groups
pass.

A separate reset/replacement policy now prevents four lifecycle events from
collapsing into one destructive operation. Ordinary factory reset preserves
both selector records and protected history. Selector reseed routes to the
existing exact-bound authorized path only while protected history is intact;
temporary source failure blocks selector access, and missing/replaced history
on the same device cannot become first use. Same-device protected-source
replacement requires future independent external recovery authority. Only an
independently established blank replacement device may route to fresh-domain
commissioning, and retained-selector import is rejected. The fixed-shape
classifier carries no erase, reset, generation-lowering, credential, device-ID,
or state-import authority. Ten groups pass.

The resulting protected-domain authorization handoff now derives one of the two
permitted scopes from that policy before backend access. It requires a consumed
local-USB service grant bound to the exact route, lifecycle state, coherent
empty or quarantined selector evidence, a nonzero proposed 128-bit domain, boot
session, six reviewed confirmations, committed local revision, and short time
window. Temporary source failure, retained media on a claimed new device,
wireless/radio transport, malformed grant, mismatch, expiry, and replay cannot
mint the non-copyable permit. Only the bounded domain provisioner can consume
it. Ten groups pass.

A separate 80-byte `OTMD/v0` codec now records the map trust-domain lifecycle
without changing `OTM0/v0`. Fresh-device state carries a nonzero current domain,
zero retired domain/floor, epoch 1, and either pending-first-baseline or active
state. Same-device replacement requires distinct current/retired domains, epoch
at least 2, pending-reseed or active state, and an accepted selector generation
strictly above the quarantined floor before activation. Record generation,
commit-last marker, reserved bytes, and CRC are canonical. OT-016S now binds the
retired domain explicitly. Ten codec groups pass.

The separate abstract two-slot `OTMD/v0` store now accepts generation 1 only
for fresh pending-first-baseline state on readable empty media, then requires
the exact next record generation and a linked maintenance, activation,
accepted-selector, or replacement transition. It preserves the prior committed
slot across twelve interrupted prepared-write boundaries, commits byte 75 last,
verifies exact readback, repairs known degraded peers, and fails closed on
unreadable, invalid-only, conflicted, exhausted, backward, floor-lowering, or
immediate retired-domain-reuse state. It has no erase/reset API and no
provisioning authority. Ten store groups pass.

The permit-consuming trust-domain provisioner now requires exclusive ownership
and burns exact binding, boot, and checked-time authority before any I/O. It
accepts only stopped or mapless ownership, verifies all domain/selector/source
preconditions, commits the exact pending `OTMD/v0` record first, then verified-
clears retained selector media, and only then establishes and reads back an
independently uninitialized protected source at generation zero. Matching
pending state can resume under a new permit, including exact readback after an
applied-then-failed source call; initialized sources cannot be reset or rebound.
Success is prepared/mapless state, not an active map. Thirteen groups pass; no
protected target backend, concrete credential/continuity/entropy evidence,
target lock, or physical durability evidence exists.

The stable trust-domain activation coordinator now completes that prepared
state without weakening replacement rollback floors. Fresh commissioning saves
selector generation 1; replacement saves exactly retired-floor-plus-one. It
then atomically advances the exact protected domain/generation, reverifies the
selector, marks `OTMD/v0` active, rechecks protected state, and only then
publishes the map. Pending selector/source/domain steps and an already-active
exact stable baseline can resume after restart, including applied-then-failed
advance or domain-commit calls. Fourteen groups pass. Domain-aware candidate
entry is separate; later paragraphs record the trial boot and runtime
transition boundaries that build on it. No protected target backend or
physical result exists.

The read-only active trust-domain boot coordinator now restores a stable map
only after the canonical active `OTMD/v0` record, protected domain/generation,
exact stable `OTM0/v0` selector, supplied package evidence, and policy agree.
It keeps restoration private, rereads the domain record, exactly reverifies the
selector, rereads protected state, and only then publishes. Missing, pending,
unreadable, rollback, ahead, changed, trial, or mismatched evidence stays
mapless. It never writes, repairs, erases, or mutates protected state. Thirteen
groups pass; domain-aware candidate entry and later runtime lifecycle work are
separate, while physical target boot remains open.

The domain-aware candidate coordinator now moves one stable active map into a
private alternate-slot trial using fixed selector-before-protected-before-domain
ordering. It verifies the active domain and protected generation, persists and
reverifies selector generation `N+1`, atomically advances/readbacks the exact
protected domain, advances/readbacks `OTMD/v0`'s accepted generation, then
rechecks all three owners before publication. Safe candidate rejection also
requires final exact rechecks. Any later failure remains fail-visible mapless
without rollback. Thirteen groups pass; domain-aware trial boot/recovery and
runtime transitions are separate. Physical target transitions remain open.

The domain-aware trial boot coordinator now resumes that private trial and
reconciles only the exact interruption gaps left by candidate entry or an
earlier trial boot or runtime transition. It accepts `D=S=G` or selector
`G=D+1` with protected generation on either side, restores trial, fallback, or
canonical active state privately, persists resumed trial or boot-limit fallback
state, advances protected then accepted domain history as needed, and rereads
all three owners before publication. Unrelated gaps and every uncertain post-
selector result stay fail-visible mapless. Fourteen groups pass.

The domain-aware runtime transition coordinator now keeps healthy-read progress,
promotion, deadline/failure fallback, valid fallback completion, and previous-
package cleanup synchronized across the active `OTMD/v0` record, exact domain-
bound protected source, and selector store. Volatile and rejected operations
require unchanged three-owner rechecks. Persistent operations save selector
generation `N+1`, advance/read back protected history, advance/read back the
accepted domain generation, and reread all three owners before publication.
Interrupted promotion and uncertain domain commit recover through the expanded
boot boundary; invalid fallback evidence retains protected/domain history and
routes to service instead of first use. Eleven groups pass. Physical target
transitions remain open.

The selector now has a backend-neutral key/value adapter contract. Fixed
`ot_state` / `ot_maps` / `otm_sel_a|b` binding, exact 64-byte reads, durable
commit after staged write/erase, full-blob marker rewrite, idempotent missing-
key erase, and upper-store composition pass ten host groups. The accompanying
ESP-IDF plan records official NVS commit semantics, prohibits namespace/
partition-wide erase, fixes a local-service authority handoff, and defines the
physical interruption matrix. No ESP-IDF source, partition table, target
task/lock, encryption/trusted-generation choice, physical result, or concrete
service-authentication backend exists.
All twenty-seven map suites pass 100/100 focused repeats, and the complete
95-executable host matrix passes.

The `OTFP0/v0` four-person standalone pilot plan fixes the first live-test
boundary at four identical self-contained clients, no repeater/server/internet/
phone/laptop/vehicle dependency during a session, at least three materially
different one-hour sessions, 300 message origins, and 900 peer-delivery
opportunities. Its privacy, traffic math, zero-dependency declarations, and
hardware-readiness transition fail closed under host validation. The plan is
`draft_blocked`: no exact client model or firmware is selected until four units
prove battery, enclosure, GNSS, display, local input, and USB recovery.

The companion `OTPR0/v0` result evaluator now derives the 900 expected peer
opportunities from the plan and emits `pass`, `fail`, `ineligible`, or `invalid`.
It rejects wrong hardware/firmware/topology/duration/dependencies, incomplete
evidence, identity/transport/location/secret fields, impossible deliveries, and
noncanonical shapes before applying the provisional reliability gates. Its six
base groups pass publicly. A fail-closed template generator now refuses blocked
plans and existing output, preloads only frozen public configuration, and leaves
all evidence confirmations false; the expanded nine groups pass publicly. No
live four-person result exists.

The latest bounded hardware run delivered 30/30 alternating two-second
three-radio messages with zero loss/duplicates/new errors, exact +30 repeater
flood RX/TX, repeat preserved, empty final client queues, 2/2 cleanup, and no
lease journal. Its public `OTFL0` record contains aggregate role-labeled evidence
only; the raw capture remains in ignored local build state.

## Decisions captured

- Decision 0037 accepts the deterministic `OTCBL0/v0` pre-crypto two-build
  baseline and result
  `BUILD-BASELINE-FROZEN; OTCB0-EXECUTION-BLOCKED`. Exact source/raw-byte,
  configuration, stable-version, ESP-IDF/tool, Python-isolation, receipt, and
  seven-artifact equality evidence is frozen. The OT-005 plan remains
  `draft_blocked`; no candidate, suite/wire, implementation, device operation,
  physical result, or score is accepted.
- Decision 0036 accepts a provisioning-independent public rendezvous lane and
  separately configurable Public Assistance Broadcast as a post-V2 direction
  only. It is deferred until V2 is fully functional and accepted, has no
  schedule or progress credit, and preserves V1's pairwise-only `OTSL0/v0`
  boundary. `OTQ0/v0` cannot be reused as the public alert packet. Exact packet,
  radio, security, privacy, abuse, regulatory, and physical-acceptance designs
  remain future work. See the [future-concepts register](FUTURE_CONCEPTS.md).
- OpenTrail and OpenGauge are separate projects and must remain independently operable.
- The owner-approved working hierarchy is `Limited Underground` as parent;
  `Limited Underground Trail` as Android application and family; Essential,
  Gold, Platinum, and Trail Repeater as the provisional hardware/role names;
  and `Limited Underground Firmware Loader` as shared desktop tooling. All
  remain provisional pending professional clearance, use no `®`, and stay out
  of protocol/GATT fields, stored schemas, cryptographic domains, compatibility
  and board identifiers, device identity, and repository engineering names.
- A reusable funding-readiness packet now exists, but it is planning material,
  not an application or award. All cash, hardware, discount, loan, sponsorship,
  and service-credit activity is on owner-directed hold, including opportunity
  research for outreach, contact, submission, account connection, acceptance,
  shipment, and announcement. Legal applicant/payee, opportunity eligibility,
  exact bill of materials, dated quotes, and owner approval remain later gates.
- Website and hosting operations are maintained privately outside this public
  repository. Local device operation cannot depend on the website or any
  future service.
- Historical capacity planning modeled four clients without a repeater, four
  clients plus one repeater, then eight clients plus one repeater. Its host-only
  load evidence accounts for source attempts, forwarding copies, and configured
  LoRa airtime; it is not physical capacity or regulatory evidence.
- Decision 0033 now defines V1 as exactly two supported Heltec-and-Android pairs
  over direct LoRa with no relay, server, or internet dependency. Four supported
  nodes belong to the separate unmeasured V1.5 interoperability gate; a relay
  claim requires a physical three-radio sender-to-relay-to-receiver path.
- The cryptographic benchmark order is now fixed without selecting production
  cryptography: Espressif libsodium first, pinned ESP-IDF mbedTLS/PSA and
  Monocypher comparisons, Noise-C reference only. Noise XK is a leading join
  prototype only with signed invitation key pinning. Exact target benchmark,
  entropy, protected storage, target-bound rollback protection,
  interoperability, and physical lifecycle evidence remain required.

The algorithm-neutral secure-randomness boundary now gives future crypto code a
production-facing interface with explicit not-ready/ready/failed state, bounded
1-64-byte requests, and complete output or no buffer change on every failure.
Its predictable scripted source is isolated under test support. Eight readiness,
failure, exhaustion, retry, transition, and request-boundary groups plus 100
focused repeats pass. No ESP-IDF entropy adapter, strong-DRBG choice,
production key generation, or physical cold-start/brownout/RF-concurrency
evidence is claimed.

The checked monotonic-clock boundary now gives target composition one boot-local
64-bit millisecond source that remains separate from UTC. Equal ticks succeed;
temporary not-ready preserves continuity, while decreasing time or source
failure latches the current guard closed without consuming later samples.
Target code must share one successful value across a cooperative cycle instead
of letting each state machine read hardware independently. Eight groups plus 100
focused repeats pass. No ESP-IDF adapter, task/deep-sleep/brownout behavior,
accuracy/drift, long-run continuity, or physical failure evidence is claimed.

The power-state boundary now normalizes one atomic adapter observation without
assuming battery chemistry or estimating state of charge from voltage. Source
readiness, external power, battery presence, orthogonal charge state, optional
percentage/voltage, and monotonic sample age remain explicit. Composition must
inject nonzero freshness plus distinct low/critical percentage thresholds;
zero/default or incoherent policy fails before reading hardware. Eleven groups
plus 100 focused repeats cover exact bands, charging, external-only operation,
missing readings, reported fault, invalid combinations, future/stale time, and
bounded fake ordering. No target adapter, charger control, hardware threshold,
shutdown policy, endurance, or physical power evidence is claimed.

The local display/input boundary now gives portable-client application logic a
fixed semantic frame with status indicators, notices, and up to four action
slots. Target adapters own pixels, labels, coordinates, buttons, and touch.
Only a successfully presented, strictly increasing boot-local revision becomes
active; stale, disabled, out-of-range, unknown, and source-failed input remains
typed. The critical-confirmation screen has a canonical confirm/cancel shape and
requires a hold before resolving the confirm request. Twelve groups plus 100
focused repeats cover capabilities, frame validation, atomic presentation,
revision/input binding, failure behavior, critical confirmation, system-fault
restrictions, and bounded fakes. No renderer, physical input/readability,
critical-alert delivery, target performance, or supported display is claimed.

The portable-client composition preflight now binds all ten target-facing
endpoints needed by the first self-contained client and aggregates missing,
invalid, insufficient-MTU, insufficient-action, and missing-hold evidence. The
whole-contract audit caught and corrected a real distinction: two 704-byte
`ODS0` replay slots require `DuplicateCheckpointStorage` in addition to the
64-byte multi-domain `PersistentStorage`. The composition reuses pure power and
display validators and queries only radio MTU; it performs no storage, entropy,
clock, GPS, power, display, input, log, send, receive, or service operation.
Eight groups pass in the complete 33-executable matrix. This is structural host
evidence only. No concrete ESP-IDF target, board adapter, partition/pin map,
application task, target build, or physical result exists.

An algorithm-neutral outbound counter prerequisite now has host evidence. The
fixed `OTCN` store commits one nonoverlapping 64-bit range before returning it,
alternates exact readback-verified slots in a separate persistence domain, binds
the high-water mark to a 128-bit domain plus group epoch, and deliberately skips
unused/commit-uncertain ranges after restart. Ten groups pass locally and on
public `main`. A separate five-group
[key/value composition](security/OUTBOUND_COUNTER_KV_COMPOSITION_V0.md) proves
the real lease store reaches only `ot_state` / `ot_counter` / `slot_a|b`,
rotates across fresh adapter instances, retries a range only when restart shows
the failed prepared commit was absent, and skips a range when its marker became
durable despite a reported commit failure. It passes 100/100 focused repeats in
the complete 95-executable host matrix. A separate seven-group boundary packs the adapter-supplied
32-bit prefix and rollback-safe counter only after full 128-bit lease/key domain
equality. A fixed 52-byte `OTKD/v1` encoder separately binds group, epoch, full
sender fingerprint, and three output purposes across eight groups. Exact KDF,
epoch-secret handling, AEAD, target protected storage, secure rollback, and
physical power interruption remain unproved.

The immutable one-repeater host path now has a reboot coordinator around its
duplicate state. It requires protected namespace evidence matching the expected
group/epoch, requires the store and fixed `ODS0/v1` record to embed that exact
binding, restores or repairs the checkpoint before operation, and readback-
verifies a new replay checkpoint before allowing a queued frame to transmit.
Wrong-group/epoch and legacy unbound v0 media require service without restore or
overwrite. Failed or uncertain persistence and unreadable media disable
forwarding. Ten store groups, nine coordinator groups, the complete 28-
executable matrix, and 100 focused repeats of both suites pass locally. The
context-bound published matrix passes on public `main` in run `31374678550`.
The RAM
queue is not durable, so the accepted safety tradeoff can lose a frame after
replay save and before transmit. Target protected binding, rollback protection,
physical power-cut/wear, and durable-outbox behavior remain unproved.

The host-tested `OTCB0/v0` benchmark boundary now requires exact board,
toolchain, dependency-lock, sdkconfig, radio, timing, memory/stack/flash,
watchdog, artifact-hash, and eight security/lifecycle gate fields. Eight groups
show that a blocked plan cannot create a template and incomplete, mismatched,
private, or measured-failing evidence cannot pass; the suite passes locally and
on public `main`. The public plan remains `draft_blocked`; this is
reproducibility tooling, not target cryptographic evidence or a library
selection.

A separate protected-packet sizing model makes candidate security overhead
explicit without freezing packet v1. Requirements reconciliation found that the
former 36-byte profile omitted space for the required authenticated 64-bit
destination alias. Under the corrected 44-byte header plus 16-byte-tag profile,
a 163-byte example MTU leaves 103 plaintext bytes; the existing 16-byte position
payload becomes a 76-byte frame with 276,992 us theoretical airtime at the bench
PHY. The signed-group candidate adds a 64-byte source signature, leaving 39
plaintext bytes and making that position 140 bytes/461,312 us. A 64-byte signed
`OGA0` alert or `OGK0` ACK requires two candidate fragments, 312 transmitted
bytes, and 1,025,024 us theoretical source airtime. All ten groups pass locally and on public
`main`. Final fields, nonce/signature construction, crypto, target MTU/airtime,
and fragmentation/reassembly remain unproved; no alert is approved for this
candidate framing yet.

A separate fixed-memory verified-fragment reassembler now bounds four concurrent
messages, 16 fragments, and 103 bytes per fragment. Ten host groups cover the
39+25-byte alert shape, reorder, duplicates, conflicts, capacity, timeout,
clock rollback, and maximum completion without releasing partial plaintext.
The input remains a future crypto-adapter obligation; this is not raw-packet,
AEAD, source-signature, receiver-replay, target-resource, or radio evidence.

Decision 0004 now fixes the initial routing boundary without claiming an
implementation: zero or one authorized repeater validates source/auth/epoch/
permission, suppresses duplicates, and retransmits exact immutable packet bytes
once. It does not rewrite an end-to-end authenticated TTL. Named-source claims
require source authentication beyond common group-key access. Multi-repeater
routing needs a separate outer construction/new packet version and is deferred.

The algorithm-neutral host policy now implements that ordering: authentication,
authorization, context/epoch, local-role/destination, and immutable permission
are checked before replay observation; eligible exact bytes then face duplicate,
queue, rate, and queue-age limits. Nine groups and the full 27-executable matrix
pass locally and on public `main`. Its verified metadata is a future crypto-
adapter obligation, not implemented proof. Protected replay persistence, target
radio/task binding, reboot/power-loss, and field behavior remain open.

- Public hardware evidence is generated through the fail-closed `OTFL0` boundary;
  raw captures and recovery journals remain local, while committed summaries
  omit transport/hardware identifiers, channels, coordinates, and secrets.
- OpenTrail will not decode raw vehicle CAN/J1939 traffic.
- Hardware-specific code will be isolated behind interfaces.
- LoRa will carry compact state/events/messages, not map packages or high-rate telemetry.
- Forwarding will be controlled and measured before any mesh topology is adopted.
- Protocols and stored configuration will be versioned and defensively decoded.
- Loss of GPS, maps, UI, peers, or OpenGauge must degrade independently.
- Offline-map formats/providers remain replaceable and must permit offline use with correct attribution.
- OpenGauge alerts cross a fixed 64-byte `OGA0` semantic boundary with canonical units and explicit assert/clear lifecycle IDs. CRC detects corruption only; the transport must supply authenticated and authorized producer identity before OpenTrail accepts an alert.
- Critical-alert acknowledgements cross a separate fixed 64-byte `OGK0` boundary with explicit accepted/rejected disposition, canonical rejection reason, original lifecycle identities, consumer boot session/sequence, and observed age. CRC detects corruption only; transport authorization, replay persistence, delivery-controller/outbox correlation, and physical delivery remain required.
- The ACK responder produces `OGK0` only from a final ingress decision: accepted and identical duplicate alerts become accepted/none; authenticated unauthorized/stale/conflict/rate decisions become explicit rejection; malformed/untrusted/identity-mismatched/local-clock-invalid input is silent. Sequence advances only after encoding. A separate two-slot `OTAS` allocator commit-last persists consumer/authorization binding and increments a nonzero boot session before returning it; corruption, identity/epoch change, equal-generation conflict, exhaustion, read failure, and uncertain state fail closed. Ten allocation groups plus affected responder/configuration suites each repeat 100 times. Its exact `ot_proto` key/value composition adds six groups and 100/100 repeats for fresh-instance rotation, both ambiguous-commit outcomes, durable reset/reseed, and wrong-sized-value refusal in the complete 95-executable matrix. Per-session sequence remains RAM-only; protected target storage, trusted rollback resistance, authenticated response delivery, physical interruption, and OpenGauge rebind remain.
- Two role-reversed OT-017D bench cycles carried exact 64-byte `OGA0` and responder-produced correlated `OGK0` frames over temporary MeshCore channel text. All correlation checks passed, round trips were 1009.6-1014.0 ms, loss/duplicates/new errors were zero, the SenseCAP recorded exact aggregate +4 flood RX/TX, repeat stayed on, and 4/4 endpoint cleanup checks passed. The host supplied authenticated/authorized context; this is physical byte/composition evidence, not authenticated on-device delivery.
- Two later OT-017E cycles retained the physical and cleanup checks while independently admitting each returned ACK through OpenGauge's real authorization/session/replay/correlation ingress and completing its exact reconstructed outbox entry. Both ended with one acknowledgement and zero queued/in-flight state. OpenGauge state was reconstructed after receipt; persistent live target state remains unproved.
- Two OT-017F role-reversed stale-policy cycles returned exact correlated rejections through the same physical path. OpenGauge processed both but recorded zero acknowledgements, `outbox_completed=false`, no retry release, and explicit terminal failure. Radio loss/duplicates/errors were zero, SenseCAP aggregate was exact +4 flood RX/TX, and cleanup passed 4/4. Retryable rejection and persistent failure/restart/revoke cases remain.
- Two OT-017G role-reversed rate-limit cycles returned exact correlated retryable rejections. OpenGauge processed both with zero acknowledgements/completions, exactly one queued retry, zero in flight, retry release, and no terminal failure. Radio loss/duplicates/errors were zero, SenseCAP aggregate was exact +4 flood RX/TX, and cleanup passed 4/4. Persistent backoff and a later retry-to-accept cycle remain.
- OT-017H added two role-reversed four-leg rejection/retry/accepted sequences. Exact backoff and byte-identical preparation passed; both lifecycles ended with one acknowledgement, one remote retry, zero queued/in-flight/terminal failures, exact aggregate +8 SenseCAP flood RX/TX, and 4/4 cleanup. State was reconstructed after both responses rather than kept live during the physical wait.
- OT-017I repeated both four-leg sequences with one real OpenGauge process already holding the in-flight event before the first send and retaining authorization/replay/outbox state through final completion. Both live lifecycles passed with exact aggregate +8 SenseCAP flood RX/TX and 4/4 cleanup. Restart/power-loss durability and on-device state remain unproved.
- OT-017J records the cross-project restart boundary: OpenGauge's canonical `OOC0` checkpoint is now wired into boot-only atomic live-outbox export/import. Host tests reconstruct queued retry readiness, in-flight ACK timeout, maximum lifetime, exact frames/state/attempts against a new monotonic origin and fail closed on prepared, corrupt, mismatched, nonempty, or unrepresentable state. Durable coordinated storage and physical/on-device restart remain unproved.
- OT-017K records that `OOC0` compatibility is no longer trusted caller metadata: OpenGauge derives a nonzero versioned fingerprint from all four outbox timers, maximum attempts, and emergency reserve. Determinism and sensitivity to every field pass in the full matrix plus 100 focused repeats. Coordinated durable storage remains unproved.
- OT-017L records OpenGauge's canonical `OCR0` coordination boundary: one nonzero generation now contains the exact ACK replay/authorization and outbox checkpoints. Four groups, the full 30-executable matrix, and 100 repeats validate the envelope. Serialized live dual import and recoverable durable storage remain unproved.
- OT-017M records preflighted live `OCR0` coordination: OpenGauge exports both state owners into one generation and validates both exact boot imports on private component copies before committing either live state under exclusive ownership. Exact retry readiness and replay duplicate rejection survive the host restart. Five groups, the full 31-executable matrix, and 100 repeats pass. Recoverable durable storage and physical/on-device restart remain unproved.
- OT-017N records OpenGauge's recoverable two-slot `OCR0` host store: increasing generations, full readback/byte/decode verification, newest-unique selection, prior-good preservation under partial/corrupt writes, degraded-I/O visibility, and equal-generation conflict rejection pass eight groups, the full 32-executable matrix, and 100 repeats. Target NVS, physical power-cut/wear, secure integrity/rollback, and on-device restart remain unproved.
- OT-017O records store-owned recovery generation allocation: empty media starts at 1, saves advance/rotate monotonically, unreadable or conflicted baselines fail closed, and 64-bit exhaustion occurs before export/write. Ten store groups, the full 32-executable matrix, and 100 repeats pass. Factory-reset authority, target NVS, physical power-cut/wear, secure integrity/rollback, and on-device restart remain unproved.
- OT-017P records conservative uncertain-commit handling: all write errors retain intended slot/generation, 16 `OCR0` boundary interruptions preserve the prior newest-good slot, and a full write followed by an I/O error reconciles as committed on boot. Twelve store groups, the full 32-executable matrix, and 100 repeats pass. Target-backend semantics and physical power-cut/wear remain unproved.
- OT-017Q records OpenGauge's canonical `OPA0` peer-authorization restart boundary. The fixed 256-byte record preserves active/revoked logical peers, role permissions, channel, opaque key handles, and authorization epochs, refuses pending approvals, and imports atomically only into a clean boot registry. Eight groups, the full 33-executable matrix, and 100 repeats pass. Coordinated `OPA0`/`OCR0` restore, protected target storage, authenticated integrity, rollback resistance, and physical power-loss evidence remain unproved.
- OT-017R records OpenGauge's recoverable `OPS0` peer-authorization store. The fixed 288-byte envelope and two-slot host store allocate monotonic generations, require exact byte/decode readback, preserve the prior good generation across ten interrupted-write boundaries, reconcile a full write followed by I/O error as committed at boot, expose degraded reads, and fail closed on conflict/exhaustion. Ten groups, the full 34-executable matrix, and 100 repeats pass. Coordinated authorization/replay/outbox restore, protected target storage, authenticated integrity, rollback resistance, and physical power-loss evidence remain unproved.
- OT-017S records OpenGauge's atomic `ORS0` system-recovery boundary. The fixed 1280-byte record binds exact `OPA0` peer authorization and exact `OCR0` ACK/outbox state to one generation. Import restores authorization/outbox only in private candidates and constructs a temporary ACK ingress against them, so epoch, policy, pointer, replay, and retry dependencies preflight before any live owner changes. Six groups, the full 35-executable matrix, and 100 repeats pass. Recoverable `ORS0` storage, target binding, authenticated integrity, rollback resistance, physical power-loss, and on-device boot evidence remain unproved.
- OT-017T records OpenGauge's recoverable two-slot `ORS0` host store. Normal saves own monotonic generations, preserve the newest good slot, require exact readback/decode, recover across eleven interrupted-write boundaries, reconcile a full write followed by I/O error at boot, expose degraded reads, and refuse conflict/exhaustion. Eight groups, the full 36-executable matrix, and 100 repeats pass. ESP-IDF protected storage, authenticated integrity, rollback/reset authority, physical power-loss/wear, and on-device boot evidence remain unproved.
- OT-017U records the external trusted-generation contract: OpenGauge restore rejects a newest valid `ORS0` below the supplied trusted minimum before any live owner import, and save allocation advances beyond both last-trusted and valid local generations. Ten focused groups, the unchanged 36-executable matrix, and 100 repeats pass. A hardware-backed trusted source, authenticated integrity, reset/replacement authority, and physical evidence remain unproved.
- OT-017V records the target recovery-adapter plan and its current host implementation boundary. OpenGauge now binds the two exact 1280-byte `ORS0` slots to `og_state` / `og_recovery` / `ors0_a|b` and composes real boot/save coordinators through restarted adapter/store instances. Thirteen groups and 100/100 repeats prove normal restart, applied-uncertain trusted-floor catch-up, and unapplied-uncertain prior-generation recovery in the public 43-executable matrix. The connected Heltec/SenseCAP MeshCore radios do not run an OpenGauge target, so no exact ESP-IDF, protected-key/trust, physical-interruption, or on-device durability claim exists.
- OT-017W records protected key-handle preflight: direct and stored OpenGauge `ORS0` restore now validate active logical peers through an injected opaque-handle boundary after private authorization restore but before outbox/ACK preflight or live mutation. Revoked peers are skipped; unavailable, wrong-purpose, and backend failures retain typed peer-specific evidence. Eight system groups, eleven store groups, the unchanged 36-executable matrix, and 100 repeats each pass. No concrete protected-key backend or on-device result exists.
- OT-017X records OpenGauge's typed host boot coordinator. It combines provisioning, trusted-generation, read-only two-slot inspection, protected-key `ORS0` restore, and exact trusted-floor reconciliation into first-boot/restored/degraded/safe-mode/service-required outcomes. Transport stays disabled until the result is operational. Nine focused groups, the complete 37-executable matrix, and 100 repeats pass. No OpenGauge target task, protected backend, or physical boot result exists.
- OT-017Y records OpenGauge's verified save coordinator. Normal persistence requires exact local/trusted generation agreement; the next `ORS0` is verified before trust advances; and exact trust readback is required before transport remains allowed. Missing, rollback, conflict, local-ahead, commit-uncertain, and trust-update failures stay typed and route to service or boot reconciliation. Eight focused groups, the complete 38-executable matrix, and 100 repeats pass. Physical storage/trust durability remains unproved.
- OT-017Z records OpenGauge's unreadable-slot fail-close. Known empty/invalid peer media may restore operationally degraded, but unreadable media could conceal a newer committed generation. Any visible restore remains private, trust does not advance, and transport stays disabled under a service-required result. Ten boot groups, the complete 38-executable matrix, and 100 repeats pass. Physical backend diagnosis remains unproved.
- OT-017AA records OpenGauge's known-degraded repair coordinator. Only current operational degraded evidence with matching active/trusted generation and one valid plus one known empty/invalid slot can write. The next `ORS0`, trust update/readback, and final two-valid-slot inspection must all pass. Healthy, unreadable, service, stale, and uncertain cases fail closed. Five groups, the complete 39-executable matrix, and 100 repeats pass. Physical repair durability and unreadable-media service remain unproved.
- OT-017AB records OpenGauge's redacted recovery-status boundary. Boot, save, and repair results map into one fixed-shape record with operator state/reason/action, slot health, observed/trusted generations, key-failure class, and transport/repair flags. Peer IDs and key handles are structurally absent, and unknown or inconsistent inputs fail closed. Seven groups, the complete 40-executable matrix, and 100 repeats pass locally. Target logging/rendering, persistent audit retention, and physical service workflows remain unproved.
- OT-017AC records OpenGauge's versioned recovery diagnostics adapter. One 32-bit event carries the redacted operation, state/reason/action, slot health, protected-key failure class, and transport/attention/repair/redaction flags. Generations and identity-bearing fields are omitted; magic/version/enums and coherence are validated before a ring write. Eight groups, the complete 43-executable matrix, and 100 repeats pass publicly. Target log binding, persistent retention/export, and physical service capture remain unproved.
- OT-017AE records the target-shaped cross-project recovery boundary as implemented host plumbing rather than a plan-only gap. The backend-neutral `ORS0` key/value adapter and real boot/save composition pass thirteen groups, 100/100 repeats, and the complete public 43-executable matrix. OpenTrail still has no exact ESP-IDF backend, protected key/trust source, physical interruption, or on-device composition.
- OpenTrail has its own GitHub Actions validation on `main` pushes and
  pull requests. The commit-pinned Windows 2025/Python 3.13/UCRT64 job builds
  six verifier/planning/operator CLIs and runs all 119 C++ executables plus the
  Python MeshCore lease, privacy-safe field/pilot, and crypto-benchmark evidence
  suites. The matrix includes position scheduling/privacy control,
  experimental packet/priority admission, the quick-status payload/menu/parent,
  opt-in breadcrumb archive sessions,
  bounded outbox/durable-ack handoff, checked-time retry, privacy-safe archive
  presentation, single-read archive status capture, serialized archive snapshot
  adapter, private serialized archive runtime owner, revision-bound local
  archive consent, complete local archive workflow, optional archive parent
  page, exact-revision archive navigation, durable lease-to-workflow bootstrap,
  restart-safe archive session leases and their key/value
  composition, single-owner archive UI,
  loss-aware priority-to-delivery
  handoff, checked-time outbound service coordination, fail-visible outbound
  position safety, checked-time position commands, single-owner position UI,
  privacy-safe position UI diagnostics and strict offline position/recovery
  operator decoding and the unified diagnostic entry point,
  portable-client composition, local-interface, power, time, randomness,
  replay, map activation/checkpoint/store/boot, protected-generation boot,
  first baseline, authorized service reseed, candidate replacement, and
  runtime-transition recovery, protected-domain provisioning, and recoverable
  stable trust-domain activation, read-only active-domain boot, and
  domain-aware candidate entry, restart-safe trial boot, domain-aware runtime
  transitions, update checkpoint, duplicate checkpoint, and multi-domain
  persistent key/value storage, non-erasable map trust-domain key/value
  storage, outbound-counter key/value composition, ACK-session key/value
  composition, and archive-lease key/value composition,
  pilot, and benchmark boundaries.
  This is host/build evidence, not
  physical MeshCore,
  target firmware/bindings, cryptography, or measured-radio evidence.
- Public OpenGauge GitHub Actions separately validates the shared Windows host matrix on every `main` push and pull request. Its current-main warning-free run passes all 41 executables with zero annotations; OpenTrail links that evidence without conflating the two scopes.
- Project software and documentation are published under Apache-2.0; external contributions follow `CONTRIBUTING.md`, and sensitive reports follow `SECURITY.md`.

## Available hardware and current evidence

| Item | Current status | Required evidence |
| --- | --- | --- |
| Two Heltec V4 LoRa-capable boards | `OT-DEV-001` is the first experimentally flashed OpenTrail target. OT-061 proved exact-profile write/verification, boot self-check/USB heartbeat, and one privacy-safe BLE advertisement observation. OT-064 then applied one factory-app-only update and physically accepted a recognizable Trail startup logo followed by `BLE ADVERTISING`, boot self-check PASS, four heartbeats, and one exact-service Android candidate without selection, connection, pairing, or identifier retention. `OT-DEV-002` remains untouched by OpenTrail and continues to run MeshCore USB Companion `v1.16.0-07a3ca9`. Historical MeshCore bench evidence established matching USA/Canada configuration, bounded bidirectional delivery, repeater transport, alert/ACK behavior, and GNSS detection. | OT-064 closes only the selected-unit startup/status OLED binding. Protected storage, GATT authorization, Ready, LoRa/GNSS, interactive display/input, exact controller/revision/RF/full pinout/power, recovery-after-loss, field range/endurance, regulatory acceptance, and support remain open. Both write authorizations are consumed; no standing write or unit-2 authority remains. See `tests/hardware/OT-061-2026-08-16.md`, `tests/hardware/OT-064-2026-08-17.md`, `hardware/INVENTORY.md`, and the historical MeshCore evidence records. |
| Seeed SenseCAP solar node | Runtime-identified as **Seeed SenseCap Solar**, USB `VID 2886:0059`, running MeshCore Repeater `v1.16.0-07a3ca9` at 910.525 MHz/BW 62.5/SF7/CR5/22 dBm with repeat enabled. The owner purchase record is SenseCAP Solar Node **P1-Pro**, ASIN `B0FMDHBWX8`; Seeed's current MeshCore product maps that variant to SKU `100023690` with XIAO nRF52840 Plus, Wio-SX1262, L76K GNSS, and battery. Its coordinate-free GNSS status progressed from active/no-fix/0 satellites to a live fix, with later checks at 4, 7, and 8 satellites. Both Heltecs received its advert and remotely read its synchronized clock. A temporary private-channel run produced exactly +2 flood RX/+2 flood TX. Explicit one-hop direct routes then succeeded both ways; with repeat off, the same route failed with +1 direct RX/+0 direct TX and no destination message, proving the repeater was required. A non-secret channel lease passed real stopped-session recovery. The 300-minute alternating close-bench run delivered 300/300 (150 each direction), zero loss/duplicates/errors, 229.8-312.1 ms latency, exact +300 repeater flood RX/TX, repeat preserved, empty queues, and verified exact-name channel/journal cleanup. OT-017D added exact aggregate +4 flood RX/+4 flood TX while two role-reversed alert/ACK cycles passed; repeat remained on and errors stayed zero. See `tests/hardware/OT-003A-2026-08-12.md`, `tests/hardware/OT-009-2026-08-08.md`, `tests/hardware/OT-009A-2026-08-09.md`, and `tests/hardware/OT-017D-2026-08-09.md`. | Exact received label/revision and internals, physical GPS/antenna details, GNSS accuracy/cold-start/loss/power behavior, solar endurance, physical field behavior/range, and regulatory validation remain. |
| Wio Tracker L1 Pro for MeshCore | Owner reports the unit arrived after already being flashed as USB Companion and configured for a USA frequency plan; shipping/pre-write state was not preserved or verified. Windows reported public USB model `Seeed Wio Tracker L1`, family `2886:1667`; fixed read-only MeshCLI returned USB Companion `v1.17.0-727fc05` build 09-Aug-2026, repeat false, 910.525 MHz/BW 62.5 kHz/SF7/CR5/configured and maximum 22 dBm, 4.111 V, and zero errors/queue/packets/airtime/receive errors. Three more cycles kept public model/firmware/profile and zero error/traffic state stable while uptime increased. GNSS was detected but inactive with no GPS telemetry. A non-transmitting comparison matched channel 0 and absent default-scope state only in memory, confirmed distinct identities without emitting them, and found clocks within one second. No transient port, identity, secret, or coordinate is retained. C# and Python loaders now recognize the family; three built-in live refreshes and three exact-roster source-free external UI Automation cycles returned one Heltec, one SenseCAP, and one Wio, all runtime-identified and zero ready. See `tests/hardware/OT-020-2026-08-13.md`. | OT-020 is `partial` and the unit is only `experimented`. Exact label/SKU/revision, pre-write state, antenna/RF/regulatory evidence, over-air interoperability, GNSS activation/fix/loss, power/endurance, BLE, DFU/recovery, and clean-machine evidence remain. The package result is loader recognition evidence only, not hardware compatibility or support |
| Two Waveshare ESP32-S3 1.75-inch round AMOLED touch boards | Owner reports two ordered; not received or tested. The product family is advertised as 466x466 touch with ESP32-S3R8, 8 MB PSRAM, 16 MB flash, and standard/case/GPS variants; exact ordered and received variant remains unconfirmed | Preserve shipping firmware/recovery evidence; confirm exact labels/variant, display/touch/storage interfaces, usable memory, power/thermal behavior, and map/peer/alert rendering under OT-018 |
| Two approximately 7-inch touchscreens | Original test intent; no exact hardware identified | Board/display/controller, interface, resolution, memory/storage needs, availability |

Hardware is not added to a tested-compatible list until repeatable evidence exists.
The [2026-08-10 hardware/regulatory reconciliation](../hardware/HARDWARE_REGULATORY_INVENTORY_2026-08-10.md)
separates exact-unit observations from official family specifications. Its US
field gate remains open: exact labels and FCC grants, installed antenna/gain and
cable loss, frozen firmware/radio settings, and grant coverage for the complete
configuration must be verified. A USA preset or an in-band center frequency is
not treated as proof of authorization.

## Assumptions to validate

- The Heltec boards are legal/configurable for the user's operating region and can form the first two-node test bed.
- ESP32 resources are sufficient for selected map/UI behavior after benchmarking.
- Local Wi-Fi SoftAP may support setup and map transfer without Internet, subject to UX/security/storage testing.
- Connected GPS/GNSS hardware still needs exact module/wiring identification, target-profile binding, Heltec privacy-safe fix/satellite evidence, repeatable accuracy/loss behavior, and suitable antenna/power validation. The SenseCAP's bounded live-fix result does not close those wider gates.
- Alerting is supplemental and cannot guarantee delivery, location accuracy, or emergency response.

## Unresolved decisions

### Product and hardware

- Exact commercial Heltec and Seeed SKUs, regulatory authorization, antenna/RF details, and power-source characteristics
- Reference MCU/radio/display/GPS/storage hardware and minimum supported resource tier
- Portable, vehicle, fixed-relay, and touchscreen power/environmental requirements
- Whether a single ESP32 can meet the chosen large-display map workload

### Protocol and security

- Direct/repeater topology, modulation profiles, airtime budget, final
  position/status cadence, and congestion policy. The position scheduler's
  fixed start/stop/coalescing mechanics and semantic local privacy control are
  host-tested, but policy values and rendered physical behavior remain
  unselected pending measurement
- Identity/name/alias/membership boundaries and the OT-013 invitation/promotion/revoke/rekey/recovery policy are defined and host-tested. Exact Node-ID/alias derivation, production administrator quorum, authenticated join-handshake instantiation, encryption, key storage, rollback protection, persistent recovery, rendered UX, and physical lifecycle evidence remain under partial OT-005 and later gates
- Packet-v0 encoding/budget, quick-status and position payloads, host-only acknowledgement/retry/expiry/duplicate/forwarding/priority policies, the external `OGK0` alert-ACK codec, and OT-014 non-secret configuration persistence are bounded and tested. The [NVS-ready multi-domain adapter](persistence/PERSISTENT_STORAGE_KV_TARGET_ADAPTER_V0.md) now isolates five exact 64-byte namespaces and preserves erase/partial-write/sync ordering across twelve groups and 100/100 focused repeats in the complete 109-executable matrix. Its outbound-counter, ACK-session, and archive-lease compositions add five, six, and five groups respectively, each at 100/100 repeats, without granting protected storage. It is not a protected secret store, ESP-IDF backend, or physical durability result. Generic packet-v0 ACK composition, authenticated routing/priority/ACK transport, measured deployed timing, authenticated message/duplicate counter integrity and secure rollback, realistic contention, and final queue/cache limits remain
- Duplicate checkpoints have a canonical fixed 672-byte `OTD0` codec with CRC, strict padding/capacity/version checks, duplicate-key rejection, atomic decode, and remaining-lifetime restoration. Seven codec groups, the full 23-executable matrix, and 100 codec/window repeats pass. Atomic durable storage, wear/privacy policy, authenticated integrity, and rollback protection remain
- The fixed 704-byte `ODS0` store now uses context-bound v1: its formerly reserved bytes carry the exact nonzero group-context ID and epoch, and every active inner key must match. Wrong binding and structurally valid legacy unbound v0 media fail without live mutation or overwrite. Original generation/rotation/readback/degraded/conflict/exhaustion behavior remains. Ten store groups, the full 28-executable matrix, and 100 focused store/coordinator repeats pass locally; the exact matrix passes publicly in run `31374678550`. A separate [NVS-ready key/value adapter](persistence/DUPLICATE_CHECKPOINT_KV_TARGET_ADAPTER_V0.md) fixes exact `ot_state` / `ot_replay` / `ods_dup_a|b` bindings, 704-byte values, explicit durable commits, idempotent erase, and applied-then-failed restart discovery. Nine groups and 100/100 focused repeats pass in the complete 95-executable matrix. Protected ESP-IDF namespace access, physical atomicity/endurance, authorized migration/reset, authenticated integrity, and trusted rollback protection remain
- The v0 update/recovery architecture requires signed hardware-bound bundles,
  complete inactive-slot readback, persisted bounded trials, independent health
  confirmation, automatic rollback, a trusted firmware floor, and documented
  physical/USB recovery. A pure guard passes eight host groups across candidate,
  write, trial-health, deadline, boot-limit/mismatch, clock, and rollback paths.
  A separate canonical 64-byte `OTU0/v0` checkpoint binds hardware, baseline,
  candidate, exact policy, trial count, rollback reason, and caller-owned
  generation across eight more groups. Restore is atomic and intentionally
  clears boot-local health, time, and session evidence. An abstract two-slot
  store now owns normal generation allocation, preserves prior-good state
  across partial/corrupt writes, verifies readback, repairs a known invalid
  peer, and fails closed on unreadable/conflicted state. A caller-supplied
  trusted-generation contract rejects missing/stale generations before live
  restore and allocates beyond the greater local/trusted value. Read-only
  inspection exposes empty, degraded, unreadable, invalid, and conflicted media
  without mutating the guard or slots. All 20 store groups plus 100 focused
  repeats pass. A separate
  [NVS-ready key/value adapter](update/UPDATE_CHECKPOINT_KV_TARGET_ADAPTER_V0.md)
  fixes exact `ot_state` / `ot_update` / `otu_chk_a|b` bindings, 64-byte values,
  explicit durable commits, idempotent erase, native-error containment, and
  restart recovery after an applied-then-failed commit. Nine groups and 100/100
  focused repeats pass in the complete 95-executable matrix. No ESP-IDF
  backend, target partition/security configuration, task lock, signer,
  updater adapter, authenticated target storage, hardware-backed trusted
  generation source, or physical interruption/recovery evidence exists. A
  typed host boot coordinator now holds guard state private until the observed
  image is validated, any trial/rollback transition is readback-verified, and
  the trusted generation advances with exact readback. Fifteen groups plus 100
  focused repeats pass. A verified normal-save coordinator now requires exact
  local/trusted agreement, verifies the next checkpoint before advancing trust,
  and verifies exact trust readback before reporting committed. Ten groups plus
  100 repeats pass; uncertain/local-ahead state requires reboot reconciliation.
  A lifecycle-transition coordinator now applies health, tick, confirmation,
  and rollback to a private guard copy, publishes reboot-relevant state only
  after that verified save commits, and stops the original live guard on any
  persistence failure. Ten groups plus 100 repeats pass. A fixed redacted
  operator-status boundary validates boot, save, and transition coherence before
  emitting only coarse state/reason/action, generation evidence, and recovery
  flags. Hardware/candidate identity, checkpoint payloads, raw adapter errors,
  and nested results are absent; unknown or contradictory input blocks normal
  operation as service-required. Eight groups plus 100 repeats pass in the
  complete 58-executable matrix. A versioned `OTRD0` adapter now records one
  coherent status through the existing logger as one fixed hexadecimal 32-bit
  word. Generations and identity-bearing detail are omitted; magic, version,
  reserved bits, enums, flags, and state/action/reason coherence fail closed.
  Eight groups plus 100 repeats pass. A bounded production-facing RAM ring now
  retains the newest 32 canonical records, assigns boot-local sequences,
  snapshots oldest-first without partial output, counts rollover/rejection,
  and captures real `OTRD0` events across eight groups plus 100 repeats. A
  separate host-only CLI accepts exactly one canonical uppercase recovery
  record, reruns all v0 word/coherence checks, and emits stable coarse names.
  It reads no file, device, log, or network and has no recovery execution
  authority. Target
  task binding/concurrency, retained audit/export, rendering, scheduling and
  reboot execution, target boot tasks, terminal
  cleanup/reset authority, protected backends, and physical restart evidence
  remain absent.
  A semantic presentation adapter now maps valid `OTRD0` outcomes into the
  existing checked status/system-fault frame. Invalid words fail visibly to a
  generic critical service frame; only nonblocking notices expose
  acknowledgement, which has no confirm/cleanup/reboot authority. Nine groups
  plus 100 repeats pass. Exact renderer, target scheduling/revision ownership,
  and physical operator workflow remain absent.
  A separate position-sharing adapter now presents stopped, active, waiting,
  deferred, and terminal-failure scheduler states through that same checked UI
  boundary. Start only arms the scheduler, stop is immediate, stale frame input
  is rejected, and unrelated actions cannot mutate position sharing. Ten groups
  plus 100 repeats pass. Renderer wording/layout, exact target synchronization,
  direct radio/GPS composition, and physical privacy behavior remain absent.
  An experimental packet-admission sink now revalidates the scheduler's
  canonical current payload, obtains injected ephemeral packet-v0 metadata,
  encodes one exact 38-byte frame, and admits it only as background traffic
  using the actual scheduler attempt time. Ten groups plus 100 repeats cover
  round-trip, expiry, priority, pressure, and failure behavior. Packet v0 is
  unauthenticated and prohibited for real coordinates; identity/counter
  lifecycle, authenticated composition, delivery/radio binding, and physical
  behavior remain absent.
  A single-owner priority-to-delivery handoff now peeks before admission and
  commits only after the delivery controller accepts. Full/rejected delivery
  admission retains the queue entry, and remaining queue lifetime bounds the
  delivery expiry. Ten groups plus 100 repeats cover strict priority, pressure,
  rejection, exact expiry, rollback-safe time handling, and the complete
  position-packet path through a fake radio. Authentication, target concurrency,
  direct-radio binding, and physical delivery remain absent.
  A checked-time outbound service coordinator now reads the guarded monotonic
  clock once and orders optional active-sharing location, scheduling, handoff,
  delivery, and radio service. It performs no GPS or downstream work when time
  is unavailable, and permanent clock faults stop sharing and latch service
  closed. Ten groups plus 100 repeats pass. Target task/concurrency, inbound
  processing, concrete adapters, and physical behavior remain absent.
  The same coordinator now owns target-facing position Start/Stop. Start obtains
  one fresh checked sample internally; not-ready defers without scheduler
  access; rollback/source failure latches closed; Stop is immediate without a
  clock read. Ten groups plus 100 repeats pass. No target task/clock binding,
  rendered retry behavior, reboot policy, or physical input is claimed.
  A cooperative position-sharing UI coordinator now owns revision allocation,
  initial/current presentation, one checked input poll, live Start/Stop, and the
  required post-action refresh. Temporary clock deferral retains the truthful
  Start frame. Post-action display failure and revision exhaustion stop sharing
  and latch input closed. Ten groups plus 100 repeats pass. Exact ESP-IDF
  task/lock serialization, rendering, and physical behavior remain absent.
  The same owner now compares live user-visible position semantics before
  polling input. GPS wait/recovery, sink deferral, and permanent clock faults
  publish a higher revision first; nonvisible counters/timestamps do not.
  Failed observation refresh stops sharing and latches input closed. Ten groups
  plus 100 repeats pass. Exact target atomicity and physical timing remain
  absent.
  A separate `OTPD0/v0` adapter records validated position UI outcomes as one
  fixed 32-bit public event while omitting revisions, timestamps, counters,
  location, content, and identity. Idle polls are suppressed. Ten groups plus
  100 repeats pass; target retention/export and physical service evidence are
  absent.
  A runtime-aware position overlay now validates the coordinator status before
  presentation or Start/Stop application. Latched rollback/source failure and
  incoherent state produce a no-action critical frame; stale healthy Start is
  rejected, while Stop remains safe. Ten groups plus 100 repeats pass. Exact
  renderer/input, reboot, and physical behavior remain absent.

### Maps and interface

- The v0 offline-map gate fixes legal/source metadata, off-device preparation,
  immutable staged activation, prior-good recovery, and mapless fallback. The
  provider/data, package/container, renderer, storage medium, transfer method,
  signature policy, limits, and exact target remain open pending OT-016/OT-018
  experiments
- `OTMD/v0` now has canonical lifecycle, recoverable abstract-store, permit-
  consuming preparation, and stable-baseline activation evidence through final
  protected/domain readback, exact read-only stable boot, and synchronized
  candidate entry, restart-safe trial boot/recovery, and synchronized runtime
  promotion, fallback completion, and previous-package cleanup. Protected
  target storage, continuity evidence, secure domain generation, physical
  package operations, and physical interruption recovery remain open
- Touchscreen UI framework and distracted-driving/safe-use constraints
- OpenGauge authenticated on-device transport, peer/key lifecycle, persistent replay/outbox state, failure UX, and direct radio integration; the v0 semantic schema/policy is host-tested and OT-017D/OT-017E supply bounded physical byte and host-component completion evidence
- Field-session repetition count, movement/terrain profiles, acceptance
  thresholds, and the final position/status cadence after measured contention

### Governance

- Professional clearance and final adoption of the provisional Limited
  Underground Trail family and Firmware Loader working names
- Legal applicant/payee, funding bookkeeping, award authority, and application-
  specific eligibility/terms
- Public privacy policy, location-data retention/deletion, authentication,
  backup, and incident-response rules
- Code of conduct, CI, release/signing process, and supported-hardware evidence policy; Apache-2.0 licensing, contribution guidance, and security reporting are established

## Next decision checkpoint

The hardware-abstraction set and host-tested portable-client composition now
cover all ten enumerated target-facing endpoints. The whole-contract review is
complete and records the separate 64-byte and 704-byte storage obligations.
OT-003F now supplies the first checked-time outbound runtime composition, but
OT-003 remains partial because no exact ESP-IDF radio/GPS/log/storage/
entropy/time/power/display/input adapters, target application build, or
on-device composition evidence exists. Exact adapters, thresholds, and rendered
behavior wait for frozen client hardware. OT-023 remains a blocked historical
four-client standalone plan, not the V1 Companion completion gate. Under
Decision 0033, OT-090 has frozen and host-tested the practical pairing/
replacement state machine without implementation credit, OT-091 has frozen the
separate algorithm-neutral secure-LoRa lifecycle/admission semantics with the
same host-only boundary, OT-093 has frozen the deterministic pre-crypto build
baseline without running a candidate, OT-094 has frozen the separate
candidate-readiness contract, and OT-095 has frozen the zero-source source-lock
admission contract while all six closure requirements remain blocked.
The next security checkpoint is to close those target/configuration/dependency/
radio requirements, accept a new immutable executable benchmark plan, and run
the exact candidate comparison under separate authority, followed by explicit
suite/library, handshake/KDF, and packet-v1 wire selection. Pairing/replacement
and secure-LoRa target/Android
implementation, physical acceptance, and the coherent two-pair run remain
separately authorized later gates. Continue the partially
executed OT-020 procedure without reconstructing the Wio's unpreserved shipping
state.
Authenticated on-device transport, protected target state, physical restart and
power-failure injection, GPS evidence, field performance, and direct-radio
airtime remain explicit later gates.
