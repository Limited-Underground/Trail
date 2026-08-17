# BLE Companion GATT v0

Status: host-tested codec/session/coordinator foundation, historical OT-042 NimBLE
GATT target definition, OT-044 response-safe GATT lifecycle, OT-046 host-only
one-phone authorization, OT-047 Android authorization UX, OT-048/049 fixed
authorization-wire codec/tracker parity, OT-050/051 restricted provisional
orchestration, OT-052 real NimBLE callback composition, OT-053 Android
protected-read production composition, OT-056 runtime owner, OT-057
renderer-neutral Android presentation, and OT-061 experimental physical boot
and BLE service-advertisement visibility, 2026-08-16. One target was flashed,
post-write verified, and observed through boot/self-check/USB heartbeat; one
physical Android phone found one compatible advertisement without selection,
connection, or pairing. Protected storage admission remains denied, every
connection is coded for immediate termination, and no GATT exchange,
authorization, or Ready evidence exists.

## Purpose and authority

This contract is the smallest production-facing boundary between one phone and
one screenless client. The device remains authoritative for identity and group
secrets, membership, radio traffic, durable queues, message and acknowledgement
identifiers, delivery outcomes, GNSS validity/freshness, position-sharing
policy, and history cursors. The phone renders state and submits user intent; it
does not manufacture queued, sent, delivered, acknowledged, secure, or current-
position evidence.

The contract is brand-neutral. Its `OTB0` and `OTC0` values are technical
OpenTrail compatibility identifiers, not customer-facing product names. The
simulator's newline `OTS0` helper is explicitly unauthenticated test traffic and
is not reused here. The compact embedded `UiFrame` is a renderer boundary and
is not serialized over BLE.

## GATT surface

The target will expose one custom 128-bit service with three characteristics.
These technical UUIDs are stable for v0 and must not contain a working product
name.

| Surface | UUID | Properties | Purpose |
| --- | --- | --- | --- |
| Companion service | `5e0f2a00-7c6b-4ea3-a210-0c4f1f43b7d0` | service | Groups the production companion boundary |
| Protocol info | `5e0f2a01-7c6b-4ea3-a210-0c4f1f43b7d0` | Read | Exact supported version, role, capabilities, and bounds |
| Command | `5e0f2a02-7c6b-4ea3-a210-0c4f1f43b7d0` | Write With Response only | Phone-to-device snapshot or action request |
| Stream | `5e0f2a03-7c6b-4ea3-a210-0c4f1f43b7d0` | Indicate and Notify | Device-to-phone snapshots, action results, and events |

Normal v0 session traffic on all three characteristics requires an encrypted,
authenticated, application-authorized connection. The restricted v0.1 claim
phase is the sole exception: an exact bonded, encrypted, authenticated link may
read protected Protocol Info and use only claim kinds on Command/Stream before
application authorization. Advertising, discovery, or Android bond state alone
is never authorization or current-link security evidence. The target must
refuse normal Command and Stream traffic if the negotiated ATT MTU is below the
Protocol-info requirement. v0's maximum 148-byte value requires ATT MTU 151
because an ATT notification/indication has three bytes of overhead.

Action results, session-opening snapshots, critical alerts, and critical
acknowledgement outcomes use Indicate. Replaceable status/position refreshes may
use Notify; loss is repaired by an explicit snapshot request. Write Without
Response is not admitted.

## Protocol info: exact 16-byte `OTB0/v0`

| Offset | Bytes | Meaning |
| ---: | ---: | --- |
| 0 | 4 | ASCII `OTB0` |
| 4 | 1 | Major version, zero |
| 5 | 1 | Minor version, zero |
| 6 | 1 | Role: `1` screenless client |
| 7 | 1 | Capability bits |
| 8 | 2 | Maximum fragment payload, little-endian, at most 128 |
| 10 | 2 | Minimum ATT MTU, little-endian |
| 12 | 1 | Maximum fragments, at most 16 |
| 13 | 1 | Maximum active controllers, exactly one |
| 14 | 2 | Reserved zero |

Capability bits are: bit 0 quick status, bit 1 critical-alert
acknowledgement, bit 2 position state, and bit 3 message history. Unknown bits,
roles, nonzero reserves, impossible MTU/payload combinations, and versions other
than exact v0 fail closed. A capability advertises only a protocol surface; it
does not prove target or physical behavior.

## Fragment envelope: `OTC0/v0`

Every Command or Stream value is one canonical fragment.

