# OT-0101e remaining lifecycle plan proposal

## Scope and approval boundary

This report records preparation, the approved observer-only host increment and
the separately approved custody-engine host increment
within the existing OT-0101e scope. Canonical project: `C:\lu\OpenTrail`; selected worktree:
`C:\lu\OpenTrail\.private\ot0101e-gnss`. The earlier documentation-only
proposal and observer result are retained below. The owner subsequently authorized
the bounded independent clock/restart custody mode described in the 2026-10-12
section. Runner, issuer, CLI and handoff integration and a physical trial remain
unapproved; this scenario is not supported by the current runner.
No request, grant, reservation, package, device access, firmware/app rebuild,
publication, V1 credit or public website capability change is supplied here.

## First rejecting boundary and retained evidence

The [v6 closeout](OT-0101e-LIFECYCLE-V6-CLOSEOUT-2026-09-29.md) records:

| Stage | Elapsed from observation start | Accepted result |
| --- | ---: | --- |
| Baseline | 99,757 ms | Protected Ready, clock sync/visible time, GPS FIX and fresh US915. |
| Phone stopped | 178,923 ms | Normal app disconnect and verified stopped service. |
| Phone disconnected | 371,656 ms | Clock advanced after more than two minutes; service stayed stopped. |
| GPS loss | 671,673 ms | Inconclusive; no complete observation inside its five-minute window. |

The first rejected transition was the manual/environmental GPS-loss observation
after accepted retention. The 300,017 ms interval consumed the configured
five-minute window. Finding a reception condition and obtaining the owner reading
were inside that window. Faster owner responses are not the correction.
This identifies an unsuitable observation design, not a GPS, clock, BLE or
firmware fault. Later owner-reported NO FIX has unknown occurrence time and no
confirmed advancing clock. The blank screen coincided with restoration and
does not establish a candidate runtime fault.

Keep the accepted retention result and its 177.630790-second receipt hold.
Do not rerun that acceptance test merely because later steps were inconclusive.
GPS recovery, candidate restart, clock clearing and fresh candidate reconnect
remain unaccepted. Verified restoration, custody closure and the owner's usual
screen confirmation apply to the restored original, not those candidate cases.

## Why the current runner cannot execute a split

Source inspected on 2026-10-11:

| Authority | Current boundary |
| --- | --- |
| [Observer](../../tools/gnss_lifecycle_observation.py) | Fixed nine-row `CASES`; GPS loss/recovery precede restart. `validate_restart_prefix` requires six passed rows. Each deadline starts before the instruction is emitted. |
| [Custody engine](../../tools/gnss_observation_trial.py) | Request validation requires the exact current `PLAN`. Restart requires that prefix to match durable journal history, one unused restart and valid remaining grant time. |
| [Handoff](../../tools/gnss_lifecycle_handoff.py) and [CLI](../../tools/gnss_lifecycle_handoff_cli.py) | Confirmation uses current case ordering, actual durable ACK/history and fresh next checkpoint. |
| [Runner](../../tools/run_gnss_lifecycle_trial.py) | One observation is bounded by the existing plan and remaining grant expiry. |

Changing an operator document alone cannot remove GPS rows or unlock restart.
Do not skip rows, fabricate passes, alter old plans, revive consumed authority,
or extend deadlines. Retained behavior must remain distinct from any future
explicit scenario. Full runner/custody/handoff integration is a later separately
bounded task, not silently included in the observer preparation below.

## Proposed remaining scenarios

### Independent clock/restart scenario

Prepare current original control, exact selected artifact/phone, recovery path
and owner readiness before execution. Fresh protected Ready, clock synchronization,
visible time and region readback are scenario preconditions; historical retention
is linked as accepted evidence rather than repeated. A proposed explicit scenario
would verify stopped phone service, permit one guarded warm restart, require
unknown time before resynchronization, then verify fresh protected reconnect,
clock sync and matching retained region, followed by verified original restoration.
No GPS-loss row would be a prerequisite for this scenario. That divergence needs
source-reviewed explicit scenario admission; the current runner cannot do it.

### Separate environmental GPS loss/recovery scenario

