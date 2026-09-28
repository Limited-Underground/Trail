# OT-0101e remaining-case operator preparation

## Result

VERIFIED host preparation, 2026-09-28: the maintained controller now supports one
ordered GPS/clock session and one additional guarded warm restart, followed by
the unchanged independently verified original-span restoration. No device was
opened, discovered, reset, flashed or connected by this increment.

- 28 new lifecycle checks and45 affected legacy GNSS/connection/stack checks pass.
- Independent source/plan review caught an unpinned identity helper; its required
  binding entry and omission/tampering regression are corrected.
- Real isolated-runtime input check passes with hardware_access:false and
  serial_enumeration:false. All27 binding pins and unchanged candidate/APK match.
- New request/grant/binding schemas reject historical grants and revision1.
- The [case plan](OT-0101e-LIFECYCLE-TRIAL-PLAN-2026-09-28.md) fixes nine stages,
  one36-minute maximum observation budget, one restart and exact cleanup.
- No firmware/Android source changed or artifact rebuilt; no Git publication,
  public website status change, V1 credit or full OT-0101e completion.

## Boundaries that changed

`gnss_observation_trial.py` accepts a distinct lifecycle request only for the
corrected A_STACK_8192 profile, exact case plan and corrected approved revision.
Every passed/terminal stage is durably journaled. Before the extra boot, the
engine requires its exact passed prefix, the completed120-second disconnect,
unused restart, unexpired grant with720seconds remaining, and fresh identity.
It invokes the existing transport's exact-candidate readback/boot operation.
Initial and warm sanitized startup results are preserved separately. Existing
restoration and restore-only recovery code are unchanged.

`gnss_lifecycle_observation.py` supplies typed per-phase checkpoints and one
monotonic deadline, never renewed by pending replies. The phone-stopped
acknowledgement precedes the120-second retention timer. Phone service stays off
through GPS loss/recovery and restart, so resynchronization cannot conceal clock
loss. Unknown time must be seen before reconnection; final protected region
readback must match baseline. Environmental absence is inconclusive, and the
first failed/cancelled/expired/invalid stage ends observation and is retained
through restoration. Terminal elapsed evidence is not clipped to a deadline.

`run_gnss_lifecycle_trial.py` binds the actual runtime, source, exact artifact,
APK and identity-helper inputs; check/import cannot touch devices. Execute
requires a current original-firmware control and matching corrected-revision
attestation plus a fresh one-use grant. Those flags are operator attestations,
not proof of live approval; the hosted record must be checked before execution.
Restore-only mode neither depends on the candidate/APK/registry nor exposes a
warm-restart capability. No grant issuer or phone automation was added.

No passive recorder or capture worker runs during the observation. This avoids
new serial ownership, segmented-job recovery and duplicate flashing paths.
The three existing240-second transport bounds are reserved before restart;
deadline/overrun outcome is checked after the call. Cleanup continues after
observation expiry. This is a bound on subprocess calls, not a hard real-time
guarantee for filesystem/host scheduling. Startup capture may miss early data;
there is no continuous no-reboot/panic evidence for a future session.

## Exact validation and inputs

The final lifecycle suite includes token/phase replay, private-field rejection,
disconnect timing, pending/deadline behavior, environment inconclusive outcomes,
region mismatch, early automatic resync, restart reserve/overrun, real custody
restoration, old-grant rejection, prefix/one-use guard, grant expiry, ambiguous
restart, fresh restore-only recovery, distinct startup retention and binding
tamper/recovery checks. These are host/injected-adapter evidence, not physical
GPS or protected region readback proof.

```text
.private/ot0101e-runtime/python.exe -I -S -B tests/host/gnss_lifecycle_trial_tests.py
.private/ot0101e-runtime/python.exe -I -S -B -c "import pathlib,unittest,sys; sys.path.insert(0,str(pathlib.Path('tests/host').resolve())); names=['gnss_observation_trial_tests','connection_diagnostic_trial_tests','connection_stack_retest_tests']; suite=unittest.TestLoader().loadTestsFromNames(names); result=unittest.TextTestRunner(verbosity=1).run(suite); sys.exit(not result.wasSuccessful())"
.private/ot0101e-runtime/python.exe -I -S -B tools/run_gnss_lifecycle_trial.py check --binding .private/gnss-lifecycle-plan-v1/binding.json --binding-sha256 c96d17a2d19467194cf51121f9d2663dd08424559f3fafdb35127e85ed98796f
git -c core.safecrlf=false diff --check
```

Added the lifecycle suite to `tools/Test-Host.ps1`; the full firmware/Android
matrix was not rerun because neither product implementation nor artifact changed.
The complete affected operator matrix was run. Repository documentation checks
and18 documentation regressions are the final record gate.

Private source/artifact manifest:
`.private/gnss-lifecycle-plan-v1/preparation.json`.
Frozen binding SHA-256:
c96d17a2d19467194cf51121f9d2663dd08424559f3fafdb35127e85ed98796f.
The two changed v4-bound engine/host-driver byte inputs are retained with their
exact historical hashes in that private plan folder; the consumed v4 binding
and old grants were not rewritten or reissued. New files/changes are local,
uncommitted and unpublished; unrelated dirty work is preserved.

## Preflight and next gate

The applicable porting lessons were reviewed. Exact target/artifact, partition,
stack, transport and rollback boundaries reuse the corrected-stack accepted
evidence because the firmware/APK inputs are unchanged. Reproducible target
builds are not repeated for a host-operator-only change. Resource/concurrency
review and host authority tests cover the new sequence. Live port/device, current
original control, saved-state baseline and current owner presence are deliberately
unverified here: they must be freshly checked before an exact hardware grant.
No console/radio/GNSS-driver modification or invasive power test is part of this
increment; no applicable hardware gate is waived.

The sanitized host result was saved to the private checklist. The bounded
expectation correction is now revision2, Pending/Proposed (observed version276),
with aligned simple owner instructions. The earlier accepted evidence remains
linked; no completion decision or hardware grant was issued. Automatic approval
review rejected a detailed upload containing internal paths before execution;
a reduced path-free summary succeeded. Raw detailed evidence stayed local.

Next: owner reviews the corrected restart expectation, then separately approves
one exact Bench2/S24 lifecycle trial after fresh readiness. Reuse these frozen
inputs and stop at the first nonpassed stage. Silent-stream physical STALE,
cold-power and deferred battery/case work remain explicitly unverified; natural
NO FIX is not substituted for them. Whole-task acceptance remains open.
