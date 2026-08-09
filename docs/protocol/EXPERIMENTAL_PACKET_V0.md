# OpenTrail Experimental Packet Envelope v0

Status: experimental only, 2026-08-08

This envelope exists to test codec and transport boundaries. It is not a stable
public protocol, provides no authentication or encryption, and must not carry
sensitive or safety-critical user data.

## Wire layout

All multi-byte integers are little-endian.

| Offset | Bytes | Field | v0 rule |
| ---: | ---: | --- | --- |
| 0 | 2 | Magic | ASCII `OT` (`4F 54`) |
| 2 | 1 | Version | `0` |
| 3 | 1 | Type | `F0` experimental probe only |
| 4 | 1 | Flags | Must be zero; reserved |
| 5 | 1 | Header length | `20` |
| 6 | 4 | Source node ID | Nonzero ephemeral test identifier |
| 10 | 4 | Network ID | Nonzero ephemeral test identifier |
| 14 | 4 | Message ID | Nonzero per-source test identifier |
| 18 | 2 | Payload length | Exact number of following payload bytes |
| 20 | N | Payload | Opaque bytes |
| 20+N | 2 | CRC-16/CCITT-FALSE | Header and payload, init `FFFF`, polynomial `1021` |

Fixed overhead is 22 bytes. A transport reporting a 163-byte MTU therefore has
a 141-byte v0 payload budget; a direct 255-byte LoRa frame would have 233 bytes.
Adapters must use their measured usable MTU rather than assuming either number.

## Decoder behavior

The decoder rejects incorrect magic/header length, truncated or trailing bytes,
unknown versions/types, nonzero reserved flags, zero test identifiers, and CRC
failure. It returns a view into the caller-owned frame and performs no dynamic
allocation.

The CRC detects accidental corruption only. It is not a message authentication
code. Identity derivation, group membership, encryption, authentication,
replay protection, timestamps, priority, acknowledgements, retry, TTL, and
forwarding remain intentionally outside v0 and must be threat-modeled before a
packet v1 is proposed.
