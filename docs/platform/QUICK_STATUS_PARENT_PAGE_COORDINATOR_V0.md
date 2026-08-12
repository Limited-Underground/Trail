# Quick-Status Parent Page and Restored Selection Handoff v0

Status: **host-tested narrow parent/menu handoff; no complete application shell,
outbound queue, authenticated packet, radio send, peer receipt, renderer,
target firmware, or physical-device claim**, 2026-08-12.

## Purpose

The local chooser returns a typed request and a minimum newer parent revision,
but it does not own the page that opened it. This coordinator supplies one
reviewable parent without claiming the full home/messages/alerts/position/
archive shell.

The parent is a semantic `status` frame with exactly two actions:

1. `open_quick_status_menu`; and
2. `cancel` (Back to a future broader shell).

It copies the same identity-free radio/position/power/peer/unread summary into
the nested menu so page changes do not invent a different system status.

## Handoff behavior

1. External activation presents the parent at one valid newer revision.
2. Exact local Open activates the first menu page at parent revision plus one.
3. Nested Next/Previous/Back/selection remains owned by the menu coordinator.
4. Nested Back restores the parent at the menu's minimum newer revision and
   returns no selection.
5. A typed choice is retained privately while the parent is restored. Only a
   successful parent presentation returns `selection_requested` to the outer
   application.
6. Parent Back returns `exit_requested` without a selection.

If menu presentation is temporarily not ready, the already displayed parent
remains active and the user may try Open again. If parent restoration is
temporarily not ready, the coordinator retains the exact pending revision and
typed choice, polls no second input, and surfaces nothing until restoration
succeeds.

Invalid activation, stale/invalid input, input/display failure, nested menu
failure, or revision exhaustion cannot manufacture a choice. The owner is
non-copyable and non-movable and has no radio, queue, packet, delivery,
identity, storage, GPS, archive, server, or critical-alert reference.

## Truthful outcome boundary

`selection_requested` means the displayed local choice was resolved and the
parent frame was restored. It does **not** mean encoded, queued, sent, relayed,
received, acknowledged, delivered, or acted upon. A later outbound owner must
surface those states separately.

`Need assistance` remains a generic request, not guaranteed rescue and not a
replacement for the held critical-alert workflow.

## Host evidence

Ten deterministic groups plus 100/100 focused repeats cover:

1. the exact Quick status/Back parent frame;
2. idle parent polling without a selection;
3. exact newer first-menu entry;
4. nested Back restoring the parent without a selection;
5. first-page typed selection after successful restoration;
6. second-page typed selection after exact page navigation;
7. deferred restoration withholding the selection and avoiding another poll;
8. deferred menu opening retaining a retryable parent;
9. parent Back, inactivity, and newer reactivation; and
10. invalid/stale/failed input producing no selection.

Compile-time checks keep the owner non-copyable/non-movable and its status at
or below 64 host bytes. The complete 108-executable matrix and Python
publication checks pass.

## Next gates

- Define one broader base-client shell that owns the home/status revision and
  intentionally composes this page with position, critical alerts, archive,
  messages, and system/recovery status.
- Connect typed selections only to an authenticated packet-v1 admission path
  with bounded priority, expiry, replay, deduplication, retry, and ACK policy.
- Present selected/queued/sent/delivered/failed states separately.
- Bind exact target renderers/inputs and physically verify page discovery,
  stale touch behavior, Back, deferral/failure, latency, and recovery.
