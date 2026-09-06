# Heltec 128 x 64 OLED V1 Layout

Status: owner-approved display direction; host renderer/clock foundation added
2026-09-06. Target adapter passes the complete host matrix and two matching builds;
physical acceptance remains pending. See
[Decision 0105](../decisions/0105-ot178-host-oled-presentation.md) and the
[synthetic pixel preview](../testing/OT-178-OLED-PREVIEW-2026-09-06.png).

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

- configured device name, with bounded ASCII display/truncation defined in Decision 0105;
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
clock. The host model expires time at24hours, rejects rollback and old/future sync
callbacks, and needs a fresh authorized sync after invalidation. Actual drift,
phone resync transport/cadence and physical acceptance remain open.

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

Host frame, clock, target mapping and actual-port tests pass, along with two
reproducible target builds. Physical OLED inspection on both V1 units, runtime
stack high-watermark, hardware failure injection, reboot/cold-power and
current-draw impact remain unproved. Authenticated configuration/time sources
are still required before the normal status page can be accepted.

## Current target adapter boundary - 2026-09-06

OT-178 now connects the existing Heltec display port to the fixed OLED presentation owner. Ordinary target frames show region required and TX disabled because no live configuration authority is bound. The startup logo and large ephemeral pairing digits remain on their established paths. Invalid time, rollback and draw failure contain or conceal the display. No clock-sync transport, configured name/group/region authority or physical OLED acceptance is added. The complete host matrix passes and two fresh firmware builds match all six artifacts; no firmware was installed.

The normal status page above remains the product goal. It is not currently
reachable through invented configuration values: missing region authority
has priority. No phone clock-sync handler exists, so the host clock model does
not establish a working phone-synchronized target clock. Region/name/group
configuration and telemetry sources require their own accepted contracts.

Prepare the authenticated configuration and time-sync contract before broad normal-page hardware acceptance. Define exact device/session binding, validated name/region/group observations, clock format and sync freshness, and retain unknown states until real sources are bound. A separate bounded display-only physical preflight may inspect the verified build and restoration route; it does not install firmware automatically. Cold-power and website updates remain owner-deferred.

## Configuration/time implementation contract

[Decision 0106](../decisions/0106-oled-configuration-time-authority.md) now defines device-confirmed values and session-bound civil-time admission. It is design evidence only. Phone drafts remain separate from device readback, radio TX remains separately controlled, and a disconnected clock expires after its existing 24-hour limit. Next implement the host time-admission owner; no new OLED runtime behavior is accepted by this contract.

## Host time-admission increment

OT-178 adds a host-only fixed-memory time-admission owner around OledClock and an injected trusted authority source. It binds one challenge to exact owner/session generations, rejects stale or duplicate work without refreshing time, and separates disconnect retention from revocation/reset invalidation. All 21 focused groups and the complete host matrix pass; no wire, target or phone integration is added. The chosen two-second challenge deadline does not establish measured BLE latency or phone clock accuracy. Device integration and physical clock behavior remain open. See [evidence](../testing/OT-178-TIME-ADMISSION-2026-09-06.md).
