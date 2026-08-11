# OpenTrail Experimental Position Broadcast v0

Status: host-tested OT-012 foundation, 2026-08-08

This payload reports a compact current, stale, or explicitly unknown position.
It is not authenticated or encrypted, and the two-radio hardware proof did not
transmit it. Real coordinates must not be sent through packet v0 or a public
test channel.

## Wire layout

All multi-byte integers are little-endian. The payload is always 16 bytes.

| Offset | Bytes | Field | Rule |
| ---: | ---: | --- | --- |
| 0 | 1 | Payload version | `0` |
| 1 | 1 | Position state | `0` unavailable, `1` current, `2` stale, `3` invalid |
| 2 | 1 | Flags | bit 0 accuracy present, bit 1 age saturated, bit 2 accuracy saturated; other bits zero |
| 3 | 1 | Reserved | Must be zero |
| 4 | 2 | Fix age | Seconds, rounded up; `FFFF` with saturation flag is 65,535 seconds or older |
| 6 | 4 | Latitude | Signed degrees multiplied by 10,000,000 |
| 10 | 4 | Longitude | Signed degrees multiplied by 10,000,000 |
| 14 | 2 | Horizontal accuracy | Metres, rounded up; zero only when absent |

Packet-envelope type `01` identifies this payload. With the 22-byte
experimental packet-v0 overhead, a direct position frame is 38 bytes.

## State, age, and unknown semantics

- `current` and `stale` carry validated coordinates. State comes from the
  OT-011 location tracker rather than being inferred by the codec.
- `unavailable` and `invalid` are canonical unknown reports: flags, age,
  coordinates, and accuracy must all be zero. Encoding canonicalizes these
  states so an invalid fix cannot leak coordinates accidentally.
- Age and accuracy round upward, never making a fix appear newer or more
  precise than the source measurement.
- Age is the source fix age when encoded. It remains a lower bound after queue,
  transport, or forwarding delay because monotonic clocks are not comparable
  between nodes.
- Accuracy is independently optional. A saturated value means at least 65,535
  metres, not exact accuracy.
- UTC is intentionally absent. A node can broadcast a fresh spatial fix before
  synchronized wall-clock time exists.

The decoder rejects wrong length/version, unknown state, reserved bits,
out-of-range coordinates, zero present accuracy, nonzero fields on unknown
states, and saturation flags without their canonical maximum values.

## Direct-radio airtime model

The deterministic calculator uses the standard LoRa symbol-time model. For the
currently documented bench modulation—SF7, 62.5 kHz bandwidth, coding rate
4/5—this budget additionally assumes an eight-symbol preamble, explicit header,
payload CRC, and low-data-rate optimization disabled.

For a 38-byte direct-radio frame, the model yields 68 payload symbols, 80.25
total symbols, and **164.352 ms** airtime. This is a theoretical direct SX1262
frame budget. MeshCore's text adapter adds its own framing/encoding and its
whole-second counters cannot validate this per-packet figure.

Projected raw channel occupancy if every node sends at the same interval is:

| Interval | 1 node | 5 nodes | 10 nodes | 20 nodes |
| ---: | ---: | ---: | ---: | ---: |
| 30 s | 0.54784% | 2.73920% | 5.47840% | 10.95680% |
| 60 s | 0.27392% | 1.36960% | 2.73920% | 5.47840% |
| 300 s | 0.05478% | 0.27392% | 0.54784% | 1.09568% |

These figures exclude contention, collisions, retries, acknowledgements,
forwarding, other traffic, receiver timing, and regional operating limits. They
are not evidence of legal compliance or field performance.

The host-tested [start/stop scheduler](POSITION_BROADCAST_SCHEDULER_V0.md)
accepts an injected nonzero cadence and retry delay, coalesces delayed service,
and emits only current validated fixes. It deliberately does not select a
moving/stationary profile or unknown heartbeat. Earlier 60-second moving,
300-second stationary, and 30-second-floor values remain planning candidates
only. State changes and any no-fix heartbeat still require congestion,
priority, privacy, and regulatory review. No cadence is a safety guarantee.

The host-tested [experimental packet-admission sink](POSITION_PACKET_ADMISSION_V0.md)
now revalidates only `current` payloads, wraps them in the exact 38-byte
packet-v0 frame, and admits them as background traffic using the scheduler's
actual attempt time. This is component-composition evidence only; packet v0 is
not authenticated or permitted to carry real coordinates.

## Host evidence and remaining gates

Eight codec scenario groups cover current/stale/unknown states, no-UTC input,
conservative rounding, explicit saturation, canonicalization, validation, and
malformed input. A separate integration scenario carries the 16-byte payload
inside a 38-byte packet-v0 frame through the fake radio transport. Four airtime
scenario groups cover the bench settings, a 125 kHz reference, low-data-rate
optimization, and invalid inputs.

Authentication/encryption, rendered privacy UX, authenticated packet/priority
composition, selected cadence, multi-node contention, direct SX1262 binding,
per-packet hardware airtime, motion/field behavior, and regional compliance
remain later gates.
