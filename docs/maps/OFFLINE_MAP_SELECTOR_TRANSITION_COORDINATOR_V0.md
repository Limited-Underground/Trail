# Offline Map Selector Transition Coordinator v0

Status: deterministic host-tested runtime persistence boundary, 2026-08-11

The transition coordinator extends the
[boot coordinator](OFFLINE_MAP_SELECTOR_BOOT_COORDINATOR_V0.md) ordering rule
through runtime map lifecycle changes. It applies an operation to a private
copy of the live guard and replaces the live guard only after the current
checkpoint is proven exact and any persistent change has passed commit-last
save plus exact readback.

## Required context

Every call supplies the exact activation policy, caller-held current record
generation, and optional external minimum generation. Before applying live
authority, the coordinator requires:

1. a running guard in active, trial, or fallback-required state;
2. a newest unique readable two-slot checkpoint at or above the external
   minimum;
3. an exact byte match between that checkpoint and the live guard's exported
   persistent state;
4. an exact match between the stored generation and caller-held generation;
   and
5. exact policy agreement.

Zero/stale generation tokens, rollback-floor failure, equal-generation
conflict, unreadable media, invalid records, live-state mismatch, and policy
mismatch block the operation and remove map exposure.

The [selector store](OFFLINE_MAP_SELECTOR_STORE_V0.md) now exposes read-only
`verify_current` for this purpose. It does not rewrite or repair media while
verifying.

## Operations and persistence

| Operation | Volatile-only case | Persistent case |
| --- | --- | --- |
| trial read | another healthy read below the promotion threshold | promotion to active or failure to fallback-required |
| trial tick | monotonic time advances before the deadline | deadline or clock failure enters fallback-required |
| complete fallback | none | exact prior package becomes active |
| mark prior removed | rejected unless cleanup is permitted | prior slot/generation authority is cleared |

Volatile-only success and rejected operations do not write. Persistent changes
are encoded from the private attempted guard, written to the alternate/degraded
slot only if the preflight generation is still current, committed last, and
read back exactly. Only then is the attempted guard published and the returned
generation advanced. The exact-generation comparison closes the preflight-to-
save gap but is not a lock; the caller must retain exclusive store ownership
throughout the operation.

If fallback evidence is invalid, the lifecycle guard deliberately becomes
mapless. Because mapless state grants no selector authority and is not an
`OTM0` checkpoint state, the coordinator erases only the two abstract selector
records and verifies both are empty before publishing `mapless_committed`.
Partial or unverifiable clearing remains mapless but requires reconciliation.
Map packages and every other persistence domain are untouched.

## Failure behavior

A prepared-write failure, uncertain commit, corrupt readback, preflight
mismatch, storage failure, conflict, or rollback never publishes the private
attempt. The previous live map is replaced with a fresh unreadable/ambiguous
mapless guard when the supplied policy is valid. Commit/readback and
generation/state conflicts are explicitly reconciliation-required. Invalid
policy stops the map guard. None of these outcomes controls radio, messaging,
alerts, position sharing, or USB recovery.

## Current evidence

Thirteen deterministic transition groups cover volatile healthy reads,
persisted promotion, persisted trial-read failure, deadline and clock fallback,
valid fallback completion, verified selector clearing on invalid fallback,
prior cleanup, rejected operations, exact live/token/floor mismatches,
unreadable/conflicted storage, prepared/commit/readback save failures, partial
clear failure, invalid policy, and stopped ownership. The transition and store
suites pass 100/100 focused repeats under strict C++17 warnings-as-errors.

No ESP32 task binding, NVS/flash/SD adapter, atomic-byte or erase guarantee,
wear/endurance result, protected generation source, package authentication,
physical interruption, filesystem, renderer, display, package deletion, or
on-device result is claimed.
