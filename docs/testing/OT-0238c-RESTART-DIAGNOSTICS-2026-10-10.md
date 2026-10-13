# OT-0238c restart diagnosis and bounded error reporting

OBSERVED computer-only correction. The [October 10 physical attempt](../../tests/hardware/OT-0238c-RETAINED-2026-10-10.md) remains failed at automatic restart. Its inner cause is UNKNOWN: the original operator replaced the callback exception with `restart_failed`. Both originals and normal screens are confirmed, and all device ownership is closed.

## Established findings

- The old-code host probe exercised the production controller/operator restart methods, runner callback, bound backend and ROM backend. Four normal modeled paths passed; twelve injected rejections demonstrated that different causes became the same terminal error. Two normal paths also interpreted the actual worker logic against a synthetic SDK.
- This is partial composition evidence: adoption state was assigned, passive transport/startup were simulated, and the clock was fixed. It does not reproduce the physical failure or measure restart duration.
- Both saved original NVS images already contain the reset-marker namespace and pass the maintained marker check. A missing namespace does not explain this attempt. No later NVS image was retained, so a post-startup change is neither proven nor excluded.
- The source passes the custody deadline through the runner and operator using the earlier limit. No independent-clock deadline extension was established.

## Narrow source correction

The runner now preserves a separate first restart diagnostic containing only role, fixed method, fixed boundary and a sanitized category from an owned exception type. The existing terminal `restart_failed` result is preserved. An unknown exception stays generic; its text is not recorded. Cleanup cannot replace the saved first restart failure, and the callback is single-use.

The existing exact-original NVS comparison now reports `original_changed` when it rejects. Its comparison and all admission, ownership, write, deadline and restoration conditions are unchanged. This category describes a future observed comparison failure; it does not identify the past device failure.

Only `tools/enrollment_candidate_runner.py`, `tools/enrollment_candidate_rom_adapter.py` and `tests/host/enrollment_candidate_runner_tests.py` changed for this correction. The sealed runtime, firmware, familiar launcher, grants and original images were not changed or used for another device attempt.

## Validation

New composed tests use the real ROM backend and in-process worker logic with a synthetic SDK and in-memory lease. They cover sequential restart, preserving prepared candidate storage, admission/NVS/worker/closure rejection, no second restart callback, sanitized unknown errors, diagnostic bookkeeping failure, original restoration and durable receipt reporting. Physical transport and live capture adoption remain outside this fixture.

The ROM-adapter suite passed 39 tests (13.070 seconds). The runner suite completed 75 tests (540.929 seconds): 74 passed and its capture-handoff case encountered a sandbox `PermissionError`. That permission failure also reproduced with the exact prior runner and adapter. The same current test then passed with permitted execution, without source or fixture changes (1 test, 7.318 seconds). Thus all 114 affected cases have passing results, with the original environmental failure retained rather than described as one clean full run. The new composed tests also passed in the runner suite.

Exact commands, final suite outcomes, source hashes, normalized narrow diffs and independent review are retained under `.private/ot0238c-retained-diagnosis-20261010/`. Initial environment failures are retained and are not product failures.

## Remaining gate

The reporting defect is corrected locally; the physical restart cause and retained/new-key acceptance remain unresolved. Do not present this as a device fix or a ready-to-launch successor. Any next package must incorporate the reviewed source and pass its inert consistency checks before a separately authorized, targeted device attempt. Passed practice and original acquisition are not repeated.

Full OT-0238c remains In Progress. No V1 credit, firmware build, Git publication or public website capability change. The owner-approved physical result was saved to the private task checklist; approval and completion status were not changed.
