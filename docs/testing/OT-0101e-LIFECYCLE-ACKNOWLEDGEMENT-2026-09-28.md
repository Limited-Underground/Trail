# OT-0101e lifecycle acknowledgement timing

## Physical result

OBSERVED/VERIFIED 2026-09-28: one revision2-approved Bench 2/S24 lifecycle trial
used the exact 593200-byte corrected-stack candidate, SHA-256
`56a1737e5fcdcca0514aba7f615b9934efc0f025d656b393b29633c10ec808b1`, and unchanged
11974618-byte diagnostic APK, SHA-256
`79603e1a49b299f06908d9792658af61c85cf77bc498240693b67bb5684adec0`.
Cases stayed closed, batteries connected, Bench 1 unchanged; no radio transmission.

The baseline passed at 192684ms: fresh protected Ready, clock synchronization,
US915 protected readback and owner-observed GPS10/FIX with visible time.
The operator stopped the phone app and directly verified its device service was
absent. The owner confirmed the clock remained visible, but the operator's
acknowledgement missed the fixed 60-second checkpoint. First terminal evidence
is `timeout / phone_stopped` at 252702ms; the phase consumed 60018ms.
This is a host interaction timing failure, not evidence of a device fault.
The two-minute phone-disconnect retention, GPS loss/recovery, warm restart and
fresh reconnect cases were not reached. A late reply does not retroactively pass.

The maintained engine restored application/NVS, independently verified all five
original spans, reset the original and closed custody. Process exit0, active lock
absent, and saved originals' hashes independently audited. The owner confirmed
Bench 2's usual Trail screen. No retry occurred; this trial grant is consumed.
Restored-phone reconnection was initiated; fresh post-restoration Ready is not
claimed for this attempt. Restoration itself is verified independently of BLE.
The user-selected USB stay-awake setting remains enabled on the S24.

Private owning evidence is the immutable attempt journal and original captures,
with audit at `.private/gnss-lifecycle-plan-v2/closeout.json` in this checkout.
Historical plan-v1/plan-v2 bindings and grant files remain unchanged.

## Host correction and flow review

`gnss_lifecycle_observation.PLAN` is now `OT0101E-LIFECYCLE-PLAN-2`.
The [successor plan](OT-0101e-LIFECYCLE-TRIAL-PLAN-V2-2026-09-28.md) records the
same ordered cases and failure/restoration boundaries with these fixed limits:

| Transition | Prior limit | Revised limit | Evidence and stopping condition |
| --- | --- | --- | --- |
| Phone service stopped to owner clock acknowledgement | 60s | 180s | Direct service-off verification and visible clock; expiry is terminal. |
| Acknowledgement to retained/advancing clock | 180s | 300s | At least120s hold remains mandatory; no early pass. |
| Guarded restart admission | 960s remaining | 1080s remaining | Three240s transport bounds plus180s cleared-clock and180s reconnect windows; reject insufficient budget before restart. |
| Restart to cleared-clock acknowledgement | 60s | 180s | Service stays off; owner sees unknown time before reconnect. |
| Any stage to completion or recovery | 2160s overall | 2160s overall | Monotonic deadline clips every stage, pending replies never renew it; terminal evidence survives restoration. |

These changes budget operator/tool/human response time. They change no firmware,
clock validity, authentication, pairing or invitation timer. Old PLAN-1 requests
and grants are rejected before hardware claim or grant consumption. No transport,
restoration or authorization shortcut was added.

## Validation and remaining gate

All110 affected host tests passed in five separate isolated interpreters:
32 lifecycle,11 GNSS custody,21 connection trial,15 stack retest,31 capture.
Regressions cover61/179-second acknowledgement success, exact180/300-second
expiry, the120-second minimum, overall clipping,1080/1079 restart admission,
and old-plan/grant rejection before claim. Existing recovery/token/authority
negative controls remain. Firmware/APK artifacts were reused without rebuilding.
Repository documentation checker, all18 documentation regressions and
`git diff --check` also passed. Independent final review found no blocking issue.
The initial combined-suite probe polluted module inventory; separate interpreter
runs resolve that harness-only failure. Isolated legacy suites require their
existing tests/host import path; final commands include it.

