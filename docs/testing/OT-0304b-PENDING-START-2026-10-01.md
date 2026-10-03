# OT-0304b explicit Start after pending attachment

2026-10-01, approved revision 1. Host implementation and validation complete;
owner-accepted host scope. No phone or radio operation occurred.

## Failure and correction

The [Note20 checkpoint](OT-0304-NOTE20-STARTUP-2026-10-01.md) observed an enabled
Start button that did not establish a current connection. The prior app was
restored and reached fresh protected Ready. That observation alone did not
instrument Android's bind return/callback or uniquely establish its cause.

The actual host composition reproduces one definite defect: passive existing-only
binding accepts a local lease but supplies no callback. The Activity shows
Idle/Start required while `serviceRequested` suppresses its explicit Start action.
The old fixture returned null immediately when the service was absent and missed
this pending-lease boundary.

[TrailActivityController](../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/TrailActivityController.kt)
now lets one visible Start retire only a pending passive attachment with no
admitted port. It retires request, binding and observation authority before
closing the local lease. A cleanup guard prevents nested Start or passive
reacquisition. Fresh lifecycle, mode, request/port/binding and permissions decide
whether explicit startup proceeds; permission is checked again after STARTING
presentation callbacks. An already observed owner is never preempted.

| Transition | Required and verified host effect | Forbidden effect / recovery |
| --- | --- | --- |
| Passive lease accepted, callback absent | Idle/Start required; explicit permitted Start retires local wait and submits once | No automatic startup, scan or claim |
| Retired callback arrives during cleanup or later | Binding/observation fences reject it | Cannot replace current state or close the surviving owner |
| Cleanup reenters Start, lifecycle start/stop, close or mode exit | Nested starts/reacquisition blocked; lifecycle/mode/close changes cancel startup | No stale resumed request |
| Permission changes during cleanup or STARTING publication | Fresh state admitted or refusal recorded | No missing-permission service start |
| Existing observed owner, replacement or current disconnect | Repeated Start preserves observed owner; current owner/disconnect remains visible | No cached Ready; only current accepted observation |

Production `TrailConnectedDeviceService.onStartCommand` already returns for an
existing owner before creating another. That idempotence is source-reviewed;
the host connector does not execute Android's service entrypoint. No service,
connector, protocol, firmware, UI layout or authorization policy was changed.

## Discriminator and focused checks

[ActivityServiceAttachmentTest](../../android/app/src/test/kotlin/io/github/nbjelanovic/otclient/ActivityServiceAttachmentTest.kt)
uses actual Activity/app controllers, BLE runtime and connected-device owner.
Binding, permission, GATT and authorization I/O are controlled host seams.

The primary discriminator failed before the correction: its already Ready owner
had one previous startup, and explicit Start left submissions at 1 instead of 2.
The exact 28,364-byte old controller, SHA-256
`a8f340b59008fc06bc2961569bf9989d45e3389f000b4421eeeaac2b40cac370`,
and exact baseline fixture/log/XML are privately retained. After correction,
**62 focused tests passed**: 30 lifecycle/adapter tests and 32 existing service
tests, zero failures/errors/skips. Both focused test tasks executed.

All 19 prior OT-0304a lifecycle/adapter tests remain. Eleven new cases cover
accepted pending leases with a Ready or absent owner, stale/delayed callbacks,
cleanup reentrancy/throw, permission loss/change/regrant, repeated Start,
observed/closed/replaced owners and current disconnect. The before failure is
intentional evidence; there were no unexpected compile/test failures in this task.

Final controller: 29,793 bytes, SHA-256
`469836ff22245a0b844595896e76a8e5727a4b98dd78664ff4e6440568db254e`.
Final fixture: 43,491 bytes, SHA-256
`b1db0f86fdd20bf499bb3b4430ce1a998dadf7e6718a37c615cda6e0cdec3e45`.

## One final affected Android gate

Independent frozen-source review passed before one offline final matrix.
1,350 unit tests executed and 58 results were reused: **1,408
total**, zero failures/errors/skips. The exact 30 lifecycle/adapter methods are
present and passing in all three app variants. Three lint reports have zero
issues. Actual-output and unsigned-release audits passed.

