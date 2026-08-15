# Android client foundation v0

Status: OT-036 build/test foundation, 2026-08-14. This is not live BLE or
physical-device evidence.

## Accepted boundary

The first Android increment has two layers only:

1. A pure Kotlin, brand-neutral `OTB0/v0` and `OTC0/v0` codec that mirrors the
   fixed C++ bounds and consumes shared golden vectors.
2. A native Kotlin/Jetpack Compose application shell with explicit
   Disconnected, Selecting, Connecting, Connected, and Failed states over a
   deterministic fake transport.

The visible working identity is `Limited Underground Trail`. Stable technical
package/application identifiers do not contain that provisional product name.
The manifest requests no Bluetooth, nearby-device, location, internet, storage,
or device-management permission. The fake transport provides no discovery,
security, session, GATT, LoRa, GNSS, queue, or delivery evidence.

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

## Deliberate exclusions and next gate

The current activity-scoped controller is not configuration-change or process-
recreation safe and must never own a production BLE lease. A lifecycle-aware
owner, fixed typed error mapping, Android permission UX, allowlisted service
discovery, MTU negotiation, authenticated/bonded/application-authorized session,
Stream subscription, reconnect, background policy, and physical one-phone/
one-device evidence remain later work under the accepted BLE GATT contract.

No semantic quick-status, alert, position, message/history payload is invented
here. The Android client cannot claim a functional field workflow until those
typed device-authoritative payloads, a real target adapter, and physical two-
device evidence pass independently.

## 2026-08-14 local build evidence

The accepted local build used Temurin `17.0.20+8` (download SHA-256
`418497be5cf585bdd2203d6486a565d66d3f5e992d5630d45104cb873fab8122`),
the checked-in Gradle `8.11.1` wrapper (distribution SHA-256
`f397b287023acdba1e9f6fc5ea72d22dd63669d59ed4a289a29b1a76eee151c6`),
Android platform 35 revision 2, and build-tools `35.0.0`. The official Android
command-line-tools bootstrap archive `11076708` had observed SHA-256
`4d6931209eebb1bfb7c7e8b240a6a3cb3ab24479ea294f3539429574b1eec862`.
All environment changes were process-scoped; no global PATH was changed.

The focused gate passed six protocol tests, four application-state tests,
warning-as-error Android lint with no reported issue, and debug APK assembly.
The exact local debug APK was 9,914,201 bytes with SHA-256
`9DD61C05A0724001AAD32D3F82C39B255F73489D0161E85EE0125110DC495608`.
`aapt` confirms package `io.github.nbjelanovic.otclient`, min SDK 26, target SDK
35, the expected visible label, and no Bluetooth, nearby-device, location,
internet, storage, or management permission. AndroidX adds only its same-app
`DYNAMIC_RECEIVER_NOT_EXPORTED_PERMISSION`. The artifact is debug-signed local
evidence, not a release APK. No emulator, phone, BLE peripheral, serial port, or
LoRa device was enumerated, opened, installed to, or otherwise accessed.
