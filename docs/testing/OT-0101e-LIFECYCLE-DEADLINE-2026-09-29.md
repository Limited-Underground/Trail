# OT-0101e controller deadline and restored trial

## Physical result

The owner authorized one Trail Bench/Note20 test under revision2. Attempt
`946f7e1db5a85b9a5b545b47e116e966` installed and verified the unchanged
593200-byte A_STACK_8192 candidate, SHA-256
`56a1737e5fcdcca0514aba7f615b9934efc0f025d656b393b29633c10ec808b1`.
The Note20 diagnostic APK remained11974618bytes, SHA-256
`79603e1a49b299f06908d9792658af61c85cf77bc498240693b67bb5684adec0`.
Selected hardware: OT-DEV-001, Heltec V4.2 ESP32-S3/16MB, US915;
Note20 SM-N986U. Both remained on USB, cases closed and batteries connected.
Bench2/S24 were untouched. No radio test or transmission was requested.

The app reached the saved protected connection, reported display-clock sync,
and freshly read US915. The owner observed GPS:7 / GPS FIX and a visible clock.
Those observations were not accepted as a completed baseline by the controller:
its300-second step had expired before the operator prepared the reply.
The journal records baseline timeout at300001ms; no later lifecycle case ran.
This result does not establish a GPS, clock or BLE failure.

The maintained controller restored application and NVS, independently verified
all five original spans, reset the original, closed custody and exited0.
All five saved-original descriptors were independently rechecked and the custody
lock was absent. The owner confirmed the usual Trail screen. The Note20 then
reconnected, reported clock sync and freshly read US915 again.

Private evidence in the selected worktree:
`.private/gnss-observation-946f7e1db5a85b9a5b545b47e116e966.json` and
`.private/gnss-lifecycle-plan-v5/closure-and-timing.json`.
No retry occurred; this grant is consumed.

## Established host fault

The actual controller checkpoint was already in the tool response at
22:01:27.988 UTC. Its300-second deadline therefore fell no later than
22:06:27.988 UTC. The submission sidecar was created at22:06:48.596328 UTC:
the reply was prepared **at least20.608328 seconds too late**, before additional
forwarding latency. The local conversation tool timestamps and sidecar were
independently reviewed; no device clock or location is recorded here.

The helper incorrectly used its later `begin` time as the start of the full
checkpoint budget. It measured only289.792838 seconds between initialization
and preparation and therefore permitted an already-expired reply. The producer
correctly retained its original deadline and restored the device. Delays in
collecting output, creating the state file and processing the conversation
consumed real time that the helper's upper-bound calculation omitted.

Three independent device-free terminal probes passed: Python readline with LF,
readline with CR, and the actual production threaded Lines reader with LF then
CR. They disprove a blanket Windows-terminal newline incompatibility. They do
not establish every property of the full physical execution. The proven late
submission already explains why this attempt could not advance; no extra
delimiter, uncertain reply replay or deadline extension was attempted.

## Correction and acceptance boundary

The computer-only correction carries the controller's own monotonic deadline
through the checkpoint and persisted handoff, rather than deriving another
upper bound from delayed receipt. The two-minute disconnected-clock minimum
remains measured conservatively after confirmed receipt. Controller budgets,
firmware, app, device security deadlines and restoration behavior stay unchanged.

All70 focused host tests pass:19 handoff,13 persistent CLI and38 lifecycle.
They include delayed initialization/submission, expired next checkpoints,
terminal expiry, the unchanged120-second lower bound colliding with the original
upper deadline, valid delayed warm-restart acknowledgement, separate-process
monotonic-clock comparison and the complete actual stdin/observer sequence.
The latter simulates clock progression and restart; it is not physical acceptance.
Parent review independently checked the implementation and the deadline source.
Commands use `.private/ot0101e-runtime/python.exe -I -S -B` with
`tests/host/gnss_lifecycle_handoff_tests.py`,
`tests/host/gnss_lifecycle_handoff_cli_tests.py` and
`tests/host/gnss_lifecycle_trial_tests.py`.

The final device-free operational probe also passed through the actual
`exec_command(tty:true)` / `write_stdin` path with the production observer/Lines,
persistent CLI and real monotonic clock. Baseline produced a durable ACK and
the next schema2 checkpoint; CLI confirmation succeeded. A terminal cancellation
then ended phone_stopped cleanly with exit0 in58.074 seconds. Its journal/request
were synthetic; no grants, devices or simulated clock were involved. Private
evidence: `.private/gnss-handoff-pty-deadline-probe/evidence.json` and adjacent
actual output, producer result and persistent receipts. Documentation checker,
18 repository documentation tests and diff check pass.

The correction cannot guarantee that a later tool call delivers a prepared line
in time. Immediate forwarding and actual durable ACK remain required. A late
completed-case ACK can be verified from durable history; it cannot revive an
expired next step. Controller and helper must share the same host/boot clock.
The historical v5 binding/issuer and all trial evidence remain preserved; source
changes invalidate that binding for future execution. No new binding or grant
is issued by this report. Remaining disconnect, GPS loss/recovery, restart and
fresh-reconnect acceptance remains open. V1 progress and public website status
are unchanged. Local changes are uncommitted; no publication occurred.
