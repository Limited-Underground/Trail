# Offline Map Selector Trust-Domain Provisioner v0

Status: deterministic host-tested preparation coordinator, 2026-08-11. No
ESP-IDF binding, credential implementation, physical durability result, or
on-device provisioning is claimed.

This coordinator is the sole common-code consumer of the boot-local,
non-copyable permit minted by the
[protected-domain authorizer](OFFLINE_MAP_SELECTOR_DOMAIN_AUTHORIZATION_V0.md).
It prepares either a blank device for its first map baseline or an authorized
same-device replacement for a later selector reseed. Preparation deliberately
does not activate a map or complete the pending `OTMD/v0` lifecycle transition.

## Permit and ownership boundary

The caller must hold exclusive ownership of the live map guard, domain-record
store, selector store, and protected source for the entire call. The
provisioner burns the permit before any I/O, then rechecks the exact operation
binding, boot session, checked use time, and scope. A mismatch, early use,
expiry, or replay cannot reach storage, and an exact-expiry use is rejected.

Only an already stopped or running-mapless guard with no active, previous, or
staged slot/generation is accepted. The coordinator stops that private owner
and never republishes a map. Messaging, radio, alerts, and USB recovery remain
outside this map-only boundary.

## Protected-source contract

The injected target boundary can read its state and can establish a fresh
domain only when the target has independently determined that the source is
uninitialized. It has no erase, reset, lower, or rebind operation. Common code
therefore cannot turn an initialized source into a replacement source. The
same boundary now supports an atomic exact-domain/exact-generation increase for
the separate activation coordinator; mutation failure is commit-uncertain.

An uninitialized read must contain a zero domain and selector generation zero.
A ready read must contain a nonzero domain. Establishment requests carry the
authorized scope plus exact retired and proposed domains, but result/status
objects deliberately omit all domain bytes. Any establishment error is treated
as commit-uncertain and requires reconciliation, because hardware may have
applied the operation before reporting failure.

## Recoverable mutation order

After read-only preflight of all three persistence boundaries, the coordinator
uses this fixed order:

1. assemble the exact fresh or linked-replacement pending `OTMD/v0` record;
2. save and exactly verify that pending record through the two-slot domain
   store;
3. if retained selector media was authorized, erase both selector slots and
   verify both are empty; otherwise re-inspect the already-empty selector to
   detect a race;
4. establish the proposed domain at protected selector generation zero; and
5. read back and require the exact proposed domain and generation zero.

Fresh commissioning creates record generation 1, domain epoch 1, and
`pending_first_baseline`. Same-device replacement links the active current
domain as retired, advances both record generation and domain epoch by exactly
one, clears accepted selector generation, and preserves a quarantine floor no
lower than either the previously accepted or reviewed retained generation.
Generation/epoch exhaustion and retained-media rollback fail before mutation.

The successful result is only `prepared`: the pending record is durable,
selector media is verified empty, the protected source is verified at
generation zero, and map exposure remains blocked. The separate
[activation coordinator](OFFLINE_MAP_SELECTOR_DOMAIN_ACTIVATION_V0.md) now
persists and protects the stable baseline, marks the domain active, and only
then publishes the map.

## Retry and reconciliation

A matching pending record is resumable only with a newly authorized permit and
the exact operation binding. This permits safe recovery after selector-clear or
protected-source uncertainty without writing a second domain transition. If a
prior source call applied the exact proposed domain but reported failure, a
retry may verify that exact state without calling establishment again.

Failures before the pending record is committed leave no domain preparation.
A domain commit-call or readback failure is explicitly commit-uncertain.
Failures after a pending record is known durable return
`reconciliation_required`, keep maps unavailable, and expose only coarse slot,
generation, state, and reason fields. No path silently rolls back the pending
record, imports retained selector history, or resets an initialized protected
source.

## Current evidence and limits

Thirteen deterministic scenario groups cover fresh commissioning, retained and
empty-media replacement, commit-before-clear-before-establish ordering,
permit replay/binding/boot/time rejection, active-map refusal, all read-only
preflight failures, generation exhaustion, retained rollback, domain commit
uncertainty, selector-clear failure, source failure and exact pending retry,
applied-then-failed recovery without reapply, selector races, and protected
readback mismatch.

All twenty-three map suites pass 100/100 focused repeats, and the complete
81-executable host matrix passes under strict C++17 warnings-as-errors.

This proves common-code ordering and deterministic recovery decisions only.
The target still needs independently evidenced continuity and blank-source
state, secure domain generation, credentials/physical-presence/audit/replay,
exclusive task locking, protected rollback resistance, ESP-IDF storage and
source adapters, power-cut/endurance testing, and physical on-device evidence.
The activation coordinator adds stable-baseline completion but not domain-aware
candidate/trial/runtime maintenance.