| Test group | Tests | Proven classification |
| --- | ---: | --- |
| `protocol/test` | 58 | reused_result |
| `app/testDebugUnitTest` | 432 | executed_during_matrix |
| `app/testReleaseUnitTest` | 431 | executed_during_matrix |
| `app/testV1TestUnitTest` | 487 | executed_during_matrix |

Existing JDK 17, Gradle 8.11.1, SDK 35 and build/cache paths were reused.
All 181 frozen authored inputs and 12 tool pins remained
unchanged. Source manifest SHA-256: `9f1b0a21a596985299cac93b79dd89a73281d7092ff0f0b147ba1818c4c35279`.
The driver captures the 11 Gradle tasks, timestamps, exit codes, full logs,
XML/task execution statuses and before/after output pins.

From `C:\lu\OpenTrail\.private\ot177-publication`:

```powershell
& 'C:\Program Files\PowerShell\7\pwsh.exe' -NoProfile -File '.\.private\ot0304b-pending-start-20261001\run-combined-android-final.ps1' -Freeze '.\.private\ot0304b-pending-start-20261001\android-source-freeze-final.json' -Attempt 1 -Go
```

| Artifact in selected worktree | Bytes | SHA-256 | Proven classification |
| --- | ---: | --- | --- |
| `build\android-ot0302\app\outputs\apk\androidTest\debug\app-debug-androidTest.apk` | 16,042 | `a7eb1a3dd576373d084c62ad00d99256853e12554a66d8c4f4913cbbe322dcf8` | reused_package_output |
| `build\android-ot0302\app\outputs\apk\debug\app-debug.apk` | 13,286,507 | `19228d46609bae885ee4be3b8d0970ac238ac7bb9be27a6e065547dc2ae580f4` | packaged_during_matrix |
| `build\android-ot0302\app\outputs\apk\release\app-release-unsigned.apk` | 8,771,268 | `be756f3fe66ddb8153a4d5556f7f095cf5b45cde9f8f6b5ba260e37896e06b99` | packaged_during_matrix |
| `build\android-ot0302\app\outputs\apk\v1Test\app-v1Test.apk` | 12,658,887 | `fcd3e97353cf67fc345e7f81cd541237c60dd5d9d94148e960d9ff042a66b274` | packaged_during_matrix |

Instrumentation is compiled/reused and audited only, never executed on a phone
or emulator. Debug/V1-Test local signing is separate from production signing;
the release artifact remains unsigned.

## Remaining gates and custody

Actual Android startup, rotation/reopening and OS binder/service behavior for
this new artifact remain unverified. This host result does not uniquely explain
the earlier physical failure or complete OT-0304's first-use, firmware/profile,
preservation, warm-board or production gates. A later phone installation/test
requires its separate exact-artifact and recovery grant.

No ADB/device, app install, pairing, app-data clear, firmware, RF, Git mutation,
publication or website implementation/deployment occurred. The old OT-0304a and
physical reports, owner USB appendix, Repeater draft, V1 record and concurrent
HomeAssistant focus are unchanged. No V1 credit or public website capability
change. Product changes remain local/uncommitted; publication is pending.

Private `.private/ot0304b-pending-start-20261001/` owns preflight, baseline source,
before/after logs/XML, focused receipt, helper reuse pins, frozen source, source
review, matrix/output audits, documentation gate, final review and closeout.
The intermediate helper preparation receipt is superseded by the final restored
helper pins; a corrected read-only review parser assumption changed no source
or test artifact. Prior task evidence is retained as historical evidence.

The owner accepted [OT-0304b revision 1](https://limitedunderground.com/lab/tasks/48dc646c-3f2d-4989-b82c-0a702d435723);
fresh maintained checklist version 501 records Approved and Completed.
Acceptance reconciliation is retained separately in
`.private/ot0304b-acceptance-20261001/`; original closed host evidence is unchanged.

Next: prepare the separately authorized Note20 startup/rotation confirmation.
Host acceptance does not establish actual Android or full OT-0304 acceptance.
