# Android client foundation v0

Status: OT-036 build/test foundation, OT-038 semantic parity, and OT-041
lifecycle-safe unwired BLE runtime boundary, 2026-08-14.
This is not live BLE or physical-device evidence.

## Accepted boundary

The accepted Android foundation has three layers:

1. A pure Kotlin, brand-neutral `OTB0/v0` and `OTC0/v0` codec that mirrors the
   fixed C++ bounds and consumes shared golden vectors.
2. A native Kotlin/Jetpack Compose application shell with explicit
   Disconnected, Selecting, Connecting, Connected, and Failed states over a
   deterministic fake transport.
3. An unwired lifecycle-safe BLE runtime owner behind injected Android
   Bluetooth and scheduler facades. It is not selected by the visible
   application and the production facade is deliberately disabled.

The visible working identity is `Limited Underground Trail`. Stable technical
package/application identifiers do not contain that provisional product name.
The manifest requests no Bluetooth, nearby-device, location, internet, storage,
or device-management permission. The fake transport provides no discovery,
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

## Deliberate exclusions and next gate

The existing activity-scoped fake controller still does not own a production
BLE lease. OT-041 supplies the runtime/lifecycle contract only; Android
permission UX, the concrete Bluetooth and timer facades, allowlisted scan and
GATT callbacks, physical pairing/application authorization, UI integration,
configuration/process recreation, background policy, and physical one-phone/
one-device evidence remain later work under the accepted BLE GATT contract.

The current semantic workflow covers only typed status, four fixed quick
statuses, exact pending-alert acknowledgement, and position-sharing Start/Stop.
Message/history, peer position, group provisioning, and real device authority
remain absent. The Android client cannot claim a functional field workflow
until the lifecycle-safe runtime has a real Android Bluetooth adapter, real
target authority, and physical two-device evidence pass independently.

## 2026-08-14 local build evidence

The accepted local build used Temurin `17.0.20+8` (download SHA-256
`418497be5cf585bdd2203d6486a565d66d3f5e992d5630d45104cb873fab8122`),
the checked-in Gradle `8.11.1` wrapper (distribution SHA-256
`f397b287023acdba1e9f6fc5ea72d22dd63669d59ed4a289a29b1a76eee151c6`),
Android platform 35 revision 2, and build-tools `35.0.0`. The official Android
command-line-tools bootstrap archive `11076708` had observed SHA-256
`4d6931209eebb1bfb7c7e8b240a6a3cb3ab24479ea294f3539429574b1eec862`.
All environment changes were process-scoped; no global PATH was changed.

The current focused gate passes six envelope tests, ten semantic tests,
17 BLE-runtime tests, and eleven application-state tests,
warning-as-error Android lint with no reported issue, and debug APK assembly.
The exact isolated local debug APK was 9,545,745 bytes with SHA-256
`54C79FA4773A25704D1D33619B0AA93EED3CB7EA78E8B921A2D31FADEDD072BB`.
`aapt` confirms package `io.github.nbjelanovic.otclient`, min SDK 26, target SDK
35, the expected visible label, and no Bluetooth, nearby-device, location,
internet, storage, or management permission. AndroidX adds only its same-app
`DYNAMIC_RECEIVER_NOT_EXPORTED_PERMISSION`. The artifact is debug-signed local
evidence, not a release APK. No emulator, phone, BLE peripheral, serial port, or
LoRa device was enumerated, opened, installed to, or otherwise accessed.