The owner selected **Trail Bench and the Note20** for future testing because the
S24 is a personal phone. The maintained runner now supports an explicit selected-board schema2 binding.
Legacy schema1 retains Bench2 semantics; changed/unknown/wrong selections fail
before claim. Recovery remains tied to the original binding and opaque identity.
Read-only Note20 inspection verifies the installed V1-Test package (11958234bytes,
SHA-256 `9f91a3f361221d19089e685b492b5e772e5cf7d658298ff955c4feb2508694a2`),
matching signing certificate and running device service. The owner reports it
connected; the current inspected view does not independently establish Ready.
The app differs from the frozen diagnostic APK. No app upgrade occurred.
Before a next trial, verify current protected saved-device control, authorize any
required exact diagnostic app upgrade, and bind current Trail Bench recovery. Do not reuse
Bench 2's backup or issue its grant for Trail Bench. The hosted task's simple device instructions now match this owner choice without
changing acceptance or approval. A software-only selected-board binding is prepared;
it is not a grant or proof of current originals. No fresh physical trial is authorized
by this report. Original backup/reset preparation requires separate authorization.

Selected-board support adds six focused regressions; the final affected matrix
passes116 tests (38 lifecycle plus the unchanged11/21/15/31 companion suites).
The real schema2/Trail Bench software-only28-pin binding passes the isolated
runtime input check without hardware access or serial enumeration. Root review
of selection/authority/recovery found no blocking issue. No current-board
originals or hardware grant are inferred from that check.

OT-0101e remains in progress; no V1 credit or public website status changed.
These host changes are local/uncommitted. The earlier PR43 merge remains complete.

## Selected-pair preparation closure

The owner explicitly authorized the exact diagnostic app upgrade and Trail Bench
original-only backup/reset. Note20 SM-N986U / SDK33 now has the reviewed
11974618-byte V1-Test APK SHA-256
`79603e1a49b299f06908d9792658af61c85cf77bc498240693b67bb5684adec0`,
installed with data-preserving replacement and the matching signer. The saved
connection reaches Trail Bench and reports display clock synchronization.
No pairing reset, app clear or factory reset occurred.

The maintained original-only ROM transport uses the verified existing OT-DEV-001
record. A new private v4 binding preserves every earlier binding and the original
OT-DEV-002 registry record. Ten focused fake-transport checks pass; its inert check
opens no serial port. The exact owner-approved execution captures all five spans,
including application733184 and NVS12288bytes; repeated application/NVS reads match.
Historical original prefix, protected bytes, NVS reset state and exact original/
candidate image-header/checksum/digest compatibility pass. Final all-five readback
matches those captured originals; same-device reset succeeds, controller exit0,
custody closed. Independent file descriptors match and owner confirms usual screen.
No Heltec flash writes, candidate boots, radio transmission or GPS/clock lifecycle
cases were performed. Post-restart saved-device connection, clock-sync report and US915 readback pass.
This preparation does not authorize the next candidate trial or add V1 credit.

Private evidence: `.private/trail-bench-note20-preflight-20260928/closeout.json`,
`original-preflight-result.json`, `original-preflight-journal.jsonl` and
`phone-upgrade-result.json`; private operator binding:
`.private/gnss-lifecycle-plan-v4/binding.json` SHA-256
`af4848f96daa83d005c073f3e6d467507d23d442986b1aa56b52330830c96d7f`.

## Trail Bench/Note20 trial closure

One separately owner-authorized PLAN-2 trial used Trail Bench (OT-DEV-001),
Note20 SM-N986U/SDK33, the unchanged exact A_STACK_8192 candidate and diagnostic
APK above. Cases stayed closed, batteries connected; Bench2/S24 were untouched.
Candidate write/readback/boot passed. Fresh protected Ready, clock sync and
US915 readback passed; owner observed GPS9/FIX and visible time.

The journal records baseline passed at200209ms and phone_stopped timeout
at380239ms: the full180.030s acknowledgement window. The normal app disconnect
and service-off check were performed; owner confirmed a visible clock. The
controller nevertheless processed no passing acknowledgement before its deadline.
No phone_disconnected checkpoint or120s retention comparison was reached.
GPS loss/recovery, candidate warm restart, clock clearing and fresh reconnect
acceptance were not reached. This is an operator timeout, not demonstrated GPS,
clock, BLE or firmware failure.

