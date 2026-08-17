# Decision 0015: Safe Heltec Protected-Storage Partition Transition

Status: accepted for host validation only, 2026-08-17.

## Decision

OpenTrail will not promote the inactive `OTPS0/v0` protected-storage layout
through an ordinary target build. The active `OTHP0/v0` layout owns the final
1 MiB of flash as `ot_state`; the candidate layout reassigns its first 64 KiB
to encrypted `ot_auth` and retains the remaining 960 KiB as `ot_state`.
Changing only the partition table could therefore reinterpret or destroy
unknown state.

One pure target-neutral admission guard now evaluates a proposed
partition-table-only transition. It performs no I/O and admits only an exact
`OTHP0/v0` to `OTPS0/v0` operation with:

1. a fresh readback proving the installed partition layout and digest are the
   exact source;
2. either a verified-blank source region or a separately implemented and
   verified semantic migration;
3. every evidence item bound to the same nonzero operation ID, evidence-set ID,
   nonzero generation, and exact source/candidate/source-region digest;
4. exact original partition-table and recovery-application hash evidence plus
   a verified ROM recovery route;
5. no runtime activation, eFuse operation, key operation, or other flash
   operation in the same request; and
6. a nonzero operation-scoped authority bound to the exact evidence generations,
   source, candidate, source-region, recovery artifacts, ROM route,
   and partition-table-only scope.

Checks are ordered and publish only the first denial. The guard does not own,
persist, consume, renew, or execute authority; a later physical executor must
separately prove one-shot consumption.

## Heltec admission boundary

The design-only `OTPST0/v0` manifest binds the exact source and candidate CSV
bytes and the exact final-region subdivision. It requires a later read-only
observation of all 1,048,576 source bytes as `0xFF`; unknown, short, or any
non-`0xFF` observation denies, no source bytes may be retained, and even an
exact blank result satisfies source evidence only. It grants no read or write
authority.

Before authorization-partition initialization, restoration of `OTHP0/v0`
remains conditional on exact blank media, unchanged artifacts, an accepted
recovery plan, and separate physical authority. After any authorization record
is committed, restoring `OTHP0/v0` is forbidden because it would reinterpret
or overwrite authorization bytes; recovery must remain on a forward
protected-storage lineage.

## Rollback-floor boundary

Partition restoration is not the independent authorization-generation floor.
The ESP32-S3 application secure-version field is not selected for this role:
it belongs to firmware anti-rollback, is finite, and does not fit the current
factory-app recovery boundary. No user-data eFuse counter is selected either.
Any irreversible counter requires a later exact unused-bit audit, secure-boot
and recovery decision, provisioning plan, and explicit owner authorization.

Ordinary reset never lowers the floor. Future revoke, replacement, or reset
must advance it and persist the appropriate tombstone. Missing protected media
with a nonzero floor stays closed; recovery may restore only exact current-
generation state.

## Evidence and deferred authority

Host tests can prove deterministic admission, exact layout binding, denial
ordering, and absence of execution authority. They do not prove installed
partition identity, blank flash, migration, physical recovery, key/eFuse
provisioning, rollback resistance, power-loss durability, or target behavior.

The active partition table, sdkconfig, target contract, runtime, image, and
device remain unchanged. No transition, read, write, erase, reset, key, eFuse,
pairing, GATT authorization, or Ready authority is granted.
