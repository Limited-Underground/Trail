# Offline Map Selector Candidate Coordinator v0

Status: deterministic host-tested replacement ordering boundary, 2026-08-11

The candidate coordinator connects externally produced
[`MapPackageEvidence`](OFFLINE_MAP_ACTIVATION_GUARD_V0.md) to the
[activation guard](OFFLINE_MAP_ACTIVATION_GUARD_V0.md) and
[recoverable selector store](OFFLINE_MAP_SELECTOR_STORE_V0.md) without giving
an uncommitted candidate live map authority. It coordinates replacement of one
stable active map only.

## Deliberate baseline limit

First-ever installation on a mapless device is not supported by this
coordinator. An `OTM0` trial must name an exact prior-good package so a restart
can recover from failed candidate reads. Inventing that prior state would make
the checkpoint unsafe. The separate
[first-baseline coordinator](OFFLINE_MAP_SELECTOR_BASELINE_COORDINATOR_V0.md)
now owns initial installation without changing this replacement boundary.

The input candidate is typed evidence for package bytes already placed in the
alternate physical package slot by an external adapter. The coordinator does
not receive or manipulate a path, file, mount, package stream, or renderer.

## Persist-before-exposure ordering

Under exclusive selector-store ownership, replacement follows this order:

1. require a running, stable `active` live guard and nonzero caller-held record
   generation;
2. export the live checkpoint and require exact activation-policy agreement;
3. verify that the newest unique stored checkpoint exactly matches the live
   guard, generation token, and optional trusted minimum;
4. copy the live guard and stage the candidate only on that private copy;
5. mark the exact candidate slot and package generation committed on the
   private copy, producing trial state with the prior package retained;
6. re-inspect the store and require the same exact record generation seen by
   preflight;
7. save the trial checkpoint using the store's commit-last protocol and exact
   readback; and
8. publish the private trial guard only after that save succeeds.

The exact-generation save closes the gap in which a newer checkpoint could
appear after read-only preflight but before save allocation. It is not a
locking primitive: the caller must still prevent another writer from mutating
the selector store during one coordinator call.

## Failure behavior

- Invalid, incomplete, same-slot, or same-generation candidate evidence is
  rejected before persistence. The existing active map remains available.
- A stopped, mapless, staged, trial, or fallback-required guard is not a stable
  replacement baseline and is rejected without touching storage.
- Missing, unreadable, conflicting, stale, policy-mismatched, or live-
  mismatched selector evidence removes map exposure and returns a typed service
  or reconciliation outcome.
- Prepared-write failure, generation exhaustion, uncertain commit, corrupt
  readback, or a newer generation observed at save time never publishes the
  private candidate. The live guard becomes fail-visible mapless.
- Commit ambiguity, bad readback, generation conflict, rollback, and
  generation/state races explicitly require reconciliation.

Map failure remains independent of radio, messaging, alerts, position sharing,
vehicle integration, and USB recovery.

## Data and authority limits

The interface carries only activation policy, package evidence, record
generations, fixed state/error enums, and booleans. It cannot transfer,
authenticate, open, modify, delete, or render a map package; mount or repair a
filesystem; choose a provider; control a radio; identify a participant; or
access credentials, keys, URLs, geographic content, or free text.

Physical package-slot staging and rollback retention must be guaranteed by a
later target adapter. The coordinator's `stage()` call changes lifecycle state
only; it is not a storage write.

## Current evidence

Eleven deterministic host groups cover successful replacement, invalid and
same-slot/generation evidence, non-active baselines, exact live/token/floor
mismatch, unreadable/conflicted media, prepared-write failure, commit
ambiguity, corrupt readback, a newer checkpoint between preflight and save,
invalid policy, and generation exhaustion. The candidate, transition, boot,
and store suites each pass 100/100 focused repeats under strict C++17
warnings-as-errors.

This is host replacement-ordering evidence only. Initial installation is owned
by the separate baseline coordinator; this API does not implement it. No
physical storage or package-slot adapter, concurrency primitive, atomic-byte
guarantee, wear/endurance or power-loss result, protected trusted floor,
package authentication, filesystem, renderer, display, target task, or
on-device result is claimed.
