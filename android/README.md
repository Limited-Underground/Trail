# Android client foundation

Status: OT-036 host/build foundation. This directory contains a buildable
Android application shell and a pure Kotlin implementation of the brand-neutral
`OTB0/v0` and `OTC0/v0` codecs.

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

The activity-owned controller is deliberately a shell fixture. It does not
survive Android configuration change or process recreation and must not own a
future Bluetooth lease. A later lifecycle-aware runtime owner must preserve
device-authoritative session and reconnect rules. Likewise, the fake transport's
free-form failure strings must not become the production adapter boundary; a
real adapter must map typed internal failures to fixed, privacy-safe public copy.

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

No signing key, production variant, Play Store configuration, BLE permission,
BLE adapter, device access, or installation command is present.
