# Outbound Position Safety Overlay v0

Status: **host-tested semantic safety composition; no renderer, target task,
reboot recovery, or physical-input claim**

## Purpose

`OutboundServiceCoordinator` stops the position scheduler when a checked clock
rollback or source failure latches the outbound cycle. Scheduler status alone
would then look ordinarily stopped and could offer Start. The target-facing
position presentation closes that gap by requiring both scheduler and outbound
status. Target-facing action application now uses the coordinator-owned command
boundary documented in
[Outbound Position Command Authority v0](OUTBOUND_POSITION_COMMAND_V0.md).

The original scheduler-only functions remain useful lower-level tests. They are
not sufficient target composition after the outbound coordinator exists.

## Status validation

Before presentation, the overlay requires:

- a known latched-clock enum;
- service counters no greater than service calls and clock counters no greater
  than all coordinator service/command operations;
- position-command outcome counters no greater than command calls;
- zero last time when no successful time exists;
- no latched error/failure/refusal while runtime is healthy; and
- a source-failure or rollback cause plus failure evidence when faulted.

A faulted runtime with an active scheduler is contradictory because the runtime
stops sharing when it latches. Unknown or contradictory input fails closed.

## Presentation and action precedence

| Condition | Presentation | Start | Stop |
| --- | --- | --- | --- |
| Healthy runtime | Existing scheduler mapping | Coordinator obtains one checked action-time sample | Immediate, no clock read |
| Coherent latched clock fault | Critical `position_sharing_failed`, no actions | Rejected before scheduler/clock-source access | Safe idempotent no-op |
| Incoherent/unknown copied status | Same safe critical frame | Live coordinator remains authoritative | Live coordinator remains authoritative |
| Revision zero | Not presentable | n/a | n/a |

The action overload no longer accepts copied runtime status or `now_ms`. It asks
the live coordinator to apply Start/Stop after local-interface action
resolution. A Start resolved from an older healthy frame is therefore rejected
if the runtime faults before application. Publishing a newer fault frame also
makes old input stale through the existing checked revision contract.

## Privacy boundary

The overlay handles only coarse runtime/scheduler state, revision, and semantic
Start/Stop actions. Frames and results contain no coordinates, packet bytes,
message or peer identity, keys, addresses, credentials, or free text.

## Host evidence

Ten deterministic groups cover:

1. healthy stopped runtime preserving Start;
2. healthy active runtime preserving Stop;
3. real clock-source failure overriding stopped presentation;
4. real rollback overriding stopped presentation;
5. previously resolved Start rejected after a fault;
6. newer fault revision making old input stale;
7. direct Start rejection without scheduler mutation;
8. safe/idempotent Stop after fault;
9. incoherent runtime status failing closed; and
10. revision-zero and unknown-clock-state refusal.

The focused executable passes 100/100 repeats. The complete 55-executable
OpenTrail host matrix plus all Python and publication-safety checks pass.

The [position-sharing UI coordinator](POSITION_SHARING_UI_COORDINATOR_V0.md)
now owns cooperative revision publication, checked input, live action
application, and fail-closed post-action refresh. Its
[semantic observation contract](POSITION_SHARING_UI_OBSERVATION_V0.md) also
publishes a newly latched outbound fault before any queued input is polled.

## Remaining gates

- bind the host-tested cooperative owner to one exact target task/lock without
  races against outbound service;
- add a distinct coarse system clock/service notice only if renderer/user
  research shows `position_sharing_failed` is insufficient;
- prove boot/restart behavior and prevent in-place recovery of a latched boot
  composition;
- render and physically test the fault on button and touch targets, including
  stale input timing, readability, glove/wet use, and distracted-use review; and
- integrate privacy-safe diagnostics without exposing raw time or location.
