# Revision-Safe Generic Quick-Status Menu v0

Status: **host-tested local selection only; no outbound queue, authenticated
packet, radio send, peer receipt, renderer, target firmware, or physical-device
claim**, 2026-08-12.

## Purpose

The generic catalog has four choices while the portable local-interface
boundary permits at most four action slots and every touch/button event must be
bound to the exact displayed revision. A single page with four choices would
leave no safe Back action.

`QuickStatusMenuCoordinator` therefore owns two canonical pages:

| Page | Slot 1 | Slot 2 | Slot 3 | Slot 4 |
| --- | --- | --- | --- | --- |
| First | I'm OK | Need assistance | Next | Back |
| Second | Anyone online? | Available to help | Previous | Back |

The C++ actions carry semantic enums rather than text. A target renderer owns
localized labels, visual layout, focus/order, accessibility, and physical input
mapping.

## Behavior

- An external parent activates the menu with a nonzero newer revision and one
  copied identity-free status summary.
- A selection returns one typed `QuickStatusKind` plus the minimum newer parent
  revision, then the menu becomes inactive.
- Back returns no selection and the same newer-parent revision requirement.
- Next/Previous presents the other canonical page at exactly one newer
  revision.
- Initial display-not-ready leaves the menu inactive so activation can retry
  the same revision.
- Transition display-not-ready retains the pending page/revision and retries it
  without polling a second input event.
- Stale, invalid-slot, disabled, and wrong-gesture events produce no selection.
  Input failure, display failure, impossible action, or revision exhaustion
  latches this local menu closed.

The owner is non-copyable and non-movable. It holds no radio, queue, packet,
delivery, group, identity, storage, GPS, archive, server, or critical-alert
reference.

## Meaning of a selection

`selection_requested` means only that the checked local interface resolved the
displayed semantic action. It does **not** mean encoded, queued, sent, relayed,
received, acknowledged, delivered, or acted upon. The parent application must
later connect the typed request to an authenticated outbound path and present
each state truthfully.

`Need assistance` remains a generic request, not a guaranteed rescue service
and not a replacement for the separate held critical-alert workflow.

## Host evidence

Ten deterministic menu groups plus 100/100 focused repeats cover:

1. exact first-page shape and copied status summary;
2. both first-page typed selections;
3. both second-page typed selections;
4. forward and previous navigation revision ownership;
5. Back from both pages and exact parent revision floors;
6. stale and invalid-slot rejection without a selection;
7. initial display deferral and same-revision retry;
8. transition deferral without a second input poll;
9. local input/display failure containment; and
10. invalid activation/summary plus revision-exhaustion fail-close.

The underlying local-interface suite now has thirteen groups, including strict
canonical page validation and rejection of the older ambiguous generic-submit
action. Both suites pass 100/100 focused repeats. The complete 108-executable
matrix and Python publication checks pass.

## Next gates

- Compose through the host-tested
  [parent-page handoff](QUICK_STATUS_PARENT_PAGE_COORDINATOR_V0.md), then define
  the broader home/messages/alerts/position/archive shell above it.
- Encode and admit a selection only through an authenticated packet-v1
  quick-status path with bounded priority, expiry, retry, deduplication, and
  acknowledgement policy.
- Present selected/queued/sent/delivered/failed states without conflation.
- Bind exact target renderers and physical controls; verify font, contrast,
  glove/wet use, stale touch behavior, latency, and recovery on selected units.
