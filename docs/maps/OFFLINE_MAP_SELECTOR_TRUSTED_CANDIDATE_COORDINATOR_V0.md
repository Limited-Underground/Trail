# Offline Map Selector Trusted Candidate Coordinator v0

Status: deterministic host-tested protected-generation replacement composition,
2026-08-11. No protected target backend, ESP-IDF task, hardware counter,
physical package adapter, or on-device result exists.

## Purpose

The ordinary candidate coordinator accepts caller-supplied current and minimum
selector generations so its store and lifecycle rules remain independently
testable. Those scalar values are not rollback protection when they come from
ordinary application state.

`MapSelectorTrustedCandidateCoordinator` composes that existing coordinator
with `MapSelectorTrustedGeneration`. It derives both generation values from the
protected source, performs selector verification and candidate save against a
private activation guard, and does not publish trial state until protected
history exactly matches the new selector record.

## Ordering

The live guard must be a running stable active baseline. The target must hold
exclusive ownership of selector storage, the protected source, and package-slot
staging for the complete call. The coordinator then:

1. inspects protected history before any selector-store access;
2. supplies that exact generation as both the current selector generation and
   rollback floor to the ordinary candidate coordinator;
3. lets current-selector verification, private candidate staging, exact-
   generation save, commit-last persistence, and selector readback finish;
4. atomically advances protected history from the old exact generation to the
   newly saved selector generation and reads it back exactly; and
5. publishes private trial state only after the final values match.

Candidate validation rejection performs no selector write, but the protected
source is rechecked before the current active map remains available. This
prevents a rejected candidate call from retaining map exposure after the trust
value changed concurrently.

## Fail-closed outcomes

- A stopped, mapless, staged, trial, or fallback-required guard is not a
  candidate baseline and causes no protected-source or selector-store access.
- A failed initial trust read blocks selector access and contains the currently
  visible map as unreadable or ambiguous mapless state.
- Zero, stale, rolled-back, conflicted, or live-mismatched selector state cannot
  be labelled current by caller state and remains mapless.
- Candidate validation rejection preserves the current map only after an exact
  final trust recheck; a failed or changed recheck contains map exposure.
- Selector save failure never advances protected history.
- Once selector save succeeds, every failed advance, pre-write protected
  conflict, uncertain commit, or non-exact readback requires fresh-boot
  reconciliation and keeps trial state private.

The result retains typed candidate, trusted-source, containment, and ordering
evidence. It contains no package bytes, paths, coordinates, participant or
device identity, credentials, keys, URLs, or free text.

## Evidence and limitations

Eleven deterministic groups cover selector-save-before-trust ordering, exact
recheck after candidate rejection, initial trust failure and storage isolation,
zero/stale trust, live selector mismatch, selector write failure, uncertain
applied advance, protected conflict after selector save, final trust change,
invalid policy containment, and non-baseline ownership isolation. The suite
passes 100/100 focused repeats; the complete 72-executable host matrix and
publication-safety scan pass locally under strict C++17 warnings-as-errors.

This is common host composition, not a lock, package transfer, or protected
backend. First baseline now has a separate protected-source composition;
service reseed and protected reset/replacement recovery remain separate gates.
Target serialization, physical package-slot retention, NVS/counter backends,
authentication, power-loss interruption, flash replacement, wear, physical
attack resistance, filesystem/renderer behavior, and on-device replacement
remain unproved.
