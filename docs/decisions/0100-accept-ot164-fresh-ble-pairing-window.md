# Decision 0100: accept the OT-164 fresh BLE pairing window

- **Status:** Accepted
- **Date:** 2026-08-29
- **Scope:** Two experimental Heltec V4 bench nodes; local BLE pairing-window input and display behavior

## Context

OT-090 froze the target-neutral pairing and replacement contract, but the
experimental Heltec target did not yet expose its fresh six-digit pairing
window through a physical input or the OLED. OT-164 implements the first
bounded target-side portion: active-low GPIO0, a boot-release prerequisite,
40 ms debounce, hold for at least 3000 ms and release, one fresh uniformly
sampled six-digit candidate per exact 30-second window, and fail-closed visual
concealment.

The initial V0 application was installed and read back on both nodes, then
rejected before owner acceptance because a footer-restore failure could leave
the PIN visible. That authority is consumed by abort. The corrected V1 build
adds emergency OLED concealment and is a distinct reproducible application
under a fresh one-attempt authority.

## Decision

1. Accept the corrected V1 two-node application-only installation and exact
   readbacks under the non-reusable OT-164 V1 authority.
2. Accept the owner's bounded observation on both nodes: a short press does
   not open pairing; a hold of at least three seconds followed by release
   shows `PAIR` and six digits; the display conceals the candidate after about
   30 seconds; and reset conceals an active candidate.
3. Record no displayed PIN value, endpoint, port, MAC address, device-specific
   identifier, filesystem path, or raw command output.
4. Keep the authority consumed by success, non-reusable, and without any
   continuation or inherited attempt.
5. Admit no Android pairing, passkey-entry exchange, bond ownership,
   persistence, reconnect, replacement, protected-GATT authority, end-to-end,
   supported-hardware, field-readiness, release, or regulatory claim.

## Consequences

Both anonymous experimental Heltec nodes now run the identical corrected
507,296-byte application and have physically accepted the local pairing-window
input/display lifecycle. Android pairing and durable single-phone ownership
are the next distinct capability gate. V1 remains exact 43.75% and displayed
44%; this demonstrated device capability changes public status but not the
completion percentage.

## Evidence

- [OT-164 evidence](../../tests/hardware/OT-164-2026-08-29.md)
- [Corrected V1 authority](../../tests/benchmarks/display/OT-164-HELTEC-V4-BLE-PAIRING-WINDOW-BUILD-AND-FLASH-AUTHORITY-V1.json)
- [Corrected V1 execution receipt](../../tests/benchmarks/display/OT-164-HELTEC-V4-BLE-PAIRING-WINDOW-EXECUTION-RECEIPT-V1.json)
- [Rejected V0 execution receipt](../../tests/benchmarks/display/OT-164-HELTEC-V4-BLE-PAIRING-WINDOW-EXECUTION-RECEIPT-V0.json)
