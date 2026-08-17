# Decision 0010: Reversible Companion Protected-Storage Foundation

Status: accepted for build and host validation, 2026-08-17.

## Decision

OpenTrail will implement the phone-authorization persistence algorithm before
provisioning irreversible target secrets or changing an installed device's
partition table. The common implementation uses two record slots and an
independent monotonic-generation authority. It publishes authorization state
only when one exact record matches the freshly read trusted generation.

The first target plan reserves a dedicated `ot_auth` NVS partition and keeps
the remaining application state in a separate `ot_state` partition. That plan
is a candidate only. The active Heltec partition table and sdkconfig remain
unchanged until partition migration, encryption keys, rollback authority,
recovery, and physical-write permission are accepted separately.

## Required write and restore order

1. Read the trusted generation from the independent floor authority.
2. Read and validate both record slots.
3. Accept only one exact slot whose encoded record generation equals the floor.
4. Write and exactly verify the inactive slot for `floor + 1`.
5. Atomically compare-and-advance the independent floor.
6. Re-read the floor and both slots.
7. Publish only when the new floor and exact candidate record agree.

Prepared-ahead, stale-only, duplicate-current, conflicting, corrupt, missing,
or ambiguous state fails closed. An error after either storage layer may have
changed is uncertain and requires a fresh boot reconciliation.

## Security boundary

- NVS encryption, an opaque private bond-reference store, and the binding PRF
  require target implementations and physical evidence.
- The binding PRF key must be distinct from any NVS-encryption key.
- Ordinary NVS, redundant slots, CRCs, and encrypted flash snapshots do not
  provide an independent rollback floor.
- No eFuse key is selected or provisioned by this decision. Provisioning is
  irreversible and requires a later exact plan and owner authorization.
- No current GATT write, pairing, application authorization, or Ready state is
  enabled by this foundation.

## Reset and recovery

Ordinary reset does not lower or erase the trusted floor. Same-device storage
replacement, owner deauthorization, blank replacement hardware, and recovery
remain separate authenticated operations. Until those operations and their
physical evidence exist, missing or replaced protected state remains closed.

## Evidence boundary

Host tests may prove record selection, inactive-slot rotation, exact readback,
floor ordering, reboot reconciliation, and failure containment. They do not
prove target encryption, secret provisioning, rollback resistance, power-loss
durability, secure deletion, physical recovery, or hardware compatibility.
