# Complete Local Breadcrumb Archive Workflow Coordinator v0

Status: **host-tested cooperative workflow; no parent navigation, renderer,
physical input, ESP-IDF binding, or on-device claim**, 2026-08-12.

## Purpose

Archive status presentation, local confirmation, consent application, and
post-action refresh were separately host-tested boundaries. A target must not
join them with ad hoc frame revisions or expose the private runtime through a
second control path.

`BreadcrumbArchiveWorkflowCoordinator` owns one complete local sequence after
a parent shell enters the optional archive page. It privately composes the
existing consent controller with the same serialized runtime that supplies its
snapshots. It creates no task, renderer, physical input adapter, network input,
server command, or automatic Start path.

## Local sequence

| State | Fresh evidence | Local choices | Result |
| --- | --- | --- | --- |
| Controls, coherently stopped | One serialized snapshot | Request Start; Cancel | Start request opens a newer canonical hold confirmation |
| Controls, coherently active | One serialized snapshot | Request Stop; Cancel | Stop request opens a newer canonical immediate confirmation |
| Controls, failed/unknown/incoherent | Redacted warning | Request Stop; Cancel | Start is never offered while truth is uncertain |
| Start confirmation | Exact active revision | Hold Start; Cancel | Confirmed Start passes through checked-time consent |
| Stop confirmation | Exact active revision | Stop; Cancel | Stop is clock-independent and passes through serialized runtime ownership |
| Post action | New serialized snapshot | None until refreshed | A newer control frame must show the resulting state |

The controls page has exactly one snapshot-derived request plus Cancel.
`request_archive_start` and `request_archive_stop` only enter confirmation;
neither action mutates the runtime. Final Start and Stop actions remain invalid
on every screen except their canonical archive confirmation.

The parent application owns initial entry to this optional page and must replace
its frame after `exit_requested`. Re-entry into the same boot-lived coordinator
requires `open_archive_controls` already resolved against the exact active
parent-frame revision. The coordinator resumes at the next revision without
resetting its cursor inside the caller-supplied durable session-ID range. The
range must come from the
[restart-safe lease store](../persistence/BREADCRUMB_ARCHIVE_SESSION_LEASE_STORE_V0.md)
before construction. The workflow refuses Start after the inclusive final ID;
it does not allocate, wrap, or reuse a range. A complete parent navigation
owner is not yet implemented. Merely scheduling or entering this workflow
cannot Start an archive because a second revision-matched local hold is still
required.

## Ordering and deferral

While the controls page is active, every service call takes a fresh serialized
snapshot before polling input. A semantic change replaces the frame with a new
revision and returns without consuming the queued input. Snapshot contention
therefore defers before any control event can resolve against stale state.

Runtime lock contention during confirmed Start or Stop returns
`action_deferred`, retains the confirmation, and requires a new explicit local
action. Start contention does not consume the candidate session ID. Cancel
performs no consent/runtime mutation and returns to fresh controls only after a
new snapshot and display commit succeed.

## Privacy-safe failure behavior

One revision is always reserved after an actionable frame so the workflow can
show the result. If the boot-local revision sequence can no longer do that, the
coordinator attempts serialized Stop and latches.

After a successful Start, failure to obtain or display the required truthful
post-action controls also attempts serialized Stop and latches. The returned
result records whether containment was attempted and whether it completed. A
failed Stop refresh latches the UI workflow but does not restart capture.

Display failure cannot make itself visible on that display. Typed outcomes and
bounded counters are available to a separate redacted diagnostic path. No
failure in this optional workflow changes base messaging or radio service.

## Authority boundary

The workflow can only snapshot, locally confirm Start/Stop, and issue the
privacy containment Stop described above. It has no capture-position trigger,
upload scheduler override, outbox discard, export, deletion, retention,
endpoint, account, remote receipt, group packet, or radio-service authority.

Common-code ownership is not target proof. ESP32 composition must keep the
serialized runtime object inaccessible to radio/server/automatic or duplicate
UI call paths and serialize this workflow with capture/upload service calls.

## Host evidence

Fourteen deterministic scenario groups plus 100/100 focused repeats cover:

1. initial snapshot-backed controls and no premature input poll;
2. exact-revision Start entry and hold enforcement;
3. Cancel returning to fresh controls without runtime mutation;
4. confirmed Start, active refresh, and session allocation;
5. immediate clock-independent Stop and stopped refresh;
6. stale control input reaching no consent operation;
7. snapshot contention deferring before input polling;
8. failed/unknown runtime state offering Stop but never Start;
9. live semantic refresh occurring before queued input can resolve;
10. runtime contention retaining confirmation and session candidate;
11. failed post-Start display refresh stopping and latching;
12. controls Cancel producing one exit request without further polling;
13. exact-parent-revision re-entry preserving the session allocator; and
14. invalid/exhausted revision containment.

The complete 105-executable host matrix and Python evidence checks pass. This
is deterministic host evidence, not rendered wording/layout, physical
button/touch behavior, target concurrency, memory/latency/power measurement,
target lease/workflow composition, server behavior, or field proof.

## Next gates

- Add a parent local navigation owner that enters/exits this optional page
  without making archive service a base-client requirement.
- Preserve the host-tested
  [lease-to-workflow bootstrap](BREADCRUMB_ARCHIVE_WORKFLOW_BOOTSTRAP_V0.md)
  so allocation/recovery failure leaves Start unavailable without constructing
  this workflow or affecting base messaging.
- Bind one reviewed ESP-IDF synchronization primitive and prove all archive
  capture/upload/snapshot/control calls use it under concurrent stress.
- Render and physically evaluate consent wording, active-state visibility,
  touch/button hold behavior, accessibility, and distracted-use constraints on
  frozen client hardware.
