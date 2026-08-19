# Android client foundation

Status: OT-036/OT-038 fake application foundation, the OT-041 lifecycle-safe
BLE runtime boundary, the OT-043 Android BluetoothGatt facade, and the OT-045
explicit local-test/Bluetooth UI binding, plus the OT-047 host-tested one-phone
authorization request state/UX and the OT-049 authorization wire-codec/tracker
mirror, plus the OT-051 provisional-authorization runtime
orchestration, the OT-053 protected-read production composition, the OT-055
user-started connected-device foreground-service ownership boundary, and the
OT-060 foreground-only screen-retention contract, the OT-061 one-candidate
physical BLE discovery result, the OT-062 exact Trail entry-artwork integration
and physical visual acceptance, and the OT-085A bounded physical public BLE
read/disconnect acceptance. This directory contains a buildable
Android application shell and pure Kotlin implementations of the brand-neutral `OTB0/v0`,
`OTC0/v0`, `OTX0/v0`, `OTN0/v0`, `OTA0/v0`, and `OTR0/v0` records.

The visible working product name is `Limited Underground Trail`. The stable,
technical application and package namespace is
`io.github.nbjelanovic.otclient`; customer-facing working names do not enter the
protocol or package identity.

## What runs today

- `protocol` encodes and strictly decodes the 16-byte protocol-info record and
  bounded fragment envelope. Tests consume the same golden bytes recorded for
  the C++ codec in `tests/fixtures/companion_protocol_v0_vectors.csv`.
- `app` first requires an explicit Local test or Bluetooth device choice. It
  never substitutes fake data after a Bluetooth failure. Each mode renders its
  own disconnected, selection/scanning, connecting, connected, and failed states.
- Local test mode uses `FakeCompanionTransport`. It exposes two deterministic
  local choices and permits one active fake connection. It performs no scan,
  opens no Bluetooth or USB device, and proves no radio behavior.
- Bluetooth device mode binds `BleCompanionRuntime` to the concrete Android
  facade. The runtime owns one scan or GATT lease, rejects stale callbacks,
  enforces the accepted security/MTU/protocol-info/Stream/initial-snapshot
  order, requires Stream indications for authoritative snapshots/results,
  honors the peer's payload bound, correlates exact session/action fragments,
  confines callbacks/state publication to its creation thread, and owns bounded,
  cancellable negotiation, action-result, and reconnect timers.
- State observers are notification-only. Reentrant lifecycle, disconnect, and
  close controls are bounded and deferred until delivery ends (final close wins);
  request, selection, and protocol mutations are rejected. Every post-publication
  scan, GATT, claim, and timer side effect revalidates its generation and lease.
- `TrailConnectedDeviceService` is the sole owner of the real facade, runtime,
  authorization controller, GATT leases, and timers. The Activity owns only Local
  test state plus a bounded binder observation. Activity stop, rotation, and
  unbind release only that observation; they never create a second runtime, stop
  the service, or retry a claim. Explicit mode exit stops the service, while
  service destruction closes the controller/runtime/facade graph exactly once.
- `AndroidBluetoothGattFacade` compiles against SDK 35 and supports Android
  API 31+ with version-gated API 31/32 compatibility calls. It uses an exact
  service-UUID scan filter, opaque bounded endpoint tokens, `autoConnect=false`,
  LE transport, the exact service and three-characteristic GATT profile, write-with-response,
  and CCCD indications. It serializes callbacks and timers on the Android main
  thread, owns at most one scan or connection, bounds scans to 15 seconds, and
  ignores stale callbacks while closing permission-revoked work with typed failures.
- The Activity can start the service only from the visible, explicit Bluetooth
  action after re-reading both Android 12+ Nearby Devices permissions. Lifecycle,
  restore, binding, and permission callbacks cannot start it. The manifest adds
  only the base and `connectedDevice` foreground-service permissions plus
  `POST_NOTIFICATIONS`; it retains `BLUETOOTH_SCAN` with `neverForLocation` and
  `BLUETOOTH_CONNECT`, and adds no location, network, or storage permission.
  Android 8-11 can run Local test mode, but real BLE remains explicitly
  unsupported below API 31 despite the app's minSdk 26.
- The service is non-exported, uses `START_NOT_STICKY`, has no boot receiver or
  background auto-start, and calls `startForeground` before constructing the BLE
  owner. Its fixed low-importance notification contains only the public product
  and service-running labels—no device name/address, message, token, or wire
  correlation. On Android 13+, notification denial does not block foreground-
  service start: the UI reports reduced drawer visibility and preserves Android's
  Task Manager disclosure rather than claiming the service is hidden or stopped.
- The explicit Bluetooth mode now composes the concrete facade with
  `RuntimeDeviceAuthorizationClaimClient`. Android `BOND_BONDED` is only a
  prerequisite; it is never represented as Android-measured encryption or MITM
  authentication. Only a successful read of the exact current ProtocolInfo
  characteristic, protected by the device's ATT access rules, can admit the
  provisional v0.1 claim path. There is no silent downgrade to fake mode.
