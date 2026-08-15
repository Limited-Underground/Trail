# BLE Companion GATT v0

Status: host-tested fragment codec and one-controller session-admission
foundation, 2026-08-14. No BLE stack, Android application, target binding,
pairing method, radio path, authenticated group packet, or physical-device
evidence exists.

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

Known phone-to-device kinds are `1` snapshot request and `2` action request.
Known device-to-phone kinds are `0x81` snapshot, `0x82` action result, and
`0x83` event. Unknown kinds and versions are rejected rather than guessed.

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

The next semantic payload increment should bind existing typed behavior rather
than reproduce presentation objects. The minimum order is:

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

Initial action payloads should cover only the existing fixed quick statuses,
critical-alert acknowledgement, and explicit Start/Stop position sharing.
Snapshots/events should expose typed radio/GNSS/power state, queue summary,
message/alert records, self position, and peer position with validity and age.
Their exact binary payload schemas are intentionally not claimed by this
foundation.

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

This does not implement the UUIDs in a BLE stack, pairing/bond storage,
application authorization, target session allocation, result caching, request-
conflict comparison, reassembly,
semantic state/action payloads, Android, background behavior, radio, GNSS,
persistence, target build, physical transport, accessibility, packaging,
signing, store distribution, or support. No device was accessed or written.
