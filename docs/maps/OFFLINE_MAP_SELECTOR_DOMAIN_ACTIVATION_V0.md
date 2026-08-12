# Offline Map Selector Trust-Domain Activation v0

Status: deterministic host-tested stable-baseline activation coordinator,
2026-08-11. No ESP-IDF binding, physical protected source, package staging,
renderer, or on-device activation is claimed.

This coordinator completes the prepared state created by the
[trust-domain provisioner](OFFLINE_MAP_SELECTOR_DOMAIN_PROVISIONER_V0.md). It
creates one stable selector baseline, advances the exact domain-bound protected
generation, persists the `OTMD/v0` pending-to-active transition, and publishes
the map only after all three durable owners agree.

It supports both fresh-device commissioning and authorized same-device domain
replacement. It does not run ordinary map candidate, trial, fallback, or
cleanup transitions.

## Entry and authority

Activation accepts only a stopped or running-mapless guard with no active,
previous, or staged map/generation. A running guard must already carry the
exact requested activation policy. The baseline package must independently
satisfy all existing package-evidence checks.

No new ephemeral permit is consumed here. The already committed pending
`OTMD/v0` record is the durable result of the permit-consuming preparation
step, which is necessary for restart recovery. Missing, unknown, or unrelated
domain state cannot create a selector. The caller must exclusively own the live
guard, both stores, protected source, and physical package-slot state for the
complete call.

## Generation selection

Fresh commissioning uses selector record generation 1. Same-device replacement
uses exactly `retired_selector_generation + 1`, so a newly established source
at generation zero jumps above all quarantined history rather than importing or
reusing it. A maximum retirement floor or exhausted domain-record generation
fails before selector access.

An active selector checkpoint remains the existing fixed `OTM0/v0` format and
contains no domain bytes. Cross-domain agreement comes from the separate
`OTMD/v0` record and protected source, not from extending or ambiguously
repurposing the selector codec.

## Recoverable durable order

After read-only domain, selector, package, and protected-source preflight, the
coordinator uses this fixed order:

1. create and exactly read back the stable selector at the chosen generation;
2. atomically compare the exact protected domain and generation zero, advance
   to the selector generation, and require exact readback;
3. reverify the selector is byte-exact for the private candidate;
4. save and exactly read back the next `OTMD/v0` record in `active` state with
   the accepted selector generation;
5. recheck the exact protected domain/generation; and
6. only then publish the private active guard to the live owner.

The protected interface has no erase, reset, lower, or rebind operation. Its
advance request includes the exact expected domain, current generation, and
higher proposed generation. Any reported mutation failure is commit-uncertain
and leaves the map unavailable for a later fresh-call reconciliation.

## Restart reconciliation

Each durable step can be recognized without guessing:

- pending domain plus empty selector plus protected generation zero starts the
  normal path;
- pending domain plus the exact selector and protected generation zero resumes
  at protected advance;
- pending domain plus exact selector/protected generation resumes at domain
  activation without rewriting either lower owner; and
- an already active domain publishes only when the exact stable selector,
  supplied package evidence, and protected domain/generation all agree.

This also reconciles an advance or domain commit that reached hardware before
its adapter reported failure. Unexpected selector generations, wrong package
evidence, wrong domain, source generation drift, a selector race after
protected advance, or a final source change remains mapless and
reconciliation-required. No path rolls state backward.

## Current evidence and limits

Fourteen deterministic groups cover fresh generation-1 activation, replacement
floor jumps, exact selector-before-protected-before-domain ordering, live/
policy/package/domain rejection, selector/storage/exhaustion preflight,
protected-source mismatch, selector write/commit/readback failures, protected
advance uncertainty, exact pending restart, applied-then-failed advance without
reapply, pending and already-active domain-commit recovery, active restart,
wrong package/source refusal, selector races, source readback/final change, and
domain readback failure.

All twenty-three map suites pass 100/100 focused repeats, and the complete
81-executable host matrix passes under strict C++17 warnings-as-errors.

This is stable-baseline lifecycle evidence only. It does not bind a real secure
domain generator, physical continuity decision, credential/audit backend,
protected rollback-resistant storage, ESP-IDF task/lock, package filesystem,
power-cut behavior, or rendered map. Domain-aware trial/candidate/runtime
generation maintenance remains a separate later composition. The
[active-domain boot coordinator](OFFLINE_MAP_SELECTOR_DOMAIN_BOOT_V0.md)
now supplies read-only restart for the exact stable active state.