- Bluetooth selection explicitly asks either to authorize this phone or replace
  a lost phone. A bounded 30-second state machine accepts only a bounded,
  lease-local event token and an exact authoritative pending, accepted, denied,
  or replaced result, plus a local expired/uncertain state. The device-issued
  128-bit wire correlation remains private inside the strict response tracker; it
  never enters controller/UI state and is neither displayed nor persisted.
  Replacement requires the distinct replaced result, and the phone cannot invent
  ownership. Mode changes, permission loss, lifecycle stop, and destroy cancel the
  claim and suppress stale results.
- Protected-read, claim, and lifecycle tests remain host-only. The visible
  physical-control instructions and deterministic tests do not prove a button
  press, bonding, application authorization, phone replacement, radio continuity,
  or a live Bluetooth connection on hardware.
- OT-049 mirrors the frozen C++ `OTL0`, `OTP0`, and `OTF0` authorization records
  plus their dedicated `OTC0` frame kinds from the same ten-row fixture. Its pure
  Kotlin provisional-response tracker requires explicit future capability,
  encrypted-link, authenticated-bond, transport-generation, device-session,
  exchange, purpose, and opaque-correlation evidence before it can report a
  client-observed accepted/replaced terminal. It performs no I/O and grants no
  device authority.
- OT-051 adds a strict separate 20-byte `OTB0/v0.1` decoder for the explicit
  claim capability and device-issued provisional session nonce. The runtime-backed
  claim client requires an exact device-protected record read after the bonded-device
  prerequisite, requests the decoded normal MTU,
  enable Stream indications, write one exact claim Start, track exact Pending and
  terminal frames, and send an explicit Snapshot Request only after an exact
  Accepted/Replaced terminal. Normal snapshot/action state remains unavailable
  before promotion. Denial, timeout, permission loss, disconnect, malformed or
  stale frames, and lifecycle release close the exact generation without automatic
  claim retry; transport timeout is local uncertainty, never an invented denial.
- `MainActivity` reaches the service-owned runtime-backed claim client only after
  explicit Bluetooth mode, Nearby permission, and a second visible service-start
  action. Exact v0.0, missing claim capability, bond/security/authorization errors,
  and an unreachable device produce bounded unsupported/unavailable states; a
  post-Pending connection loss is authority-unknown and requires checking the
  physical device. OT-056 now codes and build-links firmware startup,
  registration, and advertising, but that path was not run or flashed;
  protected storage/private-bond and physical-control admission remains denied,
  every connection is coded for immediate termination, and no claim or normal
  command is admitted. This source checkpoint therefore provides no live claim
  or device evidence.

The Activity-owned Local/mode controller does not persist state across process
recreation and returns conservatively to the explicit mode choice. A service
process recreation is `START_NOT_STICKY`, creates no owner from a null/stale
intent, restores no Ready/claim state, and performs no automatic reconnect or
claim resend. A configuration-change unbind does not destroy an already started
service; rebinding only observes its current bounded public state. Platform errors map to fixed typed values;
raw addresses, names, identifiers, status codes, and exception text do not enter
UI state.

The concrete gap to a first real Ready session is therefore explicit: start and
advertise the accepted target service/controller, bind the provisional lifecycle
to exact target security, physical-control, private-binding, and persistence
events, then run the exact app against that device firmware. The
current tests validate the pure mode, foreground-service admission/ownership,
lifecycle, permission, notification-visibility, binder generation, token,
authorization codec/tracker,
provisional orchestration, GATT-profile, and operation-order boundaries plus compilation/lint; they do not
execute an Android OS service lifecycle, notification drawer, Bluetooth hardware,
or claim a live connection.

OT-057 adds a renderer-neutral Group / Location presentation model and a
scrollable, screen-reader-readable panel reachable from both Local test and
Bluetooth Ready views. Position state is explicit current, stale, or unavailable;
the model accepts only fixed-point coordinates, bounded source-reported age,
optional bounded accuracy, exact sharing state, and at most eight peers with
separately validated public display aliases. The Android presentation policy
accepts current ages from 0 through 300 seconds and stale ages from 301 through
604800 seconds. A status-only device revision degrades every retained coordinate
to unavailable instead of refreshing or freezing an old fix.

The production path creates only an unavailable device-authoritative location
view because the accepted device status payload does not yet carry position or
peer-location records. It never substitutes phone GPS. Local test mode uses fixed
deterministic coordinates and marks the section and every position/age/accuracy
card as fake. Coordinates and public aliases are redacted from normal model
`toString` surfaces. No phone-location API or permission, network permission, map
SDK, tile request, cache, or offline-map package renderer was added. This is a
host-tested model and UI-source foundation, not device location, map, emulator,
accessibility-service, or physical-screen evidence.

The accepted OT-060 Android-only gate passes 135 JVM tests across thirteen
suites (protocol 29; application 106) with zero failures, errors, or skips, plus
warning-as-error lint with `No issues found.` The isolated debug APK is
9,677,165 bytes with SHA-256
`9CE206EEEAE2B13FC5C1092CEF41C226607FD3A9905A5797D4EBE31F3DC7F01C`.
It adds no wake-lock permission or other manifest surface.

