# Android client foundation

Status: OT-036/OT-038 fake application foundation, the OT-041 lifecycle-safe
BLE runtime boundary, the OT-043 Android BluetoothGatt facade, and the OT-045
explicit local-test/Bluetooth UI binding, plus the OT-047 host-tested one-phone
authorization request state/UX and the OT-049 authorization wire-codec/tracker
mirror. This directory contains a buildable
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
- `TrailAppLifecycleBinding` is the sole lifecycle owner. It synchronously
  releases scan, GATT, action, negotiation, and reconnect work at lifecycle stop
  and closes the runtime then facade exactly once at destroy. Reentrant lifecycle
  requests are deferred and drained after observer delivery; final close wins.
- `AndroidBluetoothGattFacade` compiles against SDK 35 and supports Android
  API 31+ with version-gated API 31/32 compatibility calls. It uses an exact
  service-UUID scan filter, opaque bounded endpoint tokens, `autoConnect=false`,
  LE transport, the exact service and three-characteristic GATT profile, write-with-response,
  and CCCD indications. It serializes callbacks and timers on the Android main
  thread, owns at most one scan or connection, bounds scans to 15 seconds, and
  ignores stale callbacks while closing permission-revoked work with typed failures.
- The activity constructs the concrete facade but exposes it only through
  Bluetooth device mode, and requests the two Android 12+ Nearby Devices
  permissions only after an explicit user action. Denial remains in Bluetooth mode with fixed public copy
  and a route to app settings. The manifest declares only `BLUETOOTH_SCAN` (with
  `neverForLocation`) and `BLUETOOTH_CONNECT`.
- The injected production security authority still defaults to deny-all. A
  granted user can explicitly scan and select a compatible advertisement, but
  no connection can reach Ready until the pairing/application-authorization
  authority is accepted and supplied. There is no silent downgrade to fake mode.
- Bluetooth selection explicitly asks either to authorize this phone or replace
  a lost phone. A bounded 30-second state machine accepts only an opaque,
  device-issued claim token and exact authoritative pending, accepted, denied,
  or replaced result, plus a local expired/uncertain state. Replacement requires the distinct replaced result;
  the phone cannot invent ownership, and claim tokens are neither displayed nor
  persisted. Mode changes, permission loss, lifecycle stop, and destroy cancel the
  claim and suppress stale results.
- The production authorization-claim adapter remains disabled. The visible
  physical-control instructions and deterministic injected tests do not prove a
  button press, bonding, application authorization, phone replacement, or radio
  continuity on hardware.
- OT-049 mirrors the frozen C++ `OTL0`, `OTP0`, and `OTF0` authorization records
  plus their dedicated `OTC0` frame kinds from the same ten-row fixture. Its pure
  Kotlin provisional-response tracker requires explicit future capability,
  encrypted-link, authenticated-bond, transport-generation, device-session,
  exchange, purpose, and opaque-correlation evidence before it can report a
  client-observed accepted/replaced terminal. It performs no I/O and grants no
  device authority.
- The tracker is not wired to `MainActivity`, `BleCompanionRuntime`, or the
  Android GATT facade. Current Protocol Info has no authorization capability bit,
  and current firmware denies protocol-info/command access before application
  authorization, so a live claim path would be circular. A later coordinated
  firmware/Android increment must add a restricted encrypted, bonded provisional
  transport and device-issued session bootstrap before enabling the client seam.

The Activity-owned mode controller does not survive Android configuration change
or process recreation; destruction closes its session and a recreated Activity
returns to the explicit mode choice. Platform errors map to fixed typed values;
raw addresses, names, identifiers, status codes, and exception text do not enter
UI state.

The concrete gap to a first real Ready session is therefore explicit: implement
and test the restricted provisional authorization transport/session bootstrap,
bind the tracker into the Android claim client, separately implement the
device-side authority/persistence/promotion, and run the exact app against device firmware
that exposes the secured GATT contract and authoritative initial snapshot. The
current tests validate the pure mode, lifecycle, permission, admission, token,
authorization codec/tracker,
GATT-profile, and operation-order boundaries plus compilation/lint; they do not
execute Android Bluetooth hardware or claim a live connection.

The accepted OT-049 gate passes 77 JVM tests across eight suites (protocol
suites 6, 10, and 10; application suites 6, 17, 11, 1, and 16) with zero failures, errors, or
skips, plus warning-as-error lint with `No issues found.`
The isolated debug APK is 9,627,825 bytes with SHA-256
`967FCD7A032ECED63789378F5B3C0F6AC86D06CE9CF3B6B16205E7C49B8093A3`.
It is local debug evidence only, not a release-signed, installed, emulated, or
physical-device result.

The platform contract follows the official Android documentation for
[Bluetooth permissions](https://developer.android.com/develop/connectivity/bluetooth/bt-permissions),
[service-UUID scan filters](https://developer.android.com/reference/android/bluetooth/le/ScanFilter.Builder),
[BluetoothGatt operations](https://developer.android.com/reference/android/bluetooth/BluetoothGatt),
and [BluetoothGatt callbacks](https://developer.android.com/reference/android/bluetooth/BluetoothGattCallback).

## Build

The checked-in wrapper pins Gradle 8.11.1 and its distribution SHA-256. The
project pins Android Gradle Plugin 8.7.3, Kotlin/Compose plugin 2.0.21,
compile/target SDK 35, build-tools 35.0.0, and Java 17.
`Test-AndroidFoundation.ps1` requires explicit JDK and SDK roots, changes no
global PATH, and puts Gradle caches and build outputs below the supplied
user-local cache root. The gate runs protocol tests, application-state tests,
warning-as-error Android lint, and debug assembly.

```powershell
.\Test-AndroidFoundation.ps1 `
  -JdkRoot 'C:\path\to\jdk-17' `
  -AndroidSdkRoot "$env:LOCALAPPDATA\Android\Sdk"
```

No signing key, production variant, Play Store configuration, accepted security
authority, device access, live Bluetooth evidence, or installation command is
present.