Agree a safe reception setup and realistic finite setup/wait/restoration limits
with the owner. Keep USB powered, cases closed and batteries connected; do not
cover vents or open the case. Prepare the environmental condition and operator
handoff before starting a short observation measurement. NO FIX plus advancing
clock and stopped phone service must be observed together for loss acceptance;
FIX plus advancing clock and stopped service must be observed together for
recovery acceptance. Returning to the recovery position is setup, not proof.

Environmental acquisition remains uncertain. Setup/wait must remain finite and
inside original authority and recovery safeguards; it must not renew a grant,
reset the controller deadline or weaken product/security timers. If readiness
cannot be achieved in the agreed limits, record inconclusive and restore.
Exact limits and separate setup/measurement transitions require owner agreement
and source review before this scenario can become executable.

## Next implementation gate: one observer-only host microtask

1. **Diagnosis:** the observer has only the inseparable nine-row lifecycle and
   cannot represent an independent clock/restart observation. Scope the next
   task to an explicit observer-only scenario; do not integrate custody,
   authority issuance, runner, handoff or devices in that task.
2. **Proposed behavior and measurable completion:** after owner acceptance,
   add an explicit host observation path for clock/restart with synthetic
   restart capability. Preserve the existing nine-row mode unchanged. A
   complete synthetic sequence must accept one guarded restart, unknown time
   before reconnect, and fresh clock/region observations; reject wrong order,
   missing guard, repeated restart and expired measurements. The unsupported
   scenario must remain unadmitted by the existing real runner/request engine.
   No new protocol/schema identifier is selected by this proposal.
3. **Expected files, at most three:**
   `tools/gnss_lifecycle_observation.py`,
   `tests/host/gnss_lifecycle_trial_tests.py`, and this report for evidence.
   If source inspection finds this observer boundary cannot be isolated,
   diagnose and stop rather than expand the task.
4. **Exact validation commands:** from the selected worktree, run
   `.private/ot0101e-runtime/python.exe -I -S -B` with each existing suite:
   `tests/host/gnss_lifecycle_trial_tests.py`,
   `tests/host/gnss_lifecycle_handoff_tests.py`, and
   `tests/host/gnss_lifecycle_handoff_cli_tests.py`. Run the two documentation
   commands below and `git diff --check`. Add discriminating cases to the
   existing lifecycle suite, preserve legacy outcome/deadline cases, and verify
   current custody request rejection of unsupported plans before claim.
   This was the approved implementation validation; results are recorded below.

## Host preparation validation and reuse

Read-only hashing on 2026-10-11 matched all 34 current v6 binding file pins.
The [v6 preparation](OT-0101e-LIFECYCLE-V6-PREPARATION-2026-09-29.md) and
[deadline report](OT-0101e-LIFECYCLE-DEADLINE-2026-09-29.md) therefore retain
their unchanged relevant source/artifact inputs: 70 focused host tests, the
58.074-second real PTY handoff probe, seven mocked issuer checks and inert runtime
validation. These historical results are reused, not rerun or relabeled as
physical acceptance. No private journal identifiers or payloads are reproduced.

Documentation validation for this preparation, from the selected worktree:

```powershell
& .private/ot0101e-runtime/python.exe -I -S -B tools/check_repository_docs.py
& .private/ot0101e-runtime/python.exe -I -S -B tests/host/repository_docs_tests.py
git diff --check
```

On 2026-10-11 the documentation checker passed, all 18 repository documentation
tests passed, and `git diff --check` passed. Manual inspection verified all eight
report links exist inside the selected repository and exactly one 2026-10-11
progress heading is present. All 34 v6 pinned files remain unchanged.
Independent critical review recorded 2026-10-11: APPROVED for this documentation-only proposal, zero blocking findings. The separate reviewer inspected the v6 closeout, observer/request/custody/runner/handoff sources and lifecycle tests; independently confirmed 34/34 unchanged pins and all eight contained links. Reviewed report SHA-256 before this record: 9efd96a31bff34c0542521d6fcf0fd85801a5c6d0b46f871ffd7c2b9858333a2. This review is not owner plan acceptance, code authority or physical acceptance.
The documentation checker covers curated entry documents, not every report link;
manually verify this report's relative links remain inside the selected repository.

