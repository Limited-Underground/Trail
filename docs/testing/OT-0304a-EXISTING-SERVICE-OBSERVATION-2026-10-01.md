# OT-0304a existing-service observation correction

2026-10-01, approved revision 1. Host implementation and validation complete;
owner-accepted host scope. Actual phone confirmation remains separate.

## Defect and correction

The [two-phone checkpoint](OT-0304-ACTIVITY-RECREATION-2026-10-01.md) found that
rotation retained drafts but the replacement screen lost observation of the
started connection service. The actual replacement controller initially stayed
at ChooseMode because it required a request flag belonging to the old screen.

[TrailActivityController](../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/TrailActivityController.kt)
now probes only the existing service on ordinary lifecycle start.
[The Android connector](../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/AndroidConnectedDeviceServiceConnector.kt)
uses the actual binder with binding flags 0. Service creation remains behind the
separate explicit user-start path. Nothing persists a Ready claim or endpoint.

Live state is admitted after a successful observation lease and fresh permission,
exact port, owner generation, binding generation and observation-token checks.
An owner replacement on the same binder gets a fresh observation. Rejected,
dead or unavailable passive attachment releases local leases and does not stop
the surviving owner. Authority is retired before cleanup callbacks can run.

| Transition | Required result and host evidence |
| --- | --- |
| Ready owner; first screen stops/closes | Only its observation retires; ten start/scan/claim/disconnect/close counters remain unchanged |
| Replacement screen starts with current permission | Same current Ready session and owner generation observed across three replacements |
| Service absent, closed, null or dead | No invented Running/Ready, no service creation; ordinary explicit startup remains available |
| Old callback, changed permission or owner generation | Obsolete callbacks rejected; replacement owner observed freshly and genuine current disconnect displayed |
| Same screen reopens while attachment is delayed/absent | Idle/Start required until a current observation is accepted; no retained Running label |

## Discriminating and focused validation

The [composed regression](../../android/app/src/test/kotlin/io/github/nbjelanovic/otclient/ActivityServiceAttachmentTest.kt)
uses actual Activity controller, app controller, BLE runtime and service owner.
Android binding, GATT security/I/O and authorization transport are controlled
host seams; they do not establish physical transport or OS behavior.

The original production source failed the replacement assertion at ChooseMode.
The same primary regression passed after correction. Its exact before fixture
is retained privately: 10,105 bytes, SHA-256
`dbd746628a19d227cd119fb473b000d0817623b4c60e2c0501e5a7ece8560641`.
Independent review also caught a retained Running label after backgrounding;
the observer-history discriminator failed before that correction and passed
after clearing detached presentation. The first freeze remains superseded
evidence rather than being overwritten.

Final focused validation: **51 passed**, comprising 18 composed lifecycle tests,
one actual-adapter source-policy test and 32 existing service tests. Binding and
observation rejection/throw, synchronous callback then failure, revoked
permission, zero/invalid generation, actual closed/replaced owner, repeated
recreation, stale callbacks, genuine disconnect and cleanup reentrancy are covered.
The source-policy test verifies the non-creating adapter path; it is not an
Android binder execution test.

Earlier wrapper/native-cache ACL failures occurred before compilation and are
environment evidence. A corrected new-fixture compile error and two focused
integration failures are retained and classified separately from the final pass.

## Final affected Android gate

One offline final matrix ran after independent frozen-source review: 1,317
unit test executions and 58 reused results, 1,375
total, with zero failures/errors/skips. All three app variants include the 19
new regression/policy methods. Three lint reports, actual-output auditing and
unsigned-release inspection passed. Debug/V1-Test/unsigned Release APKs were
packaged during this matrix. The existing instrumentation APK was reused and
audited; instrumentation was not executed on a phone or emulator.

The existing Java 17, Gradle 8.11.1, SDK 35 and build/cache paths were reused.
All 180 authored Android/fixture/validation inputs and tool pins were unchanged
through the gate. Final source manifest SHA-256:
`b6030c612a43e84ec9be0ef62c261cf7a83a816fb1fb86a8a2d1bf3b5c994c72`.

From `C:\lu\OpenTrail\.private\ot177-publication`:

```powershell
& 'C:\Program Files\PowerShell\7\pwsh.exe' -NoProfile -File '.\.private\ot0304a-activity-observation-20261001\run-combined-android-final.ps1' -Freeze '.\.private\ot0304a-activity-observation-20261001\android-source-freeze-final-2.json' -Attempt 1 -Go
```

The driver records the exact 11-task Gradle command, timing, exit codes,
executed/reused classifications, source/output pins and all audit commands.

| Artifact in the selected worktree | Bytes | SHA-256 |
| --- | ---: | --- |
| `build\android-ot0302\app\outputs\apk\androidTest\debug\app-debug-androidTest.apk` | 16,042 | `a7eb1a3dd576373d084c62ad00d99256853e12554a66d8c4f4913cbbe322dcf8` |
| `build\android-ot0302\app\outputs\apk\debug\app-debug.apk` | 13,286,344 | `9974237bc6ec7ef0f75f81dd2b9e3cd0174a30f74a860bf0319abbc3ef572b6a` |
| `build\android-ot0302\app\outputs\apk\release\app-release-unsigned.apk` | 8,771,268 | `0bdb6974ea45d7c6f43e01c60c8ac9afaa4bf04a5228de1e304e8535987c207c` |
| `build\android-ot0302\app\outputs\apk\v1Test\app-v1Test.apk` | 12,658,667 | `1f085063c2a7f7e35700617faacad7895a9c57fbba26633ec90d35622979fa5e` |

## Remaining gates and custody

No device operation occurred in this host correction. The new APK is not
installed on either phone. Actual rotation/reopening, OS binder/service survival
and physical permissions remain unverified for this artifact. OT-0304 fresh
first-use, firmware/profile admission, preservation and production acceptance
remain open; this task changes no firmware, pairing, V1 credit or public website
capability. Git publication is pending separate authorization.

Private evidence under `.private/ot0304a-activity-observation-20261001/` owns
the approved execution record, exact baseline fixture/recipe, failed and passing
focused XML/logs, implementation receipt, superseded/final freezes, independent
source review, final matrix/audits, documentation gate and closeout manifest.
These are custody references, not public downloads or operational grants.

Owner-accepted host scope: [OT-0304a](https://limitedunderground.com/lab/tasks/7d1bf0b5-40b5-40c3-93af-c0c19d11a28a).
Owner acceptance is confirmed. Prepare the separately scoped short real-phone
rotation/reopen confirmation; preserve app data/pairings and the existing setup.
