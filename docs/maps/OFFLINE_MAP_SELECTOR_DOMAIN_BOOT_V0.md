# Offline Map Selector Active Trust-Domain Boot v0

Status: deterministic host-tested read-only stable-domain boot composition,
2026-08-11. No ESP-IDF binding, protected target backend, package filesystem,
renderer, physical restart, or on-device boot is claimed.

This coordinator is the restart counterpart to the
[stable trust-domain activation coordinator](OFFLINE_MAP_SELECTOR_DOMAIN_ACTIVATION_V0.md).
It will expose an existing stable map only when the active `OTMD/v0` lifecycle
record, exact `OTM0/v0` selector, protected domain source, supplied package
evidence, and activation policy all agree.

It is deliberately read-only. Boot cannot establish, advance, reset, lower, or
rebind protected state; write, commit, erase, and protected-mutation methods are
never called. A disagreement remains mapless for service or reconciliation.

## Entry and stable-only boundary

The live guard must be cleanly stopped with no active, previous, or staged
slot/generation. The caller must exclusively own the guard, both stores,
protected source, and physical package-slot evidence for the complete call.

The supplied package must independently pass the existing policy and package-
evidence checks before durable state is read. The domain record must be
canonical, uniquely newest, and `active`; pending preparation or activation is
not silently completed by boot. The accepted selector generation must be
nonzero and exact.

Only a stable active selector is accepted. Candidate, staged, trial, fallback,
previous-cleanup, and generation-maintenance state cannot enter through this
boundary and remain separate domain-aware compositions.

## Read and publication order

The coordinator uses this fixed order:

1. validate a private stable package candidate;
2. inspect and retain the exact active `OTMD/v0` record;
3. require the protected source to match its exact current domain and accepted
   selector generation before selector access;
4. restore the selector at the accepted floor into a private guard and require
   the exact generation plus stable active shape;
5. reinspect the domain record and require every field to be unchanged;
6. exactly reverify the current selector against the private guard;
7. reread the protected source and require the same exact domain/generation;
   and
8. only then publish the private guard to the live owner.

Readable one-record degradation may boot while reporting repair-required for
the domain or selector store. The boot coordinator performs no repair. An
unreadable store, missing selector under active protected history, wrong domain,
generation drift, changed record, failed exact selector recheck, or final
source change cannot expose a map. Invalid policy and a dirty live owner are
rejected before durable access.

## Result privacy

The fixed-shape result contains coarse lifecycle state/reason, slot health,
store/source error categories, accepted generation, record generation, domain
epoch, repair flags, and publication booleans. Domain identifiers, package
bytes, paths, coordinates, device or participant identity, credentials, keys,
URLs, timestamps, and free text are structurally absent.

## Current evidence and limits

Thirteen deterministic groups cover fresh and replacement-domain stable boot,
clean-owner isolation, invalid policy/package preflight, missing/pending/
unreadable domain state, protected-source failure and domain/generation
mismatch before selector access, empty/rollback/ahead/unreadable selector
state, trial-checkpoint refusal, domain and selector races, final source change,
second-read failures, and operational degraded records. Every group asserts
that domain/selector writes, commits, erases, and protected mutations remain
zero.

All twenty-three map suites pass 100/100 focused repeats, and the complete
81-executable host matrix passes under strict C++17 warnings-as-errors.

This is host common-code evidence only. Domain-aware candidate, trial,
fallback, cleanup, and runtime accepted-generation synchronization remain
open. Protected rollback-resistant storage, target locks/tasks, authenticated
integrity, package mounting/rendering, physical power interruption, wear, and
on-device boot remain unproved.
