# Decision 0029: Bounded read-only BLE link status

- Status: Accepted for build-only implementation
- Date: 2026-08-18
- Work item: OT-085
- Scope: Current Heltec V1 non-privileged BLE surface

## Context

Decision 0028 defers rollback-protected companion authorization beyond the current Heltec V1 target and keeps authorization-dependent controls closed. OpenTrail still needs a small functional step that can prove the firmware's BLE connection lifecycle without creating an ownership, identity, configuration, messaging, radio, location, or authorization surface.

The existing target displays `BLE ADVERTISING`. OT-085 may define and build-test the next bounded state transition, but a build is not evidence that the firmware was flashed or that a phone connected successfully.

## Decision

OT-085 is a build-only implementation of:

1. one privacy-safe public fixed link-information read; and
2. a bounded BLE connection/display lifecycle.

The public link-information value is fixed product/protocol capability information. It is identical across units running the same build contract and must not contain or derive from a MAC address, chip identifier, serial number, device name, owner, phone, pairing record, group, radio configuration, location, path, operation identifier, or other persistent or unit-specific value.

The characteristic is read-only. It has no write, write-without-response, indicate, notify, subscribe, command, configuration, provisioning, authorization, or persistence behavior.

## Connection and display lifecycle

The build-only state machine has these visible states:

- no active BLE link: `BLE ADVERTISING`;
- one active BLE link: `BLE CONNECTED`; and
- after disconnection: return to `BLE ADVERTISING`.

Only one active link is represented. Duplicate connect callbacks must not create additional state, and stale or duplicate disconnect callbacks must fail closed without inventing a connection. A restart begins from the existing advertising state and does not restore a prior connection.

`BLE CONNECTED` means only that a BLE transport link is active. It must not be interpreted or displayed as paired, trusted, authorized, claimed, owned, ready, provisioned, online, radio-ready, or message-ready.

## Explicit exclusions

OT-085 does not provide or expose:

- BLE writes, commands, configuration, pairing authority, application authorization, or persistent bond/owner state;
- a stable device identity, unit name, MAC address, chip identifier, serial number, phone identity, or operation identifier;
- storage writes, counters, keys, secrets, groups, channels, messages, emergency/public transmission, or private data;
- LoRa state, frequency, region, transmit/receive state, signal, group readiness, or radio control;
- GNSS, GPS fix, location, map, battery, power, time, internet, offline/online, or device-health claims;
- `Ready` or any equivalent readiness label;
- OTA/DFU, firmware update, reset, recovery, bootloader, provisioning, or privileged behavior; or
- any dependency on the deferred companion-authorization runtime.

The implementation must add no production path to the protected-storage, authorization, provider, provisioning, or recovery foundations.

## Build-only acceptance boundary

OT-085 may be accepted locally only through source review, deterministic host tests, static target-admission checks, target compilation, and the complete repository gate.

Build evidence may establish that:

- the fixed public value contains only allowlisted product/protocol fields;
- the characteristic permissions are read-only;
- the connection state machine handles connect, duplicate connect, disconnect, stale disconnect, and restart deterministically;
- display text is limited to `BLE ADVERTISING` and `BLE CONNECTED`; and
- forbidden identity, write, persistence, authorization, radio, GNSS, telemetry, readiness, and privileged surfaces are absent.

Build evidence must not claim that firmware was flashed, a BLE service was observed over the air, a phone connected, the display changed on hardware, or the link remained stable in use.

## Physical acceptance remains separate

Flashing a device and performing live BLE/display acceptance require later explicit owner authorization. That operation must define the exact build, unit, port, flash/recovery boundary, phone-side observation, private evidence handling, stop conditions, and cleanup before any hardware action.

Until that separately authorized acceptance passes, public wording must say build-tested or build-only, not hardware-tested, device-validated, connected, operational, or ready.

## Progress and publication consequences

This decision alone creates no implementation or evidence and does not support a score change. OT-085 progress may change only after its separately recorded build evidence satisfies the canonical rubric. Hardware acceptance remains a distinct later gate.

Any public update must preserve the Decision 0028 authorization deferral and describe OT-085 as a non-privileged, read-only BLE link/status surface.
