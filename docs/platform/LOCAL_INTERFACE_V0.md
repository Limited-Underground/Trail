# Local display and input boundary v0

Status: **host-tested semantic contract; no renderer or physical-display claim**

## Purpose

The first four-person OpenTrail pilot requires each standalone client to have a
locally readable display and local controls for quick status and critical-alert
actions. The same application behavior should remain possible on a small OLED
with buttons, a touch display, or a later accessible input adapter without
making screen dimensions, color, touch, or a specific UI framework part of the
application state machine.

This contract separates semantic frames and normalized actions from pixels,
fonts, coordinates, GPIOs, touch controllers, localization, and rendering.

## Capabilities

Target composition supplies one explicit `DisplayCapabilities` record:

- nonzero pixel width and height;
- color depth from 1 through 32 bits;
- one through four base action slots;
- touch and/or button input; and
- whether a hold gesture is available.

No resolution, color depth, or touch mode is declared "supported" merely by
passing host validation. A target adapter still has to render the required
states legibly and prove its input mapping on physical hardware.

The four-action limit is the minimal portable-client surface, not a claim that
larger displays cannot show more information. Additional pages or target-local
presentation can exist without expanding the base application contract.

## Semantic frame

`UiFrame` is fixed-size and contains:

- a nonzero, strictly increasing boot-local revision;
- one screen role: home, status, quick-status menu, critical confirmation,
  archive controls, archive confirmation, or system fault;
- attention and notice enums;
- radio, position, and power indicator states;
- optional peer count and bounded unread count; and
- optional bounded breadcrumb-archive queue count from zero through 16;
- up to four ordered semantic action bindings.

It contains no coordinates, free-form labels, peer identity, message text,
radio address, key handle, credential, or other private identifier. Renderers
choose localized wording and layout for known enum values.

Known notices now include coarse update-recovery tokens for trial active,
transition rejected, reboot required, cleanup required, safe mode, service
required, and reboot reconciliation. The separate
[recovery-presentation adapter](../update/UPDATE_RECOVERY_PRESENTATION_V0.md)
selects these tokens from validated `OTRD0/v0`; the base UI boundary still
assigns no update, cleanup, reboot, reset, or service authority to a notice.

Known notices also include stopped, active, waiting-for-fix, deferred, and
failed position sharing. `start_position_sharing` and
`stop_position_sharing` are semantic requests, not radio commands. The separate
[position-sharing control adapter](POSITION_SHARING_CONTROL_V0.md) is the only
host-tested mapping from these requests to the scheduler: start arms it without
submitting a payload, while stop disables it immediately. A renderer neither
gains coordinate access nor acquires radio, emergency, or update authority.
The [position-sharing UI coordinator](POSITION_SHARING_UI_COORDINATOR_V0.md)
now owns the host-tested revision/poll/action/refresh sequence around that
adapter; it does not provide an ESP-IDF task, lock, or physical renderer.

Known notices also include archive stopped, active, queued, upload waiting,
queue full, upload failed, start confirmation, and stop confirmation. The separate
[breadcrumb-archive presentation adapter](../location/BREADCRUMB_ARCHIVE_PRESENTATION_V0.md)
reduces copied session/outbox/retry status to these coordinate-free tokens and
a bounded queue count. Archive presentation has no capture, upload, discard,
export, deletion, server, or base-radio authority. The separate
[local archive-consent boundary](../location/BREADCRUMB_ARCHIVE_LOCAL_CONSENT_V0.md)
adds canonical confirmation-only actions without granting the renderer,
radio, server, or automatic recovery path archive authority.

The archive-controls screen is also canonical. It contains exactly one
snapshot-derived request plus Cancel: coherent stopped state may request Start;
coherent active state may request Stop; failed, unknown, or incoherent state may
request only Stop. `request_archive_start` and `request_archive_stop` merely
open the corresponding confirmation. They cannot execute an archive operation.
The [complete archive workflow](../location/BREADCRUMB_ARCHIVE_WORKFLOW_COORDINATOR_V0.md)
owns the control/confirmation/cancel/action/refresh revision sequence.
`open_archive_controls` is a parent-navigation request rather than an archive
operation; re-entry accepts it only after the checked interface resolves it
against the exact active parent frame.

