# Opt-in Breadcrumb Archive Session v0

Status: host-tested local boundary, 2026-08-12. This is not a server, account,
remote-recovery service, or production target implementation.

## Purpose

The optional archive path needs a narrow client-side boundary before any
provider, account, retention system, or network transport is selected. The
`BreadcrumbArchiveSession` composes the existing position scheduler with an
injected nonblocking transport. It proves that capture is locally explicit,
bounded, minimized, and unable to become a dependency of the base radio path.

The session is separate from ordinary group position broadcasting. Starting or
stopping archive capture does not start, stop, acknowledge, or otherwise control
the radio. A target composition must present the archive state independently.

## Local session rules

- Capture starts only through an explicit `start(session_id, now_ms)` call.
- Session ID zero is invalid. During one object lifetime, each new session ID
  must be strictly greater than the prior ID.
- Starting an active session is rejected. `stop()` is immediate and idempotent.
- The session submits only the existing canonical 16-byte `current` position
  payload. Missing, stale, malformed, or invalid fixes do not reach transport.
- Delayed service coalesces instead of producing a stale catch-up queue.
- Sequence starts at 1 and advances only after local transport acceptance.
  Retryable not-ready/full results retain the same sequence.
- Monotonic rollback, time-horizon exhaustion, an invalid policy, or permanent
  sink failure stops or refuses the session visibly.

The same-boot monotonic session rule prevents accidental reuse within this
object. It is not restart-safe allocation, stable identity, authorization, or
rollback protection. A future target must obtain session IDs from an approved
boot/session authority before this can carry private coordinates.

## Canonical `OTBA/v0` record

Every accepted record is exactly 56 bytes in little-endian form:

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII magic `OTBA` |
| 4 | 1 | Version `0` |
| 5 | 1 | Record type `1` (position) |
| 6 | 2 | Header length `32` |
| 8 | 8 | Opaque nonzero session ID |
| 16 | 4 | Nonzero session-local sequence |
| 20 | 4 | Canonical zero reserve |
| 24 | 8 | Boot-local capture time in milliseconds |
| 32 | 16 | Canonical current-position payload |
| 48 | 4 | Canonical zero reserve |
| 52 | 4 | CRC-32 over bytes 0-51 |

Encoding is atomic: rejected input does not partially alter the caller's
output. Decoding requires the exact length, magic, version, type, header length,
zero reserves, checksum, nonzero session/sequence, and a canonical current
position. The caller's output changes only after complete validation.

CRC-32 detects accidental corruption only. It provides no authentication,
confidentiality, authorization, origin proof, or rollback protection.

## Data minimization

The record contains no stable device identifier, participant name, group name,
route label, message text, URL, provider, credential, account, retention rule,
or remote retrieval authority. The session ID is an opaque boot/session value,
not a public user or device identity.

Precise coordinates are private data. Real records must never be committed to
the public repository; only privacy-reviewed aggregate evidence may be public.

## Acceptance meaning

`BreadcrumbArchiveTransportError::none` means only that the injected local
transport copied or durably accepted the exact record according to its own
contract. It does **not** prove delivery to, acknowledgement by, persistence
on, or later retrieval from a remote server.

If a future device dies, only records that a separately implemented and
verified remote path successfully retained could be recoverable. The archive
is not a rescue guarantee and cannot recover a position that GNSS never
produced or transport never accepted.

## Evidence

Ten deterministic host groups plus 100/100 focused repeats cover:

1. exact codec shape and round trip;
2. atomic rejection of invalid/noncanonical/corrupt records;
3. explicit start/stop, same-boot reuse refusal, and increasing session IDs;
4. minimized first submission;
5. cadence and delayed-service coalescing;
6. no-fix suppression;
7. backpressure retry with the same sequence;
8. visible permanent transport failure;
9. clock failure and automatic session end; and
10. invalid policy and time-horizon refusal.

This evidence is host-only. No ESP-IDF task, GNSS adapter, network stack,
encryption/authentication, remote service, storage backend, account/access
model, retention/export/deletion workflow, physical interruption test, or
on-device capture exists.

The separate [bounded RAM outbox](BREADCRUMB_ARCHIVE_OUTBOX_V0.md) now provides
the first concrete transport composition. It holds 16 exact records without
overwrite, preserves FIFO data under remote pressure, and removes one only
after an explicit durable-ack result. It is volatile host evidence, not a real
network or remote-durability result.

## Next gates

Before any real-coordinate trial, the optional archive still requires an
explicit local UI composition, protected authenticated transport, evaluated
persistent/offline retention, restart-safe session allocation, participant/group
authority,
encrypted remote storage, retention/export/deletion controls, failure/retry
policy, privacy review, and target/physical evidence. Base messaging and local
recovery must continue when every archive component is absent or failed.
