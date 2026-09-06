# OT-177 service-lifetime logging and cold-power deferral - 2026-09-06

The recorder previously followed the activity's service binding. It could miss
service transitions with no bound screen and terminate a trace on UI detach.
The V1-Test Application now supplies one observer owned by the service graph,
installed before controller activation and released once on owner teardown.
UI detach/rebind neither duplicates observation nor ends the service trace.

The main-source hook is optional and has no connection authority. Exceptions in
diagnostic callbacks are isolated. Construction failure, controller detach/close
failure and late callbacks cannot retain or double-release the observation.
Terminal recording is generation-bound, so old owner cleanup cannot end a newer
transaction. The four existing UI observation slots remain independent.

Only the V1-Test manifest names the recorder Application. The activity still
records its own lifecycle separately. Existing format-3, redaction, 512-record /
64-KiB storage and bounded queue rules remain. For format compatibility the
existing SERVICE_BINDING_RELEASED stage now denotes actual owner termination in
this wiring. Process death may prevent a final record; this is not lossless
telemetry or production logging. There is no upload or automatic service start.

## Software validation

Candidate based on remote-verified 442864d9b629d17810e5afdb4ef43fafb1d33dd3.
Its prior GitHub Host run 34014932829 is completed/success. The dirty owner
checkout is preserved; this increment changes only the isolated candidate.

Windows, Microsoft JDK 17.0.20, existing Android SDK, Gradle 8.11.1 wrapper.
Used the established normal-user Gradle cache and isolated build output:
`build/android`, project cache `build/android-cache`.

Focused `:app:testV1TestUnitTest` passed. Final affected matrix:

```text
:protocol:test
:app:testDebugUnitTest :app:testReleaseUnitTest :app:testV1TestUnitTest
:app:lintDebug :app:lintRelease :app:lintV1Test
:app:assembleDebug :app:assembleDebugAndroidTest
:app:assembleRelease :app:assembleV1Test
```

BUILD SUCCESSFUL: 184 actionable tasks, 40 executed, 144 up-to-date.
JUnit results: protocol 30, debug 229, release 228, V1-Test 279 = 766 test
executions; zero failures, errors or skips. Six new shared owner tests run in
all app variants and three new V1-only behavioral tests cover lifetime,
diagnostic failure and stale-generation termination. Independent source review
found no blocking defect. Instrumentation APK was built, not executed.

`android/Test-AndroidUnsignedReleaseArtifact.ps1` passed: unsigned/non-debuggable
production application, expected ID/version/SDK/permissions, two DEX files,
14 test-only diagnostic symbols excluded, temporary extraction cleaned.
Final gate passed: 10 future-concepts governance groups, 23 Android operational-
release admission groups, publication-safety scan and git diff --check.
No firmware source changed; prior firmware build/host evidence was reused.

| Local artifact (not installed) | Bytes | SHA-256 |
| --- | ---: | --- |
| build/android/app/outputs/apk/v1Test/app-v1Test.apk | 12018142 | 1A46466E2413022594C473063D5B62C5B34F7E87EF5191C8E5A859047D54F55C |
| build/android/app/outputs/apk/release/app-release-unsigned.apk | 8557744 | DE3EEAEE879065C965FBFBB5DF313582CD48E3E78E304D5B3FE7281E0F68B8F8 |
| build/android/app/outputs/apk/debug/app-debug.apk | 12644416 | 59445C0669DBF91BCA480C5BEED7DCE69E3999A2957B744E16DD40B65316D21A |
| build/android/app/outputs/apk/androidTest/debug/app-debug-androidTest.apk | 16042 | A7EB1A3DD576373D084C62AD00D99256853E12554A66D8C4F4913CBBE322DCF8 |

## Cold-power preflight and handback

Before software work, a fresh baseline used the retained Heltec V4/V4.2 and
Samsung Note20 Ultra SM-N986U, Android 13. Existing installed V1-Test APK readback
matched SHA-256 CB07BCFFB15082EA9980D2BDC1D97811E22C360FCEF492E0B9CB247A44B19431.
The unchanged corrected firmware remains 563,776 bytes, version
ot177-reconnect-v1, SHA-256
9ACDC90EEA9D0489ABABDD9B6F4E3C7AFF97162D03DDC09F8E4965D5A1D12784.
Radio configuration was unchanged; no radio transmission or range test occurred.

Session 11 transaction 1 reached fresh authorization, Snapshot and Ready in
1,602 ms from connection attempt (about 30 seconds discovery excluded).
Authorization took 195 ms and Snapshot 97 ms. This is an ordinary powered
baseline on the prior APK, not cold-power evidence or new recorder acceptance.

The owner confirmed USB plus battery power and that battery disconnection
requires opening the enclosure, then explicitly chose to defer cold-power and
continue other work. No power removal, serial open, reset, firmware write or APK
install occurred in this increment. Both Android apps were force-stopped and
verified stopped; temporary UI XML was removed. Ownership, bonds and app data
were preserved. Private baseline trace remains local; no device identifiers or
pairing material are published.

## Next gate and completion accounting

Prepare one non-destructive installation/acceptance of the exact new V1-Test APK:
retain owner/bond/data, verify fresh authorization/Snapshot/Ready, leave the
activity while the service runs, reopen it without false terminal/duplicate
milestones, then explicitly stop the service and verify one terminal record.
This requires phone acceptance; it has not occurred in this software increment.
The previous four reconnect results apply to the prior APK; see
[accepted physical evidence](OT-177-RECONNECT-ACCEPTANCE-2026-09-06.md).

Cold-power is owner-deferred. Factory-reset, two-pair, secure LoRa, endurance,
field, signing and production-release gates remain open. OT-177 remains partial.
All V1 milestone values are unchanged: exact weighted 43.75%, displayed 44%.
Canonical records are updated; website synchronization and deployment remain
owner-deferred to a bulk update.
