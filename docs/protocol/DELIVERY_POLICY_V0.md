# OpenTrail Experimental Delivery Policy v0

Status: host-test policy for OT-008, 2026-08-08

This state machine is above the opaque radio transport and below application
workflows. It does not add a generic acknowledgement type to packet v0,
provide authentication, reserve emergency capacity, or claim field-safe timing.
The separate `OGK0` critical-alert ACK codec can supply an external semantic
acknowledgement after authenticated transport and correlation are composed;
that composition is not part of this controller yet.

## Message-class policy

The current values are deterministic host-test defaults only:

| Class | Acknowledgement | Maximum accepted sends | Retry interval | Expiry |
| --- | --- | ---: | ---: | ---: |
| Emergency | Required | 4 | 2 s | 30 s |
| Critical alert | Required | 4 | 3 s | 30 s |
| Direct message | Required | 3 | 4 s | 30 s |
| Chat | Required | 3 | 5 s | 45 s |
| Position | None | 1 | none | 10 s |
| Status | None | 1 | none | 15 s |

These values are not firmware/regulatory defaults. Airtime measurements,
regional constraints, congestion tests, emergency capacity, UI expectations,
and field behavior must determine deployed values.

## Send and acknowledgement semantics

- A transport acceptance increments the attempt count. It means locally queued,
  not received or acknowledged.
- `busy`, `queue_full`, `not_ready`, `peer_unavailable`, and I/O errors are
  transient. They defer the next send without consuming an attempt; expiry still
  prevents an infinite queue. Acknowledged traffic uses its configured retry
  interval; one-shot traffic uses a 100 ms transport-rejection backoff rather
  than spinning against a busy driver.
- Invalid arguments, payload/receive-buffer contract failures, and internal
  failures terminate the delivery as transport-rejected.
- Acknowledged classes retry at the configured interval until confirmation,
  expiry, or maximum accepted sends.
- Unacknowledged classes complete as `sent_unconfirmed` after one transport
  acceptance. The UI must not call that delivered.
- An acknowledgement is accepted only after at least one send and before
  expiry. The controller assumes its caller already authenticated and decoded
  the acknowledgement.
- Packet v0 has no generic acknowledgement type. The external `OGK0` alert-ACK
  bytes are defined and host-tested, but authenticated transport, replay
  persistence, controller correlation, and hardware acknowledgement evidence
  remain required.

## Duplicate suppression

The receive window keys entries by full tuple:

```text
(group epoch, source network alias, message ID)
```

- Zero-valued keys are rejected.
- A repeated observation does not extend retention, preventing a replay from
  pinning itself indefinitely.
- Expired entries are reusable.
- When all 32 fixed slots are active, the entry expiring first is evicted.
- Duplicate suppression controls local delivery/forwarding, but a valid
  duplicate may still require an authenticated acknowledgement so a sender can
  stop retrying.

## Reboot behavior

The duplicate window exports a versioned checkpoint containing keys and their
remaining lifetimes. Restore applies those remaining durations to the new
monotonic clock rather than persisting meaningless pre-reboot timestamps.
Malformed, zero-lifetime, duplicate-key, or wrong-version checkpoints are
rejected atomically.

The fixed 672-byte `OTD0` codec now serializes the checkpoint canonically with
explicit little-endian fields, zeroed unused capacity, semantic validation, and
CRC-32 accidental-corruption detection. Decode is atomic and rejects duplicate
keys, invalid lifetimes, malformed capacity, noncanonical padding, unsupported
versions, and repaired-CRC semantic tampering. See
`DUPLICATE_CHECKPOINT_CODEC_V0.md`.

The context-bound `ODS0/v1` host boundary wraps `OTD0` in a fixed 704-byte two-
slot record with exact group-context/epoch binding, nonzero generation,
canonical outer fields, CRC, automatic slot rotation, readback verification,
partial-write recovery, and explicit degraded-recovery state. I/O uncertainty,
legacy unbound media, wrong-group/epoch records, and conflicting equal
generations fail closed. See
`../persistence/DUPLICATE_CHECKPOINT_STORE_V1.md`.

Neither codec nor store is a secure persistence result. ESP32/NVS binding,
physical atomicity/endurance, wear budget, privacy lifecycle, authenticated
integrity, and rollback-resistant counter behavior remain open. Packet-v0 also
lacks a group epoch field, so current codec integration supplies an
experimental external epoch; packet v1 must bind the epoch cryptographically.

## Host evidence

Tests cover class policies, confirmation, lost ACK/retry, attempt exhaustion,
expiry, one-shot unacknowledged traffic, transient/permanent transport errors,
queue/ID validation, duplicate scope and expiry, capacity eviction, reboot
checkpoint restoration, invalid checkpoint atomicity, and an end-to-end lost
ACK scenario through the packet codec and fake radio transport. Seven codec
groups cover canonical serialization, corruption and semantic rejection, and
remaining-lifetime restore; the codec and duplicate-window suites each pass
100 consecutive repeats. Nine two-slot store groups cover rotation, partial
writes, corrupt-newer recovery, I/O and readback failure, semantic tamper,
generation conflict/exhaustion, and reset; store, codec, and window suites each
pass 100 consecutive repeats.