The operator mistakenly treated terminal echo as confirmation of a transition
and wrote disconnect-window.json before observing the next checkpoint. That
timestamp is invalid retention evidence. The original file is preserved with
disconnect-window-correction.json marking the error. Exact live submission
newline/timing is not retained well enough to distinguish late input from
delivery failure. A read-only independent review and no-device same-runtime
PTY probe confirm two consecutive LF-terminated replies work; that probe
does not establish what the live call delivered. No security/product timer
or frozen trial source was changed.

Before any future execution, require explicit LF-terminated input, submission
time, and the next checkpoint's exact case/fresh token before advancing owner
actions or starting retention timing. Echo alone is never acceptance. If no
acknowledgement appears, inspect the current durable case/deadline; do not
blindly resend an uncertain queued reply or assume a transition. This delivery
check must pass without devices before another candidate trial is proposed.

Restoration completed through the maintained engine: original application/NVS
write/readback, final independent all-five-span sweep, original reset, exit0 and
custody closed. Independent captured-file descriptors match and active lock is
absent. Owner confirms usual Trail screen. No retry or candidate warm restart
occurred. V1 credit and public website status are unchanged; repository changes
remain local/uncommitted.

Private evidence: .private/gnss-observation-5b170576c510bd6426b68731ec089b09.json,
its five captured originals, .private/gnss-lifecycle-plan-v4/trial-closeout.json
(SHA-256565d9e1f0cd058586a01b1ed40eeeb75db2d24300ece945678f9477efe7cf62c),
baseline-observation.json, phone-stop-observation.json and the timer correction.

Post-restoration Note20 saved-device reconnection, display clock-sync report and
protected US915 readback also pass. App data/bond remain intact. Sanitized control:
.private/gnss-lifecycle-plan-v4/restored-phone-control.json.

## Computer-only handoff correction

The owner authorized completing this correction without their input or devices.
The production observation code now emits OT0101E-LIFECYCLE-ACK-1 with the exact
case/token/passed timestamp only after on_case durably records that pass. Pending
and terminal replies do not emit a passed ACK. Automatic warm_restart has no
human token and emits none. Final fresh_reconnect ACK means observation passed,
not original restoration or custody release.

The device-free tools/gnss_lifecycle_handoff.py prepares one actual LF-terminated
reply, forbids duplicate/expired submissions and an early120s retention reply.
It accepts a transition only with matching typed ACK, matching durable passed
history and the exact next case/fresh token. Its bounded output parser rejects
terminal echo, incomplete lines, duplicate ACKs and out-of-order checkpoints.
Delayed acknowledgement starts the conservative hold at next-checkpoint receipt,
not submission. Restart progression requires the automatic durable restart pass.
The helper is an operator API for the next reviewed execution composition; it
cannot launch a trial, access hardware, grant authority or change controller
budgets. Controller deadlines and restoration remain authoritative and unchanged.

All136 host checks pass:13 new handoff,38 lifecycle,11 original custody,21
connection trial,9 current stack-retest entrypoint,31 diagnostic capture and13
capture-custody checks. The13 new checks include an actual isolated subprocess,
real stdin Lines reader and production observe through all nine stages/eight ACKs;
clock/restart are simulated and no device is opened. A10s watchdog bounds that
probe. The discriminating ACK test fails against the pre-ACK observer and passes
with the correction. Invalid token/case, echo-only, duplicate, pending/terminal,
wrong durable history, bool/float timestamp and fragmented-output controls pass.
Two isolated companion entrypoints initially lacked their tests/host import path;
adding the established explicit path resolves that test-launch issue.

Commands use .private/ot0101e-runtime/python.exe -I -S -B and the seven named
host entrypoints. Connection diagnostic, stack retest and capture entrypoints
use -c with sys.path.insert(0,'tests/host') and runpy.run_path(...,'__main__').
No firmware/APK rebuild, phone/device operation or new hardware grant occurred.
The maintained runner now requires both new helper/test source pins in any fresh
binding. Historical v4 binding/consumed grant and captured originals are preserved;
changed source means that binding cannot execute again. Prepare a fresh reviewed
binding before proposing another exact owner-authorized physical trial.

Private evidence: .private/gnss-lifecycle-handoff/before-fix-regression.json and
validation.json. Changes are local/uncommitted; no V1 credit or public website
status change. The remaining physical GPS/clock cases are still unaccepted.
