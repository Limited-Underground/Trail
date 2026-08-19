# Android client foundation v0

Status: OT-036 build/test foundation, OT-038 semantic parity, OT-041
lifecycle-safe BLE runtime boundary, OT-043 Android 12+ platform facade,
OT-045 explicit Bluetooth-mode/lifecycle UI wiring, OT-047 device-
authorization UX, OT-049 authorization-wire codec/tracker parity, OT-051
default-disabled provisional-authorization orchestration, and OT-053
protected-read production composition, OT-055 explicit user-started
connected-device foreground-service ownership, OT-060 foreground-screen
retention plus physical Android install acceptance, OT-061 physical
OpenTrail service-advertisement discovery, and OT-062 exact Trail entry-artwork
integration plus physical visual acceptance, 2026-08-16. OT-061 is
advertisement-visibility evidence only—not GATT, authorization, Ready, LoRa, or
supported-hardware evidence. OT-062 adds only exact artwork packaging and one
owner-accepted physical visual observation.

## Accepted boundary

The accepted Android foundation has twelve layers:

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
6. A device-authorization claim seam and UI for authorize-
   this-phone or replace-lost-phone. Only exact bounded device results can
   advance state; timeout and invalid/lost results remain explicitly uncertain.
7. A pure fixed-size authorization-wire codec and externally serialized
   provisional-response tracker mirrored from the accepted C++ records and ten
   shared vectors. OT-049 alone left it unwired; OT-051 used it inside a
   default-disabled injectable runtime-backed client.
8. A strict `OTB0/v0.1` authorization-capability decoder and runtime-backed
   claim client that can execute the restricted Protocol Info, MTU, Stream
   subscription, Claim Start, Pending, terminal, and explicit Snapshot Request
   order when injected. At OT-051 acceptance the shipped activity did not
   inject it.
9. OT-053 composes that runtime-backed client with the concrete Android GATT
   facade in explicit Bluetooth mode only. A successful device-protected
   Protocol Info read is the security-path evidence; Android bond state alone
   is only a prerequisite. Failure never falls back to Local test mode.
10. OT-055 places that real facade/runtime/authorization graph under one
    explicit user-started, non-exported `connectedDevice` foreground service.
    Activity lifecycle owns only Local test state and a bounded observation of
    the service; it cannot create a second BLE owner or automatically retry a
    claim.
11. OT-060 adds Activity-window `FLAG_KEEP_SCREEN_ON` immediately after
    superclass creation. Android owns its visible-window lifecycle: it prevents
    timeout/dimming only while Trail is visible, releases on background, and
    adds no wake lock, permission, service, lock-screen, or brightness behavior.
12. OT-062 renders the exact owner-approved Trail PNG as a fitted decorative
    image above the existing accessible brand text. It uses the real Compose
    entry surface with no timed splash, delay, new navigation state, permission,
    service, or Bluetooth behavior.

The visible working identity is `Limited Underground Trail`. Stable technical
package/application identifiers do not contain that provisional product name.
The manifest declares `BLUETOOTH_SCAN` with `neverForLocation`,
`BLUETOOTH_CONNECT`, base and `connectedDevice` foreground-service permission,
and `POST_NOTIFICATIONS`; AndroidX adds its generated same-app receiver
permission. It declares no location, internet, storage, or device-management
permission.
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
- OT-055 admits a real service start only from the visible explicit Bluetooth
  action after re-reading both Nearby permissions. The non-exported service
  calls `startForeground` before constructing its single BLE owner, returns
  `START_NOT_STICKY`, has no boot receiver/background auto-start, and creates
  no owner from a null or stale intent. Stop, rotation, and unbind release only
  the Activity observation; explicit mode exit stops the service and service
  destruction closes the graph once. Android 13+ notification denial is
  reported as reduced drawer visibility while Android's Task Manager disclosure
  remains available; it is not treated as proof the service is hidden or
  stopped.

## Deliberate exclusions and next gate

The visible application owns a production-shaped Bluetooth path and a separate
explicit fake path. OT-053 wires the runtime-backed claim client only in
explicit Bluetooth mode; it has no fake fallback. The app treats successful
access to the device-protected exact v0.1 Protocol Info value as current-link
security-path evidence and never upgrades Android bond state into invented
encryption or authentication evidence.

