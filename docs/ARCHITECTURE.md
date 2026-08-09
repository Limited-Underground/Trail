# OpenTrail Initial Architecture

Status: proposed foundation, 2026-08-08. This document records boundaries and evaluation criteria; it does not claim an implemented system.

## Goals and constraints

OpenTrail must remain useful offline, tolerate intermittent peers, isolate failures, fit ESP32-class resources, and respect LoRa bandwidth and regional radio regulations. Raspberry Pi-class hardware is outside the baseline unless measured requirements make it necessary.

## Logical layers

```text
User interface / alert presentation / map view
                 |
Group, message, position, and emergency services
                 |
Delivery policy: priority, retry, deduplication, TTL, store-forward
                 |
Versioned OpenTrail packet codec and authentication boundary
                 |
Transport interfaces: LoRa | local setup/transfer | test transport
                 |
Hardware interfaces: radio | GPS | display/touch | storage | power
```

Application and protocol logic must depend on interfaces, not concrete boards. Packet codecs and delivery state machines should be host-testable. Board bindings and role composition belong in target applications.

## Proposed node roles

- **Client:** originates and consumes user messages, alerts, and positions.
- **Client/Repeater:** a client explicitly configured to forward eligible traffic under controlled rules.
- **Fixed relay:** powered or solar relay optimized for availability and forwarding.
- **Command/interface node:** larger display or field-case role that aggregates group state without becoming a single point of failure.

Roles are configuration/composition choices, not separate wire protocols. A portable or vehicle-mounted form factor may use any appropriate role.

## Networking direction

The first protocol should be topology-neutral enough to test direct and controlled-forwarding behavior, but it must not assume unrestricted mesh flooding.

Every packet envelope should eventually include:

- protocol version and message type
- source node ID and message ID
- group/network identifier or scoped addressing information
- priority and flags
- creation time or monotonic age information where available
- hop limit/TTL when forwarding is permitted
- payload length and integrity protection
- authentication metadata once the security model is chosen

Core receive behavior should validate length/version/integrity, authenticate when enabled, suppress duplicates, apply group/address rules, deliver locally, and only then consider bounded forwarding. Acknowledgements are message-class policies rather than mandatory responses to all traffic.

Priority classes should reserve capacity for emergency and critical traffic. Position updates may be coalesced or dropped when stale; chat should not starve emergency traffic. Exact airtime budgets and retry policies depend on measured modulation settings and regional constraints.

## Identity, joining, and security

Node identity, human-readable name, group membership, and cryptographic identity are distinct concepts. QR/join codes may simplify provisioning but must not expose long-lived group secrets in reusable plaintext. Encryption/authentication, key rotation, lost-node removal, physical reset, and recovery remain design decisions. Security must be threat-modeled before packet v1 is frozen.

## Location and time

GPS is an optional provider behind an interface exposing fix validity, age, position, altitude, heading, speed, accuracy when available, and UTC. Messaging continues without a fix. Alerts clearly indicate missing or stale position. Time-dependent protocol logic must tolerate nodes booting without UTC.

## Offline maps

Map rendering and map-package ingestion are separate from radio networking. LoRa never distributes map packages. A phone/computer may transfer packages locally, potentially through Wi-Fi SoftAP or removable storage, after hardware evaluation.

The package contract should be replaceable and include format/version, coverage, zoom/detail limits, attribution/license metadata, integrity, and storage requirements. Public `tile.openstreetmap.org` is not an offline bulk-download source; a provider or self-hosted pipeline must expressly permit offline use and redistribution.

## External critical-alert interface

OpenTrail accepts normalized events, never raw CAN/J1939 frames. The boundary should support at least schema version, event type, severity, source/vehicle identity, event time/age, optional typed value/unit, validity, and diagnostic context. OpenTrail adds its own node and current GPS context before radio transmission. Producers are untrusted inputs: values, lengths, rates, and event types require validation and rate limiting.

The bounded v0 contract is now specified in
`docs/integration/OPENGAUGE_CRITICAL_ALERT_V0.md`. It uses an explicit 64-byte
codec, opaque lifecycle identities, canonical units, CRC-32 corruption
detection, and a transport-supplied authenticated/authorized producer context.
OpenTrail host tests enforce freshness, duplicate/conflict, monotonic-time, and
emergency-reserve rate policy. This does not validate a physical transport or
replace transport authentication and replay protection.

Initial transports to evaluate are a local serial interface and an authenticated local wireless interface. The semantic event schema should remain transport-independent.

## Persistence and diagnostics

Persistent configuration is schema-versioned, checksummed, recoverable to safe defaults, and separated from secrets. Store-forward queues require explicit capacity, expiry, and wear strategy. Logging uses compile/runtime levels `ERROR`, `WARN`, `INFO`, `DEBUG`, and `TRACE`; release builds can remove verbose paths. Diagnostics must not leak secrets.

## Failure boundaries

- GPS loss marks positions unavailable/stale but does not stop messaging.
- Map/storage failure leaves messaging and alerts available.
- UI failure must not wedge radio processing.
- OpenGauge loss removes vehicle-derived events only.
- Peer/repeater loss triggers retries/expiry without an infinite queue.
- Corrupt, incompatible, or unauthenticated packets are rejected safely.

## Architecture gates before product firmware

1. Confirm exact development boards, radio region, frequency plan, antennas, and legal operating constraints.
2. Measure two-node LoRa airtime, loss, latency, and usable payload behavior across candidate settings.
3. Define identity/security threat model and packet-size budget.
4. Freeze only a minimal experimental packet envelope, then validate direct and controlled-forwarding behavior.
5. Benchmark candidate display, storage, map, GPS, and local-transfer options before selecting UI/map technologies.
