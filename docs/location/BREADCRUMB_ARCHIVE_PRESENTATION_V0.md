# Privacy-Safe Breadcrumb Archive Presentation v0

Status: **host-tested semantic adapter; no rendered-UI or target claim**, 2026-08-12.

## Purpose

The optional breadcrumb archive needs to tell an operator whether it is stopped,
capturing, holding records, waiting to retry, full, or closed after a failure.
That status must not expose the private breadcrumb records, coordinates, remote
endpoint, credentials, participants, or receipt details.

`make_breadcrumb_archive_presentation` is a fixed-memory, pure adapter from
copied archive-session, RAM-outbox, and checked-time retry status into the
existing semantic `UiFrame`. It has no capture, upload, discard, export,
deletion, server, or base-radio authority.

The separate
[single-read snapshot boundary](BREADCRUMB_ARCHIVE_SNAPSHOT_V0.md) now gives
target composition one status-source call for the complete three-owner tuple.
Temporary not-ready state produces no new frame; failed or unknown source state
ignores partial output and produces only the generic action-free warning.

## Semantic output

Every presentable result uses the ordinary status screen, contains no action
bindings, and exposes only a bounded queue count from 0 through 16. The adapter
selects one coarse notice:

| Runtime meaning | Notice | Attention |
| --- | --- | --- |
| Capture stopped and queue empty | `archive_stopped` | none |
| Capture active and queue empty | `archive_active` | information |
| One or more records queued | `archive_queued` | information |
| Queued head has a checked-time retry deadline | `archive_upload_waiting` | information |
| Queue or local transport reports full | `archive_queue_full` | warning |
| Retry path latched or copied owners disagree | `archive_upload_failed` | warning |

Failure of this optional path is not presented as a system fault and does not
claim that group radio service is unavailable. There are deliberately no
operator actions in v0: retention, discard, export, deletion, retry override,
and service-recovery policy remain separate decisions.

## Coherence and privacy checks

Before selecting a normal notice, the adapter requires copied owner state to be
self-consistent: session and scheduler activity agree, active-session counters
and next sequence are possible, the outbox count is bounded, retry schedule and
deadline presence agree, and a retry latch carries a non-success error with no
remaining schedule. Incoherent input fails visibly as `archive_upload_failed`.
An impossible queue count is reduced to zero rather than copied into the frame.
A zero frame revision produces no presentable frame.

The result carries no record bytes, latitude/longitude, session/sequence
identifier, monotonic deadline, error detail, endpoint, credential, participant
identity, or server receipt. Renderers choose user-facing wording for the known
semantic notices.

## Host evidence

Ten deterministic scenario groups plus 100/100 focused repeats cover:

1. stopped archive presentation;
2. active empty archive presentation;
3. bounded queued-record count without actions;
4. scheduled retry distinct from an unscheduled queue;
5. full-queue warning precedence;
6. latched optional-path failure without a base system-fault claim;
7. incoherent session containment;
8. incoherent retry-schedule containment;
9. impossible queue-count redaction; and
10. zero-revision refusal.

Every normal frame is also accepted through the checked local-interface
boundary. This is semantic host evidence only; there is no renderer, physical
display/input, ESP-IDF composition, concurrent runtime snapshot, localization,
accessibility, power, server, or real-coordinate evidence.

## Next gates

- Implement the defined snapshot source under an exact target task/lock and
  prove the copy cannot interleave with owner mutation.
- Add renderer wording and verify the full state set on frozen client hardware.
- Decide authorized recovery, discard, retention, export, deletion, and
  lost-device workflows before offering any action.
- Keep server endpoints, credentials, private routes, and participant data out
  of public documentation, logs, and semantic UI frames.
