# Bounded Breadcrumb Archive Outbox v0

Status: host-tested RAM boundary, 2026-08-12. This is not persistent client
storage, a network protocol, or a remote archive service.

## Purpose

The optional breadcrumb path must tolerate an unavailable uploader without
dropping or overwriting private records and without blocking base radio
operation. `BreadcrumbArchiveOutbox` is the fixed-memory transport used by the
host-tested archive session. `BreadcrumbArchiveUploader` independently moves
the FIFO head toward an injected remote-transport contract.

The outbox and uploader have no radio authority. Queue pressure can defer the
next archive record, but it cannot stop messaging, alerts, local display, USB
recovery, or ordinary group position behavior.

## RAM outbox rules

- Capacity is exactly 16 canonical `OTBA/v0` records: 896 record bytes plus
  fixed bookkeeping. Capacity is a host boundary, not a final retention target.
- Every input is decoded and fully validated before queue mutation.
- The first record must have sequence 1. Within a session, sequence must advance
  by exactly one. A newer session ID must be strictly greater and begin at 1.
- Reused session/sequence pairs are rejected. The outbox retains only the last
  session/sequence metadata after a record leaves; it does not retain a second
  full coordinate-bearing copy for duplicate comparison.
- Full means full: no oldest-record overwrite, preemption, automatic expiry, or
  silent eviction occurs. The archive scheduler therefore retries the same
  sequence after capacity becomes available.
- `peek()` never removes. `commit_front()` removes only the exact expected FIFO
  session/sequence pair.
- `discard_all()` is an explicit lower-level action. It zeroes the occupied RAM
  entries and retains ordering metadata so the discarded last record cannot be
  mistaken for a new acceptance. Ordinary C++ zeroing is not a certified secure
  erase. Target authorization and confirmation for discard do not yet exist.

The queue is RAM-only. Reset, power loss, memory corruption, or process exit can
lose every uncommitted record. No durability or post-restart recovery is
claimed.

## Durable-ack handoff

The uploader attempts at most the current FIFO head per cooperative service
call. The injected remote result has four meanings:

| Result | Local action |
| --- | --- |
| `durable_ack` | Commit the exact FIFO head |
| `not_ready` | Retain and expose deferred state |
| `rejected` | Retain and expose remote rejection |
| `failed` | Retain and expose transport failure |

`durable_ack` is a strict future adapter contract: it must mean the remote
system has acknowledged the exact record across its declared durability
boundary. The host fake can exercise that result but does not prove any real
server honors it.

If the remote reports durable acknowledgement but the exact local FIFO commit
cannot be completed, the uploader latches closed. It will not blindly resend or
pretend the local/remote state is reconciled. A future boot/service workflow
must resolve that ambiguity.

There is no automatic retry cadence, backoff, connectivity detection, TLS,
authentication, authorization, request format, endpoint, provider, account,
receipt verification, or remote read/delete API in this component.

## Privacy boundary

Queued records contain precise coordinates and are private. No real queue dump,
route, participant identity, device identity, credential, endpoint, or upload
receipt may be committed publicly. Only privacy-reviewed aggregate counters may
become public evidence.

The outbox stores canonical records only. Its status exposes queue/counter and
last session/sequence metadata, not coordinates or record bytes. A target UI
must avoid turning those opaque values into public participant identifiers.

## Evidence

Ten deterministic host groups plus 100/100 focused repeats cover:

1. FIFO peek and exact commit;
2. invalid/corrupt/noncanonical refusal without mutation;
3. first-record, sequence, session, and duplicate ordering;
4. full-capacity no-overwrite and same-record retry after space opens;
5. archive-session-to-outbox composition without radio control;
6. removal only after durable acknowledgement;
7. identical-head retry after not-ready;
8. retention after remote rejection and failure;
9. fail-closed latching on post-ack local commit mismatch; and
10. explicit discard, retained ordering history, and empty-uploader behavior.

This is host-only evidence. No ESP-IDF task/lock, persistent queue, checked-time
uploader, protected storage, physical reset/power-loss test, network adapter,
server, account, cryptography, participant authority, retention/export/deletion
workflow, target UI, or on-device result exists.

## Next gates

Before real coordinates can use this path: define protected authenticated
transport and exact durable receipt semantics; add checked-time retry/backoff;
decide whether a protected persistent outbox is justified; bind participant and
group authority; implement visible queue/failure/discard controls; define
retention/export/deletion and lost-device access policy; and validate target
memory, concurrency, interruption, and privacy behavior. The archive remains an
optional aid, never a rescue guarantee.