## Approved observer-only implementation - 2026-10-11

The owner accepted the bounded observer-only microtask after the documentation
proposal review. Changes are confined to `tools/gnss_lifecycle_observation.py`,
`tests/host/gnss_lifecycle_trial_tests.py` and this report. Existing dirty changes
are preserved. Legacy nine-row `PLAN`, `observe`, result/prefix validators and
deadlines remain unchanged; no custody, runner or handoff source changed.

The separate `observe_clock_restart_host` API requires an injected synthetic
restart, clock, output sink and token source. It has no executable CLI or device
integration. Its output carries `host_only:true` and local scenario provenance,
without a legacy checkpoint/result schema or a new over-the-air identifier.
It follows baseline protected Ready/visible synchronized time/region, stopped
phone-service guard, one restart, unknown clock with service stopped, then fresh
protected Ready/visible synchronized time/matching region. GPS and the accepted
two-minute retention test are not prerequisites or repeated acceptance cases.

Independent host validators require the exact order and guarded restart prefix.
The observer calls the synthetic restart once, marks it used before invoking it,
and never retries uncertainty. Unknown clock must pass before reconnect. Original
per-stage budgets, the 36-minute cap and 1,080-second restart reserve are retained
for this synthetic path. Expired input is terminal; elapsed evidence is not
clipped. Cancellation, wrong tokens/order, missing/false/nonboolean fields and
invalid clock/budget values are refused. This API supplies no real restart
authority or physical evidence.

Tokens, clock and observation booleans are supplied by a trusted synthetic
fixture. No real-session replay protection or freshness across invocations is
established. Future real integration must bind these observations to session and
custody authority; the host-only provenance is not a substitute for those guards.

Thirteen new lifecycle-suite cases cover the complete synthetic sequence,
missing guard/order/stale token, unknown time before reconnect, independent fresh
Ready/time/sync/region requirements, duplicate restart records, uncertain
restart without retry, each stage deadline, overall expiry/reserve, cancellation,
malformed input and invalid/backward clocks. A real custody-engine test rejects
the unsupported five-row plan before claim, used receipt or observer callback.
Legacy result admission also rejects the host result. The unchanged maintained
runner continues to use the legacy observer and exact legacy request admission.

The earlier 34/34 pin statement is the pre-implementation snapshot. This increment
changes the observer and lifecycle-test bytes, so the frozen v6 binding is now
stale for execution: exactly those two pins changed and the other 32 still match.
The historical package and its evidence are preserved and
not repackaged, refreshed or issued. Earlier physical acceptance remains intact;
the new synthetic checks do not supersede it or establish a physical result.

Final host validation on 2026-10-11, using the selected worktree's isolated runtime:

```powershell
& .private/ot0101e-runtime/python.exe -I -S -B tests/host/gnss_lifecycle_trial_tests.py
& .private/ot0101e-runtime/python.exe -I -S -B tests/host/gnss_lifecycle_handoff_tests.py
& .private/ot0101e-runtime/python.exe -I -S -B tests/host/gnss_lifecycle_handoff_cli_tests.py
& .private/ot0101e-runtime/python.exe -I -S -B tools/check_repository_docs.py
& .private/ot0101e-runtime/python.exe -I -S -B tests/host/repository_docs_tests.py
git diff --check
```

All 51 lifecycle, 19 handoff, 13 CLI and 18 repository documentation cases pass
(101 total); the documentation checker and diff check pass. The affected host
matrix was rerun once after source changes; historical PTY/issuer/runtime trials
were not rerun. No hardware, SDK, firmware build or real authority was used.
Independent critical implementation review recorded 2026-10-11 by the separate
`firmware_gate_review` reviewer: APPROVED for this observer-only host scope,
with no blocking findings. The reviewer independently inspected source, tests
and evidence; reconstructed the pre-change observer to confirm legacy behavior
code is preserved; and confirmed exactly two changed v6 pins with the other 32
unchanged. Custody, runner and handoff sources remain unchanged. The reviewer
checked discriminating failure tests and the real-request refusal boundary,
without repeating the passed matrix or operating devices.

Reviewed SHA-256 values before adding this review record:

