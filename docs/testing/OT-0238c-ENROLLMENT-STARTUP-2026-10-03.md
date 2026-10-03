# OT-0238c Enrollment startup preparation

Date: 2026-10-03. Computer-only implementation and validation; ready for review.
Full OT-0238c remains In Progress. No device action or new physical result.

## Result and correction

The first-enrollment controller now uses the successful startup sequence on each
device immediately after its guarded boot. A dedicated read-only observer waits
for bounded diagnostic coverage, followed by strict fresh HELLO and BOOTSTATUS.
It closes that handle before installation/boot work continues on the other node.
The later enrollment connection performs its own fresh queries. No readiness,
authorization or endpoint is reused from the probe, and no wire CLOSE is sent by
the probe because that would shut down the candidate session.

The previous path booted A, then installed and verified B before opening either
application connection. That ordering did not preserve the proven v8 observation
sequence and could miss bounded startup replay. Even booting both only after both
installations is insufficient: B's guarded boot reads its full image first.
This is a source-established scheduling hazard, not proof of the unique cause of
the historical physical failures. The successful v8 A-only startup and its
limitations remain in [the physical report](../../tests/hardware/OT-0238c-STARTUP-V8-A-2026-10-02.md).

Each logical boot generation has distinct probe and operation leases. The
existing lease monotonicity is preserved; no exclusivity rule is weakened.
Retained/recovery warm restarts use the same per-role boot/probe/close sequence.
Operational connection opening and fresh queries have a bounded startup cap;
the endpoint lifetime remains limited by the original execution authority.
The existing 60-second startup and five-second query limits are retained.
Preparation, invitation, activation and cleanup deadlines are not extended.

Probe failures preserve the first role/stage/category and take the maintained
restoration path, including a real final screen checkpoint even when enrollment
never began. Fixed SUMMARY-3 records identify role and logical boot generation;
bounded counts, exact schemas and diagnostic loss flags remain explicit. The
existing startup_A SUMMARY-2 path remains supported. No private rows or raw
serial contents are added to durable telemetry.

## Validation

- Final affected matrix: **10 suites, 344 tests passed** on the final source.
- A timing fixture was strengthened during the full runner run. That corrected
  test and two neighboring cases then passed separately on the final inputs;
  this combined gate is recorded explicitly rather than repeating the full run.
- Focused tests cover real controller/operator/parser/custody/collector composition
  with explicitly simulated physical I/O, delayed installation, first enrollment,
  retained/recovery generation two, strict failure before BEGIN and restoration.
- Fresh operation admission, stale/partial queued controls, stopped/refused boot,
  timing expiry, duplicate probe, handle closure and owner checkpoint boundaries
  are included. Details and actual commands are retained in the focused logs.
- Independent source and packaged-runtime/preparation reviews have no unresolved
  findings. Review checks compare this increment with preserved v8 source.
- Additive v9: 14 policy files and 3574
  capsule entries; actual isolated preflight and capture-preflight both passed.
- Complete maintained package preflight passed using the actual v9 runtime and
  images with fake identities/original pins and an expired 1970 test grant.
  The real clock rejects that grant. No device authority was created or consumed.
- All 377 firmware repository dependencies
  and 18 paired artifact files match the prior
  accepted build evidence. No firmware source changed and no rebuild was needed.

The actual target's physical USB timing, both-node enrollment, real owner gestures
and later lifecycle acceptance are still untested with v9. Host results are not
radio, hardware restoration, production integration or V1 acceptance.

## Prepared next case

One `first`, group-1 two-node USB enrollment case, eight fixed authenticated
status deliveries, then full original return. No phones or RF. The detailed
procedure includes the own/peer identity and comparison checks, explicit one-
second BOOT gestures, existing human windows, failure handling and closure.
The exact private input bindings point to the already verified image copies
under .private; the prior build-directory admission error is corrected before
physical preparation. Stable profile/identity-binding inputs are reused without
copying secrets. A new runtime-bound capture request carries no live authority.

Fresh readiness/approval, current routing, new original captures and independent
live package/handoff admission remain mandatory. No old physical grant, original
snapshot or controller process may be reused. The original held controller owns
the effective deadlines; a prelaunch conversion is not their comparison origin.

## Evidence and commands

Private root: `.private/ot0238c-enrollment-startup-20261003/`.
The root contains baseline, firmware-reuse, focused test logs, matrix-receipt,
packaging, inert-entrypoints, complete-package-preflight, independent source and
runtime reviews, and final closeout. `next-trial/` contains the reviewed procedure,
fixed inputs, inert session and static validation; no owner-authorization file.

Interpreter: `C:/Python314/python.exe -X utf8 -B`.
Commands: `validate.py firmware`, `validate.py matrix`, `validate.py matrix_finalize`, `validate.py package`,
`validate.py entrypoints`, `prepare.py package_preflight`, `prepare.py bind`.
Individual suite commands and exact input/log hashes are in matrix-receipt.json.
Documentation validation uses `tools/check_repository_docs.py` and
`tests/host/repository_docs_tests.py`; whitespace uses `git diff --check`.

V9 assembly SHA-256: `b12553b4e8c3f3f97ce37805610dce457f1ad88b2d0c51fdeca626d25053a6a2`.
V9 runtime SHA-256: `8afdc19d6cd0a21d9a866a7fd3ef6d702193e2175078b12ab1afff7de918223a`.
Source pins SHA-256: `03a0f9e24a1d224c57c2b614f318673f4cb3d30051d7ca0a41f7d3b09b63b5db`.

## State and next action

Local and uncommitted. Prior dirty work is preserved, with an empty index and
unchanged branch/HEAD checked at closeout. No Git network/publication, new devices,
public website status change, deployment or V1 credit. Concurrent HomeAssistant
focus and the V1 progress record remain unchanged.

Next: review this prepared first-enrollment case, obtain fresh owner readiness and
exact one-trial authorization, then use the maintained sequential capture,
handoff, execution and original-return procedure. No repeat startup-only trial is
proposed. Full retained/rekey/recovery/revoke/reset and product acceptance remain.
