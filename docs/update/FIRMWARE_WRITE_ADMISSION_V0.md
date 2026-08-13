# Firmware write admission composition v0

Status: pure host-tested composition with eight deterministic scenario groups;
no firmware writer, erase/reset/reboot call, target adapter, Windows UI, or
physical update result exists.

## Purpose

The future loader has two independent fail-closed decisions:

1. Is the signed firmware bundle acceptable under release policy?
2. Is the connected exact board and requested install mode acceptable for that
   image?

`FirmwareWriteAdmission` composes those results and refuses to produce
`ready_to_write` unless both pass and every field shared between their policies
agrees exactly. This prevents a UI or adapter from validating a legitimate
bundle against one profile and then evaluating a different board requirement.

## Exact cross-gate binding

The bundle policy and board-install requirements must have identical:

- hardware-profile identifier;
- processor family;
- target role (`bench_client`, `complete_client`, or `packaged_repeater`);
- minimum and maximum board revision;
- minimum bootloader schema; and
- maximum permitted image length.

The signed manifest image length must also equal the candidate length passed to
the board preflight.

## Decision rule

`ready_to_write` is true only when:

- bundle admission has no issue;
- board/install preflight has no issue; and
- the cross-gate binding issue mask is zero.

This result is an in-memory authorization state for a future exclusive loader
owner. It performs no I/O and is not evidence that a writer, flash layout,
readback, trial boot, rollback, or recovery process works.

Clean and recovery installs retain all board-preflight gates. In particular,
recovery still requires separate explicit destructive-erase confirmation and
physical recovery authorization even when the bundle is valid.

## Host evidence

Eight groups cover exact composition, independent bundle failure, independent
board failure, hardware-profile divergence, processor/role divergence,
revision/bootloader divergence, image capacity/candidate-length divergence,
and both destructive recovery authorizations.

## What remains

- construct both requests only from bounded parser/probe and protected policy
  owners;
- add one exclusive loader state machine that consumes a fresh result once;
- invalidate admission on reconnect, device change, file change, timeout, or
  operator selection change;
- implement signed-container and exact-board adapters;
- implement inactive-slot write/readback, boot selection, trial confirmation,
  rollback, and recovery; and
- build and visually/physically validate the Windows operator workflow.
