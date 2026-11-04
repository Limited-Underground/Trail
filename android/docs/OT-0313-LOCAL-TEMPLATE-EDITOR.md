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
