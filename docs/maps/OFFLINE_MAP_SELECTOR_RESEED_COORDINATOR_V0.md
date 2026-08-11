# Offline Map Selector Service Reseed Coordinator v0

Status: deterministic host-tested service ordering boundary, 2026-08-11

The service-reseed coordinator gives a previously used or dirty abstract map
selector domain one explicit recovery path. It is not a normal install or
replacement shortcut: first use remains with the
[first-baseline coordinator](OFFLINE_MAP_SELECTOR_BASELINE_COORDINATOR_V0.md),
and replacement of a healthy active map remains with the
[candidate coordinator](OFFLINE_MAP_SELECTOR_CANDIDATE_COORDINATOR_V0.md).

## Required service evidence

The coordinator proceeds only when all of these conditions are present:

1. the caller supplies a fresh, exact-operation permit minted by the
   [reseed authorizer](OFFLINE_MAP_SELECTOR_RESEED_AUTHORIZATION_V0.md) after an
   injected local-service backend verifies and consumes a short-lived grant;
2. the live guard is already running mapless with no active, prior, or staged
   slot and no map exposure;
3. the complete activation policy exactly matches the live guard;
4. the proposed baseline package passes every existing activation-evidence
   check before selector storage is read or changed;
5. the selector store can be inspected without an I/O failure; and
6. the caller holds exclusive ownership of the selector store for the entire
   operation.

The permit binds the complete policy, proposed package evidence, reviewed
trusted generation, boot session, and validity window. It is non-copyable and
single-use. The coordinator consumes it and rechecks boot plus checked use time
before checking the live owner or reading selector storage; wrong binding,
boot, time, or replay therefore causes no selector access. The upstream backend still owns
real authentication, administrator policy, challenge/replay state, local
confirmation, and audit. No concrete backend exists yet.

An exactly empty selector with a zero trusted floor is rejected and routed to
the non-destructive first-baseline path. A healthy active owner is also
rejected unchanged; it belongs to normal replacement rather than service
reseed.

## Clear, advance, commit, expose

For a valid service request, the coordinator:

1. records the highest locally observable selector generation, including the
   common generation exposed by an equal-generation conflict;
2. chooses the greater of that local generation and the reviewed external
   trusted floor, rejecting 64-bit exhaustion before erase;
3. constructs the proposed stable active state privately;
4. erases only the two abstract 64-byte selector records;
5. reads both records back and requires exact empty state;
6. re-inspects through `save_if_empty` so a selector inserted after clearing
   is never overwritten;
7. writes a canonical stable checkpoint at generation `base + 1` using the
   commit-last store protocol and requires exact readback; and
8. publishes the private active guard only after that verified save succeeds.

`reset_and_verify_empty` reports each erase result and both post-erase slot
states. An adapter returning success without actually clearing a record is a
verification failure, not a successful reset.

## Protected-generation composition

The separate
[protected service-reseed coordinator](OFFLINE_MAP_SELECTOR_TRUSTED_RESEED_COORDINATOR_V0.md)
replaces the caller-supplied reviewed floor with a protected-source inspection
before selector access. The permit must match that exact value. This lower
coordinator clears and saves against a private guard; the wrapper then advances
and exactly reads back protected history before publishing the recovered map.
Initial source failure reaches no selector storage, and post-save trust
uncertainty remains mapless for fresh-boot reconciliation.

## Failure behavior

- Missing, mismatched, or consumed permit, wrong live ownership, invalid
  package evidence,
  and clean first use cause no erase or write.
- Storage I/O failure or generation exhaustion detected during preflight
  prevents erase.
- Partial erase, retained content after a reported-success erase, a selector
  race after verified clear, uncertain commit, or bad readback stays mapless
  and requires reconciliation.
- A known prepared-write failure after verified clear stays mapless and
  requires service; the result preserves the fact that clearing was verified
  before the failed save.
- Map unavailability never grants authority over radio, messaging, alerts,
  position sharing, vehicle integration, or USB recovery.

## Data and authority limits

The interface contains typed policy, package evidence, generations, slot
states, an opaque permit, and errors only. It has no path, filename,
geographic content, participant/device identity, credential, key, URL, or free
text. It cannot erase, transfer, open, authenticate, modify, delete, mount, or
render package bytes and cannot touch any non-selector persistence domain.

This host boundary is not a lock, concrete target service UI or credential
verifier, authenticated transport, audit trail, protected rollback source, or
factory reset. Package retention remains target-backend evidence, not something
this coordinator can verify.

## Current evidence

Twelve deterministic host groups cover permit absence, exact binding and
single use, stopped/active owner rejection, clean-first-use routing,
advancement above local and trusted
generations, equal-generation conflict recovery, dirty media, invalid package
and policy, storage failure, exhaustion, partial and dishonest erase adapters,
prepared-write failure, uncertain commit, corrupt readback, stable restart
restore, and a selector appearing after verified clear. The selector-store
suite separately covers exact clear readback across fourteen groups. The
separate authorization suite adds ten groups. All ten map suites pass 100/100
repeats, and the complete 68-executable host matrix passes under strict C++17
warnings-as-errors.

Twelve additional protected-reseed groups cover protected-history-first access,
exact permit binding, selector-save-before-protect ordering, source failure,
clear/save/race isolation, and post-save protected uncertainty. That suite also
passes 100/100 focused repeats. All sixteen current map suites and the complete
74-executable host matrix pass locally.

This is abstract host ordering evidence plus a backend-neutral key/value
mapping only. No ESP-IDF backend, physical erase/atomicity/endurance/power-loss
result, concrete operator-authentication backend, protected-generation backend,
package authentication, filesystem, renderer, display, target task, or
on-device result is claimed.