- Observer: `C2B4EA9AFFD25F0820AAE5E9EF564C51C9DB5DE8B23186BE3EAFBBED54275A38`.
- Lifecycle tests: `C834B35310F71068215C52B48B74F59AFBFD26BD228ECBCC17CD22910E057C5F`.
- Report: `0806F61658507A2836E9E5EB8185F3330E93E0C7534EC38C369AB75E58BC359A`.

This closes the approved host microtask only. Changes remain local and
uncommitted. No physical acceptance, V1 credit, public status change or authority
for runner/custody/handoff integration is established.

## Approved custody-engine host increment - 2026-10-12

**Diagnosis and accepted micro-change:** the host-only five-row scenario has no
real custody admission, and admitting a different plan through the retained
request would let the unchanged runner forward it to the legacy observer. The
owner accepted one engine authority change: an explicitly selected, independent
clock/restart custody mode, validated entirely through synthetic host callbacks.
Only `tools/gnss_observation_trial.py`,
`tests/host/gnss_lifecycle_trial_tests.py` and this report change in this increment.
The existing dirty worktree is preserved. This supplies no real observation
adapter, grant issuer, runner, CLI, handoff integration or device execution.

The engine default selector remains `execution_mode='retained'`. Execute and
restore-only recovery require `execution_mode='clock_restart'` for the new
request, and reject a selector/request mismatch before grant admission, consumed
receipt, journal, active custody lock or backend claim. The unchanged maintained
runner omits that selector and cannot select this mode. The new internal custody
identities are:

| Purpose | Exact identity |
| --- | --- |
| Plan | `OT0101E-CLOCK-RESTART-PLAN-1` |
| Request | `OT0101E-CLOCK-RESTART-REQUEST-1` |
| Grant | `OT0101E-CLOCK-RESTART-GRANT-1` |
| Observation | `OT0101E-CLOCK-RESTART-OBSERVATION-1` |
| Case | `clock-restart-and-restore`, approval revision exactly integer `1` |

These identities are local to this engine/report, within existing OT-0101e scope.
They are not new engineering task IDs, public model names, over-the-air protocols
or claims of external registry registration. Approval revision `1` is the new
internal plan revision, not website approval or product/physical status. No grant
was issued and no physical authorization is supplied by this increment. Legacy lifecycle REQUEST-1,
GRANT-1, nine-row PLAN-2, validators and observer admission remain independent.
The synthetic `observe_clock_restart_host` API is unchanged and remains host-only.

The exact new plan is pinned as immutable canonical bytes. It contains five rows:
baseline, restart_guard, warm_restart, clock_cleared and fresh_reconnect. Stage
budgets are 300/30/720/180/180 seconds, with the retained 2,160-second observation
cap and 1,080-second restart reserve. Baseline requires protected Ready, visible
synchronized time and valid region readback. The service-stopped guard must be
durably recorded after baseline before the engine exposes the single restart.
The supplied two-row prefix must equal both engine state and the loaded durable
journal. The restart intent is durable before backend invocation; uncertainty
never authorizes a retry. The grant and remaining reserve are rechecked before
boot. Subsequent passed rows require verified restart. Unknown time and stopped
phone service must pass before fresh protected reconnect, visible synchronized
time and matching region. GPS and the previously accepted retention interval
are absent; historical retention remains accepted evidence.

Real observation rows contain exact boolean observation fields as well as case,
status and elapsed time. Their independent validator requires exact order,
terminal failure, stage/overall expiry and all required passing observations.
The result has the distinct observation schema and exact request hash; host-only
envelopes and host-only rows are rejected, including rows with a rewritten result
schema. Journaled rows and admitted results are copied so callback mutation cannot
alter saved custody history. Grant validity is checked at every row and again
after the callback returns. The callbacks remain a trusted local runtime boundary;
these tests do not establish real observation freshness, cross-attempt operator
replay protection or truthful device readings. Request-hash binding supplies
contract provenance only; no real observation producer or session integration exists.

Original capture, protected-span checks, exact application/full-NVS restoration,
final readback, original reset and active-lock closure reuse the retained engine
custody path. Failed observation still restores. Failed restoration retains custody
and requires a fresh request-bound, mode-selected restore-only grant; recovery
never grants candidate restart authority. A failed durable guard save prevents
restart and retains custody for recovery.

