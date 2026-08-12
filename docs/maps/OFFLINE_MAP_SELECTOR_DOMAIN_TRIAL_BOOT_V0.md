# Offline Map Selector Domain-Aware Trial Boot v0

Status: deterministic host-tested restart and interrupted-entry recovery
composition, 2026-08-11. No ESP-IDF binding, protected target backend, package
filesystem, renderer, physical interruption result, or on-device boot is
claimed.

This coordinator is the restart boundary after
[domain-aware candidate entry](OFFLINE_MAP_SELECTOR_DOMAIN_CANDIDATE_V0.md).
It composes the existing private
[selector boot coordinator](OFFLINE_MAP_SELECTOR_BOOT_COORDINATOR_V0.md) with
the active `OTMD/v0` lifecycle record and exact domain-bound protected source.

It resumes a trial, persists a boot-limit transition to fallback-required,
restores an already-committed active runtime transition, or reconciles the two
narrowly defined single-generation gaps left when candidate entry, an earlier
trial boot, or a domain-aware runtime transition was interrupted. It does not
perform promotion, fallback completion, or previous-package cleanup itself,
transfer package bytes, or lower or rebind trust history.

## Accepted starting relationships

Let `D` be the active domain record's accepted selector generation, `S` the
protected source generation, and `G` the newest selector record generation.
All three must use the same nonzero current domain, the live guard must be
cleanly stopped, and the domain record must remain canonical and active.

Only these relationships are accepted:

- committed state: `D = S = G`; or
- one interrupted-generation gap: `G = D + 1`, with `S = D` or `S = G`.

A selector behind the domain, a protected generation outside that interval,
or a gap larger than one cannot be attributed uniquely to the prior bounded
operations and therefore stays fail-visible mapless for reconciliation.

## Durable recovery order

After the read-only relationship check, boot recovery uses this order:

1. restore the trial or fallback checkpoint into a private guard using
   `max(D, S)` as the caller-owned floor;
2. if the checkpoint is a trial, save and exactly read back its incremented
   boot count; if the limit was already reached, persist and exactly read back
   fallback-required state;
3. reread the unchanged active domain record and exactly reverify the private
   selector;
4. when needed, atomically advance the exact protected domain from `S` to the
   booted selector generation and require exact readback;
5. exactly reverify the selector;
6. when needed, save the next active `OTMD/v0` record with the booted selector
   generation and require exact readback;
7. reread the final domain record, selector, and protected source; and
8. only then publish the private trial or fallback-required guard.

An already-persisted fallback-required or active checkpoint at `D = S = G` is
restored without a selector, protected-source, or domain write. Active state
may retain a previous package pending cleanup or may represent completed
fallback/cleanup state. A resumed trial counts as another conservative boot
attempt even when it also reconciles an earlier interruption.

## Failure and privacy behavior

No source or domain advance can occur before selector persistence and exact
verification. Any later domain race, selector race, protected advance/readback
failure, domain save uncertainty, or final mismatch publishes only mapless
containment and requires reconciliation. An applied-then-failed protected
advance or uncertain domain commit is not rolled back or reapplied in the same
call.

The fixed-shape result contains lifecycle state/reason, nested selector boot
and verification evidence, coarse slot/source errors, generation numbers,
domain epoch, ordering/publication flags, and no domain identifier. Package
content, paths, coordinates, identity, credentials, keys, URLs, timestamps,
and free text are structurally absent.

## Current evidence and limits

Fourteen deterministic groups cover healthy trial restart and exact selector-
before-protected-before-domain ordering, both candidate-interruption gaps,
replacement domains, boot-limit fallback persistence, already-persisted
fallback restart, dirty ownership and invalid policy isolation, unrelated gap
rejection, selector boot/package/storage failures, a pre-advance domain race,
failed and applied-then-failed protected advance, protected readback mismatch,
domain write and uncertain-commit failure, final owner changes, degraded-domain
successor repair, and generation exhaustion.

All twenty-seven map suites pass 100/100 focused repeats, and the complete
90-executable host matrix passes under strict C++17 warnings-as-errors.

This is host common-code evidence only. Healthy trial promotion, fallback
completion, and previous-package cleanup now use the separate
[domain-aware runtime transition boundary](OFFLINE_MAP_SELECTOR_DOMAIN_TRANSITION_V0.md).
Later candidate cycles, protected
rollback-resistant target storage, authenticated integrity, target locks/tasks,
physical package retention, power interruption, wear, rendering, and on-device
transitions remain unproved.
