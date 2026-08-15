# Android client foundation v0

Status: OT-036 build/test foundation, OT-038 semantic parity, OT-041
lifecycle-safe BLE runtime boundary, OT-043 Android 12+ platform facade, and
OT-045 explicit Bluetooth-mode/lifecycle UI wiring, 2026-08-15.
This is not live BLE or physical-device evidence.

## Accepted boundary

The accepted Android foundation has five layers:

1. A pure Kotlin, brand-neutral `OTB0/v0` and `OTC0/v0` codec that mirrors the
   fixed C++ bounds and consumes shared golden vectors.
2. A native Kotlin/Jetpack Compose application shell with explicit
   Disconnected, Selecting, Connecting, Connected, and Failed states over a
   deterministic fake transport.
3. A lifecycle-safe BLE runtime owner behind injected Android Bluetooth and
   scheduler facades.
4. A concrete Android 12+ Bluetooth facade with exact UUID/profile,
   permission, API-version, main-thread, and privacy-safe failure policy.
5. An explicit Local test versus Bluetooth-device mode controller and Compose
   surface. Bluetooth mode owns permission request/settings recovery and
   scan/select/connect/disconnect without silently falling back to Local test.

The visible working identity is `Limited Underground Trail`. Stable technical
package/application identifiers do not contain that provisional product name.
The manifest declares only `BLUETOOTH_SCAN` with `neverForLocation` and
`BLUETOOTH_CONNECT`; AndroidX adds its generated same-app receiver permission.
It declares no location, internet, storage, or device-management permission.
The fake visible transport provides no discovery,
security, GATT, LoRa, GNSS, or delivery evidence. Its session, status, and queue
values are deterministic test state only.

## Fail-closed behavior

- Protocol info requires exact v0, the one known role, known capability bits,
  internally possible payload/MTU limits, at most 16 fragments, and exactly one
  active controller.
- Fragments require exact v0, a known direction/kind, zero reserve, nonzero
  unsigned 32-bit session/exchange IDs, valid index/count, and no more than 128
  payload bytes or 148 total bytes. Oversized input is rejected before copying
  payload bytes.
- Selection connects only an exact current fake token. Controller transitions
  cannot replace or strand an active fake connection; disconnect is explicit.
- User copy labels all connection evidence as fake and denies Bluetooth/radio
  proof.
- OT-038 mirrors the exact `OTX0/v0`, `OTN0/v0`, `OTA0/v0`, and `OTR0/v0`
  layouts and validates their allowed `OTC0` kinds. Nine shared C++ golden rows
  prevent a separate Kotlin wire dialect.
- Fake actions use bounded monotonic session/exchange IDs and round-trip the
  exact correlated result envelope. Queue/revision/session/exchange exhaustion,
  stale alerts, and invalid actions reject without changing fake state.
- OT-041 fixes the four accepted v0 GATT identifiers and requires encrypted and
  authenticated bond plus application authorization before MTU and Protocol
  Info negotiation. It accepts only an indication-capable Stream, opens with an
  exact authoritative snapshot, enforces both global and peer fragment bounds,
  and correlates every action result to the active session and exchange.
- One creation-thread owner holds at most one scan/GATT/reconnect lease.
  Generation checks reject released-lease callbacks; phase and action-result
  timers are bounded and cancelled on progress or shutdown; stop/destroy closes
  every lease. Observer callbacks cannot re-enter public mutations, and observer
  exceptions detach without interrupting transport ownership.
- OT-043 filters the exact service, bounds scan time/results and opaque tokens,
  supports the API 31/32 legacy and API 33+ value-safe GATT call shapes, and
  validates the complete service/characteristic/indication profile. Every
  active state- or data-bearing queued callback rechecks its operation-specific
  Scan or Connect permission; disconnected cleanup remains best-effort.
  Revocation and contained `SecurityException` clear local ownership and
  surface only a typed privacy-safe failure. Raw addresses and platform
  exception text never enter the runtime state.
- OT-045 admits Bluetooth work only after explicit mode selection and current
  Nearby Devices permission. One lifecycle binding owns stop/resume/destroy,
  runtime-to-facade final closure, late permission callbacks, and mode changes
  while scanning, connecting, or Ready. Cleanup requested during an observer
  callback is deferred and drained; close wins over queued transitions.

## Deliberate exclusions and next gate

The visible application now owns a production-shaped Bluetooth path and a
separate explicit fake path. The injected production application-authorization
authority remains deny-all, so Bluetooth cannot reach Ready. An accepted
physical authorization/revocation workflow, configuration/process recreation,
background policy, and physical one-phone/one-device evidence remain later work
under the accepted BLE GATT contract.

The current semantic workflow covers only typed status, four fixed quick
statuses, exact pending-alert acknowledgement, and position-sharing Start/Stop.
Message/history, peer position, group provisioning, and real device authority
remain absent. The Android client cannot claim a functional field workflow
until the lifecycle-safe runtime and concrete facade are wired to an accepted
security authority and UI, the target exposes a real secured GATT service, and
physical two-device evidence passes independently.

## Local build evidence

The accepted local build used Temurin `17.0.20+8` (download SHA-256
`418497be5cf585bdd2203d6486a565d66d3f5e992d5630d45104cb873fab8122`),
the checked-in Gradle `8.11.1` wrapper (distribution SHA-256
`f397b287023acdba1e9f6fc5ea72d22dd63669d59ed4a289a29b1a76eee151c6`),
Android platform 35 revision 2, and build-tools `35.0.0`. The official Android
command-line-tools bootstrap archive `11076708` had observed SHA-256
`4d6931209eebb1bfb7c7e8b240a6a3cb3ab24479ea294f3539429574b1eec862`.
All environment changes were process-scoped; no global PATH was changed.

The current focused gate passes six envelope tests, ten semantic tests, six
Android-platform-policy tests, 17 BLE-runtime tests, eleven fake application-
state tests, and nine mode/lifecycle tests,
warning-as-error Android lint with no reported issue, and debug APK assembly.
The exact isolated local debug APK was 9,595,057 bytes with SHA-256
`0CCD4DECAAAE712A587DB97BC744B515E97008532FAA834BBF3D7BE4C715D76C`.
`aapt` confirms package `io.github.nbjelanovic.otclient`, min SDK 26, target SDK
35, the expected visible label, optional BLE hardware, Scan with
`neverForLocation`, Connect, and no location, internet, storage, or management
permission. AndroidX adds only its same-app
`DYNAMIC_RECEIVER_NOT_EXPORTED_PERMISSION`. The artifact is debug-signed local
evidence, not a release APK. No emulator, phone, BLE peripheral, serial port, or
LoRa device was enumerated, opened, installed to, or otherwise accessed.
