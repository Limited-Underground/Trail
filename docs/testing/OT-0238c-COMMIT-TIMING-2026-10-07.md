# OT-0238c COMMIT timing diagnostics — 2026-10-07

This computer-only increment adds bounded timing observations around the enrollment controller, operator wrapper, and runner's COMMIT/READY path. Production diagnostic code is frozen for this review. The October 7 live checklist check recorded version 591, approved revision 1 and In Progress; final independent review accepted this host increment. This is not overall OT-0238c completion.

## Result

The observations cover controller guard and decision points, wrapper checks, and USB write, read, and parse boundaries for COMMIT and READY. They use existing clock samples and do not add authority-clock calls, refresh deadlines, skip fresh checks, retry commands, or change the shared deadline. Fixed privacy fields record bounded phase, role, boundary, failure category, counters, and durations. The collector accepts one bounded summary only after closure. Ordinary observation and logging failures remain best-effort; malicious callback reentry still changes terminal safety state and rejects pending success.

Focused validation passed 10 tests and 37 subtests. Three independent adversarial checks passed. The affected evidence is an accepted composite of 311 tests across seven suites: six passing suites with 301 tests from the original final run, plus all 10 runtime tests in a fresh standalone interpreter. The source pins were reverified. The original combined in-process final run remains failed with two runtime `assembly_invalid` errors: the inert-import test reloads `Assembly` after prior suites imported the old class, and the strict exact-type guard remains unchanged. The initial `runpy` runtime attempt also retains two missing-import errors caused by the script import path; invoking the actual test script standalone passes. The sandbox's synthetic capture transfer hit WinError 5 at `os.link`; its exact isolated synthetic case passed outside the sandbox. These retained harness/environment failures are not represented as passing runs.

The accepted composite commands were:

```text
C:/Python314/python.exe C:\lu\OpenTrail\.private\ot177-publication\.private\ot0238c-commit-timing-20261007\validate_commit_timing.py focused
C:/Python314/python.exe C:\lu\OpenTrail\.private\ot177-publication\.private\ot0238c-commit-timing-20261007\validate_commit_timing.py final host-test-outside-sandbox
C:\Python314\python.exe tests/host/enrollment_candidate_runtime_tests.py
```

The `final` helper's first run recorded the original combined matrix failure. The accepted 311-test result is specifically the six passing suites in that receipt plus the standalone runtime run, not a successful rerun of the newly added subprocess orchestration helper. Focused coverage and the accepted composite are recorded in [the focused receipt](../../.private/ot0238c-commit-timing-20261007/focused-receipt.json), [the accepted cohort receipt](../../.private/ot0238c-commit-timing-20261007/accepted-cohort-receipt.json), and [the independent review](../../.private/ot0238c-commit-timing-20261007/independent-review.json).

## Limits and next action

Host samples measure diagnostic boundaries, not physical timing overhead or an integrated timing fit. Durations from controller, wrapper, and USB-client observations use uncorrelated clocks and must not be subtracted from one another. A successful write or parsed reply does not prove target delivery or application. The historical COMMIT B delivery/apply state remains unknown. The prior October 6 trial's six physical Button 1 confirmations and verified restoration remain closed evidence; this increment performed no physical USB, ADB, native UI, firmware build, grant, device, or Git remote operation. The live checklist alone was read and updated with scoped evidence.

The portable added-logical-line whitespace audit passed 5,105 lines, preserving the existing test files' CRLF bytes. Raw Git whitespace output remains recorded: inherited per-file attributes flag CRLF as trailing whitespace. No attribute or source-line-ending normalization was used to hide that limitation. USB source/tests and the V1 progress record retain their baseline hashes. The final documentation checks and artifact descriptors are in the [session receipt](../../.private/ot0238c-commit-timing-20261007/session-closeout.json).

`tools/check_repository_docs.py` passed, and all 18 `tests/host/repository_docs_tests.py` tests passed with process-local `TMP` and `TEMP` set to the private `test-tmp` folder. The initial documentation test attempt hit sandbox WinError 5 in the default temporary folder (36 setup/cleanup errors); its log is retained separately. No source correction or global environment change was required.

The next bounded action is computer-only: assess source-bound final-command guard cost from host evidence against the unchanged remaining deadline, then identify a safe optimization only if it preserves every fresh check and existing deadline. Physical final-command latency remains unmeasured. No new device trial is authorized or needed for that assessment. No accepted evidence changes public website status or V1 credit. See the [October 6 physical result](../../tests/hardware/OT-0238c-FIRST-2026-10-06.md).

