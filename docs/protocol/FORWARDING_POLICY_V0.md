# OpenTrail Controlled Forwarding Policy v0

Status: three-node host simulation for partial OT-009, 2026-08-08

This policy demonstrates bounded forwarding behavior without selecting an
uncontrolled mesh. Routing metadata is supplied outside packet v0 because the
experimental packet does not yet encode group epoch, destination, hop limit, or
forwarding permission. A future authenticated packet format must bind those
fields before hardware forwarding.

## Processing order

The controller assumes frame integrity and authentication have already passed.
It then:

1. rejects malformed metadata and frames;
2. rejects the wrong group or epoch without adding it to the duplicate window;
3. suppresses duplicates by `(group epoch, source alias, message ID)`;
4. rejects a previously unseen frame claiming the local alias;
5. determines whether the frame is locally deliverable;
6. stops unicast forwarding once the destination is reached;
7. requires explicit per-packet forwarding permission and a configured repeater
   role;
8. requires at least one remaining hop and decrements it exactly once;
9. applies fixed queue and forwarding-rate limits; and
10. queues an unchanged opaque frame with updated routing metadata.

Broadcast frames may be delivered locally and forwarded once. A reflected frame
with the same duplicate key is neither delivered nor forwarded again. Traffic
that cannot be forwarded because of TTL or congestion is not rescued by a later
duplicate, avoiding replay-driven amplification.

An originating node records its own `(epoch, alias, message ID)` before its first
send. A later reflected copy is therefore a duplicate. If that seeding step was
missed, a received frame claiming the local alias is still dropped rather than
delivered or forwarded.

## Congestion boundaries

The implementation has eight compile-time queue slots. Each configured node may
choose a smaller active depth plus a maximum number of forwards per monotonic
time window. A full queue or exhausted rate window drops forwarding while still
allowing an otherwise valid broadcast to be delivered locally.

The simulation uses small deterministic limits to prove enforcement; it does not
select field defaults. OT-010 must separately address priority reservation,
preemption, stale data, emergency rate limits, and UX.

## Three-node simulation evidence

The test topology is:

```text
client A -> controlled repeater B -> destination client C
```

Eight scenarios cover unicast delivery with one exact hop decrement, reflected
loop suppression, zero-TTL and forwarding-permission enforcement, a non-repeater
client role, origin-window seeding and untracked self-alias rejection,
wrong-group rejection without duplicate-window poisoning,
broadcast local-delivery plus forwarding, queue saturation, rate limiting, and
rate-window reset.

## Remaining hardware gate

Only two physical Heltec boards are currently available. OT-009 therefore stays
partial until a third compatible radio can run a bounded A-to-B-to-C experiment
with direct A-to-C conditions characterized, repeat disabled on client roles,
authenticated routing metadata, packet/error/airtime counters, TTL evidence,
duplicate injection, congestion load, cleanup, and recovery behavior.
