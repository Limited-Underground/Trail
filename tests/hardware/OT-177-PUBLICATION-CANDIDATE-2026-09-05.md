# OT-177 isolated recorder publication candidate — 2026-09-05

This candidate isolates the V1-Test connection recorder and format-3 diagnostic
correction from the broader uncommitted UI, inbox, setup and device-name work.
Its base is `25b5f9c43fc9741ec073d0d723442e2b7c9345f1`.

## Scope

V1-Test observes the existing connected-device service without stealing its
observer or stopping that service when its own binding closes. Its atomic log
holds at most 512 records and 64 KiB. Typed trace rows preserve both runtime
failure and the exact nullable lower-level connection diagnostic. Formats 1/2
remain readable and upgrade without inventing missing facts; malformed shapes,
unknown enum names and overflowing versions are rejected. Export explains that
rejection stage names identify the observed failure phase, not a proven explicit
device rejection.

The recorder, sharing cache/provider, retention policy and coarse connection
projection live only in the V1-Test variant. MainActivity receives only the
additional-tools hook and theme visibility needed by the test launcher. Service
notification routing resolves the current package's launcher, preserving the
normal application as fallback. The production UI redesign, status strip,
fullscreen behavior, screen gallery, setup drafts, inbox implementation and
device-name protocol are excluded.

## Evidence limits

The earlier mixed-worktree run had 921 Android tests and a different set of
source changes. Those test counts and APK identities are not this candidate's
build evidence. A prior phone observation reached accepted authorization and
then failed during initial Snapshot; its old log omitted the lower-level
diagnostic. The cause remains unresolved and cannot be reconstructed from that
log. This candidate has not been installed or physically accepted.

## Validation

The focused V1-Test suite passed 269 tests in 1m44s. The complete Android matrix
then passed in 2m35s: **742 tests**, zero failures/errors/skips (protocol 30,
debug 222, release 221, V1-Test 269). Debug/release/V1-Test lint and all requested
assemblies passed; 184 actionable tasks (118 executed, 42 cached, 24 up-to-date).

Microsoft JDK 17.0.20.1, Android SDK 35 / build-tools 35.0.0 and Gradle 8.11.1
were used with the existing user Gradle cache. From this isolated checkout:

```powershell
# Set JAVA_HOME, ANDROID_HOME and GRADLE_USER_HOME to the installed toolchains.
$env:OT_ANDROID_BUILD_ROOT = Join-Path (Get-Location) 'build/android'
./android/gradlew.bat -p android --no-daemon --project-cache-dir build/android-cache `
  :protocol:test :app:testDebugUnitTest :app:testReleaseUnitTest `
  :app:testV1TestUnitTest :app:lintDebug :app:lintRelease :app:lintV1Test `
  :app:assembleDebug :app:assembleDebugAndroidTest :app:assembleRelease :app:assembleV1Test
./android/Test-AndroidUnsignedReleaseArtifact.ps1 -JdkRoot $env:JAVA_HOME `
  -AndroidSdkRoot $env:ANDROID_HOME `
  -ArtifactPath build/android/app/outputs/apk/release/app-release-unsigned.apk
python tests/host/android_release_admission_tests.py
python tools/Test-PublicationSafety.py --root .
git diff --check
```

The affected host admission passes all 23 groups. The tracked/untracked
publication scan and whitespace checks pass. Independent unsigned-release
inspection verifies two DEX files, exclusion of all 12 test-only components,
unsigned status and temporary-file cleanup.

Artifacts under `build/android/app/outputs/apk/`:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `release/app-release-unsigned.apk` | 8,557,744 | `0BE7B9662B4C9D23599457E6CAF1B87C4A7AD92BE214D2568AB6A81BA6B40756` |
| `v1Test/app-v1Test.apk` | 11,663,306 | `CB07BCFFB15082EA9980D2BDC1D97811E22C360FCEF492E0B9CB247A44B19431` |

V1-Test artifact timestamp: 2026-09-06T02:47:44.5068855Z. These are new candidate
identities, not the prior mixed-worktree APKs. Production has the minimal shared
hooks described above, so no byte-identical production-APK claim is made.
No APK was installed, and no signing/release/physical acceptance is inferred.

Exact local commands and logs remain in the owner workspace's ignored `.private`
directory: `ot177-candidate-validate.ps1`, `ot177-candidate-focused.log`,
`ot177-candidate-matrix.log` and `ot177-candidate-host.log`. Independent source
review found no blocking defect; the recorder, trace and runtime match the
owner source, and the extracted category mapping preserves every runtime case.

No firmware or protocol implementation is modified. The prior Host validation
covers unchanged firmware/native components; this isolated candidate requires
the complete Android test/lint/assembly matrix, release-artifact inspection and
the affected host admission/publication checks. No phone, serial, radio or
hardware operation is part of this validation.

## V1 and publication boundary

This observation-only increment adds no V1 credit: Android 60%, V1 Companion
exact 43.75% / displayed 44%. The canonical next gate remains a separately
authorized app-restart reproduction with fresh enumeration and separate
discovery/connection latency, authorization, Snapshot, Ready and both failure
fields. No device-name activation is included.

Owner implementation files and the owner index remain untouched by candidate
staging/commit; canonical closeout records are updated separately. At this local
candidate checkpoint, remote ancestry, push and remote-commit verification remain
pending. The public website has not been changed by this candidate preparation;
the corrected Android milestone qualification still needs projection/publication.