One owner-authorized physical Android 16/API 36 handset installed and launched
that exact APK. With the handset's original USB stay-awake value restored and
its normal 30-second timeout active, the untouched visible Trail Activity
remained awake, interactive, and at normal active brightness for 40 seconds.
Backgrounding released focus and reopening succeeded. The exact APK also
reached visibly fake Local test mode without a Bluetooth permission prompt or
the real BLE service. Under OT-061, the same exact APK was rebuilt byte-for-byte,
installed on one physical Android 13/API 33 phone, and reported exactly one
compatible OpenTrail service advertisement. No candidate was selected,
connected, paired, or identified. This is physical install, foreground-display,
and BLE-advertisement visibility evidence—not release signing, battery/heat/
endurance, accessibility-service, GATT exchange, authorization, Ready, LoRa/
GNSS, or field evidence.

OT-062 adds the owner-supplied 1774 x 887 Limited Underground Trail PNG
byte-for-byte as a fitted decorative image above the existing accessible brand
text. It is part of the real Compose entry surface, not a timed splash screen,
and introduces no delay, navigation state, permission, service, or Bluetooth
behavior. The exact isolated gate passes 136 JVM tests across thirteen suites
(protocol 29; application 107), lint with `No issues found.`, and produces a
12,236,702-byte APK with SHA-256
`0E3A9C91E4AB68F0D6C45FB1D5A613CED7EE33154155AB2D0E76CE453F52918E`.
The packaged artwork remains byte-identical to the approved source. That APK
was installed over the prior debug build on the physical Android 13/API 33
phone, cold-launched successfully, and the owner visually accepted the artwork
and framing; a Home/reopen check also succeeded. No screenshot or device
identifier was retained. Heltec OLED and future touchscreen rendering remain
separate hardware gates.

OT-085A used one temporary, test-only instrumentation surface on that Android
13/API 33 phone to select the only compatible advertiser, connect without a
pairing request, require the suffix-`0x04` characteristic to be READ-only, and
match the exact fixed 16-byte zero-capability public value. After a two-second
hold, phone disconnect completed and the same in-memory endpoint was
rediscovered. The owner separately observed `BLE CONNECTED` followed by
`BLE ADVERTISING`; Android did not measure the OLED. The transient run emitted
only `OT085_PHONE_ACCEPTANCE=PASS`, and no address, name, phone identifier,
private binding, screenshot, or detailed log was retained.

OT-085B extended that retained test-only instrumentation to wait passively for
the target's automatic disconnect. It binds callbacks to one exact GATT and
monotonic phase, accepts the successful disconnect callback only inside the
policy-derived 13–20-second window around the frozen 15-second target policy,
and contains no phone disconnect or automatic reconnect call. The first timed
attempt rejected an address-equality rediscovery assumption that was unsuitable
for the target's privacy-aware address rotation. The accepted run instead
required exactly one exact-service advertiser before and after the link. The
phone emitted four fixed PASS fields for public read, automatic termination,
compatible-advertiser return, and overall acceptance; the owner observed
`BLE CONNECTED` followed by `BLE ADVERTISING`. No endpoint identity was
required, inferred, or retained.

The updated standard gate passes 139 JVM tests across fifteen suites, lint,
debug assembly, and instrumentation assembly. This evidence still proves no
pairing, protected authorization, Ready, operational release, or supported-
device behavior.

The platform contract follows the official Android documentation for
[Bluetooth permissions](https://developer.android.com/develop/connectivity/bluetooth/bt-permissions),
[service-UUID scan filters](https://developer.android.com/reference/android/bluetooth/le/ScanFilter.Builder),
[BluetoothGatt operations](https://developer.android.com/reference/android/bluetooth/BluetoothGatt),
and [BluetoothGatt callbacks](https://developer.android.com/reference/android/bluetooth/BluetoothGattCallback).
The OT-055 foreground boundary also follows the official Android guidance for
[launching foreground services](https://developer.android.com/develop/background-work/services/fgs/launch),
[connected-device service types](https://developer.android.com/develop/background-work/services/fgs/service-types),
and [notification permission behavior](https://developer.android.com/develop/ui/compose/notifications/notification-permission), and [keeping an Activity screen on](https://developer.android.com/develop/background-work/background-tasks/awake/screen-on).

## Build

The checked-in wrapper pins Gradle 8.11.1 and its distribution SHA-256. The
project pins Android Gradle Plugin 8.7.3, Kotlin/Compose plugin 2.0.21,
compile/target SDK 35, build-tools 35.0.0, and Java 17.
`Test-AndroidFoundation.ps1` requires explicit JDK and SDK roots, changes no
global PATH, and puts Gradle caches and build outputs below the supplied
user-local cache root. The gate runs protocol tests, application-state tests,
warning-as-error Android lint, debug assembly, and debug instrumentation
assembly.

```powershell
.\Test-AndroidFoundation.ps1 `
  -JdkRoot 'C:\path\to\jdk-17' `
  -AndroidSdkRoot "$env:LOCALAPPDATA\Android\Sdk"
```

No signing key, production variant, Play Store configuration, device access,
live Bluetooth evidence, or installation command is present.
