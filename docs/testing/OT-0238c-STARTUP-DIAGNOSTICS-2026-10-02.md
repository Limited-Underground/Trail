# OT-0238c host startup diagnostics and returned-failure preservation

Date: 2026-10-02. Host-only correction; full OT-0238c remains In Progress.

## Result and boundary

**HOST VALIDATED**: nine fixed startup boundaries are now recorded through the
actual operator/runner, and both result collectors preserve a structurally validated
returned first failure before their unchanged post-return clock checks. Independent
review has no unresolved findings. All **10 affected suites / 231 tests**, additive
runtime v5 and both actual isolated inert entrypoints pass.

This corrects a proven host observation defect. It does **not** establish the earliest
cause of the [previous physical attempt](../../tests/hardware/OT-0238c-FIRST-V4-2026-10-02.md).
That attempt remains failed, with 0/8 statuses, originals restored and custody closed.
No new device, grant, firmware change/build or physical validation ran. No owner
action is needed for this host increment.

## Complete flow and safeguards

The existing flow remains admission, fresh original custody, sequential guarded
candidate writes/boots, passive USB startup, HELLO/BEGIN and enrollment, then
sequential original restoration, fresh owner checkpoint and release.

- `_BoundOperator.run()` validates the actual `TrialResult` before its post-return
  clock check. A validated failure survives that later clock rejection. Late pass,
  malformed/spoofed results and initially expired authority still reject.
- Custody consumes the validated `first_failure` before its existing post-return
  budget check. Cleanup errors remain separate and restoration uses its original
  ceiling; the original failure is not replaced by a generic case timeout.
- Fixed phases are `runtime_verify`, `route`, `serial_api`, `serial_config`,
  `lease_attach`, `serial_open`, `identity_guard`, `hello` and `begin`. The collector
  accepts only their exact four-field schema, A/B roles, integer generations 1-2
  and at most 64 progress records, with existing request/grant bindings and sequence.
  No routes, identities, secrets, command arguments/replies, exception text or private
  checkpoint values are included.
- Fresh checks and a strict durable callback acknowledgement surround each record.
  Recording failure, reentry and expiry stop before the marked operation; cleanup
  remains possible. A marker identifies the last **recorded boundary**, not proof
  that the following operation began or completed.
- Independent review caught post-append sequence reuse. Successful append/readback
  now consumes its sequence before the retained post-write clock check. Expiry still
  rejects the operation; restoration receives the next sequence.

Deadlines, freshness/identity/security guards, parser contracts, wire commands and
wire timeouts are unchanged. No automatic BOOTSTATUS query, cached authorization,
invitation extension or retry was added. Controller and firmware bytes are unchanged.

## Discriminating validation

New startup assertions fail in seven subcases against the saved prior production
operator and pass after correction. Seven returned-result groups ran against both
versions: six expose old collector failures, and all seven pass after correction.
Late-pass, clock rollback, malformed-result, recording, cleanup and private-field
negative controls remain.

Two additional composed regressions fail against the old masking/counter behavior
and pass after correction. Actual `runner.run()` with simulated HELLO timeout and
cleanup crossing execution retains `hello_A / serial_operation_failed` in both
returned layers, custody journal and runner receipt, with full original restoration,
synthetic owner ACK and lease release before the unchanged restoration ceiling.
The fixed transport category is the actual USB collector outcome, not a physical
root-cause claim. Post-append expiry retains sequence 0; restoration gets 1/2.

Two early test expectations were corrected: the actual owned transport category
is not `deadline_expired`, and synthetic restoration advances the clock slightly
beyond cleanup. Failed attempts are preserved. Final assertions require the same
first failure and completion within the unchanged restoration ceiling.

Operator 24/24, custody 33/33 and runner 25/25 focused groups plus two final composed
selectors pass. One final complete affected matrix passes all 10 suites / 231 tests
in 119.504 seconds with three bounded workers. Its fourteen policy
inputs and ten test inputs were pinned before/after. Actual final runner is 27/27.
Simulated SDK/serial/flash and test ACKs never count as physical evidence.

Existing complete 76-suite/native security evidence is reused for unchanged native
inputs. All 375 current firmware dependencies match the reproducible nine-artifact
pair; no target changed, so no new target build was required. This increment claims
the affected Python matrix, not a new complete 76-suite execution.

## Runtime, commands and evidence

Additive v5 binds 14 policy sources and 3574 capsule files.
Assembly SHA-256: `ece6253faf8df2acdb52fb1ec9d1c09bbfd6e5bc1c0181309ea05be779b02fb0`.
Manifest SHA-256: `eaab2fb38f41b3575b0a1256629f849399cd0ca28253b1ee6048ce047a226705`.
Source-pins SHA-256: `39b80e5700bbd6cc944ab68ebe6a1231c645fda428e8282859a2ce224621c389`.
Historical v4 descriptors/capsule-member bytes are preserved. Maintained assembly
verification and actual isolated `preflight` / `capture-preflight` pass without a
private package, enumeration, device access, grant or lease consumption. This is
inert admission, not an operational package, authority or physical readiness.

Private evidence root: `C:/lu/OpenTrail/.private/ot177-publication/.private/ot0238c-startup-diagnostics-20261002`.

- [Startup before/after and focused proof](../../.private/ot0238c-startup-diagnostics-20261002/startup-validation.json)
- [Returned-failure and composed proof; retained expectation failures](../../.private/ot0238c-startup-diagnostics-20261002/deadline-validation.json)
- [Frozen independent source review](../../.private/ot0238c-startup-diagnostics-20261002/independent-review-final.json)
- [Affected matrix: exact commands, source/log pins and reused evidence](../../.private/ot0238c-startup-diagnostics-20261002/matrix-receipt.json)
- [v5 packaging](../../.private/ot0238c-startup-diagnostics-20261002/packaging.json)
- [Actual isolated inert entrypoints](../../.private/ot0238c-startup-diagnostics-20261002/inert-entrypoints.json)

All commands use `C:/Python314/python.exe -X utf8 -B`. Final gate executes
`.private/ot0238c-startup-diagnostics-20261002/validate_host.py matrix`, then `package`,
then `entrypoints`; matrix receipt names every suite command. Documentation checks
and final file-only preservation audit are retained beside these records.

## Remaining gate

Review a bounded discriminating startup plan before another device attempt. It must
use the retained phase/original failure, distinguish guarded boot from application
readiness, and obtain fresh exact-image/device/recovery authority. Do not blindly
repeat full enrollment or infer application health from its logo. Existing BOOTSTATUS
could narrow initialization, but was not queried or added here.

First enrollment, retained-session rekey/recovery, cancellation, revocation, reset
and production/security acceptance remain open. No V1 credit or public website
capability change. Implementation is local/uncommitted; no Git mutation, Git network
access, publication or deployment occurred.
