# OT-0101e persistent operator handoff

This computer-only interface completes the operational use of the
[reviewed handoff guard](OT-0101e-LIFECYCLE-ACKNOWLEDGEMENT-2026-09-28.md).
It changes no device firmware, security deadline, trial budget or recovery path.
It supplies no hardware approval. Use only with a fresh reviewed binding and the
maintained trial runner; never reuse the consumed v4 grant.
The v5 trial is also closed and consumed. Its input binding is historical;
checkpoint/state schema2 requires a fresh reviewed binding before any future trial.

## Operator sequence

Use the selected checkout's `.private/ot0101e-runtime/python.exe -I -S -B` and
`tools/gnss_lifecycle_handoff_cli.py`. Store per-attempt files under `.private`.
Do not print or copy the complete custody journal: it contains private bindings.

Run controller and helper on the same host in the same boot session. Schema2
checkpoints carry `deadline_monotonic` from the controller's system-wide clock.
Never derive an upper deadline from when a file is created or read. A checkpoint
may already have expired when collected; begin/prepare must refuse it. Finish
preparation before device execution; during a timed stage, collect independent
phone and owner observations together and forward prepared input immediately.
Do not spend the stage window on unrelated administration. A prepared line is
not proof of timely delivery; only the actual durable ACK establishes acceptance.

1. Start the separately authorized maintained runner. When its actual baseline
   checkpoint arrives, save that complete JSON object as `checkpoint.json`.
   Run `begin --state baseline.json --checkpoint checkpoint.json --journal`
   followed by the exact active journal path. It refuses a closed/different
   attempt, nonempty history, or a later-stage checkpoint.
2. Finish the current operator/owner observations. Save only the exact boolean
   fields for that case to `fields.json`. Run `prepare --state baseline.json
   --fields fields.json`. Forward that command's stdout **unchanged**, including
   its LF terminator, to the existing controller session's stdin. Persisting the
   exclusive `.submitted` sidecar precedes emission. Never recreate/delete that
   receipt or resend after an uncertain write.
3. Collect actual controller output from submission onward in `output.txt`.
   A printed input/terminal echo is not acceptance. Run `confirm --state
   baseline.json --output output.txt --next-state phone_stopped.json`.
   Confirmation requires the ordered typed ACK and exact next checkpoint, plus
   independently reread matching durable history from that same journal/request.
   Missing/incomplete output creates no next state. Continue collecting within
   the existing deadline; never resend or reset the deadline.
4. Only `next_checkpoint_confirmed` permits the next owner action. Repeat using
   the saved next-state file. `phone_disconnected` preparation refuses until
   at least120 seconds after confirmed receipt of that checkpoint. This adds a
   conservative receipt hold without extending the original upper deadline.
   An expired next checkpoint cannot produce a usable next state. A completed
   case's ACK can arrive after its former deadline (for example, during automatic
   restart); matching durable history and the live following checkpoint are required.
   Keep the phone service stopped until clock-cleared acceptance.
5. After restart_guard, wait for the actual automatic restart and clock_cleared
   checkpoint. Confirmation also requires the durable warm_restart pass. Do not
   send a restart reply or reconnect early.
6. After fresh_reconnect, call confirm without `--next-state`.
   `observation_confirmed` explicitly reports `restoration_confirmed:false`.
   Continue monitoring the original controller until verified restoration,
   reset/custody closure and the owner's usual-screen confirmation. Only this
   final observation confirmation may read an already-closed journal; it still
   makes no restoration claim. A refused helper operation never permits a retry.

For a terminal outcome, `prepare --state CURRENT.json --terminal cancelled`
or `failed` emits one matching terminal reply. `not_observed` is allowed only
for GPS loss/recovery. A terminal receipt cannot create another stage. Pending
Ready/GPS conditions may be investigated within the original stage window;
do not submit a passed reply before all observations are true.

If the host/controller stops, follow existing custody recovery. Do not reopen a
state, alter timestamps, or reconstruct a passed stage from chat. A new physical
attempt requires a fresh grant; these files cannot create one.

## Evidence

Thirteen interface tests cover persistence across invocations, echo-only refusal,
exclusive reservation before failed stdout delivery, duplicate/reopen refusal,
changed journal/request/attempt, backward clock, terminal cancellation,
interrupted confirmation reservation, final confirmation after closure and a
complete real-stdin/production-observer sequence through this CLI. The complete
sequence simulates only clock progression and the restart capability; no devices
are accessed. Nineteen guard and38 lifecycle tests also pass. Delayed initialization,
expired following checkpoints, terminal expiry and the receipt-hold/upper-deadline
collision are covered; separate processes verify the same-host monotonic clock.
See the [deadline evidence](OT-0101e-LIFECYCLE-DEADLINE-2026-09-29.md). These results
do not accept the remaining physical GPS/clock cases.
