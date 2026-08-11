# Position-Sharing UI Coordinator v0

Status: **host-tested cooperative ownership; no ESP-IDF task/lock, renderer,
physical input, or target concurrency claim**

## Purpose

The position scheduler, checked outbound command boundary, semantic frame
mapper, and revision-bound local interface are individually tested. Target code
still had to choose frame revisions and sequence presentation, input polling,
Start/Stop application, and the resulting refresh.

`PositionSharingUiCoordinator` establishes one bounded cooperative owner for
that sequence. Callers do not supply revisions, copied runtime status,
scheduler references, or action timestamps.

## Cooperative service contract

One `service()` call performs at most one of these paths:

| Condition | Behavior |
| --- | --- |
| No frame has been published | Build current live presentation and publish the owned revision |
| No local input is ready | Return idle without runtime or display mutation |
| Input is stale/invalid | Return typed rejection without a position command or refresh |
| Start/Stop applies | Publish the resulting live state under the next revision |
| Start is temporarily clock-deferred | Keep the current Start frame/revision and permit a later fresh retry |
| Start permanently faults or scheduler rejects | Publish the resulting critical no-action frame |
| Initial display write fails | Keep the revision unused so initial publication can retry |
| Post-action display write fails | Stop sharing and latch the UI coordinator closed |
| Revision space is exhausted | Stop sharing and latch before polling/applying another action |

The post-action refresh is mandatory. An old frame cannot remain application
authority after a successful mutation or permanent rejection. If the new frame
cannot be committed, the coordinator invokes the clock-independent Stop command
and refuses all later input for this boot-local instance.

Temporary clock-not-ready is intentionally different: Start has not touched the
scheduler, so the successfully displayed stopped/Start frame remains truthful
and can resolve a later input event against the same revision.

## Revision ownership

The constructor accepts one nonzero initial boot-local revision. The coordinator
increments it only after the checked local interface commits a display write.
Zero and the maximum 32-bit value are invalid seeds. The maximum value is
reserved so an action is never accepted unless a strictly newer result frame
remains representable.

`OutboundServiceCoordinator::position_status()` exposes one read-only scheduler
snapshot so target callers do not assemble presentation state themselves. This
does not make two snapshots atomic under concurrency.

## Scope and privacy

This is cooperative ordering, not synchronization. One exact target task or
lock must serialize UI service with outbound service and any other component
access. The coordinator does not create an ESP-IDF task, interrupt handler,
mutex, renderer, text, physical input mapping, or retry dialog.

Results and status contain only coarse semantic errors, revisions, state-change
flags, and saturating counters. They contain no coordinates, packet bytes,
messages, peer/device identity, keys, addresses, credentials, or free text.

## Host evidence

Ten deterministic groups cover:

1. owned initial stopped presentation and revision;
2. idle polling without refresh or runtime access;
3. checked-time Start followed by a higher active revision;
4. clock-independent Stop followed by a higher stopped revision;
5. temporary Start deferral retaining the frame for a fresh retry;
6. permanent clock failure producing a critical no-action frame;
7. stale and failed input without position mutation;
8. initial display failure retrying the same unused revision;
9. post-action display failure stopping sharing and latching closed; and
10. revision exhaustion and invalid seed failing closed before input.

The focused executable passes 100/100 repeats. The complete 54-executable
OpenTrail host matrix plus all Python and publication-safety checks pass.

## Remaining gates

- choose and prove the exact ESP-IDF task/lock and service cadence that
  serialize this coordinator with outbound service;
- define a visible, localized retry treatment for temporary clock-not-ready;
- integrate privacy-safe diagnostics for rejected input, display containment,
  revision exhaustion, and permanent outbound failure;
- define boot/reboot/default position-sharing behavior and revision seeding;
- render and measure every state on exact button and touch targets; and
- physically test latency, stale input, display/input failure, glove/wet use,
  distracted use, low power, GPS/radio concurrency, and long sessions.
