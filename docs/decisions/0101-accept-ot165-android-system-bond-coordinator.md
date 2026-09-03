# Decision 0101: accept the OT-165 Android system-bond coordinator

- **Status:** Accepted
- **Date:** 2026-08-30
- **Scope:** Android initial system pairing admission for one exact selected BLE candidate

> **Current V1 clarification (2026-09-02):** OT-165 remains accepted historical
> implementation evidence for Android-owned passkey entry. Decision 0103 makes
> that initial attempt eligible only during the device's automatic 60-second
> unowned-boot window and provides no phone-replacement path.

## Context

OT-164 physically accepted the target-side fresh six-digit pairing window, but
the Android production facade still required a bond to exist before protected
GATT and had no path to ask Android to create that bond. The app must not read,
set, display, log, or persist the six-digit candidate; Android's system pairing
surface remains the only phone-side passkey entry authority.

## Decision

1. Bind one Android system-bond attempt to the exact selected opaque endpoint
   token and one monotonic facade generation.
2. Admit an already bonded exact candidate directly to the existing GATT path.
   For a fresh attempt, register a non-exported bond-state receiver, ask Android
   to create the bond, and require this attempt to observe `BOND_BONDING` before
   accepting `BOND_BONDED`.
3. Keep the passkey exclusively in Android's system UI. The application exposes
   no pairing-request interception, PIN setter, passkey callback, secret field,
   log, or storage path.
4. Consume the attempt on cancellation/failure, permission loss, a 30-second
   timeout, lifecycle close, disconnect, wrong action/device/generation, or an
   invalid state transition. No pairing failure may enter the runtime's
   automatic reconnect path.
5. Proceed to the existing GATT and application-authorization path only after
   the exact accepted bond outcome.
6. Admit no durable bond-store ownership, saved-owner validation, replacement
   cleanup, old-bond removal, protected-GATT target acceptance, physical phone
   result, signed release, end-to-end result, or field-readiness claim.

## Consequences

The Android production facade now owns a bounded initial OS pairing attempt
without exposing the six-digit secret and without silently retrying a failed
attempt. Durable device-side bond ownership and the physical two-phone workflow
remain separate gates. Android remains 60%; V1 remains exact 43.75% and
displayed 44%.

## Evidence

- [OT-165 evidence](../../tests/hardware/OT-165-2026-08-30.md)
- `AndroidSystemBondCoordinatorTest` and `BleCompanionRuntimeTest` focused pass:
  24/24
- Complete Android foundation matrix: 262 test executions, zero failures,
  debug/release lint and builds passed, unsigned release inspection passed
