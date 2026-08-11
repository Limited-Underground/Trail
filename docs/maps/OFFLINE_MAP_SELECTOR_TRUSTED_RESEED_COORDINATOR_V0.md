# Offline Map Selector Protected Service-Reseed Coordinator v0

Status: deterministic host-tested protected service-recovery composition,
2026-08-11

This coordinator composes the authorized
[service-reseed coordinator](OFFLINE_MAP_SELECTOR_RESEED_COORDINATOR_V0.md)
with the
[trusted-generation source](OFFLINE_MAP_SELECTOR_TRUSTED_GENERATION_SOURCE_V0.md).
It removes the caller-supplied rollback floor from service recovery while
preserving the existing exact-operation permit and selector-only reset scope.

## Accepted starting state and authority

Every call requires:

1. a running mapless guard with no active, previous, or staged slot and no map
   exposure;
2. a usable protected-generation source;
3. a fresh non-copyable reseed permit minted for the exact activation policy,
   proposed package, current boot, time window, and protected value; and
4. exclusive target ownership of selector storage, protected history, and the
   retained package for the complete operation.

The wrapper reads protected history before selector storage. It constructs the
lower reseed context itself, so the ordinary caller cannot choose the rollback
floor. The lower coordinator then consumes the permit against that exact
derived value before reading or changing selector records.

## Inspect, authorize, replace, protect, expose

The successful order is fixed:

1. reject a non-service live owner without source, selector, or permit access;
2. inspect protected history;
3. consume the permit against the exact observed protected value;
4. inspect selector media and select one generation above the greater of local
   observable history and protected history;
5. verified-clear only the two selector records;
6. save and exactly read back the replacement stable selector against a
   private guard;
7. atomically advance protected history from the initially observed value to
   the saved selector generation and exactly read it back; and
8. publish the recovered active map only after both values agree.

Protected history is never lowered or reset. A locally newer selector may be
reviewed and replaced by this authorized flow, but the new record and protected
value must both advance beyond it.

## Failure behavior

- A retryable initial source failure touches no selector storage and leaves the
  still-valid permit available for another attempt within its time window.
- A latched source failure performs no new source or selector I/O, publishes an
  ambiguous mapless state, and requires fresh-boot reconciliation.
- Wrong permit binding burns the permit inside the existing authorization
  boundary and reaches no selector storage.
- Clean empty media with protected value zero remains the first-baseline path;
  it is not converted into destructive service recovery.
- Policy, storage, exhaustion, reset, save, verification, or post-clear race
  failure never advances protected history and never exposes the map.
- Once a replacement selector is saved, any protected conflict, advance error,
  unavailable readback, or unexpected final value remains ambiguous-mapless
  and requires fresh-boot reconciliation.

Map unavailability does not stop radio, messaging, alerts, position sharing,
vehicle integration, or USB recovery.

## Current evidence and limits

Twelve deterministic host groups cover selector-save-before-protect ordering,
local/protected generation selection, empty media with nonzero history, initial
retryable and latched source failure, non-service owners, exact permit binding,
clean-first-use routing, invalid policy, storage failure, exhaustion, partial
or dishonest clear, prepared/uncertain save, post-clear selector race,
uncertain applied protected advance, pre-advance conflict, and final readback
mismatch. The suite passes 100/100 focused repeats. All sixteen map suites and
the complete 74-executable host matrix pass locally under strict C++17
warnings-as-errors, including publication safety.

This is common host composition only. It is not a lock, credential verifier,
service UI, protected counter implementation, factory reset, device-replacement
authority, physical package/storage adapter, power-loss result, filesystem,
renderer, target task, or on-device result.