Unused action slots must remain canonical zero/disabled values. Active actions
must be known and unique. A frame is committed only after the display sink
reports complete success. Not-ready or failed presentation does not advance the
active revision, so the same revision may be retried safely.

## Input binding

The target input adapter maps its physical button, encoder, touch target, or
accessible control to an action-slot number from the frame it actually showed.
Application code receives no raw coordinate or GPIO identity.

Every `LocalInputEvent` carries the exact frame revision. The checked boundary
rejects an event when:

- no frame has been successfully presented;
- the input source is not ready or failed;
- the revision is stale or from another frame;
- the slot is outside the active action count or disabled; or
- the gesture is unknown or inappropriate for that action.

This prevents a delayed touch/button event from activating the same screen
location after the application has changed its meaning.

## Critical-alert boundary

`open_critical_confirmation` only requests the dedicated confirmation screen;
it does not send an alert. That confirmation frame is canonical:

1. critical attention;
2. exactly two enabled actions in order: `confirm_critical_alert`, then
   `cancel`; and
3. a target capability that supports a hold gesture.

Only a hold on the confirmation slot resolves `confirm_critical_alert`. A tap,
stale event, disabled slot, different screen, or system-fault send action fails
closed. Resolution is still an application request, not proof of radio delivery
or emergency response. Delivery success/failure must remain separately visible.

## Breadcrumb-archive consent boundary

Archive control has a different canonical screen from critical alerts. Start
uses informational attention, `archive_start_confirmation`, and exactly two
enabled actions in order: `confirm_archive_start`, then `cancel`. The target
must support hold, and only a hold resolves Start. Stop uses informational
attention, `archive_stop_confirmation`, and exactly two enabled actions in
order: `stop_archive`, then `cancel`; Stop resolves on activate/tap so privacy
shutdown is not delayed unnecessarily.

`confirm_archive_start` and `stop_archive` are invalid on every other screen,
including home, status, critical confirmation, and system fault. Resolution is
only a revision-bound local request. The renderer has no archive runtime,
session allocation, upload, server, radio, discard, export, or deletion
authority.

## Host evidence

Twelve deterministic scenario groups cover:

1. invalid capabilities rejected before display I/O;
2. atomic presentation of a complete semantic frame;
3. not-ready retry and sink-failure preservation;
4. strict revisions, enums, capacities, canonical slots, and uniqueness;
5. action-slot and hold-capability limits;
6. the exact critical-confirmation shape;
7. ordinary action resolution against the current frame;
8. stale, out-of-range, and disabled input rejection;
9. hold-only critical confirmation;
10. not-ready, failed, unknown, and pre-frame input behavior;
11. system-fault action restrictions; and
12. bounded FIFO/script capacity and ordering in test support.

The separate ten-group archive-consent suite covers canonical archive frames,
wrong-screen rejection, hold-only Start, immediate Stop, stale/cancel paths,
checked-time/session sequencing, and absence of any radio/server action source.

The predictable display and input fakes remain under `test_support` only.

## Remaining target gates

- Implement separate Heltec, Wio, and any future touch-display adapters only
  after their exact board/controller/pin/input identities are known.
- Define fonts, localization, contrast, daylight/night readability, minimum
  target size, glove/wet use, accessible input, and distracted-driving policy.
- Prove every notice and enabled/disabled/confirmation state on real hardware,
  including display/input failure, reboot, USB recovery, low power, and radio/
  GNSS concurrency.
- Measure render and input latency, CPU/RAM/flash use, power draw, burn-in risk,
  temperature behavior, and long-session stability.
- Connect resolved application requests to the tested priority/delivery path
  without turning local acceptance into false delivery success.
- Freeze one exact four-unit client model and firmware before changing the
  four-person pilot from `draft_blocked` to `ready`.
