# BLE Companion GATT v0

Status: host-tested codec/session/coordinator foundation, dormant OT-042 NimBLE
GATT target definition, OT-044 response-safe GATT lifecycle, OT-046 host-only
one-phone authorization, OT-047 disabled Android authorization UX, and
OT-048/049 fixed authorization-wire codec/tracker parity,
2026-08-15. No GATT registration/controller/advertising, enabled target-bound
application-authorization workflow, radio path, authenticated group packet, or
physical-device evidence exists.

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

All three characteristics require an encrypted, authenticated, application-
authorized connection. Advertising or service discovery is never authorization.
The target must refuse the Command and Stream session if the negotiated ATT MTU
is below the Protocol-info requirement. v0's maximum 148-byte value requires
ATT MTU 151 because an ATT notification/indication has three bytes of overhead.

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
vectors, 100/100 repeats, and the current 119-executable host matrix pass.

OT-049 mirrors the bytes and tracker in pure Kotlin. The Android gate passes 77
JVM tests across eight suites (protocol suites 6, 10, and 10; application
suites 6, 17, 11, 1, and 16) and lint; the 9,627,825-byte debug APK has SHA-256
`967FCD7A032ECED63789378F5B3C0F6AC86D06CE9CF3B6B16205E7C49B8093A3`.
The production claim client remains disabled and is not wired to the activity,
runtime, or Android GATT facade.

Current Protocol Info has no authorization-claim capability bit, and the
current GATT contract requires application authorization before Protocol Info
and session negotiation. No provisional GATT phase or negotiated live claim
capability therefore exists. A later coordinated transport must admit only the
three authorization kinds on an encrypted, authenticated-bond provisional
connection and promote that exact connection/session only after an
authoritative Accepted or Replaced result. Failure or timeout cannot be
reported as authoritative Unsupported or Denied.

The generic target already links the shared protocol, semantic-dispatcher, and
coordinator sources touched by OT-048. Two pinned ESP-IDF v6.0.2 rebuilds
reproduce a 157,957-byte image and 158,080-byte BIN after exact authorization-
kind recognition and normal-path rejection were added. The link map excludes
the authorization wire/tracker source, and no target path admits the new kinds.
See [OT-048 target evidence](../../tests/hardware/OT-048-2026-08-15.md).

This does not implement GATT registration/startup/advertising, a provisional
authorization GATT phase or negotiated claim capability, pairing/bond
storage, target-bound application authorization, persistent result/history caching,
reassembly, Android background behavior, radio, GNSS, persistence, functional
target runtime, physical transport, accessibility, packaging, signing, store
distribution, or support. No BLE/GATT target was accessed and no
device was written. The broader full host suite's established read-only
USB-loader enumeration is documented separately in the OT-040 evidence record
and is not BLE/GATT runtime evidence.
