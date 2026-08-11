# Offline Map Selector Trusted Boot Coordinator v0

Status: deterministic host-tested protected-generation boot composition,
2026-08-11. No protected target backend, ESP-IDF task, hardware counter,
physical durability, reset authority, or on-device result exists.

## Purpose

The ordinary selector boot coordinator accepts a scalar minimum generation so
its storage and lifecycle behavior can be tested independently. That scalar is
not anti-rollback protection when it comes from an ordinary caller.

`MapSelectorTrustedBootCoordinator` closes the first composition gap between
the selector boot path and the protected-generation boundary. It obtains the
minimum directly from `MapSelectorTrustedGeneration`, keeps the selector guard
private, and will not publish it until trusted state exactly matches the
selector generation that would become live.

## Ordering

The live guard must be stopped, and the target must hold exclusive ownership of
both selector storage and the trusted-generation source for the entire call.
The coordinator then:

1. inspects the protected source before any selector-storage access;
2. passes that exact observed generation to the existing selector boot
   coordinator while using a private guard;
3. lets selector restore and any resumed-trial or boot-limit transition finish
   commit-last persistence and exact readback privately;
4. rejects an empty selector domain when protected history is nonzero;
5. rechecks an unchanged generation or atomically advances and exactly reads
   back a newer valid selector generation; and
6. publishes the private guard only after the final protected value exactly
   matches the selector generation.

A valid selector that is ahead of protected trust can occur after selector
persistence completed but the protected advance did not. The coordinator may
catch up trust only after the complete selector boot path has revalidated the
record, package evidence, policy, and readback. A selector below protected
trust remains rollback/service evidence and is never exposed.

## Fail-closed outcomes

- A dirty live guard prevents both trusted-source and selector-store access.
- A failed initial trust read prevents selector-store access and may be retried
  when the source did not perform a mutation.
- Nonzero trusted history with two empty selector slots is
  `trusted_history_missing`, not a normal first-use/mapless boot.
- Selector rollback or other selector failure can publish only the existing
  fail-visible service-mapless state; it cannot expose a map.
- Any uncertain protected advance, failed readback, or changed final value
  keeps the privately restored/saved selector unpublished and requires fresh-
  boot reconciliation when the protected enforcer is latched.

The result retains both typed selector evidence and the before/after protected-
generation evidence. It contains no package bytes, map paths, coordinates,
device or participant identity, credential, key, URL, or free text.

## Evidence and limitations

Ten deterministic groups cover exact stable release, trial save followed by
trusted advance, valid store-ahead catch-up, zero-history mapless boot,
trusted-history loss, trust-unavailable storage isolation, selector rollback,
commit-uncertain protected advance, final-value conflict, and dirty-owner
isolation. The new suite passes 100/100 focused repeats; the complete
70-executable host matrix and publication-safety scan pass locally under strict
C++17 warnings-as-errors.

Runtime transitions, candidate replacement, first baseline, and service reseed
now have separate protected-source compositions. Reset/replacement recovery
still uses separately supplied generation values. No concrete source
is protected merely by implementing the interface. Target locking, NVS and
counter backends, authentication, power-loss interruption, flash replacement,
wear, physical attack resistance, renderer behavior, and on-device boot remain
unproved.
