# Decision 0012: Protected Authorization KV Slot Media

Status: accepted for host implementation and validation, 2026-08-17.

## Decision

OpenTrail will bind the two exact `OAP0/v0` authorization record slots to a
small protected key/value backend before selecting a concrete ESP-IDF NVS
handle or changing the Heltec target. The fixed candidate binding is:

- partition `ot_auth`;
- namespace `ot_owner`;
- slot A key `oap_slot_a`; and
- slot B key `oap_slot_b`.

Each present value must be exactly 32 bytes. A missing key is an absent slot;
an inexact value, ambiguous operation, or callback reentry fails closed and
publishes no record bytes.

## Durability rules

- A write stages one complete exact value and then requires an explicit durable
  commit.
- `not_ready` or `failed` may be returned before mutation only when the backend
  can prove no bytes changed.
- Every error after a value may have been staged or committed is `uncertain`.
- The existing two-slot coordinator performs the exact post-write reread and
  independently advances and rereads the rollback floor.
- There is no erase, reset, repair, retry, initialization, key provisioning, or
  rollback-floor operation in this adapter.

## Current boundary

The backend remains injected and target-neutral. The active Heltec partition
table, sdkconfig, runtime composition, denied GATT authorities, and physical
device do not change. The candidate `ot_auth` partition remains inactive. This
decision proves only the reversible record-media contract; it does not prove
protected NVS initialization, encryption keys, private bond storage, an
independent rollback authority, pairing, application authorization, Ready,
migration, recovery, or physical durability.
