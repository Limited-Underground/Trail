# Decision 0021: Select protected-root provider types offline

## Status

Accepted on 2026-08-18 for offline contract and fail-closed host validation
only.

## Decision

OpenTrail selects two provider types without selecting a physical eFuse block,
field, key, or provisioning sequence:

1. Both protected-key roles use distinct ESP32-S3 `HMAC_UP` eFuse blocks. One
   role is limited to the ESP-IDF HMAC-backed NVS security scheme for
   `ot_auth`; the other is limited to the private bond-binding PRF. A future
   physical admission must prove two present, provisioned, read-protected,
   operational, unequal block references under one fresh operation/evidence
   binding. The configured NVS block must match only the NVS role.
2. The authorization-generation rollback floor conditionally uses a dedicated
   custom ESP32-S3 user-eFuse thermometer field. A canonical value is a
   contiguous low-order run of set bits followed only by unset bits. Advances
   are exactly one bit and require an exact reread. Holes, unknown width,
   protection mismatch, exhaustion, or possible post-burn ambiguity close
   authorization; the floor never wraps, decrements, reseeds, or erases.

These are provider classes, not physical admissions. Exact key blocks,
counter block, first bit, capacity, protection state, and provisioning state
remain absent. Factual observations default absent or false.

## Rejected alternatives

The ESP32-S3 application `SECURE_VERSION` field is not the authorization floor.
It is a small firmware anti-rollback field and does not fit the current factory
application and recovery boundary. Ordinary NVS, duplicate flash records,
hash chains, or encrypted snapshots are rewritable and cannot independently
prevent rollback. An external secure element remains a future hardware-design
option, not a current-board provider.

## Failure and recovery semantics

Reset or power loss never lowers the floor. Boot must reconcile the observed
floor with the protected record before authorization opens. A prepared record
ahead of the floor is not published. A floor ahead of the available record
requires authenticated forward recovery and never a decrement. Revoke,
replacement, and authorization reset use the next-generation tombstone.
Blank protected media with a nonzero floor stays closed. Replacement hardware
is a new device at its own floor zero.

The accepted OT-077 source-table recovery route remains pre-authorization only
and requires the initial floor. Once the floor advances, that old-table route
is denied and recovery must remain on the protected-storage lineage.

## Authority boundary

This decision adds only pure evidence evaluators, host tests, and a declarative
manifest. It does not read inventory, generate or provision a key, burn or
read-protect an eFuse, initialize protected NVS, activate persistence, advance
a floor, access hardware, or authorize a partition/application write. A later
private read-only inventory and every irreversible action require separate
owner authorization and physical evidence.