| Offset | Bytes | Meaning |
| ---: | ---: | --- |
| 0 | 4 | ASCII `OTC0` |
| 4 | 1 | Major version, zero |
| 5 | 1 | Minor version, zero |
| 6 | 1 | Frame kind |
| 7 | 1 | Reserved zero |
| 8 | 4 | Nonzero device-generated boot-local session nonce |
| 12 | 4 | Nonzero exchange ID, little-endian |
| 16 | 1 | Zero-based fragment index |
| 17 | 1 | Fragment count, 1 through 16 |
| 18 | 2 | Fragment payload bytes, 0 through 128 |
| 20 | variable | Exact payload bytes |

Normal application-authorized phone-to-device kinds are `1` snapshot request
and `2` action request. Normal device-to-phone kinds are `0x81` snapshot,
`0x82` action result, and `0x83` event. OT-048 reserves provisional
authorization kinds `0x03` Claim Start, `0x84` Pending, and `0x85` terminal,
but the current GATT/session path does not admit or transport them. Unknown
kinds and versions are rejected rather than guessed.

The client assigns a strictly increasing exchange ID to each request. An action
result echoes that request ID. Device-originated snapshots/events use a boot-
local increasing event ID. All fragments of one logical device-to-phone record
have the same session nonce, kind, exchange ID, and fragment count and arrive in
index order. A receiver keeps at most one 2,048-byte reassembly per direction,
publishes nothing partial, and rejects a missing, repeated, out-of-order, mixed,
oversized, or timed-out transfer. The exact timeout and memory owner remain
target gates. v0 Command requests are deliberately single-fragment; this keeps
user actions atomic and bounded.

The envelope has no application CRC. ATT supplies link integrity, while the
required authenticated application session supplies access control. Higher
OpenTrail group/radio payload authentication remains separate and is not
weakened or replaced by BLE security.

## One-controller session

The target adapter may open a session only after it supplies all of:

- a nonzero opaque controller binding derived privately from the BLE stack;
- an encrypted link;
- an authenticated bond; and
- persistent application authorization established through the approved owner
  workflow.

The host guard treats these as adapter obligations, not as values received from
the phone. Exact secure-connections/OOB/passkey behavior for a screenless target
is unresolved; ordinary unauthenticated "Just Works" pairing is not sufficient
evidence for the `authenticated_bond` input. A physical authorization window,
revocation/reset path, privacy-safe bond storage, and lost-phone recovery must
be designed and validated on the selected device before production use.

Only one controller binding can hold the boot-local session. The device assigns
a strictly increasing nonzero boot-local session nonce for each opening and
rejects any repeated or lower value. Reaching `UINT32_MAX` exhausts the guard;
it cannot wrap or open another session within that boot composition. Requests
must match the exact binding and nonce. The first nonzero
request ID is accepted, a higher ID is accepted, an equal ID is classified as a
duplicate and must not reapply the action, and a lower ID is stale. The runtime
owner must retain the previous exact action result long enough to answer the one
allowed duplicate deterministically. After request ID `UINT32_MAX`, only the
equal ID reaches duplicate classification; the runtime must then establish
exact request equality or fail conflict. Every other request fails as exhausted
and the client must reconnect into a new session rather than wrap.

The guard classifies equality by request ID; it does not compare the request
kind or payload. Until the runtime owns a bounded previous-request fingerprint
and result cache, an equal ID must never execute again, and differing bytes under
that ID must fail as a conflict rather than inherit the cached outcome.

Disconnect closes only the companion session. It does not erase authorization,
stop radio service, discard the device queue, change delivery state, or silently
change position-sharing policy. Reconnect creates a new session nonce, obtains
an authoritative snapshot, and resumes by device-owned history/event cursors.

## State and action sequencing

OT-035 binds the first fixed semantic payloads to the OT-033 envelope. These
records remain presentation-neutral and brand-neutral; none contains text,
coordinates, group material, a persistent device identity, `OTS0`, or the
embedded `UiFrame`.

| Payload | Bytes | Allowed envelope kind | Purpose |
| --- | ---: | --- | --- |
| `OTX0/v0` | 8 | `snapshot_request` | Exact semantic-version request after validating Protocol Info |
| `OTN0/v0` | 32 | `snapshot` | Device-owned status revision, typed radio/GNSS/power/position-sharing states, queued-action count, and optional exact pending critical-alert ID |
| `OTA0/v0` | 20 | `action_request` | One fixed quick status, exact-alert acknowledgement, or explicit position-sharing Start/Stop intent |
| `OTR0/v0` | 20 | `action_result` | Echoed typed intent and local `admitted`, device-queue `queued`, or `rejected` result |

