# OT-191 backup launch correction and physical preflight

Status: **bounded task complete; trial failed to produce an accepted receipt; both devices returned to original firmware. Full host validation pending.**

The owner authorized a bounded 30-minute backup capture and conditional release.
The initial capture and subsequent release attempt both stopped before launching
the serial child. Passive operating-system enumeration occurred, but neither
attempt performed a flash read, reset or ROM hold. These failures establish no
live backup custody and do not constitute a successful physical preflight.

## Confirmed cause and reconciliation

The isolated backup operator requested the `BackupRom` child mode, while the
shared child-argument builder rejected that mode. The refusal happened before
subprocess launch. Retained private diagnosis records zero subprocess calls for
both paths; the journal stopped at read/reset-verification intent rather than a
completed hardware operation.

The exact active-lock reconciliation preserved both attempt journals and consumed
grants. Those grants remain historical and cannot be reused. No firmware, device
state or private NVS snapshot was recovered or changed by this reconciliation.
Private request, grant, identity, route and diagnostic files remain private.

## Software correction and validation

A narrow correction admits the backup child mode through the same verified
launcher path. It does not relax runtime/source verification, child command
scope, journal-phase checks or hardware authority. The backup-operator suite now
passes 13 tests, including the launch seam that the earlier mocked composition
missed. Related controller and runtime suites pass 21, 13 and 13 tests.

The successor isolated runtime verifies 3,547 files totaling 96,202,938 bytes.
Ordinary and poisoned-environment probes pass. Renewed physical capture then
passed on both independently identified ESP32-S3 roles with matching 16 MB flash
geometry. Each role's 589,824-byte application/tail and 12,288-byte full NVS were
saved privately. Bootloader (32,768 bytes), partition sector (4,096 bytes) and OTA
selection (8,192 bytes) matched the historical expected baselines.

Both devices entered held custody. The private execution package was frozen and
verified with 33 source pins. The owner approved the exact nonradio trial,
restoration of both devices and guarded recovery.

## Physical outcome

Role A's candidate was written, readback-verified and boot-reset. Serial opening
and run intent were recorded, but no receipt was accepted (`capture_failed`).
Run intent does not prove command delivery, received bytes or successful execution
of any key/security operation. The physical cause remains unknown because this
endpoint's failures collapse to the same capture outcome.

Serial closure was confirmed before restoration. Role A's complete original
application/tail and full NVS were restored and independently read back; protected
regions matched and the original firmware reset succeeded. Role B received no
candidate write. Its original regions passed guarded readback and reset during
release. No active locks remain. No retry or separate recovery run occurred.
Both snapshots are now stale for future handoff because the original devices
restarted. Phone UI and OLED state were not independently observed.

See the [sanitized physical outcome](../../tests/hardware/OT-191-SECURITY-POLICY-OUTCOME-2026-09-10.json).

## Next acceptance gate

Diagnose the absent or rejected solicited receipt with bounded software tests and
safe transport metadata before preparing another physical attempt. Preserve the
consumed authority and exact trial history; a retry requires fresh admission and
current backups. The complete host validation matrix passed, including all 13 simulator UI tests.
Required GitHub checks must pass before the publication merge.

The bounded backup/preflight/trial task is complete with a failed trial outcome,
not a security pass. No V1 completion credit or accepted public website status
changes. The existing
[backup custody contract](OT-190-BACKUP-CUSTODY-2026-09-10.md) continues to govern
snapshots, handoff, recovery and release.
