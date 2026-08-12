# Offline Map Selector Domain-Aware Runtime Transitions v0

Status: deterministic host-tested runtime and restart-recovery composition,
2026-08-12. No ESP-IDF binding, protected target backend, package filesystem,
renderer, physical interruption result, or on-device transition is claimed.

This coordinator closes the runtime lifecycle after
[domain-aware trial boot](OFFLINE_MAP_SELECTOR_DOMAIN_TRIAL_BOOT_V0.md). It
composes the existing private
[selector transition coordinator](OFFLINE_MAP_SELECTOR_TRANSITION_COORDINATOR_V0.md)
with the active `OTMD/v0` lifecycle record and exact domain-bound protected
source.

It handles healthy trial reads and promotion, trial deadline/failure, valid
fallback completion, and previous-package cleanup. It does not transfer,
open, mount, authenticate, render, or erase package bytes, and it never lowers
or rebinds protected history.

## Entry and ownership

The live guard must be running in active, trial, or fallback-required state and
must match the supplied policy. The uniquely newest domain record must be
canonical and active. Its nonzero accepted selector generation must exactly
match the protected source's current domain and generation before selector
access.

The caller must exclusively own the live guard, selector store, domain store,
protected source, and affected physical package slots for the complete call.
This common component is not a lock.

## Durable order

Volatile healthy-read progress and rejected operations still require exact
unchanged domain, selector, and protected-source rechecks before the private
guard can replace live state.

When a transition changes persistent selector state, the coordinator uses this
order:

1. verify the live selector at the domain's accepted generation;
2. apply the lifecycle operation to a private guard;
3. save and exactly read back selector generation `N+1`;
4. recheck the unchanged active domain record;
5. exactly reverify the private selector;
6. atomically advance the exact protected domain from `N` to `N+1` and require
   exact readback;
7. exactly reverify the selector again;
8. save the next active `OTMD/v0` record with accepted generation `N+1` and
   require exact readback;
9. reread the domain record, selector, and protected source; and
10. only then publish the private active or fallback-required guard.

Promotion therefore retains the previous package until a separately verified
cleanup call commits. Valid fallback completion restores the prior package and
clears the failed candidate relationship in selector state. Previous-package
cleanup commits only after the caller has independently removed the exact
physical slot/generation named by the live guard.

## Restart recovery

Selector persistence can complete before a later protected or domain step
fails. The existing domain-aware boot boundary now also accepts a canonical
active selector checkpoint, in addition to trial and fallback-required state.
It accepts only the same synchronized relationship or exact one-generation
interruption gaps, reconciles protected and accepted domain generations, and
publishes active state only after final three-owner agreement.

An applied-then-failed protected advance or uncertain domain commit is not
rolled back or reapplied in the same runtime call. Map exposure is contained
and a fresh boot performs the bounded reconciliation.

## Fail-closed behavior

- Missing, inactive, exhausted, unreadable, or mismatched domain/source state
  blocks selector mutation and removes map exposure.
- Selector save or verification failure cannot advance protected or domain
  history.
- Any domain, selector, or source race after selector persistence remains
  fail-visible mapless and requires restart reconciliation.
- Invalid fallback evidence may verified-clear the two selector records through
  the lower transition boundary. Protected and domain history remain intact,
  so the result is service-required rather than reclassified as first use.
- A rejected operation performs no persistence and retains the live map only
  after exact final owner rechecks.

The fixed-shape result contains lifecycle operation/state/reason, nested
selector transition and verification evidence, coarse slot/source errors,
generation numbers, domain epoch, ordering/publication flags, and no domain
identifier. Package content, paths, coordinates, identity, credentials, keys,
URLs, timestamps, and free text are structurally absent.

## Current evidence and limits

Eleven deterministic groups cover volatile healthy reads, ordered promotion,
deadline fallback and valid completion, previous-package cleanup, safe rejected
operations, pre-write generation mismatch, selector-save isolation,
restart recovery after failed protected advance and uncertain domain commit,
invalid-fallback retained history, and final protected-source change.

All twenty-six map suites pass 100/100 focused repeats, and the complete
87-executable host matrix passes under strict C++17 warnings-as-errors,
including publication safety.

This is common host composition only. Protected rollback-resistant target
storage, authenticated integrity, target locks/tasks, physical package
retention/removal, power interruption, wear, rendering, and on-device
transitions remain unproved.