Every record starts with its four-byte magic, exact semantic major/minor zero,
and zero reserves. The snapshot revision is a nonzero device-owned boot-local
value. Radio, GNSS, power, and position-sharing values are closed enums. The
snapshot may expose a zero pending-alert ID for none or one exact nonzero
device-owned alert ID for acknowledgement correlation; it carries no alert
text or producer identity.

`OTX0` is magic at bytes 0-3, semantic major/minor at 4-5, and two reserved-zero
bytes at 6-7. `OTN0` uses the following exact layout:

| Offset | Bytes | Meaning |
| ---: | ---: | --- |
| 0 | 4 | ASCII `OTN0` |
| 4 | 2 | Exact semantic major/minor zero |
| 6 | 1 | Radio state |
| 7 | 1 | GNSS state |
| 8 | 1 | Power state |
| 9 | 1 | Position-sharing state |
| 10 | 2 | Device-owned queued-action count, little-endian |
| 12 | 4 | Nonzero boot-local snapshot revision, little-endian |
| 16 | 8 | Pending device-owned critical-alert ID or zero, little-endian |
| 24 | 8 | Reserved zero |

`OTA0` uses magic/version at bytes 0-5, action kind at byte 6, quick-status
detail or zero at byte 7, the critical-alert ID or zero at bytes 8-15, and four
reserved-zero bytes. `OTR0` uses magic/version at 0-5, disposition at 6,
rejection reason at 7, echoed action kind at 8, echoed quick-status detail or
zero at 9, two reserved-zero bytes, and the echoed critical-alert ID or zero at
12-19. Every multibyte integer is little-endian.

`OTA0` action IDs are fixed: 1 quick status, 2 acknowledge critical alert, 3
start position sharing, and 4 stop position sharing. Quick-status detail IDs
reuse the existing canonical wire values: 1 OK, 2 need assistance, 3 anyone
online, and 4 available to help. Only quick status may carry that detail. Only
critical acknowledgement may carry a nonzero exact device-owned alert ID.
Position sharing is two distinct intents rather than a toggle.

The semantic dispatcher rejects every record under the wrong `OTC0` frame kind;
v0 defines no event payload yet. `OTR0` is correlated by the enclosing `OTC0`
exchange ID and echoes the typed
intent. `admitted` means that a position-sharing intent was accepted locally.
`queued` means only that a quick-status or critical-acknowledgement item entered
device-owned outbound work; it is not sent, received, delivered, acknowledged,
or operator-response evidence. `rejected` always includes one closed reason:
unsupported action, stale alert, unavailable, queue full, policy denied, or
internal failure. Stale alert is valid only for exact-alert acknowledgement;
queue full is valid only for the two outbound action kinds. The remaining
reasons are intentionally generic across action kinds. Successful results carry
no rejection reason. The codec
rejects `admitted` for outbound actions and `queued` for local position actions
so no caller can use the typed result to manufacture radio-delivery evidence.

The minimum order is:

1. connect to one explicitly selected allowlisted service;
2. establish the approved encrypted/authenticated/authorized controller link;
3. read and validate exact `OTB0/v0` and negotiate ATT MTU;
4. subscribe to Stream and receive an indicated authoritative snapshot carrying
   the new session nonce;
5. submit a single-fragment snapshot or action request with a new request ID;
6. receive an indicated typed action result that distinguishes local admission
   from radio delivery; and
7. recover notification loss or reconnect through a fresh snapshot plus bounded
   device-owned history cursors.

The first payload set deliberately stops at one coarse status snapshot and
three bounded intent families. Message/history records, request fingerprint and
result caching, event payloads, alert detail, self/peer position coordinates and
freshness, reassembly, and target runtime ownership remain later increments.

## Maps sequence

Maps are downstream of the position contract, not part of GATT transport.
First prove a mapless peer list with current/stale/unavailable coordinates and
freshness. Then select a licensed offline map package/provider and attribution
path. Only after that should Android render device-authoritative peer positions
over local map data. Map packages never travel over LoRa and do not belong in
this GATT control stream.

## Host evidence and remaining gates

