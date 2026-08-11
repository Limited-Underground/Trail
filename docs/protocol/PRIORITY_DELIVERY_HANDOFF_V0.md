# OpenTrail Priority-to-Delivery Handoff v0

Status: **host-tested fixed-memory composition only; packet v0 remains
unauthenticated and prohibited for real coordinates or field transmission**

## Purpose

Priority admission and delivery previously had separate deterministic tests,
but destructively taking an entry before delivery accepted it could lose a
message under pressure. This coordinator proves a bounded handoff:

1. purge expired traffic and peek at the strict-priority/FIFO head;
2. retain that exact priority entry while asking `DeliveryController` to accept
   a copy;
3. commit the source entry only after delivery admission succeeds; and
4. preserve the original deadline when the delivery policy is constructed.

It does not authenticate, encrypt, persist, acknowledge, forward, schedule a
target task, or call a physical radio driver.

## Ownership contract

One target task must own the priority queue, delivery controller, and handoff
operation. No other owner may mutate either queue between peek, delivery
admission, and commit. An impossible commit mismatch latches the handoff faulted
instead of retrying and risking duplicate admission.

The coordinator and its results use fixed-size, trivially copyable state. It
does not allocate memory, retain packet contents outside the existing queues,
or expose identifiers beyond the existing nonzero message ID needed for typed
local correlation.

## Retention and expiry

| Condition | Handoff result | Priority entry |
| --- | --- | --- |
| No eligible entry | `idle` | none |
| Delivery accepts | `transferred` | committed/removed |
| Delivery queue full | `deferred` | retained |
| Duplicate ID, bad frame, or other delivery rejection | `failed` | retained |
| Remaining lifetime cannot be represented safely | `failed` | retained |
| Queue commit mismatch after acceptance | latched `failed` | retained; service stops |

The delivery expiry is the smaller of the experimental class expiry and the
priority entry's remaining lifetime. Handoff can therefore shorten a deadline
but cannot extend it. An entry expiring at the current service tick is purged by
the priority queue before handoff and produces the existing expiry event.

## Host evidence

Ten deterministic groups cover:

1. exact peek/commit selection and wrong-ID refusal;
2. empty-queue idle behavior;
3. critical-before-position transfer;
4. delivery-capacity deferral, retention, and later success;
5. delivery MTU rejection without source loss;
6. duplicate delivery ID rejection without source loss;
7. original-expiry preservation across handoff;
8. exact-boundary priority expiry before handoff;
9. refusal to narrow an unsafe remaining-time span; and
10. scheduler to position packet admission to priority queue to delivery
    controller to fake-radio peer, with exact packet and position decode.

The focused executable passes 100/100 repeats. The complete 58-executable
OpenTrail host matrix plus all Python and publication-safety checks pass.

The host-tested [checked-time outbound coordinator](../platform/OUTBOUND_SERVICE_COORDINATOR_V0.md)
now proves one single-owner cycle through scheduling, this handoff, delivery,
and fake-radio service using one successful monotonic sample.

## Remaining gates

- replace packet v0 with an authenticated/encrypted packet version and bind its
  priority to authenticated message semantics;
- bind metadata to the selected group, epoch, identity, and rollback-safe
  outbound counter;
- replace the host single-owner/checked-clock proof with exact target task,
  synchronization, watchdog, and concrete clock-adapter evidence;
- select lifetimes, retry timing, capacity, and congestion policy from measured
  four-client evidence and regional review;
- bind one exact direct-radio adapter and prove physical queue pressure, expiry,
  power, airtime, range, and recovery; and
- render queued, deferred, failed, sent-unconfirmed, confirmed, and expired
  states without implying guaranteed delivery.
