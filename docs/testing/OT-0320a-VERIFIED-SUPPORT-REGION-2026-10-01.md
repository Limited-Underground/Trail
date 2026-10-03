# OT-0320a — verified support region binding, 2026-10-01

Host scope complete and accepted by the owner. This is host evidence for the approved
revision-1 child under OT-0177a. Full OT-0320 support-export acceptance and its
production-usability/retention dependencies remain open. No V1 completion credit
or public website status change.

## Result

The support preview now projects `US915` from the current protected Ready
session's configuration only when configuration and region support are available,
the READ revision is positive, and the catalog ID/code is exactly 1 / US915.
Unset, unknown, zero/null revision and the other eleven catalog settings remain
`Not available`; the existing support enum supports only US915.

Pending READ/WRITE, timeout, uncertain dispatch and even an accepted WRITE
response cannot reuse a retained choice. A subsequent current positive READ is
required. Disconnect, reconnect, lifecycle stop, owner closure and every reset
state remove the region. Reconnection starts without previous readback proof.

Firmware version and device model remain unavailable because no authenticated
producer is supplied. Existing health projection, support schema, wire, enum,
setup choices and cache policy are unchanged. The binding adds no automatic
READ/WRITE, retained state, upload or recipient configuration.

## Changed sources

- `V1SupportRegionProjection.kt`: pure current-state projector, with only the
  accepted enum value leaving the configuration boundary.
- `V1HomeScreen.kt`: one support-snapshot argument bound to that projector;
  pre-existing Activity/layout changes are preserved.
- `V1SupportRegionProjectionTest.kt`: five projection/report regressions for
  accepted and unavailable states, all catalog choices, reset exclusion,
  current-session replacement and private-field sentinels.
- `BleCompanionRuntimeTest.kt`: four regressions through the existing protected
  codec/runtime/Android operation gate and actual service owner. A released and a
  current GATT deliberately receive identical valid bytes with the same nonce and
  pending exchange; only the current owner establishes support readback.

## Verification

Focused support/configuration/runtime JVM validation passed 95 tests, with no
failures, errors or skips. Independent source review has no remaining findings;
the scoped whitespace check passed. All 160 frozen Android inputs remained
unchanged through the final matrix and package audit; seven unrelated dirty
Android files still match their baseline hashes.

The single final offline foundation matrix plus V1-Test supplement passed in
3 minutes 18 seconds using JDK 17.0.20.1, Gradle 8.11.1, AGP 8.7.3, Kotlin 2.0.21
and Android SDK/build-tools 35. It reported 1,435 passing test results: 58 unchanged
protocol results reused from Gradle cache, plus 1,377 app test executions
(441 debug, 440 release, 496 V1-Test). No failures, errors or skips. Debug,
release and V1-Test lint each reported zero issues. Debug, instrumentation,
V1-Test and unsigned-release assemblies passed; instrumentation was assembled,
not run on a phone.

Exact command, executed from `C:/lu/OpenTrail/.private/ot177-publication`:

```powershell
./android/gradlew.bat -p ./android --offline --no-daemon `
  --project-cache-dir ./build/android-ot0320a-20261001-cache `
  :protocol:test :app:testDebugUnitTest :app:testReleaseUnitTest `
  :app:testV1TestUnitTest :app:lintDebug :app:lintRelease :app:lintV1Test `
  :app:assembleDebug :app:assembleDebugAndroidTest :app:assembleV1Test `
  :app:assembleRelease
```

Build outputs use an absolute `OT_ANDROID_BUILD_ROOT` pointing to the selected
checkout's `build/android-ot0320a-20261001` directory and the existing installed
dependency cache. The existing
`android/Test-AndroidUnsignedReleaseArtifact.ps1` passed against the exact
`app-release-unsigned.apk`: application ID `io.github.nbjelanovic.otclient`,
version 1 / 1.0.0, minimum SDK 26, target SDK 35, exact six permissions, two DEX
files, fourteen excluded test-diagnostic classes, non-exported application-scoped
support FileProvider, explicit URI grants, only `support-reports/` exposure,
backup/transfer exclusion, unsigned result and successful temporary cleanup.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Unsigned release APK | 8,771,268 | `c1f75985da8a564d11c87a7280f91aece0a14bb37c044329592aadf83ab5401d` |
| V1-Test APK | 12,007,386 | `41342c85fd9d80782fe15ac66ec58e8fcc3fca6ac1c199d8904f62787b676f9d` |
| Debug APK | 12,679,646 | `9bb1c01ae38635b8f595d4b011665278f8280b3ff0dbab1b0ba02f9ba3b94241` |
| Debug instrumentation APK | 16,042 | `a7eb1a3dd576373d084c62ad00d99256853e12554a66d8c4f4913cbbe322dcf8` |

## Limits and next gate

Deterministic host fakes exercise runtime/GATT ownership; these results do not
establish physical BLE or actual-phone preview, Save/Share, cancellation,
rotation, URI grant/cleanup, recipient receipt or external-copy deletion.
Pending Save text remains the historical report reviewed when Save was clicked,
distinct from a fresh preview. Opening sharing choices is not delivery evidence.
The existing eight-file / 24-hour-at-next-share cleanup rule is unchanged and is
not a guaranteed timed deletion promise.

No device access, installation, new production signing, Git mutation/network,
publication, website work or task-dependency bypass occurred. The owner accepted this revision-1 host child. Full OT-0320 retains separately authorized
phone acceptance and its existing production usability/retention requirements.