Nineteen new synthetic engine tests cover selector/request exclusion before any
custody effect, immutable plan/revision rejection, grant schema/actions/request/
runtime/raw-hash mismatch, complete capture/restoration/closure, missing and
mismatched durable prefix, mutation isolation, immediate repeated restart,
uncertain restart, unknown clock/service guard, ordering and fabricated pass,
host-only refusal, each stage/overall expiry, 1,079-second reserve rejection,
expiry during observation and after callback return, failed guard persistence,
and failed restoration followed by fresh mode-bound recovery. Existing legacy
observer/custody/runner/handoff regressions remain in the approved matrix.

Exact final host matrix from the selected worktree:

```powershell
& .private/ot0101e-runtime/python.exe -I -S -B tests/host/gnss_observation_trial_tests.py
& .private/ot0101e-runtime/python.exe -I -S -B tests/host/gnss_lifecycle_trial_tests.py
& .private/ot0101e-runtime/python.exe -I -S -B tests/host/gnss_lifecycle_handoff_tests.py
& .private/ot0101e-runtime/python.exe -I -S -B tests/host/gnss_lifecycle_handoff_cli_tests.py
& .private/ot0101e-runtime/python.exe -I -S -B tools/check_repository_docs.py
& .private/ot0101e-runtime/python.exe -I -S -B tests/host/repository_docs_tests.py
git diff --check
```

Final host validation on 2026-10-12 passed: 11 GNSS custody, 70 lifecycle,
19 handoff, 13 handoff CLI and 18 repository documentation tests (131 total),
plus the repository documentation checker and `git diff --check`. The lifecycle suite supplied focused
validation during implementation; the complete affected matrix ran at the final
source gate. Independent critical review recorded 2026-10-12 by the separate
`custody_clock_review` reviewer: APPROVED for this bounded custody-engine host
increment, with no remaining blocking findings. The reviewer independently
inspected the selector/request boundary, durable restart guard, observation
admission, expiry/reserve checks and 19 new regression cases. Original capture
and restoration functions are AST-identical to HEAD; recovery remains unchanged
after selector-aware request validation. Captured observer, runner and handoff
source hashes remain unchanged. The reviewer independently passed the diff check
and reused the verified final host matrix rather than repeating it.

Reviewed SHA-256 values before adding this review record:

- Engine: `FCF054B93F504E665E4F8B3CE1DFD6F72FEC7F677DD6EBA12A18A6A8EF6EF0D5`.
- Lifecycle tests: `E29C6555465998F01C60B77E57013C3F11E6587087844A40FA4AA563A512D7DE`.
- Report: `D59028EF1CF9281192922F9B1ECBA90ED1781DDE2AEACA079C65998434C6BCCF`.

Review refinements resolved before approval cover malformed-request handling,
reserve discrimination, repeated restart and expiry after callback return.
This closes only the approved host microtask; real producer/session integration,
fresh execution authority and physical acceptance remain separate gates.
The earlier v6 binding remains stale for execution; this increment
also changes the custody engine pin, in addition to the earlier changed observer
and lifecycle-test pins. No historical package or consumed grant is
refreshed, repackaged or issued. Host success never equals physical acceptance.
No hardware, SDK, build, real authority, publication, V1 credit or public website
status change is established. Changes remain local and uncommitted.

## Remaining owner and physical gates

The approved observer and custody host increments do not authorize runner,
issuer, CLI or handoff integration or physical execution. Any
future physical scenario requires freshly reviewed source/artifact binding,
exact current device/phone selection, current original-control verification,
current settings capture and verified restoration plan, agreed finite limits,
and separate exact one-trial authority. Historical grants are consumed.
Trail Bench/Note20 remain the intended pair; Bench 2/S24 remain excluded.
Operational device ownership stays under one controller. Host success never
equals physical acceptance. Full OT-0101e remains unfinished; accepted evidence
does not change V1 progress or public website status.

The older selected-worktree guide's automatic publication wording conflicts
with the current user-provided OpenTrail guide and root separate-authorization
rule. This conflict was reported; current user/root rules govern this preparation.
No commit, push, remote verification or website action is authorized here.
