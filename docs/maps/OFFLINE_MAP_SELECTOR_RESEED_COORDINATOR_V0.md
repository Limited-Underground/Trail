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

1. the caller supplies five explicit acknowledgements: operator confirmation,
   temporary map unavailability, selector-only scope, package retention, and
   review of the trusted generation;
2. the live guard is already running mapless with no active, prior, or staged
   slot and no map exposure;
3. the complete activation policy exactly matches the live guard;
4. the proposed baseline package passes every existing activation-evidence
   check before selector storage is read or changed;
5. the selector store can be inspected without an I/O failure; and
6. the caller holds exclusive ownership of the selector store for the entire
   operation.

The five booleans are only typed intent evidence supplied by an upstream
service workflow. They do not authenticate an operator, prove durable consent,
or protect the trusted generation. A real target must bind them to its own
authorization and audit policy.

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

## Failure behavior

- Missing acknowledgement, wrong live ownership, invalid package evidence,
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
states, acknowledgement booleans, and errors only. It has no path, filename,
geographic content, participant/device identity, credential, key, URL, or free
text. It cannot erase, transfer, open, authenticate, modify, delete, mount, or
render package bytes and cannot touch any non-selector persistence domain.

This host boundary is not a lock, target service UI, authenticated command,
audit trail, protected rollback source, or factory reset. Package retention is
an upstream acknowledgement, not something this coordinator can verify.

## Current evidence

Twelve deterministic host groups cover every acknowledgement, stopped/active
owner rejection, clean-first-use routing, advancement above local and trusted
generations, equal-generation conflict recovery, dirty media, invalid package
and policy, storage failure, exhaustion, partial and dishonest erase adapters,
prepared-write failure, uncertain commit, corrupt readback, stable restart
restore, and a selector appearing after verified clear. The selector-store
suite separately covers exact clear readback across fourteen groups. All eight
map suites pass 100/100 repeats, and the complete 66-executable host matrix
passes under strict C++17 warnings-as-errors.

This is abstract host ordering evidence only. No ESP32 storage adapter,
physical erase/atomicity/endurance/power-loss result, real operator
authorization, protected trusted floor, package authentication, filesystem,
renderer, display, target task, or on-device result is claimed.
