# Checked-Time Outbound Service Coordinator v0

Status: **host-tested cooperative ordering only; not an ESP-IDF task, target
driver composition, authenticated packet path, or physical-radio result**

## Purpose

The existing clock contract requires one checked time sample per cooperative
cycle, but the position, priority, delivery, and radio components were still
serviced independently. `OutboundServiceCoordinator` proves the first bounded
runtime composition:

1. read `CheckedMonotonicClock` exactly once;
2. read the latest GPS observation only when position sharing is active;
3. service the position scheduler with that same time;
4. attempt one loss-aware priority-to-delivery handoff;
5. service already accepted delivery work; and
6. service the opaque radio transport.

This order permits one scheduler-produced packet to reach a fake-radio peer in
the same checked cycle. It does not receive frames, process UI input, create
identity or packet metadata, perform cryptography, persist state, or choose a
target task/thread model.

## Clock behavior

| Checked clock result | Downstream behavior |
| --- | --- |
| Success, including equal time | Run the complete cycle with that exact value |
| Temporarily not ready | Defer; no GPS, scheduler, handoff, delivery, or radio call |
| Source failure | Stop position sharing and latch the coordinator closed |
| Rollback | Stop position sharing and latch the coordinator closed |
| Already latched | Refuse without consuming another source sample |

The permanent-fault choice is conservative: retry, expiry, freshness, rate, and
radio work cannot safely advance on an invented or stale timestamp. A new boot
composition is required to recover. Target diagnostics and presentation must
make that state visible; this coordinator does not render it.

## Isolation and privacy

Position sharing stopped means the coordinator does not query GPS. While time
is valid, unavailable GPS or a failed position scheduler does not block existing
priority, delivery, or radio work. A failed/deferred handoff also does not block
delivery entries that were already accepted. Full delivery retains the priority
entry through the existing transactional handoff and may retry it on a later
checked cycle.

The status record contains only clock state, the last successful monotonic
value, and saturating counters. It contains no coordinates, packet bytes,
identity, peer data, keys, addresses, credentials, or free text.

## Host evidence

Ten deterministic groups cover:

1. temporary clock-not-ready with no downstream call;
2. stopped sharing with no GPS read;
3. one-cycle scheduler-to-packet-to-priority-to-delivery fake-radio flow;
4. equal checked timestamps without early resubmission;
5. rollback stop/latch and refusal without consuming later time;
6. source-failure stop/latch without downstream work;
7. missing GPS while existing queued traffic still sends;
8. invalid position policy while existing queued traffic still sends;
9. handoff rejection while already accepted delivery still sends; and
10. full-delivery deferral followed by retained-queue recovery.

The focused executable passes 100/100 repeats. The complete 52-executable
OpenTrail host matrix plus all Python and publication-safety checks pass.

The [runtime-aware position safety overlay](OUTBOUND_POSITION_SAFETY_V0.md) now
maps this coordinator's coherent latched clock state to a critical no-action
frame and rechecks it before applying Start/Stop actions.

## Remaining gates

- define the target task, synchronization, watchdog, and service cadence;
- bind the host-tested safety overlay to exact target frame publication,
  action-time checked-clock sampling, and task/revision ownership;
- add bounded inbound receive/decode/authenticate/replay/delivery processing;
- integrate boot/recovery, protected persistence, entropy, power, and diagnostic
  service ordering;
- replace packet v0 and ephemeral metadata with authenticated/encrypted,
  rollback-safe identity/group/counter composition; and
- bind exact ESP-IDF radio/GPS/time adapters and prove physical timing, queue
  pressure, power, restart, range, and regional behavior.
