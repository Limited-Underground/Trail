# Outbound Position Command Authority v0

Status: **host-tested action-time authority; no target task, concrete clock
adapter, renderer, reboot recovery, or physical-input claim**

## Purpose

A healthy position Start must be based on time read when the action is applied.
Accepting a caller-supplied timestamp permits a target to accidentally reuse an
old value or invent one while the clock is temporarily unavailable.

`OutboundServiceCoordinator` now owns the target-facing Start and Stop command
boundary. The scheduler-only API remains a lower-level component seam, but
target UI composition does not pass `now_ms`.

## Command contract

| Command and condition | Clock access | Scheduler behavior | Typed result |
| --- | --- | --- | --- |
| Start, checked time succeeds | Exactly one read | Start with that exact value | Applied; state change reported |
| Start, clock temporarily not ready | Exactly one read | No scheduler access | Deferred/retryable |
| Start, source failure or rollback | Exactly one read | Stop and latch runtime closed | Permanent clock fault |
| Start, runtime already latched | No read | No Start attempt | Latched clock fault |
| Start, scheduler rejects policy/time state | Successful checked read | Preserve scheduler rejection | Typed scheduler failure |
| Stop in any runtime state | No read | Stop immediately | Applied; idempotent |

A successful Start arms position scheduling only. It does not read GPS, call
scheduler service, encode a position, submit a payload, hand off a packet, or
service delivery/radio work. A later successful outbound service cycle owns
those operations.

Temporary not-ready is not converted into a permanent fault and does not mutate
the scheduler. The caller may present a retryable result and apply a later Start
as a new command, which must obtain a new checked sample.

## Status and privacy

The outbound status adds saturating counts for command calls, applied commands,
temporary deferrals, and failures. Clock deferral/failure counters cover both
service-cycle and command-time reads, so presentation validation compares them
against total coordinator operations rather than service calls alone.

Command results and status contain only coarse enum outcomes, boot-local
monotonic time, state-change indication, and counters. They contain no
coordinates, packet bytes, message/peer identity, keys, addresses, credentials,
or free text.

## Target-facing adapter

The semantic position-control overload now accepts the coordinator plus one
resolved UI action. It maps temporary clock unavailability to
`outbound_not_ready`, permanent clock state to `outbound_faulted`, and scheduler
refusal to `scheduler_rejected`. Unrelated actions are rejected without calling
the coordinator.

Stop remains available after a permanent fault and is clock-independent. Start
after a previously displayed healthy frame is re-evaluated by the live
coordinator, so a fault between display and action application still fails
closed.

## Host evidence

Ten deterministic groups cover:

1. coordinator-owned checked time and no submission during Start;
2. temporary not-ready followed by a fresh successful Start;
3. command-time source failure stopping and latching active sharing;
4. command-time rollback stopping and latching active sharing;
5. Start after latch consuming no further clock sample;
6. immediate Stop without clock access;
7. repeated Start/Stop idempotence and status accounting;
8. typed invalid-scheduler-policy rejection;
9. target adapter mapping of retry, fault, and success; and
10. checked local-interface resolution followed by an exact action-time sample.

The command and safety executables each pass 100/100 focused repeats. The
complete 56-executable OpenTrail host matrix plus all Python and
publication-safety checks pass.

The [position-sharing UI coordinator](POSITION_SHARING_UI_COORDINATOR_V0.md)
now supplies one cooperative owner for revision publication, checked input,
live command application, and post-action refresh.

## Remaining gates

- bind the host-tested UI owner to one exact target task/lock shared with
  outbound service cycles;
- define visible retry behavior for a temporary not-ready Start without
  manufacturing a successful state;
- bind the checked clock to one exact ESP-IDF source and prove cold-start,
  deep-sleep, brownout, wrap, and concurrency behavior;
- add privacy-safe command diagnostics without raw time or location;
- prove reboot/default privacy behavior after a permanent boot-local fault; and
- render and physically test Start/Stop/fault/retry behavior on button and touch
  targets, including stale-input, glove/wet, and distracted-use cases.
