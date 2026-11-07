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
