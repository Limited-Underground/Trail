# Single-Owner Breadcrumb Archive UI Coordinator v0

Status: **host-tested cooperative UI ownership; no target task, renderer, or
physical-display claim**, 2026-08-12.

## Purpose

The single-read archive snapshot contract prevents a status frame from mixing
different capture, outbox, and retry cycles. A runtime UI owner must also decide
when to spend a new frame revision, when to redraw, and what remains truthful
while a target status copy or display is temporarily unavailable.

`BreadcrumbArchiveUiCoordinator` owns that narrow boundary. One valid
`service()` call performs exactly one `BreadcrumbArchiveSnapshotSource` read,
reduces it through the privacy-safe archive presentation, compares semantic
content with the last successfully presented frame, and presents only a changed
frame through `CheckedLocalInterface`.

It is a cooperative service object. It creates no task, timer, lock, renderer,
physical input, or display driver.

## Service outcomes

| Condition | Result |
| --- | --- |
| First presentable snapshot | Present the owner-supplied initial nonzero revision |
| Changed semantic state | Present one strictly newer revision |
| Unchanged semantic state | Keep the active frame; perform no display write and consume no revision |
| Snapshot `not_ready` | Keep the prior truthful frame, if any; perform no display write and consume no revision |
| Snapshot failed or unknown | Ignore partial data and attempt the generic action-free warning with queue count zero |
| Ready but incoherent tuple | Attempt the existing fail-visible archive warning, never a base system-fault claim |
| Display not ready | Keep the prior frame and retry the same candidate revision after a fresh snapshot on the next call |
| Display failure | Report failure, keep the prior frame and revision, and permit a later fresh-snapshot retry |
| Revision maximum already presented | Unchanged state may remain visible; a semantic change is rejected because no newer revision exists |

Revision zero is invalid configuration and is rejected before source access.
Every other service call takes exactly one new snapshot, including calls made
after the maximum revision has been presented.

## Semantic redraw boundary

Redraw comparison ignores only the frame revision. It includes screen,
attention, notice, every status field (including queue-count validity and
value), action count, and all fixed action slots. Archive frames remain
action-free, but checking the complete fixed frame prevents a future semantic
field from silently bypassing refresh ownership.

The coordinator retains only the last semantic `UiFrame` and bounded status
counters. It does not retain a breadcrumb record, coordinate, endpoint,
credential, participant identity, retry deadline, or remote receipt.

## Authority and failure isolation

The coordinator receives only a snapshot source and a checked local interface.
It has no reference to capture control, scheduler, outbox mutation, uploader,
retry override, storage, server, group transport, or radio service. Therefore
it cannot start or stop archiving, discard or export data, force upload, or
degrade base messaging when optional archive presentation fails.

Display failure cannot itself be made visible on the failed display. It remains
typed in the returned result and fixed status counters so target composition
can expose it through a separate diagnostic path without logging private
snapshot contents.

## Host evidence

Ten deterministic scenario groups plus 100/100 focused repeats cover:

1. initial revision ownership and no input polling;
2. unchanged-state redraw and revision suppression;
3. stopped-to-active-to-queued semantic refresh;
4. temporary snapshot deferral with truthful-frame retention;
5. failed and unknown source redaction;
6. incoherent ready-tuple warning behavior;
7. display-not-ready retry at the same revision after a fresh snapshot;
8. display-failure retention and later recovery;
9. maximum-revision unchanged service and changed-state rejection; and
10. zero-revision rejection before snapshot access.

The complete 97-executable host matrix also passes. This is deterministic host
evidence, not proof of ESP-IDF task serialization, renderer behavior, display
readability, target resource use, or physical failure recovery.

## Next gates

- Bind the host-tested
  [serialized snapshot adapter](BREADCRUMB_ARCHIVE_SNAPSHOT_ADAPTER_V0.md) and
  coordinator call inside one selected target task/lock composition, then prove
  no interleaved status copy.
- Bind a renderer and physical display while preserving the semantic frame and
  revision contract.
- Measure tuple-copy time, service latency, redraw cost, stack/RAM, contention,
  and power on frozen client hardware.
- Verify temporary source/display unavailability and generic warnings on the
  real device without interrupting base radio service.
- Keep capture, upload, discard, retention, export, deletion, and server/account
  policy outside this presentation-only owner.