`CompanionProtocolInfo` and `CompanionFragment` use fixed-capacity storage and
atomic encode/decode. Fifteen deterministic groups and 100/100 focused repeats
cover canonical vectors,
all known kinds, maximum fragmentation, exact lengths, null/capacity failures,
unknown/reserved/version rejection, full security-evidence admission, exclusive
controller ownership, exact session binding, request direction, single-fragment
commands, monotonic IDs, duplicate classification, stale rejection, close/
reopen, non-adjacent nonce reuse, session/request exhaustion without wrap, and
redacted public status.

OT-035 adds 13 deterministic semantic-codec groups and 100/100 focused repeats
covering exact vectors, strict lengths/version/reserves, the complete closed
status enum space, four canonical quick-status IDs, exact alert correlation,
explicit position Start/Stop, result coherence, atomic failure, and one real
`OTC0` envelope composition.

OT-039 adds a fixed-memory target-neutral request coordinator above those
layers. It admits one complete client request through the session guard,
dispatches the exact semantic record, obtains state or a prepared action from
injected device authority, constructs the complete response, and only then
atomically commits an action. The authority contract requires pure preparation
and no mutation on commit error. One exact completed request/response is cached
for byte-identical duplicate replay without reapplying authority; conflicting,
stale, old-session, exhausted, and terminally failed requests remain closed.
Sixteen strict groups and 100/100 repeats pass. See the
[request coordinator contract](COMPANION_REQUEST_COORDINATOR_V0.md).

OT-040 build-links that coordinator into the generic ESP32-S3 candidate and
gates its heartbeat path on a fixed-authority boot self-check. The exact request
and response bytes, single action application, and duplicate replay path are
compiled into the image, but remain `BUILD-LINKED-NOT-RUN` and `NOT-FLASHED`.
No GATT service or real device authority is implied.

OT-041 adds the corresponding unwired Android runtime owner. Its facade uses
the exact four v0 UUIDs and an indication-only Stream API; ordered security,
MTU, Protocol Info, subscription, and initial-snapshot phases each have bounded
timeouts. One owner-thread generation rejects stale callbacks and releases all
scan, GATT, reconnect, negotiation, and action timers on lifecycle shutdown.
At OT-041 acceptance the checked-in app manifest was permission-free and no
Android Bluetooth facade was implemented, so that increment did not exercise
the GATT contract.

OT-042 adds the exact service and three characteristic definitions plus a
one-connection Secure Connections-only NimBLE peripheral/GATT-server
configuration to the generic target. The application does not register the
service, initialize NimBLE/controller state, or advertise. Command writes are
denied before coordinator mutation until exact registered CCCD handles,
per-connection indication-subscription/disconnect ownership, and injected
application authorization exist.

OT-043 adds a concrete but unwired Android 12+ facade. It filters the exact
service, validates the complete GATT profile, supports API 31/32 and API 33+
call shapes, bounds opaque candidates and scan time, and rechecks the exact
Scan or Connect permission on every active state- or data-bearing queued
callback; disconnected cleanup remains best-effort. At OT-043 acceptance the
manifest declared Scan with `neverForLocation` and Connect, but the fake-only
activity had no permission UX and never constructed the facade.

OT-044 adds the exact per-connection response lifecycle required before command
mutation. It binds registered Command, Stream, and CCCD handles; security and
application-authorization evidence; indication subscription; coordinator
session; and one outstanding delivery token. The sink reserves the full
response before coordinator mutation. Congestion and guarded re-entry reject
without mutation; wrong/stale completion preserves the pending response;
actual submit failure, negative exact completion, timeout, unsubscribe, or
security loss contain and block until exact disconnect. The target build links
and self-checks this owner but still does not register/start NimBLE or advertise, and
real NimBLE Command dispatch remains denied.

OT-045 wires the Android runtime and facade into an explicit Bluetooth-device
mode beside the separately labeled Local test mode. It owns Nearby Devices
permission request/settings recovery plus scan/select/connect/disconnect and
lifecycle cleanup without silent fallback. The production application-
authorization authority remains deny-all, so this does not prove a successful
BLE session or physical peripheral.

OT-046 defines one-phone application authorization above a trusted encrypted,
authenticated BLE bond. It accepts only a private stable opaque bond token from
the future target bond store, binds exact boot/session/controller challenges,
requires bounded physical claim/revoke/replace/reset windows, and uses injected
atomic verified persistence with a trusted generation floor. It is host-only,
target-neutral, and not build-linked.

OT-047 adds the matching Android claim/replacement UI and controller seam. It
accepts only exact bounded device-issued results and labels timeout or invalid/
lost results as unknown device authority. The production claim client remains
disabled, so there is still no real application authorization or bond evidence.

