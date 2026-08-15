# Companion one-phone authorization v0

Status: host-tested target-neutral policy; not target-linked; no physical or
live BLE evidence, 2026-08-15.

## Authority boundary

The LoRa device, not the Android app, owns the authorized-phone decision. A
trusted future BLE bond store supplies a stable opaque 128-bit bond identity
token. The token must be nonzero, unique per bond, stable across reboot, and
unavailable to the peer. It must never contain a raw BLE address, public device
identifier, client-supplied bytes, or cryptographic key material.

One externally serialized `CompanionAuthorizationAuthority` instance is kept
for one complete boot challenge. It is not a thread-synchronization primitive.
Recreating it under the same boot challenge would discard replay state and is
forbidden by the target contract.

## Claim and ownership rules

- Restore must complete before any claim or authorization.
- Claims require encrypted and authenticated bond evidence supplied by the
  trusted adapter, never decoded from phone payload.
- Boot challenge, strictly increasing nonzero session challenge, and private
  nonzero controller binding must match the active controller context.
- Exactly one owner and one active controller are admitted.
- A local physical-input owner opens an exact claim, revoke, replace, or reset
  window for at most 30 seconds. Claim/replace binds the exact candidate bond;
  revoke/reset carries no candidate.
- Physical event tokens increase strictly for the boot. Invalid action enums,
  replay, clock rollback, stale session, wrong controller, second-controller
  use, same-owner replacement, and exhaustion reject without granting access.
- Revoke, reset, reclaim, and replace preserve the boot-wide session high-water
  mark so an older session challenge cannot become valid again.

## Persistence contract

The injected backend owns record encoding, confidentiality at rest, atomicity,
and rollback-resistant trusted-generation storage. `commit_and_verify` must
compare the expected generation, commit the complete candidate, advance the
trusted floor, read back, and return the exact verified record. An exact
`failed` commit result guarantees no durable change. An uncertain commit, conflict, rollback,
invalid record, or generation exhaustion latches the authority closed for the
boot.

The public status/result surface omits the owner token, physical event token,
boot/session challenge, and inactive controller binding. No implementation may
log or display those values.

## Accepted evidence and exclusions

Sixteen strict C++17 scenario groups and 100/100 focused repeats pass, including
restore, claim, duplicate authorization, second-controller denial, expiry,
clock rollback, replay, revoke, replace, reset, persistence failure/uncertainty/
conflict/rollback, invalid physical actions, re-entrant persistence callbacks,
and boot-wide session high-water preservation. The complete 118-executable host
matrix passes.

This evidence does not provide a NimBLE bond identity adapter, protected NVS
backend, physical-input binding, GATT integration, target build, phone bond,
device runtime, lost-phone field recovery, or support claim. Those remain
required before enabling the Android production claim client.
