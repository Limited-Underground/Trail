# OpenTrail Persistent Configuration Envelope v0

Status: host-tested OT-014 foundation, 2026-08-08

This component persists a small non-secret runtime configuration through a
board-independent storage interface. It proves versioning, accidental-corruption
detection, two-slot recovery, migration, safe defaults, and bounded write
behavior. It is not a secure keystore, authenticated record, monotonic rollback
counter, or validated ESP32 flash binding.

## Public configuration boundary

The current schema contains only:

- operating role (`client`, `client/repeater`, `fixed relay`, or command/interface);
- forwarding enabled;
- location broadcast enabled;
- display brightness percentage; and
- moving and stationary position intervals.

Safe defaults select the client role, disable forwarding and location
broadcasting, use 50% brightness, and use 60/300-second moving/stationary
intervals. Forwarding is valid only for repeater-capable roles. Moving intervals
must be 30–3,600 seconds; stationary intervals must be at least the moving value
and no more than 3,600 seconds.

Identity private material, group keys, recovery exports, invitation tokens,
message counters, and precise location/history do not appear in this structure.
The storage interface has separate `configuration` and `secret_material`
domains. `ConfigurationStore` always addresses only `configuration`; tests seed
a secret-domain sentinel and prove zero secret reads, erases, writes, or syncs.
An eventual secret store needs separate encryption, access control, lifecycle,
zeroization, and hardware evidence.

## Two-slot envelope

There are two fixed 64-byte slots. Multi-byte integers are little-endian.

| Offset | Bytes | Field | Rule |
| ---: | ---: | --- | --- |
| 0 | 4 | Magic | ASCII `OTCF` |
| 4 | 1 | Envelope version | `1` |
| 5 | 1 | Configuration schema | `1` legacy or `2` current |
| 6 | 1 | Envelope flags | Must be zero |
| 7 | 1 | Header length | `16` |
| 8 | 4 | Generation | Nonzero, monotonically increasing without wrap |
| 12 | 2 | Payload length | Exact schema payload length |
| 14 | 2 | Reserved | Must be zero |
| 16 | N | Configuration payload | Defensively decoded by schema |
| 16+N | 4 | CRC-32/ISO-HDLC | Header and payload; standard `123456789` vector is `CBF43926` |
| 60 | 4 | Commit marker | `ED174DC0`; written last |

Unused bytes remain erased (`FF`). A missing commit marker is `uncommitted`, not
a valid record. Malformed fields, invalid configuration combinations, and CRC
failure are rejected before the configuration is exposed.

Schema 1 has a six-byte payload with role, forwarding, brightness, and moving
interval. Migration disables the new location-broadcast field and supplies a
stationary interval no lower than both the safe 300-second default and the
stored moving interval. Loading a legacy record does not write automatically;
the next explicit save writes schema 2 at generation +1.

## Load, version, and rollback behavior

Boot reads both slots and selects the supported record with the highest valid
generation. If the newer record is corrupt or uncommitted, the older valid slot
is used. If neither is usable, the caller receives safe defaults plus an exact
diagnostic: blank/no record, integrity failure, storage failure, unsupported
version/schema, or equal-generation conflict.

A committed newer envelope or schema version blocks fallback to an older
supported record and blocks overwrite. This avoids silently downgrading settings
after older firmware encounters newer data. Equal generations with different
schema/configuration also fail closed to defaults. Reaching generation
`FFFFFFFF` requires explicit recovery rather than wrapping to zero.

The generation and CRC are not cryptographic anti-rollback. An attacker with
flash-write access can replace both. Production identity/group counters need a
separately designed rollback-resistant mechanism and secure storage evidence.

## Save and interrupted-write behavior

An explicit save validates the complete configuration, then targets the slot
that is not currently selected:

1. erase the inactive slot;
2. write header, payload, and CRC without the commit marker;
3. synchronize;
4. write the commit marker at its fixed final offset;
5. synchronize again; and
6. reread both slots and verify the new generation and exact configuration.

The previously selected slot is never erased during this transaction. Simulated
failure before the commit marker leaves the old record selected. Failure after
the marker but before the final sync may recover either the old or fully formed
new record depending on backend durability; it must never expose a torn mixture.
The in-memory fake makes the marker visible and therefore recovers the new
record in that boundary case.

## Wear policy

- An identical current-schema save returns `unchanged` and performs no erase,
  program, or sync operation.
- Changed writes alternate slots and increment generation.
- A store instance enforces a five-second minimum interval between successful
  changed writes and rejects monotonic-clock rollback.
- The fake backend models erase-before-program flash behavior and counts every
  read, erase, write, and sync by domain.

Five seconds is a host-test guard, not a flash-endurance claim. A concrete NVS,
raw-flash, FRAM, or filesystem adapter must document erase-block geometry,
atomicity/durability semantics, endurance, wear levelling, write amplification,
brownout behavior, and device-specific lifetime estimates. A reboot resets the
in-memory rate limiter, so a persistent coalescing/budget policy remains a board
integration requirement.

## Host evidence and remaining gates

Twelve scenario groups cover the CRC vector, blank/default behavior, secret
separation, current save/load, no-op suppression, rate limiting, alternating
slots and mutation counts, invalid configurations, newer-slot corruption
fallback, both-slot safe defaults, five power-loss boundaries, legacy migration,
future envelope/schema blocking, generation conflict/exhaustion, read failure,
and erase-before-program behavior.

OT-014's acceptance is complete for this bounded non-secret configuration
foundation. ESP32 storage binding, persistent duplicate/message counters,
cryptographic authentication, rollback-resistant secrets, encrypted recovery,
factory-reset zeroization, and physical brownout/endurance testing remain later
security, target, and recovery gates.