OT-048 fixes exact 8-byte `OTL0/v0` Claim Start, 24-byte `OTP0/v0` Pending,
and 28-byte `OTF0/v0` terminal payloads under the three reserved authorization
kinds. One nonzero opaque 128-bit device correlation is bound to exact
provisional session/exchange/purpose and is never an ID, address, secret,
physical token, displayed value, log field, or persisted record. A fixed C++
tracker requires explicit future negotiated support, encrypted/authenticated
bond evidence, Pending before one terminal, exact context, and exact close as
the sole release of a connection generation when called by its transport owner.
Fourteen strict groups, ten shared
vectors, 100/100 repeats, and the 119-executable OT-048-era host matrix pass.

OT-049 mirrored the bytes and tracker in pure Kotlin. Its Android gate passed
77 JVM tests across eight suites (protocol suites 6, 10, and 10; application
suites 6, 17, 11, 1, and 16) and lint; its 9,627,825-byte debug APK has SHA-256
`967FCD7A032ECED63789378F5B3C0F6AC86D06CE9CF3B6B16205E7C49B8093A3`.
At OT-049 acceptance the production claim client remained disabled and was not
wired to the activity, runtime, or Android GATT facade.

OT-050 adds a separate exact 20-byte `OTB0/v0.1` record for the restricted
phase. Capability bit `0x10`, a nonzero provisional session nonce, payload
capacity 28 through 128, a normal ATT MTU minimum of at least 151, and a
fragment-count limit from 1 through 16 provide explicit negotiated evidence.
The current lifecycle advertises exactly MTU 151 and 16 fragments. The record
fits default MTU 23; the client should then request the advertised normal MTU,
while claim admission hard-requires at least 51 for the complete 48-byte
terminal indication. Only exact registered Protocol Info/Command/Stream/CCCD
handles, trusted encrypted/authenticated-bond evidence, and current Stream
indication subscription may open the path.

The device reserves before Pending, confirms Pending before later local
physical/authority resolution, reserves terminal capacity before authority
mutation, and promotes only after exact Accepted/Replaced terminal indication
confirmation. MTU below 151 leaves normal traffic closed after promotion until
the same connection reaches the advertised bound. Denied, local timeout,
security loss, unsubscribe, disconnect, congestion, negative confirmation,
submission failure, or stale callback cannot promote. OT-051 mirrors this order
and sends an explicit Snapshot Request after promotion.

At OT-050 acceptance the 120-executable host matrix, twenty strict groups at 100/100,
target self-check 100/100, and static admission 3/3 pass. Two pinned ESP-IDF
v6.0.2 builds reproduce a 165,349-byte image and 165,472-byte BIN with SHA-256
`E2ACF6672925D2FF298BD58E7C7BCBA564D46F1B7A6853D67865CE62F09D12B9`;
the authorization wire and lifecycle are retained in the link map. See
[OT-050 target evidence](../../tests/hardware/OT-050-2026-08-15.md).

At OT-051 acceptance the Android gate passed 90 JVM tests across ten suites (protocol 6, 10, 10,
and 3; application 7, 9, 17, 11, 1, and 16) and clean lint. The 9,644,209-byte
debug APK has SHA-256
`28ED3014ACE420F8C531625211D26BD3FB9D522F1349BACA0878F94726534D8A`.
`MainActivity` still injected the disabled claim client at that checkpoint.

OT-052 now binds the frozen lifecycle to the real ESP-IDF NimBLE callback
surface. It captures registered value handles and separately discovers the
Stream CCCD, rechecks device-side encryption, authentication, bond, key size,
private binding, and MTU, reserves before mutation, and carries an immutable
submission-era connection/generation/session/exchange/value/token tuple through
indication completion. The pinned NimBLE teardown order prevents a stale
completion from being relabeled after connection-handle reuse. Protected
Protocol Info access is therefore device-enforced path evidence; bond state
alone is not. Normal commands remain denied until exact Accepted/Replaced
terminal indication confirmation.

OT-053 composes the runtime-backed claim client with the Android GATT facade in
explicit Bluetooth mode, with no fake fallback. It consumes the protected
v0.1 read, requests MTU 151, enables exact Stream indications, performs Claim
Start/Pending/terminal correlation, and sends one explicit Snapshot Request
only after promotion. The gate passes 101 JVM tests across ten suites (protocol
6, 10, 10, and 3; application 8, 15, 17, 11, 1, and 20), clean lint, and debug
assembly. The 9,644,209-byte debug APK has SHA-256
`BE385FEB8966210C4C09027388C3F560745F6A075B9CBB1ABF25DC0893C0033C`.

