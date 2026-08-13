# Generic Quick-Status Payload v0

Status: **host-tested compact payload only; no authenticated packet, delivery,
menu, renderer, target firmware, radio, or physical-device claim**, 2026-08-12.

## Purpose

The first small-group release needs a few familiar, scenario-neutral messages
without forcing an off-road, camping, racing, or emergency-specific profile.
The fixed v0 catalog is:

| Wire value | Semantic meaning | Default English label |
| --- | --- | --- |
| `1` | sender reports okay | I'm OK |
| `2` | sender requests assistance | Need assistance |
| `3` | sender asks whether peers are present | Anyone online? |
| `4` | sender offers assistance | Available to help |

The wire carries only the semantic value. A renderer owns localized wording,
layout, accessibility, and any future profile presentation. `Need assistance`
is not a guaranteed rescue request and does not replace the separate held
critical-alert workflow.

## Exact payload

`OTQ0/v0` is exactly 12 bytes:

| Offset | Bytes | Meaning |
| ---: | ---: | --- |
| 0 | 4 | ASCII magic `OTQ0` |
| 4 | 1 | schema version, currently zero |
| 5 | 1 | exact payload length, `12` |
| 6 | 1 | one known quick-status value from the catalog |
| 7 | 1 | reserved, must be zero |
| 8 | 4 | little-endian CRC-32 over bytes 0 through 7 |

The payload contains no participant, device, group, location, timestamp,
message ID, acknowledgement, free text, key handle, credential, or routing
data. Those belong to later authenticated envelope and delivery boundaries.
CRC-32 detects accidental corruption; it provides no authenticity or
confidentiality.

Encoders reject unknown status values and preserve caller output on every
failure. Decoders require the exact length, magic, version, known status,
canonical reserve, and checksum. Unsupported versions remain distinguishable
from malformed or corrupt input.

## Host evidence

Ten deterministic groups plus 100/100 focused repeats cover:

1. one independent canonical `I'm OK` byte vector;
2. round trips for all four catalog entries;
3. distinct canonical bytes/checksums for every entry;
4. unknown-status encode rejection without output mutation;
5. null and undersized encode destinations;
6. null and wrong-length decode input;
7. exact magic and declared-length enforcement;
8. distinct unsupported-version, unknown-status, and reserve failures;
9. corruption at every byte position; and
10. fixed one-byte semantic state and 12-byte identity-free payload shape.

The complete 109-executable host matrix and Python publication checks pass.

## Next gates

- Define the authenticated packet-v1 message type, priority, replay, expiry,
  acknowledgement, and deduplication policy before any radio binding.
- Connect the host-tested two-page
  [revision-safe menu](../platform/QUICK_STATUS_MENU_COORDINATOR_V0.md) to one
  parent shell and later authenticated outbound owner.
- Define clear sent/queued/delivered/failed presentation; local selection must
  never be shown as peer delivery.
- Bind and measure one selected target, then exercise four physical clients
  under the staged pilot plan.
