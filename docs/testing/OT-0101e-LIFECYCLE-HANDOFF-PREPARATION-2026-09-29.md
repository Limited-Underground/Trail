# OT-0101e executable handoff preparation

## Subsequent execution

The preparation below is the historical pre-trial checkpoint. The owner later
authorized one v5 test, which is now restored and closed. Its baseline reply
was prepared after the controller deadline because this helper rebased the
deadline at delayed initialization. See the [trial closure and correction](OT-0101e-LIFECYCLE-DEADLINE-2026-09-29.md).
The v5 grant is consumed; its binding is historical and cannot authorize a retry.

## Result

The prior guard was tested as an API but had no persistent operator command
interface. That integration gap is now closed by
`tools/gnss_lifecycle_handoff_cli.py`. The maintained runner/custody engine stay
in place. The [operator sequence](OT-0101e-LIFECYCLE-HANDOFF-OPERATOR-2026-09-29.md)
requires begin/prepare/confirm through saved per-attempt files; actual LF output
is forwarded once, then real controller output is checked against its journal
before another owner action. Neither echo nor a saved chat observation can
create the next state. This is host preparation, not physical acceptance.

The interface binds the journal attempt and request digest, opens only baseline
with empty durable history, reserves submission before stdout delivery and
reserves confirmation before publishing a usable next state. A crash or second
invocation cannot renew the hold by creating another next state. Explicit
cancel/fail/not_observed can end observation but cannot advance. A final ACK may
be confirmed after journal closure; restoration still requires separate engine
and visual evidence. Existing host deadlines and all firmware behavior remain.
Operator screen prompts now say the selected Trail device instead of Bench2.

## Validation

68 focused checks pass:10 interface,13 handoff,38 lifecycle,7 private issuer.
The interface suite drives the real production observer and Lines stdin reader
through all nine stages/eight ACKs using the persisted command API. Only time
and restart are simulated; no hardware is accessed. Tests cover echo, wrong
state/history, late/duplicate input,120s minimum, uncertain stdout delivery,
interrupted next-state publication, terminal outcomes and final journal closure.
Independent source review found the confirmation reservation ordering gap; it
was fixed and regression-tested. Final review reports no remaining findings.
The earlier unchanged companion-suite evidence is reused, not relabeled rerun.

Commands from the active worktree use
`.private/ot0101e-runtime/python.exe -I -S -B` with
`tests/host/gnss_lifecycle_handoff_cli_tests.py`,
`tests/host/gnss_lifecycle_handoff_tests.py`,
`tests/host/gnss_lifecycle_trial_tests.py`, and
`.private/gnss-lifecycle-plan-v5/test_issue.py`.
The v5 issuer `check` mode passes against real current inputs and all retained
original descriptors without serial enumeration/opening or grant issuance.
Repository documentation checks/18 regressions and diff check are the final gates.

## Frozen proposal

Private v5 binding SHA-256
`31ec999fb976bf92a0cabcc4734da51da85846adbe9cc7af2c970708d6e846b3`
pins33 inputs, including the persistent interface/tests/operator procedure.
Issuer SHA-256
`c396e93240d42e8f9b5c73db6cf155d6f312e9709c77b63eb2ee6b2bd46004ff`.
It explicitly distinguishes historical v4 original-preflight provenance from
this new request binding. Fresh one-trial/current-original-control/revision2
attestations are still required. Old bindings/grants/originals are preserved.
No v5 grant, reservation, request or ephemeral authority file has been created.
Execution recaptures current originals before any candidate write.

Selected pair: OT-DEV-001 Trail Bench, Heltec V4.2 ESP32-S3/16MB and Note20 SM-N986U.
Candidate A_STACK_8192 remains593200bytes, SHA-256
`56a1737e5fcdcca0514aba7f615b9934efc0f025d656b393b29633c10ec808b1`.
Diagnostic APK remains11974618bytes, SHA-256
`79603e1a49b299f06908d9792658af61c85cf77bc498240693b67bb5684adec0`.
Use current saved-device Ready/clock/US915 control before issuing the exact trial.
Cases stay closed, batteries connected, Bench2/S24 untouched. Firmware-target
porting gates/builds are unchanged and reused because this increment changes
only host interaction; no new target or hardware acceptance is inferred.

Current owner readiness and exact one-trial permission are pending. Remaining
GPS/clock cases stay unaccepted; no V1 credit or public website status change.
No hardware, app install, grant issue, Git mutation or publication occurred.
The unrelated FUTURE_CONCEPTS worktree edit is preserved.
