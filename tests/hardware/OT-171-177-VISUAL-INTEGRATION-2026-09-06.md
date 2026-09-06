# OT-171/172/177 Android visual integration - 2026-09-06

The existing Android visual implementation from the preserved owner checkout is
now composed with the published connection/service-logging baseline c3a0ee9.
This is a software integration checkpoint, not final visual or phone acceptance.
No APK was installed and no device operation was performed in this increment.

## Integrated scope

- Real entry into Messages/Group/Device, brief launch mark and mint theme.
- Configuration-based portrait bottom navigation / landscape rail, saved page
  drafts and fullscreen status strip with keyboard resizing.
- Existing onboarding, group and nearby-people surfaces, phone-local setup drafts,
  quick-message editing and support preview/Save/Share.
- Device snapshot projection preserves unknown satellite/battery/traffic values;
  the phone's sensors are not substituted. External power is not called charging.
- Normal support export now has one non-exported application-scoped FileProvider
  with explicit URI grants and exactly cache/support-reports access. The V1-Test
  variant inherits it. Raw connection logs remain outside the shared path.

Fourteen component files and eight existing test files were selectively imported.
MainActivity's reviewed UI diff was integrated; existing exact-session settings
and reset authority were retained. Service owner, runtime, authorization and
recorder code were not replaced by older owner versions. V1TestApplication still
owns recording before service activation and across UI unbind; its 14 diagnostic
symbols remain forbidden in the production artifact. Gallery/sample activities,
unrelated name/protocol/inbox work and firmware changes were not imported.

Local preparation does not create a group, transmit a message or commit device
settings. Unsupported device workflows remain visibly unavailable. Ordinary and
test-log attachments share compatible eight-file/24-hour retention; no automatic
message, upload or recipient selection is introduced.

## Validation

Windows, Microsoft JDK17.0.20, established Android SDK and Gradle8.11.1 wrapper.
Isolated output build/android and project cache build/android-cache; normal-user
Gradle cache reused. The focused V1-Test run compiled the combined application
and found one test-only line-ending mismatch between otherwise identical provider
XML. The comparison now normalizes line boundaries; path restrictions are intact.

Final affected tasks:

```text
:protocol:test
:app:testDebugUnitTest :app:testReleaseUnitTest :app:testV1TestUnitTest
:app:lintDebug :app:lintRelease :app:lintV1Test
:app:assembleDebug :app:assembleDebugAndroidTest
:app:assembleRelease :app:assembleV1Test
```

JUnit results: protocol30, debug265, release264, V1-Test315 = 874 executions,
zero failures/errors/skips. Coverage includes orientation policy, truthful status
and support projections, bounded local drafts/templates, share retention and the
existing service/authorization/lifecycle regressions. Independent review found
no blocking authority, integration or privacy defect. Android instrumentation
was built, not executed. Final build passed in 2m31s: 184 tasks (62 executed,
7 from cache, 115 up-to-date). All three lint variants passed. Unsigned-release
inspection passed, including non-exported application-scoped provider, exactly
one support-reports cache path and all 14 diagnostic exclusions. Temporary
artifact extraction was removed. No firmware target changed or rebuilt.

## Prior evidence and next gate

Earlier owner UI/compact phone notes and images remain local and reusable as
bounded observations of their exact earlier artifacts. They are not acceptance
of this merged APK. No broad gallery campaign or connection campaign was repeated.
The prior source baseline c3a0ee957d7a96ce04e8f9ca2c1da3982e288078 passed GitHub
Host validation run34015961690. All 101 recorded owner file hashes are unchanged.

Next implement OT-178 OLED/clock presentation with host-testable priority, pixel
bounds, valid/unknown/stale time and concealment rules. Apply firmware porting
preflight before target implementation and reproducible builds before hardware.
The final OLED is still planned; it was not implemented by this Android task.

When ready for phone acceptance, use this combined build and fold in one narrow
saved-owner/service-log leave/reopen/stop check. Revalidate changed screen layout,
rotation and keyboard behavior; broader workflow/two-device acceptance follows
the settled visuals. Do not install the superseded logging-only APK as an extra
intermediate step. Existing connection evidence remains scoped and reusable.
Cold-power and website updates/deployment remain owner-deferred. All milestones
are unchanged: Android60%, V1 exact43.75% / displayed44%. OT-172/177 stay partial.

## Exact combined artifacts (not installed)

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| build/android/app/outputs/apk/androidTest/debug/app-debug-androidTest.apk | 16042 | A7EB1A3DD576373D084C62AD00D99256853E12554A66D8C4F4913CBBE322DCF8 |
| build/android/app/outputs/apk/debug/app-debug.apk | 13256930 | 0D8CEC1EEE835B581C0E0A73146803238E1F3A8B50EC520BA52EB7B48A13D1E5 |
| build/android/app/outputs/apk/release/app-release-unsigned.apk | 8672964 | 1727C876218A2F35505D9D37AF65D3862EBFF53A2A07D9214D94E769F37646C5 |
| build/android/app/outputs/apk/v1Test/app-v1Test.apk | 12207477 | 50235BD79EE8D9C48072FCA1CB0DB69698EBA1DD16937E8992305914A15DF2E2 |

## Publication and release-policy boundary

Final governance10, Android operational-release admission23, publication-safety
scan and diff checks passed. The old source gate allowed only the reset receipt
store; it now explicitly admits the two approved local setup/template stores and
the restricted user-initiated support export. Unknown stores, raw telemetry,
networking and unrestricted file paths remain rejected. No app change or APK
rebuild followed the final Android matrix.

This source check is not acceptance of the historical OT-088 transient-only /
no-sharing release promise. The current local retention and user-directed export
behavior must be reconciled with the release plan and physically verified before
release. Frozen plan/schema IDs, release identity and signing authority are
unchanged; this remains a development integration artifact.
