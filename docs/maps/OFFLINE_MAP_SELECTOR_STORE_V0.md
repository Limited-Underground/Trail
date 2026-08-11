# Offline Map Selector Store v0

Status: host-tested abstract two-slot store, 2026-08-11

The selector store gives the fixed
[`OTM0/v0` checkpoint](OFFLINE_MAP_SELECTOR_CHECKPOINT_V0.md) a recoverable
commit sequence without choosing ESP32 NVS, internal flash, SD card, or another
physical backend. It operates through a four-operation storage interface:
read a fixed slot, write a complete prepared slot, commit one marker byte, and
erase a fixed slot.

## Commit-last protocol

Saving one selector uses this order:

1. inspect both fixed 64-byte slots;
2. reject unreadable media or equal-generation conflicting valid records;
3. derive the next nonzero record generation from the newest valid record and
   an optional external trusted floor;
4. export and encode the complete guard checkpoint;
5. select the empty/degraded peer or alternate away from the newest valid slot;
6. write the complete record with byte 59 cleared to zero;
7. write only commit marker `0xA5` at byte 59; and
8. read back, decode, and compare every byte plus the exact generation.

The CRC was calculated for `0xA5`, so the prepared record is explicitly
`uncommitted` and cannot be selected. The older committed slot is never erased
or overwritten while a peer save is being prepared.

A prepared-write failure is known uncommitted. A commit-call failure is marked
uncertain because the marker may or may not have reached media. No success is
reported until exact committed readback passes.

## Selection and repair

- Two valid unequal generations select the newer one.
- Two byte-identical valid records with the same generation are acceptable.
- Two different valid records with the same generation are a conflict and
  neither is restored or overwritten.
- One valid plus one empty, invalid, or uncommitted slot restores the valid
  record and reports recovery required.
- A later save repairs an invalid/uncommitted peer while preserving the valid
  slot.
- Any unreadable slot fails restore and save even when its peer is valid,
  because unreadable media could conceal a newer committed generation.
- No valid record plus invalid/uncommitted content requires explicit reset; the
  store never invents a baseline generation.
- Generation exhaustion prevents all writes.

`restore_at_or_above` can reject a record below an externally supplied trusted
minimum. The store does not create, protect, persist, or advance that trusted
floor, so this option is not an anti-rollback claim.

`save_after_exact` re-inspects both slots and writes only when the newest valid
record still has the caller's expected generation. Boot, runtime-transition,
and candidate coordinators use this form so a newer checkpoint observed after
their read-only preflight is not overwritten. This comparison is not a lock;
the target must provide exclusive store ownership throughout a save.

`save_if_empty` similarly re-inspects both slots and writes only when both are
still readable and empty. The first-baseline coordinator uses it to prevent a
new or restored selector from being overwritten between preflight and the
canonical generation-1 save. It never erases or treats invalid/uncommitted
media as empty.

`reset_and_verify_empty` is the service-grade clear boundary. It attempts both
selector-slot erases, then reads both slots back and succeeds only when each is
exactly empty. It reports the two erase outcomes and the two observed slot
states separately. The
[service-reseed coordinator](OFFLINE_MAP_SELECTOR_RESEED_COORDINATOR_V0.md)
uses this result before creating a new baseline. The legacy `reset()` result is
retained for existing transition behavior but does not provide verified-empty
evidence.

## Guard restore boundary

After selecting a record, the store asks the map activation guard to revalidate
the exact policy and active/prior package evidence. Policy, generation, slot,
or package mismatch becomes a checkpoint rejection and leaves map authority
fail-closed.

Trial restore increments its boot count and resets volatile health/time. Before
a target exposes that resumed candidate, the
[boot coordinator](OFFLINE_MAP_SELECTOR_BOOT_COORDINATOR_V0.md) saves and
verifies the incremented checkpoint. This store deliberately does not hide
that ordering requirement. A protected trusted-floor source and target task
composition remain separate work.

## Data and authority limits

The store handles only two opaque 64-byte records and typed results. It has no
path, filename, geographic content, participant/device identity, credential,
key, URL, or free text. It cannot open a map package, authenticate data, mount a
filesystem, delete package data, render a map, control a radio, stop messaging,
or alter alerts/position sharing/USB recovery.

`reset()` and `reset_and_verify_empty()` address only the two abstract selector
slots. Neither operation erases map packages or any other persistence domain.

The [key/value target adapter](OFFLINE_MAP_SELECTOR_KV_TARGET_ADAPTER_V0.md)
provides one tested mapping from this interface to two exact blobs with an
explicit durable-commit backend boundary. It is NVS-ready but is not an
ESP-IDF implementation or physical storage result.

## Current evidence

Fourteen deterministic host groups cover empty first save/restore, alternating
generation selection, partial prepared writes, commit-before/after-error
uncertainty, corrupt readback, degraded-peer repair, unreadable peer media,
equal-generation conflict, policy/package mismatch, trusted-floor rejection and
generation exhaustion, exact live-checkpoint verification, persisted trial-
boot increment, exact-generation save rejection, partial/successful reset, and
reported-success erase that fails exact empty readback. With the separate
reseed-authorization suite, all ten map executables pass
100/100 focused repeats under strict
C++17 warnings-as-errors.

This is abstract-store evidence plus a backend-neutral key/value mapping only.
No ESP-IDF/NVS backend, partition table, atomicity guarantee, wear/endurance
result, protected generation, package authentication, physical power
interruption, filesystem, renderer, display, or on-device result is claimed.
