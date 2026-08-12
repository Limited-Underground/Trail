# Single-Read Breadcrumb Archive Status Snapshot v0

Status: **host-tested target-facing contract; no target synchronization or
renderer claim**, 2026-08-12.

## Purpose

Archive presentation depends on three independently owned status records: the
opt-in capture session, bounded RAM outbox, and checked-time retry coordinator.
Calling their individual `status()` functions at unrelated moments could
combine values from different cooperative cycles and create a false warning or
misleading queue state.

`BreadcrumbArchiveSnapshotSource` gives target composition one read operation
for the complete status tuple. A target implementation is required to copy all
three owners under one task/lock/serialized-owner boundary. Common code calls
that operation exactly once per capture and never reads an individual owner.

This contract does not itself implement an ESP-IDF task, mutex, critical
section, or atomic primitive. It makes that missing target responsibility
explicit and testable at the integration boundary.

## Fixed status tuple

`BreadcrumbArchiveRuntimeSnapshot` contains only:

- one `BreadcrumbArchiveStatus`;
- one `BreadcrumbArchiveOutboxStatus`; and
- one `BreadcrumbArchiveRetryStatus`.

It contains no queued `OTBA/v0` record bytes or coordinates. The combined tuple
is trivially copyable and is 200 bytes on the current 64-bit host toolchain,
with a compile-time ceiling of 208 bytes. Target size and copy timing remain to
be measured on the selected ESP32 build.

The source returns one state:

| State | Common-code behavior |
| --- | --- |
| `ready` | Validate and reduce the one complete tuple through the existing privacy-safe presentation adapter |
| `not_ready` | Return no frame; the caller may retain its prior truthful presentation |
| `failed` | Ignore all partial output and return a generic action-free archive warning with queue count zero |
| unknown enum | Ignore all partial output and fail visibly with the same redacted warning |

A zero requested frame revision is rejected before source access. A ready but
incoherent tuple follows the existing fail-visible presentation fallback.

## Privacy and authority boundary

Only the existing stopped, active, queued, waiting, full, or failed semantic
frame can leave the capture function. Partial output from not-ready, failed, or
unknown source results is never copied into a frame. The capture function
offers no capture, upload, retry override, discard, retention, export, deletion,
server, credential, or base-radio authority.

The internal status tuple still contains opaque counters and ordering metadata,
so target logs and diagnostics must not dump it. The public semantic frame
continues to expose only the bounded queue count.

## Host evidence

Ten deterministic scenario groups plus 100/100 focused repeats cover:

1. one ready read and stopped presentation;
2. one tuple preserving queued/waiting state;
3. not-ready partial-output containment;
4. failed-source partial-output redaction and visible warning;
5. unknown-state redaction;
6. revision refusal before source access;
7. incoherent ready-tuple containment;
8. exactly one scripted tuple consumed per capture;
9. exhausted-source deferral without prior-data reuse; and
10. fixed fake-source capacity.

Generated frames pass through the checked local-interface boundary. This is
host contract evidence, not proof that an ESP32 adapter actually serializes the
three owners.

## Next gates

- Implement the source inside one selected target task or lock and prove that
  capture/session, upload/retry, and UI work cannot interleave the copy.
- Measure tuple copy time, stack/RAM use, render latency, and contention on the
  frozen client hardware.
- Preserve the previous frame during temporary not-ready state and verify the
  generic warning on source failure through the physical renderer.
- Keep recovery/discard/retention/export/deletion actions separate until their
  authorization and privacy policies are reviewed.
