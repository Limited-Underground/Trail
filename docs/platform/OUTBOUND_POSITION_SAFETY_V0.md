# Outbound Position Safety Overlay v0

Status: **host-tested semantic safety composition; no renderer, action-time
clock adapter, target task, reboot recovery, or physical-input claim**

## Purpose

`OutboundServiceCoordinator` stops the position scheduler when a checked clock
rollback or source failure latches the outbound cycle. Scheduler status alone
would then look ordinarily stopped and could offer Start. The target-facing
position overload closes that gap by requiring both scheduler and outbound
status for presentation and action application.

The original scheduler-only functions remain useful lower-level tests. They are
not sufficient target composition after the outbound coordinator exists.

## Status validation

Before presentation or action, the overlay requires:

- a known latched-clock enum;
- counters no greater than total service calls;
- zero last time when no successful time exists;
- no latched error/failure/refusal while runtime is healthy; and
- a source-failure or rollback cause plus failure evidence when faulted.

A faulted runtime with an active scheduler is contradictory because the runtime
stops sharing when it latches. Unknown or contradictory input fails closed.

## Presentation and action precedence

| Condition | Presentation | Start | Stop |
| --- | --- | --- | --- |
| Healthy runtime | Existing scheduler mapping | Existing typed behavior | Existing immediate behavior |
| Coherent latched clock fault | Critical `position_sharing_failed`, no actions | Rejected before scheduler access | Safe idempotent no-op |
| Incoherent/unknown runtime state | Same safe critical frame | Rejected before scheduler access | Safe immediate stop |
| Revision zero | Not presentable | n/a | n/a |

The action overload rechecks current runtime status after local-interface action
resolution. A Start resolved from an older healthy frame is therefore rejected
if the runtime faults before application. Publishing a newer fault frame also
makes old input stale through the existing checked revision contract.

The overload still receives `now_ms` for a healthy Start. Exact target
composition must supply an action-time checked sample and must not invent or
reuse time while the clock is temporarily not ready. That is a remaining gate,
not a claim of this overlay.

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

The focused executable passes 100/100 repeats. The complete 52-executable
OpenTrail host matrix plus all Python and publication-safety checks pass.

## Remaining gates

- obtain a checked monotonic sample at healthy Start application and defer
  without mutation when time is not ready;
- define the target owner of runtime status snapshots, frame revisions, action
  resolution, and scheduler mutation without races;
- add a distinct coarse system clock/service notice only if renderer/user
  research shows `position_sharing_failed` is insufficient;
- prove boot/restart behavior and prevent in-place recovery of a latched boot
  composition;
- render and physically test the fault on button and touch targets, including
  stale input timing, readability, glove/wet use, and distracted-use review; and
- integrate privacy-safe diagnostics without exposing raw time or location.
