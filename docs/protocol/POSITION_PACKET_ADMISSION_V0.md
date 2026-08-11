# Experimental position packet admission v0

Status: **host-tested packet-v0 composition only; unauthenticated, unencrypted,
and prohibited for real coordinates or field transmission**

## Purpose

The position scheduler previously stopped at an abstract payload sink. This
adapter proves the next bounded host-only composition step:

1. receive one scheduler-produced 16-byte position payload with the exact
   monotonic attempt time;
2. independently decode and require canonical `current` position state;
3. obtain one caller-owned set of ephemeral packet-v0 source, network, and
   message identifiers;
4. encode exactly one 38-byte experimental packet-v0 position frame; and
5. admit it to the existing priority queue as background position traffic.

It does not authenticate, encrypt, derive identity, persist a message counter,
deliver, transmit, forward, acknowledge, or call a radio driver. Packet v0's
CRC detects accidental corruption only. Real coordinates must not use this
adapter outside controlled non-radio host tests.

## Policy and ownership

Target composition injects:

- the priority queue;
- a `PositionPacketMetadataSource` owned by the future identity/group/message-
  counter lifecycle;
- the measured maximum frame size; and
- a nonzero queue lifetime.

The measured frame limit must accept the exact 38-byte packet and cannot exceed
the common fixed radio-frame capacity. No MTU is assumed from a board name.

Metadata is requested only after the position payload passes canonical decode.
Every successful metadata result must contain nonzero source, network, and
message IDs. The adapter consumes that metadata before queue admission; if
admission is rate-limited or full, the ID is intentionally skipped rather than
reused on retry. A production metadata source must eventually bind the selected
identity/group lifecycle and rollback-safe outbound counter. This adapter does
not supply or persist that authority.

## Admission and error mapping

Priority is derived by `PriorityTrafficQueue` from
`MessageClass::position`; callers cannot relabel the frame as normal, critical,
or emergency. The scheduler attempt time becomes the queue creation time, and
the injected lifetime determines exact expiry.

| Condition | Sink result | Scheduler meaning |
| --- | --- | --- |
| Packet admitted | `none` | Accepted; schedule normal cadence |
| Metadata not ready or background rate limit | `not_ready` | Retryable pressure |
| Reserved capacity or queue full | `full` | Retryable capacity pressure |
| Invalid policy/payload/metadata, exhausted metadata, encode failure, duplicate ID, or invalid queue | `failed` | Typed failure; no false admission |

Status contains only policy validity, the latest coarse error, and saturating
attempt/admitted/backpressure/failure counters. It contains no coordinates,
packet bytes, identifiers, keys, addresses, credentials, or free text.

## Host evidence

Ten deterministic groups cover:

1. exact 38-byte frame-capacity and nonzero-lifetime policy;
2. current-position packet round-trip with exact metadata, class, priority,
   timestamp, and expiry;
3. scheduler-to-admission propagation of the actual attempt time;
4. malformed and noncurrent refusal before metadata consumption;
5. retryable metadata-not-ready pressure;
6. exhausted, failed, unknown, and structurally invalid metadata;
7. exact background rate-window retry;
8. reserved and full queue-capacity mapping;
9. invalid queue and duplicate-message-ID failure; and
10. critical-before-position selection plus visible position expiry.

The focused executable passes 100/100 repeats. The complete 57-executable
OpenTrail host matrix plus all Python and publication-safety checks pass.

The separate [loss-aware priority-to-delivery handoff](PRIORITY_DELIVERY_HANDOFF_V0.md)
now proves that an admitted frame is retained until delivery accepts it and
that this exact position packet can cross the fake-radio host path. That does
not change packet v0's unauthenticated status or authorize real coordinates.

The [checked-time outbound coordinator](../platform/OUTBOUND_SERVICE_COORDINATOR_V0.md)
now services the scheduler, this sink, priority handoff, delivery, and fake
radio with one successful guarded clock sample.

## Remaining gates

- replace packet v0 with an authenticated/encrypted packet version after the
  cryptographic benchmark and threat-model gates;
- bind metadata to the selected group identity, epoch, and rollback-safe
  outbound counter without narrowing the counter unsafely;
- replace the host single-owner ordering proof with exact target task,
  synchronization, watchdog, UI, GPS, and clock-adapter evidence;
- choose queue lifetime, rate, cadence, and congestion policy only after
  measured four-client traffic, power, privacy, and regional review;
- replace the fake-radio-only delivery proof with one exact authenticated
  direct-radio adapter; and
- prove physical GPS loss/recovery, queue pressure, emergency preemption,
  radio airtime, power, range, and regulatory compliance without publishing
  private coordinates.