The corresponding target adapter and OT-054 persistence prerequisite are
compiled and link-retained. At OT-056 acceptance, `app_main` was coded to
initialize NimBLE, register the protected service, and advertise after its boot
checks, but that path remained `BUILD-LINKED-NOT-RUN` and was not flashed or
exercised. OT-061 supersedes only that physical flash, runtime, and advertising
boundary: one experimental target booted and one physical Android phone found
one compatible service advertisement without selection, connection, or
pairing. Target persistence admission remains denied because protected
NVS/key/private-bond/rollback-floor prerequisites are unavailable; the coded
runtime immediately terminates every connection and never admits claims or
normal commands. Consequently the production app still cannot reach a live
claim or Ready state. A write failure or timeout remains local uncertainty and
cannot be shown as authoritative Unsupported or Denied.

The current semantic workflow covers only typed status, four fixed quick
statuses, exact pending-alert acknowledgement, and position-sharing Start/Stop.
Real device message/history payloads, an accepted real-device peer-position feed, group
provisioning, and real device authority remain absent. The Android client cannot claim a functional field workflow
until the target starts the accepted secured service with trusted bond,
persistence, physical-input, and application-authority adapters, and physical
two-device evidence passes independently.

## Local build evidence

The accepted local build used Temurin `17.0.20+8` (download SHA-256
`418497be5cf585bdd2203d6486a565d66d3f5e992d5630d45104cb873fab8122`),
the checked-in Gradle `8.11.1` wrapper (distribution SHA-256
`f397b287023acdba1e9f6fc5ea72d22dd63669d59ed4a289a29b1a76eee151c6`),
Android platform 35 revision 2, and build-tools `35.0.0`. The official Android
command-line-tools bootstrap archive `11076708` had observed SHA-256
`4d6931209eebb1bfb7c7e8b240a6a3cb3ab24479ea294f3539429574b1eec862`.
All environment changes were process-scoped; no global PATH was changed.

At OT-062 acceptance, the then-current Android-only gate passed 136 JVM tests across thirteen suites
(protocol 29; application 107), including the foreground-only screen and exact
artwork/no-delay policies,
with zero failures, errors, or skips. Warning-as-error Android lint reports
`No issues found.`, and debug APK assembly passes. The exact isolated local
debug APK is 12,236,702 bytes with SHA-256
`0E3A9C91E4AB68F0D6C45FB1D5A613CED7EE33154155AB2D0E76CE453F52918E`.
`aapt2` confirms package `io.github.nbjelanovic.otclient`, min SDK 26, target
SDK 35, the expected visible label, optional BLE hardware, and the previously
accepted permission surface, with no `WAKE_LOCK`, location, internet, storage,
or management permission.

One owner-authorized physical Android 16/API 36 handset installed and launched
that exact debug APK. With its original USB stay-awake value restored and its
normal 30-second timeout active, Trail retained the Activity screen-on flag,
awake/interactive state, and normal active brightness through an untouched
40-second interval. Backgrounding removed Trail from focus and reopening
succeeded. The exact APK reached visibly fake Local test mode without a
Bluetooth permission prompt or real BLE service. OT-061 then rebuilt that APK
byte-for-byte, installed it on one physical Android 13/API 33 phone, and
observed exactly one compatible OpenTrail service advertisement. Nothing was
selected, connected, paired, or identified. This does not prove release
signing, notification lifecycle, GATT exchange, device authorization, Ready,
LoRa/GNSS, endurance, or field behavior.

OT-062 installed the new exact APK over the prior debug build on that physical
API 33 phone. Cold launch succeeded, the owner visually accepted the complete
artwork and framing, and a Home/reopen check succeeded. The packaged 2,559,044-
byte PNG has SHA-256
`A3024504BA261ADDAFD2A85F49F6BCE630D1E9AB994EEA348D5842A6D2AB7422`,
matching the approved source exactly. This is Android entry-artwork evidence,
not an Android system splash, Heltec OLED render, touchscreen render, release,
accessibility-service, endurance, or field evidence.

OT-057 adds Group / Location presentation above this ownership graph. Real
Bluetooth mode reports coordinates unavailable because no accepted device
coordinate feed exists. Local mode uses separately labeled deterministic
synthetic cards. The presentation layer adds no phone GPS, map, tile, network,
location permission, storage, identity authority, or private correlation
surface.
