# Offline Map Selector First-Baseline Coordinator v0

Status: deterministic host-tested first-use ordering boundary, 2026-08-11

The first-baseline coordinator establishes one already verified package as the
initial stable map without inventing a prior-good fallback. It is deliberately
limited to a genuinely unused selector domain and does not reuse the
replacement-map trial path.

## Why first use is stable rather than trial

An [`OTM0/v0` trial](OFFLINE_MAP_SELECTOR_CHECKPOINT_V0.md) is restart-safe only
when it names an exact prior-good package. A first device has no such package.
Creating a no-prior trial would either be unencodable or would pretend rollback
authority exists.

The existing [activation guard](OFFLINE_MAP_ACTIVATION_GUARD_V0.md) already
accepts a fully evidenced boot selection as stable active state. The baseline
coordinator uses that stable form only after proving the selector domain is
clean and committing its checkpoint before exposure.

## Required first-use evidence

Every call requires all of the following:

1. the live guard is running in `mapless` specifically because no selector
   exists—not because media is unreadable, ambiguous, invalid, or removed;
2. the caller's complete activation policy exactly matches the live guard;
3. `MapPackageEvidence` passes the guard's manifest, offline-rights,
   attribution, exact-byte integrity, reader, index, storage, size, and
   read-only checks;
4. the external trusted minimum generation is exactly zero;
5. both abstract selector slots are readable and empty; and
6. the caller holds explicit provisioning authority and exclusive selector-
   store ownership for the operation.

A nonzero trusted generation means the device has prior selector history even
if local slots are empty. That state requires reconciliation or the separate
[service-reseed coordinator](OFFLINE_MAP_SELECTOR_RESEED_COORDINATOR_V0.md); it
is never treated as first use.

## Commit-before-exposure ordering

The coordinator:

1. validates the clean mapless owner, policy, and package evidence;
2. inspects both selector slots and accepts only exact empty state;
3. constructs stable active state on a private guard with no prior slot;
4. re-inspects the store through `save_if_empty`;
5. writes canonical stable record generation 1 using the commit-last protocol;
6. requires exact committed readback; and
7. publishes the private active guard only after the save succeeds.

`save_if_empty` prevents a selector appearing between preflight and save from
being overwritten. It is not a lock; target composition must still provide
exclusive ownership for the complete call.

## Failure behavior

- Invalid package evidence is rejected before reading or writing selector
  storage, and the clean mapless guard remains unchanged.
- Active, stopped, dirty-mapless, staged, trial, or fallback state is not a
  first-use owner and is rejected without storage mutation.
- Existing valid records, equal-generation conflict, nonzero trusted history,
  or a record appearing after preflight require reconciliation.
- Invalid/uncommitted or unreadable slots require service and remain mapless.
- Prepared-write failure, uncertain commit, or bad readback never exposes the
  package. Commit/readback ambiguity requires reconciliation.
- The coordinator cannot erase or reset selector records. It cannot convert a
  used device into a first-use device.

Map unavailability does not stop radio, messaging, alerts, position sharing,
vehicle integration, or USB recovery.

## Data and authority limits

The interface contains typed policy, package evidence, fixed generations,
slot states, errors, and booleans only. It contains no path, filename,
geographic content, participant/device identity, credential, key, URL, or free
text. It cannot transfer, authenticate, open, modify, delete, mount, or render
a map package; choose a provider; reset storage; advance a protected floor; or
control communications.

The call itself is not authentication or operator consent. A target adapter
must bind it to an explicit provisioning workflow, authenticated package
policy, durable package bytes, and exclusive storage ownership.

## Current evidence

Ten deterministic host groups cover successful generation-1 commit, stable
restart restore, invalid evidence, non-clean owners, policy mismatch, nonzero
trusted history, existing selector refusal, invalid/unreadable/conflicted
media, prepared-write failure, commit ambiguity, corrupt readback, and a
selector appearing between inspect and save. All six affected map suites pass
100/100 focused repeats under strict C++17 warnings-as-errors.

This is host ordering evidence only. Service reseed is supplied separately;
no authenticated operator UX, physical package/storage adapter, concurrency primitive, atomic-byte guarantee,
wear/endurance or power-loss result, protected trusted floor, package
authentication, filesystem, renderer, display, target task, or on-device
result is claimed.
