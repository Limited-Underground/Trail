# OT-0238c bounded ROM failure attribution

Date: 2026-10-03. Host-only correction after the v9 trial and hardware closure.

## Problem and change

The [v9 first-case attempt](../../tests/hardware/OT-0238c-FIRST-V9-2026-10-03.md)
stopped at B's candidate boot boundary with `rom_operation_failed`. The adapter
collapsed subprocess timeouts, other runner exceptions and invalid responses into
that fixed category, leaving the actual operation and failure source unavailable.
The historical evidence cannot distinguish those causes retrospectively.

The adapter now retains the first bounded ROM failure snapshot, and the final
candidate-runner receipt projects it through exact-schema validation. The snapshot
identifies an allowlisted operation, failure boundary/cause, original fixed error
category and bounded timing information. Actual worker return codes are retained
only when present. It contains no commands, payloads, routes, identifiers,
exception text, stdout, stderr or native tool output. Caller mutation and later
restoration failures cannot replace the original snapshot.

Existing rejection categories, guard ordering, driver calls, deadlines, restoration
operations, security and authorization behavior are preserved. Diagnostic clock
sampling does not update the authority clock or convert an earlier error into a
different one. A timing observation alone is not a unique physical cause.

## Validation

The focused adapter/runner regressions use the actual production adapter with
injected subprocess results; they distinguish timeout, exception, valid nonzero
worker envelope, invalid response and post-operation expiry, preserve earlier
failure categories, and check immutable/sanitized receipt collection. Invalid
schemas and private extra fields are rejected. No physical SDK/device call is
needed for these host controls.

Final affected matrix: **10 suites / 352 tests passed** with four independent
host workers. Source/test bytes were unchanged throughout that run.

| Suite in tests/host | Tests | Result |
| --- | ---: | --- |
| enrollment_candidate_capture_runner_tests.py | 15 | Passed |
| enrollment_candidate_controller_tests.py | 40 | Passed |
| enrollment_candidate_custody_tests.py | 47 | Passed |
| enrollment_candidate_operator_tests.py | 42 | Passed |
| enrollment_candidate_original_capture_tests.py | 32 | Passed |
| enrollment_candidate_private_view_tests.py | 9 | Passed |
| enrollment_candidate_rom_adapter_tests.py | 39 | Passed |
| enrollment_candidate_runner_tests.py | 60 | Passed |
| enrollment_candidate_runtime_tests.py | 9 | Passed |
| enrollment_candidate_usb_client_tests.py | 59 | Passed |

Every command used `C:/Python314/python.exe -X utf8 -B` followed by the absolute
suite path under `C:/lu/OpenTrail/.private/ot177-publication/tests/host/`.
Exact commands, durations, current input pins and log hashes are in the
[matrix receipt](../../.private/ot0238c-rom-failure-20261003/matrix-receipt.json),
SHA-256 `b204d88501f1f342a52e2e0722fd202f31d99c962ca991b9d59ef8cc3bf5f9f2`. Focused results and independent patch/publication review are
retained in the same private evidence folder. Required repository documentation,
publication-safety and diff checks are recorded there before publication.

| Changed input | Bytes | SHA-256 |
| --- | ---: | --- |
| tools/enrollment_candidate_rom_adapter.py | 56097 | `ba5aac737db3c7f173a3185b498cc6e0843a96879a82f79b51f069191452ac00` |
| tools/enrollment_candidate_runner.py | 49873 | `4c5da460b56234a08f758fc553a5413e1a57da8e2c8f0590fb95565ee310f291` |
| tests/host/enrollment_candidate_rom_adapter_tests.py | 70692 | `b359bb0efc1eefc3896723154cc50c4e301ecf4079a77cec60861a8cfcf3593b` |
| tests/host/enrollment_candidate_runner_tests.py | 89326 | `af4f24f5331b36566e01398b74ac1dd411be3b363a660804c0760e0e28b3eae0` |

## Physical/runtime boundary and next gate

No firmware input changed, so no firmware rebuild was required. No device access,
new grant, candidate runtime assembly, physical retry or website deployment occurred
during this correction. The v9 assembly, source pins, trial backups and receipts
remain byte-preserved historical evidence. They do not contain this new logging
and must not be silently repacked or executed again.

Before proposing another physical case, bind an independently reviewed successor
runtime and reconcile the complete original acquisition, repeated readback,
installation, boot and restoration workload with unchanged authority ceilings.
Retain exact failure attribution and a feasible human-checkpoint procedure. Fresh
readiness and exact physical authorization remain required. B's actual cause,
full first enrollment and later retained/rekey/revoke/reset/production gates remain
open. OT-0238c stays In Progress; V1 and public website capability credit are unchanged.
The owner has separately authorized publication of this completed host correction
and sanitized result; remote publication evidence is recorded after required CI.
