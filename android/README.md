# Android client foundation

Status: OT-036/OT-038 fake application foundation, the OT-041 lifecycle-safe
BLE runtime boundary, and the OT-043 build-integrated but unwired Android
BluetoothGatt facade. This directory contains a buildable Android application
shell and pure Kotlin implementations of the brand-neutral `OTB0/v0`,
`OTC0/v0`, `OTX0/v0`, `OTN0/v0`, `OTA0/v0`, and `OTR0/v0` records.

The visible working product name is `Limited Underground Trail`. The stable,
technical application and package namespace is
`io.github.nbjelanovic.otclient`; customer-facing working names do not enter the
protocol or package identity.

## What runs today

- `protocol` encodes and strictly decodes the 16-byte protocol-info record and
  bounded fragment envelope. Tests consume the same golden bytes recorded for
  the C++ codec in `tests/fixtures/companion_protocol_v0_vectors.csv`.
- `app` renders explicit Disconnected, Selecting, Connecting, Connected, and
  Failed states in Jetpack Compose.
- The only transport is `FakeCompanionTransport`. It exposes two deterministic
  local choices and permits one active fake connection. It performs no scan,
  opens no Bluetooth or USB device, and proves no radio behavior.
- `BleCompanionRuntime` is an unwired, fail-closed boundary for a future real
  Android BLE adapter. It owns one scan or GATT lease, rejects stale callbacks,
  enforces the accepted security/MTU/protocol-info/Stream/initial-snapshot
  order, requires Stream indications for authoritative snapshots/results,
  honors the peer's payload bound, correlates exact session/action fragments,
  confines callbacks/state publication to its creation thread, and owns bounded,
  cancellable negotiation, action-result, and reconnect timers.
- State observers are notification-only. Synchronous attempts to call back into
  the runtime are rejected so presentation code cannot interrupt lease setup or
  orphan scan, GATT, or timer work.
- `BleCompanionLifecycleBinding` releases scan, GATT, action, negotiation, and
  reconnect work at lifecycle stop and permanently closes the owner at destroy.
  It is not connected to `MainActivity`; the visible workflow remains fake.
- `AndroidBluetoothGattFacade` compiles against SDK 35 and supports Android
  API 31+ with version-gated API 31/32 compatibility calls. It uses an exact
  service-UUID scan filter, opaque bounded endpoint tokens, `autoConnect=false`,
  LE transport, the exact service and three-characteristic GATT profile, write-with-response,
  and CCCD indications. It serializes callbacks and timers on the Android main
  thread, owns at most one scan or connection, bounds scans to 15 seconds, and
  ignores stale callbacks while closing permission-revoked work with typed failures.
- The concrete facade is not constructed by the activity. The manifest declares
  only `BLUETOOTH_SCAN` (with `neverForLocation`) and `BLUETOOTH_CONNECT`, but
  this build contains no permission-request flow, so the shipped activity cannot
  scan. If the facade is later wired and permissions are granted, its default
  security authority still denies every connection before the runtime reaches Ready.

The activity-owned fake controller remains a shell fixture. It does not survive
Android configuration change or process recreation and owns no Bluetooth lease.
The separate BLE runtime uses typed blockers/failures and can be attached to an
Android lifecycle. The Android facade and main-thread timer adapter exist, but
there is intentionally no permission request, scanning UI, accepted pairing or
application-authorization authority, or activity wiring. Platform errors map to
closed typed values; raw addresses, names, identifiers, status codes, and
exception text do not enter UI state.

The concrete gap to a first real connection is therefore explicit: accept and
test the screenless-device pairing/application-authorization workflow, add an
explicit Nearby Devices permission UX, inject the accepted security authority,
then bind the facade and runtime to a lifecycle-safe UI owner. Device firmware
must expose the same secured GATT contract and authoritative initial snapshot.
The current tests validate the pure admission, permission, token, GATT-profile,
and operation-order reducers plus compilation/lint; they do not execute Android
Bluetooth hardware or claim a live connection.

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

No signing key, production variant, Play Store configuration, BLE permission
request UX, accepted security authority, live UI binding, device access, or
installation command is present.
