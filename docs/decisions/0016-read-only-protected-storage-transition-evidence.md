# Decision 0016: Read-only protected-storage transition evidence

Date: 2026-08-17

## Decision

OpenTrail admits an offline verifier and a separate, currently unauthorized
read plan for the exact `OTHP0/v0` to `OTPS0/v0` source proof. The verifier
accepts only an exact 3,072-byte installed partition table and an exact
1,048,576-byte all-`0xFF` source region, both bound to one nonzero operation and
evidence-set identity. It emits only a fixed schema and sanitized outcome.

The read plan is design-only. It selects no unit, contains no port or command,
and grants no device-read, write, erase, reset, key, eFuse, or runtime authority.
A later physical read requires a new, exact, operation-scoped owner
authorization. Any successful observation proves only that OT-070's source
evidence prerequisite is satisfied; it does not authorize a partition-table
transition or any protected-storage activation.

## Evidence handling

A later separately implemented one-use executor, privately bound to the exact
unit, operation, evidence set, and port, may read only:

- 3,072 bytes at `0x008000`, expected SHA-256
  `84569AA2BADF3F7294042129B19D0B480784A93A550ADA3253B57BC92A0671AB`;
- 1,048,576 bytes at `0xF00000`, required to be entirely `0xFF`, expected
  SHA-256
  `F5FB04AA5B882706B9309E885F19477261336EF76A150C3B4D3489DFAC3953EC`.

Raw bytes and temporary paths are removed before a result is emitted. The
result retains no port, device identifier, operation identity, evidence-set
identity, nonblank digest, or nonblank byte location. Cleanup uncertainty
fails closed.

No executable hardware reader is admitted by this decision while the plan is
denied.

## Current boundary

No device read occurred in OT-071. The active partition table, target build
inputs, runtime, installed image, and device state are unchanged. OT-071 adds
no transition, migration, recovery, provisioning, rollback-floor, pairing,
GATT authorization, or Ready evidence.
