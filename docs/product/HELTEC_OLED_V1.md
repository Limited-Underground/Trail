# Heltec 128 x 64 OLED V1 Layout

Status: owner-approved display direction; exact renderer/state implementation
and physical acceptance pending, 2026-09-04.

## Priority

The display renders one exclusive high-priority surface:

1. self-check or contained failure;
2. factory-reset confirmation or reset-in-progress;
3. transient pairing code;
4. incomplete region / radio transmit disabled;
5. normal status.

A lower-priority update never obscures pairing or destructive confirmation.
Reset-in-progress never claims completion.

## Normal status

The normal page contains:

- configured device name, with deterministic truncation still to be fixed;
- phone session state: authorized/ready, reconnecting, or not connected;
- group membership and location-sharing state;
- configured radio region and TX availability;
- estimated battery with unavailable/stale behavior;
- GPS fix/freshness and optional deliberate coordinate detail;
- bounded radio TX/RX indication;
- phone-synchronized clock.

The public person name remains an Android/discovery concept and is not the
normal OLED title. Coordinates require a deliberate detail page and do not
appear on the normal status page.

The clock follows the phone's selected 12/24-hour presentation when supplied.
The firmware runs it from monotonic elapsed time after a validated sync. Before
valid sync or after detected rollback/invalidity it displays `--:--`, never an
invented time. Temporary phone disconnection does not erase a still-valid
clock. Exact drift/resync/staleness policy remains an implementation gate.

## Setup and safety

The setup screen shows one non-secret human-readable label that the Android
selection screen repeats. The label must remain replaceable metadata and must
not become a BLE address, protocol identity, key derivation input, or permanent
public radio identifier.

The pairing screen shows the fresh six-digit Android passkey only for its
bounded pairing window and conceals it on every exit/failure path. Region
incomplete explicitly keeps radio transmission disabled. BLE link status alone
does not authorize “secure” or “ready” wording.

The physical reset flow retains the accepted held-button warning,
release-and-confirm behavior, timeout, and fail-closed erasure semantics.

## Acceptance still required

Host pixel-bound tests must cover every string, truncation, priority transition,
clock state, stale metric, and concealment path. Two reproducible target builds,
physical OLED inspection on both V1 units, failure injection, reboot/cold-power,
and current-draw impact remain unproved.
