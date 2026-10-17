# OT-175 phone inbox source publication validation

VERIFIED 2026-10-17T18:11:03Z. Host-only source extraction from the preserved
Android work into the current public source baseline. Existing first-message
consent, synchronized inbox and snapshot storage code are included without
connecting them to production controllers, BLE, device storage or a radio path.

## Scope and privacy boundary

The consent model exposes an opaque peer/request correlation request while
retaining the first message locally until an exact accepted decision. The inbox
model uses injected storage and typed access; durable import and explicit Read
remain separate. These types require an authenticated trusted adapter before
real use. Correlation tokens do not provide authentication themselves.

The snapshot codec is an internal app-storage format. The Android AtomicFile
adapter is deliberately unwired and stores plaintext in noBackupFilesDir; CRC32
only detects accidental corruption. A structural test forbids production source
from instantiating that adapter. Protected-key confidentiality/authenticity,
rollback and migration/recovery remain required before production use.

Expired message bodies are hidden from presentation. Pending receipt records
can still retain their full text, so this is not secure erasure or an accepted
message-deletion policy. Retention/capacity values remain injected test policy.
No V1 progress credit, deployment, website claim or hardware acceptance changes.

The host source-admission check now exempts only the exact
java.io.FileNotFoundException type from its existing java.io.File-prefix ban.
Calibration probes reject File and FileInputStream imports while allowing the
required exception; independent probes also reject FileOutputStream and a
FileNotFoundExceptionExtra lookalike. An initial extraction escaping error was
detected by independent behavioral probing and corrected before publication.
All 23 admission groups passed after the final exception-only correction. Newer
public reset, notification, support-export and gallery-exclusion guards remain.

## Validation

Toolchain: Microsoft JDK 17, Gradle 8.11.1, Kotlin 2.0.21, Android Gradle Plugin
8.7.3 and installed Android SDK platform/build-tools 35. Existing dependency
cache was reused with isolated build outputs and project cache. No package was
installed and no device was accessed.

Focused debug selection: 25 tests, zero failures/errors/skips:

- V1PendingDirectChatTest: 7;
- V1SynchronizedInboxTest: 11;
- V1InboxSnapshotCodecTest: 6;
- ConnectedDeviceManifestPolicyTest plaintext adapter guard: 1.

The complete matrix passed in 5m 31s: 1,510 test executions, zero failures,
errors or skips (protocol 58; debug 466; release 465; V1-Test 521).
All three lint tasks and debug, debug-AndroidTest, unsigned release and V1-Test
APK assemblies passed. The initial offline matrix could not resolve five
protocol-test dependency jars; a normal restore resolved them, then the full
matrix passed. No source correction was needed for that dependency gap.

Commands from the android directory, with JDK/SDK and build/cache environment
variables supplied externally:

```powershell
.\gradlew.bat --offline --no-daemon --project-cache-dir $PROJECT_CACHE :app:testDebugUnitTest --tests io.github.nbjelanovic.otclient.V1PendingDirectChatTest --tests io.github.nbjelanovic.otclient.V1SynchronizedInboxTest --tests io.github.nbjelanovic.otclient.V1InboxSnapshotCodecTest --tests io.github.nbjelanovic.otclient.ConnectedDeviceManifestPolicyTest.plaintextInboxStorageAdapterRemainsAppPrivateAndUnwired
.\gradlew.bat --no-daemon --project-cache-dir $PROJECT_CACHE :protocol:test :app:testDebugUnitTest :app:testReleaseUnitTest :app:testV1TestUnitTest :app:lintDebug :app:lintRelease :app:lintV1Test :app:assembleDebug :app:assembleDebugAndroidTest :app:assembleRelease :app:assembleV1Test
.\Test-AndroidUnsignedReleaseArtifact.ps1 -JdkRoot $JDK_ROOT -AndroidSdkRoot $SDK_ROOT -ArtifactPath $UNSIGNED_APK
```

Unsigned release artifact: 8,804,036 bytes, SHA-256
`a913d157270fb071bd7ab329fb1c886cd27ac7cc821143f07eadc5d9fc52c762`.
Packaged inspection passed identity/version, SDK, exact six-permission allowlist,
backup rules, non-debuggable state, two DEX checksums, exclusion of 14 test-only
diagnostic types, unsigned state and temporary extraction cleanup.

Publication-safety scanner and its regression scenarios passed. All 23 Android
operational-release admission scenario groups passed. Independent source review
found no critical issue in the exact extracted source and narrow guard changes.
Publication and configured remote checks remain separate gates.

## Remaining gates

Authenticated consent and inbox transport adapters, production queue/controller
composition, encrypted storage/key lifecycle, interruption and power-loss tests,
BLE/radio integration, physical receipt/reconnect behavior, signing and release
acceptance remain open. Host-only foundations are not integrated message delivery.