At OT-052 acceptance the 121-executable native matrix, callback-adapter ten groups at
100/100, target self-check 100/100, static admission 3/3, and pinned teardown-
order admission pass. Two pinned ESP-IDF v6.0.2 builds reproduce a 170,313-byte
image and 170,432-byte BIN with SHA-256
`22CAE43F7AEA9D980602C41E1ACEB49CA1174315EE87598D15E6717A27A1E4D4`.
See [OT-052 target evidence](../../tests/hardware/OT-052-2026-08-15.md).

OT-054 adds the durable authority prerequisite below this dormant callback
path. The target link now retains a fixed owner/tombstone codec, a protected
compare/commit/readback seam with an independent rollback-floor requirement,
and a private bond-reference/device-secret PRF resolver. Seventeen strict
groups at 100/100, target self-check 100/100, static admission 3/3, all 122
native entries, and two identical pinned builds pass. The 175,824-byte BIN has
SHA-256
`D39430096B7BEDD0F69D9ECCDE2424EDCD635C0BEA904EB2E4FCA3EEED307080`.
Real target admission remains denied because the protected NVS, usable
protected-key, private-bond, separate-PRF-key, atomic-floor, and rollback-floor
proofs are unavailable; production code performs no NVS/eFuse/HMAC call.

OT-055 moves the Android real-GATT graph into one explicit user-started,
non-exported `connectedDevice` foreground service. It owns the facade, runtime,
authorization controller, GATT leases, and timers while running, returns
`START_NOT_STICKY`, has no boot/background auto-start, and never automatically
retries a claim. Local test remains Activity-owned and separate. The Android
gate passes 124 JVM tests across twelve suites, clean lint, manifest inspection,
and a 9,660,781-byte debug APK with SHA-256
`33174B72792E2AFC0D03AB52DFAC6613BAE48618BF268C3197D7E04105897722`.

OT-056 codes the real target boot owner to initialize NimBLE, register this
service, and advertise standard flags plus the public service UUID after all
boot checks. Host callbacks cross a fixed eight-entry queue into one serialized
owner; disconnect cleanup precedes bounded tokenized re-advertising, and exact
host stop/deinit is pinned to ESP-IDF v6.0.2 ordering. The application
advertising-data payload carries no name, manufacturer data, address field, or
device/user/group identifier; actual link-layer address behavior remains
unobserved.

The protected persistence preflight is still explicitly denied. Configured
SC/MITM/bonding is not usable-bond proof; the fixed authorities deny, every
connection is immediately terminated, and no claim or normal command is
admitted. At OT-056 acceptance the runtime was
`CODED-BUILD-LINKED-NOT-RUN`, nothing was flashed, and the APK was not installed.
OT-061 later flashed the exact profile on one experimental target, observed its
boot/self-check/USB heartbeat, and used the exact accepted APK on one physical
Android phone to find one compatible service advertisement without selection,
connection, pairing, or identifier retention. That proves target runtime and
advertisement visibility only; it does not prove GATT exchange, pairing,
application authorization, Ready, protected storage, notification lifecycle,
LoRa, GNSS, display, accessibility, packaging, signing, distribution, or
support.

## OT-066 host trusted-authority composition

OT-066 supplies the production composition behind the previously injected
trusted-binding and authorization seams. One exact connection generation is
resolved through an opaque private bond reference, the existing device-secret
binding resolver, and a separate private session issuer. The first valid
reference is pinned even while a downstream dependency is not ready; changed
same-generation evidence and older generations fail closed. One successful
binding is cached exactly for callback security refreshes.

The GATT authority adapter maps an empty durable owner to physical-gated claim,
a retained owner to reconnect without ownership rewrite, and an exact physical
replacement window to replacement. Disconnect releases only the active
controller lease. Incoherent success, reentry, wrong-phone evidence, and
persistence uncertainty never publish a controller.

This composition is not injected into the Heltec runtime. Its denied binding
and authorization authorities remain active. No target bond store, key,
physical gesture, pairing, application authorization, GATT exchange, or Ready
state is claimed. See
[Decision 0011](../decisions/0011-host-trusted-gatt-authority-composition.md)
and [OT-066 evidence](../../tests/hardware/OT-066-2026-08-17.md).
