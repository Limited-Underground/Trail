# Offline Map Selector Checkpoint v0

Status: host-tested codec and guard restart boundary, 2026-08-11

`OTM0/v0` is the fixed 64-byte record that lets the
[map activation guard](OFFLINE_MAP_ACTIVATION_GUARD_V0.md) distinguish a stable
map, an unfinished candidate trial, and fallback-required state after restart.
It prevents a reboot from silently promoting a candidate or forgetting the
explicit prior-package recovery path.

This is a record and state-restore contract, not a durable storage adapter.

## Canonical record

All integers use little-endian byte order.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Magic `OTM0` |
| 4 | 1 | Version `0` |
| 5 | 1 | Activation state |
| 6 | 1 | Failure reason |
| 7 | 1 | Active A/B slot |
| 8 | 1 | Previous slot or `none` |
| 9 | 1 | Trial boot count |
| 10 | 2 | Required healthy-read count |
| 12 | 8 | Active package generation |
| 20 | 8 | Previous package generation or zero |
| 28 | 8 | Per-boot trial deadline in milliseconds |
| 36 | 8 | Maximum package bytes |
| 44 | 8 | Nonzero record generation |
| 52 | 1 | Maximum trial boot count |
| 53 | 6 | Canonical zero reserved bytes |
| 59 | 1 | Commit-last marker `0xA5` |
| 60 | 4 | CRC-32 over bytes 0-59 |

The record contains no path, filename, package name, geographic bounds, route,
breadcrumb, attribution text, URL, participant/device identity, timestamp,
credential, key, or free text.

## State coherence

- `active` requires no failure reason and zero trial boots. No previous slot is
  a stable selection; a previous slot means a confirmed candidate still awaits
  permitted prior cleanup.
- `trial` requires a distinct previous slot/generation, no failure reason, and
  a nonzero trial-boot count no greater than policy.
- `fallback_required` requires the same distinct prior evidence, a nonzero
  bounded trial-boot count, and one canonical read/deadline/clock/media/boot-
  limit reason.
- `mapless`, `staged`, and `stopped` are never encoded. Interrupted staging
  leaves the existing stable selector authoritative; mapless is represented by
  no accepted record.

Unknown state/reason/slot values, unsupported versions, nonzero reserved bytes,
missing commit marker, incoherent prior fields, zero policy/generations, and CRC
mismatch fail closed. Decode and export change caller output only after complete
validation. CRC is calculated for the committed marker; a prepared record with
zero at byte 59 is never decodable as valid.

## Restart behavior

The guard rechecks the exact active slot/generation and all current
`MapPackageEvidence` before restoring any map availability. Trial and fallback
records also require the exact previous package to remain fully acceptable.

On a trial restart:

1. volatile healthy-read and monotonic-time evidence is discarded;
2. the trial boot count increments;
3. a new per-boot deadline begins from the supplied checked monotonic time;
4. the prior package remains retained; and
5. the full healthy-read threshold must pass again before cleanup.

When the persisted trial count has reached the policy maximum, the candidate
does not resume. The guard enters visible `fallback_required` with a boot-limit
reason. Missing prior evidence becomes mapless rather than guessed recovery.

A stable confirmed candidate can still start when its cleanup-pending prior has
already disappeared; the active package must independently pass all evidence,
and prior cleanup authority is removed.

## Integrity and storage boundary

CRC-32 detects accidental mutation and truncation only. It does not
authenticate the record, establish trusted generation, or prevent rollback.
The host-tested
[abstract two-slot selector store](OFFLINE_MAP_SELECTOR_STORE_V0.md) now uses
recoverable commit-last writes, exact readback, generation conflict handling,
and an optional external trusted floor. A physical backend must still prove its
atomic-byte behavior, endurance, and power-loss semantics, and a protected
rollback/authentication policy remains unselected.

The codec performs no filesystem, NVS, flash, SD-card, selector write, package
write, mount, deletion, download, network, radio, rendering, or communication
operation.

## Current evidence

Ten deterministic codec groups cover stable deterministic round trip, trial
restart and boot-limit fallback, fallback persistence, confirmed cleanup,
corruption/magic/version/reserved-byte rejection, semantic coherence, argument
and output atomicity, policy/package mismatch, missing prior evidence, and
export preconditions. Both map executables pass 100/100 focused repeats under
strict C++17 warnings-as-errors. The separate store adds twelve commit/recovery
groups; all four map executables pass 100/100 focused repeats.

This is codec, restart-policy, and abstract-store evidence only. No physical
selector backend, trusted generation, package authentication, target filesystem,
renderer, display, or on-device power-interruption result is claimed.
