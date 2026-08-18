# Decision 0018: Freeze the protected-storage candidate offline before any transition

## Status

Accepted on 2026-08-17 for offline artifact and admission work only.

## Decision

The `OTPS0/v0` candidate partition table is generated with the pinned ESP-IDF
v6.0.2 partition utility, decoded, and admitted only when its exact 3,072-byte
binary, table rows, entry checksum, and padding match the frozen contract.
Generated artifacts live under ignored build output and grant no device or
write authority.

A partition transition remains denied until one recovery set contains the
exact application already installed on `OT-DEV-001`, the exact source and
candidate partition tables, and an accepted ROM recovery route. A successful
source-region proof from an earlier operation is prerequisite evidence, not
authority for a later write. The future transition must rebind fresh installed
table, source-region, and recovery evidence to the same nonzero operation and
evidence-set identities.

## Current result

The candidate partition binary is frozen at 3,072 bytes with SHA-256
`F83EDE6D0F206D6032147A2AF0B526700BCB888A6C0CADDF6DD17724E5600E72`.
The exact installed 470,928-byte application requires SHA-256
`A7D8E672CF9169F1D1D4E86EEFF80399C47A145E7D64904C207DD5F1B23F359B`.
An offline rebuild from its recorded source commit completed but produced a
different digest, so it is not accepted or retained as recovery evidence.

Therefore the recovery bundle is incomplete and the partition transition is
not authorized.

## Boundaries

- Active `partitions.csv`, `sdkconfig.defaults`, firmware runtime, and installed
  device bytes remain unchanged.
- No port, device, flash, erase, reset, recovery, key, or eFuse action is part
  of this decision.
- No generated candidate or mismatched application binary is published as an
  installable firmware package.
- Protected NVS, bond persistence, GATT authorization, and Ready remain
  disabled.

Decision 0019 later accepted a private exact capture of the installed
application. The recovery route, fresh operation-bound evidence, keys,
rollback floor, exact-unit recovery validation, and write authority remain
open, so the recovery bundle and partition transition remain denied.

## Next gate

Decision 0019 closes the exact installed-application artifact prerequisite.
Define and validate the exact recovery route. Only after that passes may a
separate operation request fresh transition evidence and physical-write
authority.
