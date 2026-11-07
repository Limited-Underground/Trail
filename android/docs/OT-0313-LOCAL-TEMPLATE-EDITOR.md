# OT-0313 local template editor

This is the explicitly approved local-only scope B, proposal SHA-256
`69ff91f17c7f793ea7150bcd6459c6acc4a56d51a196ce0fb5660192f4a73652`.
OT-0312 remains open. This slice neither completes OT-0313 nor implements a
typed-message composer, send/queue/delivery path or authenticated authority.

The existing Messages screen now offers Edit, Save changes and Cancel edit for
custom templates. Editing replaces only the selected entry, preserves its order,
and applies the existing unique, nonempty, control-free 160-character limit and
12-entry capacity. A restored editor must still match the original entry at its
index. Reorder/remove/other-edit actions are disabled while editing. The separate
new-template draft survives entering/cancelling an edit. Form drafts are saveable
UI state, never delivery or connection authority.

Persistence keeps the existing app-private `v1-local-templates` preference file,
`template-0` through `template-11` keys, retention and normalization policy. Only
explicit local actions write. The UI boundary retains explicit numbered-key bounds
and normalization required by the unchanged production-privacy guard. The existing preference replacement now checks
`commit()` instead of claiming success after asynchronous `apply()`. Invalid input
does not call persistence. A read failure is distinguished from an empty store
and disables editing; failed/throwing saves keep the current displayed list and
draft, report uncertainty, and do not retry automatically. A failed commit does
not prove the underlying storage unchanged, and the UI makes no such claim.

Focused JVM tests exercise the real edit/load/save helpers with synthetic
preference callbacks: edit at capacity, bounds/duplicates/stale target refusal,
order retention, save and fresh reload, read/write failure and defensive copying.
Compilation verifies the actual Compose wiring. These checks do not execute
Android SharedPreferences, prove process-death/power-loss durability, or accept
rendered layout, large text, IME/focus/accessibility or emulator/device behavior.
No APK assembly/signing/install or ADB/BLE/RF operation is part of this slice.

Exact source, command, result and review state are retained in the task-owned
`ot-0313-local-editor-20261104` handoff. The frozen focused command passed on November 4, 2026: **13 tests, zero
failures/errors/skips**, native exit 0, with all three source/test hashes
unchanged before/after. Main/test Kotlin compiled; no APK was assembled.
Independent review and shared-record integration remain with the coordinator.


## Host Android preferences checkpoint — November 7, 2026

The test-only increment adds `V1SharedPreferencesTest` with Robolectric 4.14.1
and Android-35 framework classes. Its five focused tests passed with zero
failures, errors or skips, native exit 0, and independent read-only review found
no actionable defects. All 77 tracked Android production file hashes remained
unchanged. This local candidate is not yet published or adopted.

The tests write actual SharedPreferences XML, then use a distinct framework
instance to parse it without the original instance's cache. They verify edited
values and order, obsolete-key removal, empty saves, isolation from another
preferences file, and malformed-type read refusal. Two filesystem faults replace
only the temporary preference parent with a regular file: the actual framework
commit returns false, prior XML bytes stay identical, and a fresh reader sees
the prior durable values. In-memory values can still change on a failed commit;
these checks do not equate failure with rollback. No mocked commit result is used.

Run only this suite from `android` with:

```text
gradle :app:testDebugUnitTest --tests io.github.nbjelanovic.otclient.V1SharedPreferencesTest
```

The recorded final XML SHA-256 is
`d3f753d17d570e2d1528cf2befef026d03bede6e810c0e310e4ba65f3156b183`.
One non-fatal Kotlin platform-nullability warning remains at the fixture's
`java.io.tmpdir` lookup. The unchanged five-test result is reused after proving
the Android base tree equals the tested base, the test source bytes are unchanged,
and the Gradle configuration differs from reviewed input only in CRLF-to-LF
representation.
No fresh hosted Android test or main-merge result is claimed.

### Remaining process-death acceptance

A new SharedPreferences instance in one JVM is not Android app process death.
Robolectric simulates Android in a host JVM; killing a separate host JVM would
only add host-process evidence, without exercising Android OS app termination
and application relaunch. It does not meet the actual process-death requirement.
That requirement needs an authorized Android runtime and a bounded app
kill/relaunch path using the app's private store. Neither is supplied by this
no-device/ADB host-only scope, so no proxy test or production seam was added.
Rendered/IME/accessibility, process-death and device durability, and the existing
OT-0312 authenticated-messaging dependency remain open.


## Editor accessibility correction - November 7, 2026

This owner-authorized correction was locally validated on adopted base `4c7579f59507c09292135f8787bd8f3ac6864018`. Earlier dated host/no-device limits above describe their original checkpoints. The preserved first Note20 run passed seven bounded cases, including actual process death and one separately approved reboot; it left full accessibility and safe physical write-fault cases unaccepted.

The field still wraps long messages. [Compose normally inserts Tab characters in a text field](https://developer.android.com/develop/ui/compose/touch-input/keyboard-input/commands). Plain Tab/Shift-Tab now navigate this form because template data excludes control characters; Ctrl/Alt/Meta combinations retain normal handling. IME Next moves focus forward. Save/Cancel live outside the scrolling content, remain reachable above the keyboard, and have at least 48 dp controls. Invalid edits skip disabled Save and can reach Cancel. Existing numbered-key preferences, 12-by-160 policy, original-value/index fencing and explicit commit/uncertain-save behavior are unchanged. Repeated move/remove buttons identify their template index. No shared navigation/fullscreen change was inferred from clipped CLI XML. The [AOSP dumper clips node bounds to its display rectangle](https://android.googlesource.com/platform/frameworks/testing/+/refs/heads/main/uiautomator/library/core-src/com/android/uiautomator/core/AccessibilityNodeInfoHelper.java); the CLI reading alone is not a rendering or screen-reader defect.

Six real composed debug-host UI tests cover keyboard-sized viewport actions, minimum target sizes, forward/reverse traversal, invalid Save skipping/Cancel retention, IME Next/save wiring and contextual descriptions. They belong only in src/testDebug because the test activity is a debug fixture. Thirteen template tests pass per variant; five release SharedPreferences tests check the affected resource-enabled host setup. Both lint checks and 23 privacy/admission groups pass. Host tests are not TalkBack acceptance.

The matching-signer Note20 update of the standard debug app retained prior data. Automated physical observations verify keyboard-visible field/actions, Tab/Shift-Tab and Samsung Next focus, create/edit/cancel/save and force-stop/relaunch. APK SHA-256 is `fa8b7e70d21b75dcb4979e4bb4fb0c100eb506fd3e772b1bc7dbc81799fb5713`; exact modified-source hashes and all first failures are retained in the existing private task packet. With actual Samsung TalkBack on and the keyboard hidden, the owner reported correct names and outlines in both directions for the field, Save/Cancel and navigation destinations. Automated checks separately covered keyboard-visible actions. The accepted check used an assisted starting selection at Save after earlier unclear instructions and text-selection popup handling; it is bounded owner-reported evidence, not full accessibility conformance. Original evidence/worktrees remain intact. Physical failed-write injection, full OT-0313 composer acceptance and OT-0312 remain open. The validation scope excluded publication, messaging/RF and other product changes. The owner subsequently authorized the exact reviewed correction through normal commit/PR/required-check/merge verification. The existing [OT-0313 issue](https://github.com/Limited-Underground/Trail/issues/117) and repository history record actual adoption; the prior checkpoint is not a claim that publication occurred earlier. TalkBack was restored to its original Off state after the check.
