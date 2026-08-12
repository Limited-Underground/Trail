# Offline Map Selector Domain-Aware Candidate Entry v0

Status: deterministic host-tested domain-aware trial-entry composition,
2026-08-11. No ESP-IDF binding, protected target backend, package filesystem,
renderer, physical interruption result, or on-device transition is claimed.

This coordinator is the first runtime generation transition after
[read-only stable domain boot](OFFLINE_MAP_SELECTOR_DOMAIN_BOOT_V0.md). It
connects the existing private
[candidate coordinator](OFFLINE_MAP_SELECTOR_CANDIDATE_COORDINATOR_V0.md) to
the active `OTMD/v0` lifecycle record and exact domain-bound protected source.

One stable active map may enter trial only after the new selector checkpoint,
protected generation, and domain accepted generation durably advance in that
order and all three owners are reread. Candidate package bytes must already be
fully evidenced in the alternate physical slot; this boundary does not transfer,
open, mount, authenticate, render, or delete them.

## Entry and ownership

The live guard must be a running stable active baseline with no previous or
staged slot. It must match the requested policy. The caller must exclusively
own the live guard, selector store, domain store, protected source, and both
physical package slots for the complete call.

The uniquely newest domain record must be canonical and `active`. Its accepted
selector generation must exactly match the protected source's current domain
and generation before selector access. Selector or domain record-generation
exhaustion fails before candidate persistence.

## Durable order

After read-only preflight, candidate entry uses this order:

1. verify the stable live selector at the domain's accepted generation;
2. stage the alternate-slot candidate on a private guard;
3. save and exactly read back the private trial selector at generation `N+1`;
4. recheck that the active domain record is unchanged;
5. exactly reverify the private trial selector;
6. atomically advance the exact protected domain from `N` to `N+1` and require
   exact readback;
7. exactly reverify the trial selector again;
8. save the next active `OTMD/v0` record with accepted generation `N+1` and
   require exact readback;
9. reread the domain record, selector, and protected source; and
10. only then publish the private trial guard.

No step lowers or rebinds protected history. The domain store's existing
active-generation successor rule supplies the exact next record generation and
strictly increasing accepted selector generation.

## Rejection and interruption behavior

Invalid, incomplete, same-slot, or same-generation candidate evidence performs
no mutation. The existing active map remains available only after a final exact
domain, selector, and protected-source recheck.

Selector verification/save failure cannot advance protected or domain state.
If selector persistence succeeds but a later domain race, protected advance,
readback, domain save, or final recheck fails, the original live map is replaced
with fail-visible mapless state and reconciliation is required. An applied-then-
failed protected advance or uncertain domain commit is never rolled back or
reapplied within the call.

Those durable intermediate states are intentionally recognizable: selector may
be one generation ahead of domain/protected state, or selector/protected may be
one generation ahead of the domain record. The following
[domain-aware trial-boot recovery boundary](OFFLINE_MAP_SELECTOR_DOMAIN_TRIAL_BOOT_V0.md)
accepts only those exact gaps and reconciles them before any map is exposed.

## Result privacy

The fixed-shape result contains coarse lifecycle state/reason, nested selector
candidate and verification results, slot health, source/store error categories,
generation numbers, domain epoch, repair/ordering/publication booleans, and no
domain identifier. Package bytes, paths, coordinates, participant or device
identity, credentials, keys, URLs, timestamps, and free text are absent.

## Current evidence and limits

Thirteen deterministic groups cover fresh and replacement-domain entry, exact
selector-before-protected-before-domain ordering, live/policy isolation,
missing/exhausted/mismatched domain or source preflight, safe candidate
rejection with concurrent rechecks, selector verification/write failures,
domain and selector races before protected advance, failed and applied-then-
failed protected advance, protected readback failure/mismatch, domain write and
uncertain-commit failure, final domain/selector/source changes, and degraded-
domain successor repair.

All twenty-seven map suites pass 100/100 focused repeats, and the complete
90-executable host matrix passes under strict C++17 warnings-as-errors.

This is host common-code evidence only. Domain-aware trial boot/recovery and
healthy promotion, fallback completion, and previous-package cleanup are now
separate compositions. Later candidate cycles remain open. Protected rollback-resistant storage, authenticated
integrity, target locks/tasks, physical package retention, power interruption,
wear, rendering, and on-device transitions remain unproved.
