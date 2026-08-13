# Windows loader selected-device bundle match v0

Date: 2026-08-13

## Purpose

The Windows utility must not treat a familiar runtime name or USB family as
proof that firmware belongs on a received unit. This boundary compares one
already-inspected firmware-bundle manifest with the one explicitly selected
device while remaining separate from release admission and every write path.

## Required authoritative device fields

The matcher accepts only a separately established authoritative received-unit
profile containing:

- nonzero hardware-profile ID;
- exact processor identifier;
- exact product target role;
- nonzero received board revision;
- bootloader schema; and
- bounded maximum image bytes.

The manifest must match the hardware-profile ID, processor, and role exactly.
Its board-revision interval must contain the received revision, its minimum
bootloader schema must not exceed the received schema, and its complete image
must fit the received-unit capacity.

## Inputs that never create authority

The following remain useful inspection hints but can never satisfy this gate:

- USB vendor/product family;
- runtime model or installed MeshCore role;
- firmware version text;
- vendor-published family specifications;
- profile-candidate display text; or
- operator selection by itself.

Current Heltec and SenseCAP cards contain only runtime candidate evidence, so
the utility reports `Exact-device match unavailable` for all three.

## Result boundary

Every exact field has a separate match result, and any mismatch makes the whole
decision fail closed. A complete match means only that the authoritative
received-unit profile fits the inspected manifest. It does not establish:

- trusted production signer or revocation state;
- accepted release generation or rollback floor;
- destructive or recovery authorization;
- exclusive one-use write ownership;
- writer/readback/boot/rollback behavior; or
- Flash permission.

The matcher performs no file, USB, serial, reset, erase, write, reboot, DFU,
network, or identity operation.

## Evidence

The warning-free Windows loader suite passes 48 scenario groups. Coverage
includes one complete authoritative match, independent mismatches for every
field, runtime-only refusal, and proof that a match does not change bundle
admission. The live three-device inspection still reports zero ready to flash.
