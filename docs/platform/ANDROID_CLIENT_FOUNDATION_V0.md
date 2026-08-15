# Android client foundation v0

Status: OT-036 build/test foundation, OT-038 semantic parity, OT-041
lifecycle-safe BLE runtime boundary, OT-043 Android 12+ platform facade,
OT-045 explicit Bluetooth-mode/lifecycle UI wiring, and OT-047 disabled
device-authorization UX, plus OT-049 authorization-wire codec/tracker parity,
and OT-051 default-disabled provisional-authorization orchestration,
2026-08-15.
This is not live BLE or physical-device evidence.

## Accepted boundary

The accepted Android foundation has eight layers:

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
6. A disabled-by-default device-authorization claim seam and UI for authorize-
   this-phone or replace-lost-phone. Only exact bounded device results can
   advance state; timeout and invalid/lost results remain explicitly uncertain.
7. A pure fixed-size authorization-wire codec and externally serialized
   provisional-response tracker mirrored from the accepted C++ records and ten
   shared vectors. OT-049 alone left it unwired; OT-051 uses it inside the
   injectable runtime-backed client, while the shipped activity still injects
   the disabled claim client.
8. A strict `OTB0/v0.1` authorization-capability decoder and runtime-backed
   claim client that can execute the restricted Protocol Info, MTU, Stream
   subscription, Claim Start, Pending, terminal, and explicit Snapshot Request
   order when injected. The shipped activity does not inject it.

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
- OT-047 bounds opaque claim tokens at ingress, correlates exact purpose/token/
  generation, and separates authoritative Denied/Accepted/Replaced from local
  invalid-result and expired/unknown state. Claim cleanup is attempted on
  lifecycle stop, permission loss, mode switch, timeout, observer re-entry, or
  controller close; if an injected claim close throws, local claim authority and
  timer/runtime ownership still close independently. No token enters display,
  persistence, or public error copy.
- OT-049 mirrors exact 8-byte `OTL0/v0`, 24-byte `OTP0/v0`, and 28-byte
  `OTF0/v0` records under dedicated `OTC0/v0` kinds `0x03`, `0x84`, and
  `0x85`. Closed purpose/outcome/reason coherence and ten shared rows prevent a
  separate Kotlin dialect. The tracker requires explicit future negotiated
  support, encrypted/authenticated bond evidence, exact transport generation/
  session/exchange/purpose/correlation, Pending before one terminal, and exact
  close, called by the transport owner on disconnect, as the sole generation
  release. This slice supplies no disconnect callback wiring. Accepted/Replaced is a client
  observation only; it does not grant device authority. The opaque correlation
  is never displayed, logged, or persisted.
- OT-051 decodes only the exact 20-byte `OTB0/v0.1` claim record with capability
  bit `0x10`, one nonzero provisional nonce, an advertised normal MTU of at
  least 151, and a claim-capable payload limit from 28 through 128 bytes. The
  current device lifecycle advertises exactly 151. The client requires encrypted and
  authenticated bond evidence, reads Protocol Info before MTU negotiation,
  requests the advertised normal MTU, enables exact Stream indications, writes
  one Start, and accepts only exact Pending then terminal records for its lease.
  Accepted/Replaced permits one explicit Snapshot Request. Denial, timeout,
  permission loss, disconnect, malformed/stale input, and lifecycle release
  close without automatic retry; transport timeout remains local uncertainty.

## Deliberate exclusions and next gate

The visible application now owns a production-shaped Bluetooth path and a
separate explicit fake path. Its production claim client remains disabled, so
Bluetooth cannot complete phone authorization. A target-bound trusted bond
identity, physical authorization/revocation input, durable owner store,
configuration/process recreation,
background policy, and physical one-phone/one-device evidence remain later work
under the accepted BLE GATT contract.

The OT-051 client can express the accepted restricted flow, but `MainActivity`
still constructs `DisabledDeviceAuthorizationClaimClient` and the default
security authority remains deny-all. The target lifecycle is only build-linked
to an in-memory self-check; its live NimBLE definition remains baseline v0.0
and AUTHOR-denied, with no controller/service/advertising startup. Thus no live
negotiated capability or provisional GATT session exists. Enabling the client
requires exact target event wiring plus trusted bond, physical-input,
persistence, and device-authority adapters; write failure or timeout cannot be
shown as authoritative Unsupported or Denied.

The current semantic workflow covers only typed status, four fixed quick
statuses, exact pending-alert acknowledgement, and position-sharing Start/Stop.
Message/history, peer position, group provisioning, and real device authority
remain absent. The Android client cannot claim a functional field workflow
until accepted live bond/application-authorization adapters replace the disabled
claim seam, the target exposes a real secured GATT service, and physical two-
device evidence passes independently.

## Local build evidence

The accepted local build used Temurin `17.0.20+8` (download SHA-256
`418497be5cf585bdd2203d6486a565d66d3f5e992d5630d45104cb873fab8122`),
the checked-in Gradle `8.11.1` wrapper (distribution SHA-256
`f397b287023acdba1e9f6fc5ea72d22dd63669d59ed4a289a29b1a76eee151c6`),
Android platform 35 revision 2, and build-tools `35.0.0`. The official Android
command-line-tools bootstrap archive `11076708` had observed SHA-256
`4d6931209eebb1bfb7c7e8b240a6a3cb3ab24479ea294f3539429574b1eec862`.
All environment changes were process-scoped; no global PATH was changed.

The current focused gate passes 90 JVM tests across ten suites (protocol
suites 6, 10, 10, and 3; application suites 7, 9, 17, 11, 1, and 16), including
the frozen authorization-wire vectors, provisional tracker, v0.1 decoder, and
default-disabled orchestration, with zero failures, errors, or skips. Warning-as-error
Android lint with no reported issue, and debug APK assembly. The exact isolated
local debug APK was 9,644,209 bytes with SHA-256
`28ED3014ACE420F8C531625211D26BD3FB9D522F1349BACA0878F94726534D8A`.
`aapt` confirms package `io.github.nbjelanovic.otclient`, min SDK 26, target SDK
35, the expected visible label, optional BLE hardware, Scan with
`neverForLocation`, Connect, and no location, internet, storage, or management
permission. AndroidX adds only its same-app
`DYNAMIC_RECEIVER_NOT_EXPORTED_PERMISSION`. The artifact is debug-signed local
evidence, not a release APK. No emulator, phone, BLE peripheral, serial port, or
LoRa device was enumerated, opened, installed to, or otherwise accessed.
