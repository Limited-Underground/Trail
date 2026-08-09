# OpenGauge to OpenTrail Critical Alert Interface v0

Status: experimental interoperability contract, 2026-08-09

This file and the normative CSV fixtures are mirrored in the OpenGauge and
OpenTrail repositories. A change is not accepted unless both implementations
continue to encode and decode the same fixtures.

## Boundary

OpenGauge produces normalized vehicle-condition transitions. OpenTrail consumes
them, applies independent trust/freshness/duplicate/rate policy, and may add its
own node and GPS context before radio transmission.

The interface never transports raw CAN/J1939 frames, PGNs, SPNs, compiler memory
layouts, credentials, long-lived keys, a VIN, or free-form diagnostic text.

The 64-byte frame is transport-neutral. A framed serial adapter, authenticated
local wireless adapter, or deterministic test transport may carry the same
bytes. Transport selection, peer authentication, authorization, replay
protection, and delivery confirmation remain outside this semantic frame.

## Fixed frame

All multibyte integers are little-endian. Signed values use two's-complement.
Every reserved bit or unknown enum value is rejected.

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII magic `OGA0` |
| 4 | 1 | schema version, currently `0` |
| 5 | 1 | total frame length, exactly `64` |
| 6 | 1 | flags: bit 0 UTC present, bit 1 value present |
| 7 | 1 | alert type |
| 8 | 1 | severity |
| 9 | 1 | transition state |
| 10 | 1 | quality |
| 11 | 1 | canonical unit |
| 12 | 8 | opaque producer ID |
| 20 | 8 | opaque vehicle ID |
| 28 | 8 | event ID, unique per state transition |
| 36 | 8 | condition ID, stable across assert/clear transitions |
| 44 | 4 | event UTC epoch seconds, or zero when absent |
| 48 | 4 | producer-estimated age in milliseconds |
| 52 | 4 | signed value in milli-units, or zero when absent |
| 56 | 4 | producer diagnostic code, zero when absent |
| 60 | 4 | CRC-32/ISO-HDLC over bytes 0 through 59 |

CRC uses polynomial `0xEDB88320`, initial value `0xFFFFFFFF`, reflected
input/output, and final XOR `0xFFFFFFFF`. The check value for ASCII
`123456789` is `0xCBF43926`. CRC detects accidental corruption only and is
not authentication.

## Enumerations

Alert type:

| Value | Meaning | Value rule when present |
| ---: | --- | --- |
| 1 | engine over-temperature | degrees Celsius |
| 2 | low oil pressure | kilopascals |
| 3 | charging failure | volts or boolean |
| 4 | rollover detected | boolean or no value |
| 5 | vehicle immobilized | boolean or no value |
| 6 | critical fuel level | percent |
| 7 | generic critical condition | any defined unit or no value |

Severity values are `1=warning`, `2=critical`, and `3=emergency`.
Rollover requires emergency. Every other v0 type requires critical or emergency;
the warning value is reserved for a later explicitly reviewed type.

Transition state values are `1=asserted` and `2=cleared`. A condition uses
one nonzero condition ID across its lifecycle and a new nonzero event ID for
every asserted or cleared transition. Reusing an event ID with changed content
is a conflict, not an update.

Quality values are `1=valid`, `2=suspect`, `3=unavailable`, and
`4=error`. An asserted alert requires valid or suspect source data. A cleared
transition may report any defined quality, but an unavailable/error transition
cannot carry a numeric value.

Unit values are:

| Value | Unit | Valid milli-value range |
| ---: | --- | ---: |
| 0 | none | exactly 0 |
| 1 | degrees Celsius | -100000 to 250000 |
| 2 | kilopascals | 0 to 2000000 |
| 3 | volts | 0 to 100000 |
| 4 | percent | 0 to 100000 |
| 5 | revolutions per minute | 0 to 20000000 |
| 6 | boolean | 0 or 1000 |

The value-present flag requires a non-none unit. With the flag clear, unit and
value must both be zero. Producers convert source units to these canonical units
before encoding and must never invent a number for unavailable or error data.

## Identity and trust

Producer and vehicle IDs are opaque, provisioned nonzero 64-bit values. They
must not be a VIN, user name, Bluetooth address, Wi-Fi address, CAN source
address, or an unkeyed hash of one of those values.

The semantic frame does not authenticate itself. The receiving transport
supplies an authenticated producer ID and an explicit alert-publish permission.
OpenTrail rejects unauthenticated, unauthorized, or producer-ID-mismatched
frames. A physical USB connection may be approved by deployment policy, but the
adapter must still provide an explicit trusted-peer context; there is no
implicit insecure fallback.

Wireless adapters must add peer authentication, replay protection, and key
lifecycle outside this frame before production use. Secrets and exact
identifiers are redacted from normal logs.

## Freshness, duplicate, and rate policy

The producer-estimated age is mandatory and must be no more than 120000 ms when
OpenTrail receives the frame.

When UTC is present, the timestamp must be nonzero. If OpenTrail also has UTC,
it rejects events over 120 seconds old or more than 30 seconds in the future.
When UTC is absent, the timestamp must be zero and age is the freshness
authority. UTC loss does not stop the interface.

OpenTrail retains at least 16 recent event IDs for 10 minutes:

- identical producer/vehicle/event ID and frame content is a duplicate;
- the same identity tuple with different content is a conflict and is rejected;
- a duplicate does not consume rate capacity.

The v0 per-producer fixed-window policy is 10 seconds:

- four accepted unique critical events use the general allowance;
- emergency events use the general allowance first and then a reserve of two;
- additional unique events are rate-limited;
- exact duplicates are suppressed before rate accounting;
- a monotonic receive-time rollback is rejected instead of silently resetting
  limits.

The bounded OpenTrail implementation tracks eight simultaneously authorized
producers. If that fixed capacity is exhausted, additional producer frames fail
closed instead of evicting rate state. A deployment needing more producers must
increase the reviewed capacity and rerun the deterministic tests.

These bounds protect OpenTrail queues and LoRa airtime. They do not guarantee
that an alert is transmitted or received. Emergency messaging remains a
supplemental safety aid, not a rescue or vehicle-control system.

## Diagnostic code and logging

Diagnostic code zero means absent. Nonzero values are producer-defined stable
numeric codes documented by that producer. OpenTrail may retain the number for
diagnostics but must not interpret it as a J1939 PGN/SPN or forward arbitrary
vehicle data by default.

Logs may include type, severity, state, disposition, age bucket, and redacted
identifier prefixes. They must not include credentials, cryptographic material,
a VIN, precise OpenTrail location, or the full opaque IDs by default.

## Normative fixtures

`tests/fixtures/critical_alert_v0_vectors.csv` is normative. Each row defines
the semantic fields and exact 64-byte lowercase hexadecimal frame. Both
projects must round-trip every valid row byte-for-byte.

Malformed length, magic, version, reserved flags, enum, value/unit consistency,
CRC, trust, freshness, duplicate conflict, and rate-limit behavior are covered
by deterministic host tests rather than represented as accepted fixture rows.

## Evidence boundary

v0 establishes a semantic contract and deterministic host behavior only. It
does not select a physical serial connector, ESP-NOW framing, key exchange,
vehicle hardware, CAN database, OpenTrail radio packet, field rate, or
safety-certification path.
