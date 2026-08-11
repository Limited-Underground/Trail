# OpenTrail Experimental Priority Queue v0

Status: host-test foundation for partial OT-010, 2026-08-08

This component controls admission and selection before frames enter the delivery
controller. It proves bounded behavior; it does not create emergency packet
types, authenticated wire priority, regulatory-safe timing, or a finished UI.

## Priority order

1. emergency;
2. critical;
3. normal; and
4. background.

Selection is strict priority, then FIFO within the same priority. The host-test
policy uses eight active slots and reserves two for emergency/critical traffic.
Normal/background traffic cannot consume those slots.

Priority is derived from the validated message class: emergency, critical alert,
direct/chat, and position/status map to emergency, critical, normal, and
background respectively. Callers cannot independently label ordinary traffic as
emergency.

## Preemption

When the active queue is full, a new message may preempt only a strictly lower
priority entry. It removes the oldest entry at the lowest available priority.
Equal or lower priority never preempts. Every preemption produces an event with
the affected message ID, class, priority, and reason so an application can show
the failure rather than silently losing a queued message.

## Rate and stale limits

The experimental host policy admits at most two emergency, four critical, eight
normal, and four background messages per 10-second window. These are simulation
values, not field defaults. An emergency rate limit is still necessary to stop a
faulty button, sensor, or malicious member from consuming the entire channel.
The future UI must make rate-limited emergency attempts unmistakable and offer a
safe retry path.

Each entry has an explicit lifetime. Expired messages are removed before
admission or selection and generate an expiry event. A stale position/status or
alert is not sent merely because it eventually reaches the head of a queue.

## Transactional handoff

The queue exposes a single-owner `peek_next` / `commit_next` pair for integration
with the delivery controller. Peeking applies the same expiry purge and strict
priority/FIFO selection as destructive selection but retains the entry. Commit
succeeds only for the exact currently selected nonzero message ID. The legacy
`take_next` operation is implemented as that same peek followed by commit, so
selection semantics have one implementation.

The [priority-to-delivery handoff](PRIORITY_DELIVERY_HANDOFF_V0.md) uses this
pair to remove a queued frame only after delivery admission succeeds. Full or
rejected delivery admission therefore does not lose the priority entry.

## Failure surface

Admission returns a distinct result for malformed input, invalid policy,
duplicate message ID, oversized frame, reserved-capacity protection, full queue,
and rate limit. Eviction and expiry use a separate fixed-capacity event queue.
Event overflow is counted, never silently represented as successful delivery.

No result uses the word `delivered` until the delivery controller accepts a
valid authenticated acknowledgement. One-shot position/status messages remain
explicitly `sent_unconfirmed`.

## Evidence and remaining gates

Nine queue scenarios cover reserved capacity, urgent admission without wasting
remaining non-urgent capacity, strict
preemption, equal-priority protection, priority/FIFO selection, rate-window
reset, expiry events, validation, duplicate IDs, and emergency-over-critical
behavior. An integration scenario proves emergency selection reaches the
delivery controller before an existing normal backlog.

A separate ten-group
[experimental position-admission integration](POSITION_PACKET_ADMISSION_V0.md)
proves that scheduler-produced packet-v0 positions can enter only as background
traffic, use the actual attempt time for expiry, remain behind critical
traffic, expire visibly, and return typed rate/capacity pressure. It does not
add authentication or radio evidence.

A further ten-group [loss-aware handoff](PRIORITY_DELIVERY_HANDOFF_V0.md)
proves strict-priority transfer, retention under delivery capacity/rejection,
original-expiry preservation, and one scheduler-produced position packet
reaching a peer through the fake radio. It is host composition evidence, not an
authenticated or physical transport result.

OT-010 remains partial until packet types and authenticated priority are defined,
airtime/congestion values are measured, emergency rate/preemption behavior is
tested with realistic mixed traffic, and an actual UI visibly distinguishes
queued, sent-unconfirmed, confirmed, expired, preempted, full, and rate-limited
states. Emergency features remain supplemental aids and cannot guarantee rescue
or delivery.
