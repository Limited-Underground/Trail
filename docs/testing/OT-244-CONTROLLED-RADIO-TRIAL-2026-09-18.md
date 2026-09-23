# OT-244: controlled two-Heltec acceptance trial

2026-09-18. One authorized physical attempt completed with activation failure.
Both originals and saved states are restored; the user confirmed both normal Trail screens.
The preparation sections below preserve the approved scope.

## Exact candidate and case

Use the [OT243 correction](OT-243-NVS-POLLING-COST-2026-09-18.md), which passed
its full host exchange, 43-suite matrix and two reproducible target builds.
Candidate: `build/ot242-enrolled-ot243-c/heltec_v4_enrolled_eval.bin`,
528160 bytes, SHA256
`cec805d4c4b8eaea9ea6380c037b7b34a97af4a9b6fc40b02b6d98b98db7f24e`.

One attempt on the two existing Heltec V4 nodes: fresh evaluation enrollment,
three handshake transfers, matching-code review, one physical local confirmation
on each node, four activation controls, then four authenticated fixed statuses
in each direction. This is an evaluation radio test; phones are not involved.
The candidate will be temporary and both original applications and saved state
will be restored after success or failure. No phone reset, app-history clearing,
protected-storage write, production admission or reboot/rekey experiment.

Radio remains 915MHz, 125kHz, SF7, coding rate 4/5, configured 2dBm, maximum 158-byte
frames and at most 16 transmissions per role. The invitation window remains 60s.
The expected successful final counters are A: 8 attempted/8 completed TX, 7 RX;
B: 7 attempted/7 completed TX, 8 RX; both zero RX errors and stopped. Log physical
setup, total bounded observation time and available diagnostics honestly; do not
claim per-packet latency, field range or loss characteristics beyond the data.

## Preparation and reuse

All 1014 matrix source pins and both candidate images were checked against the
accepted evidence. The 12 maintained operator source pins, isolated Python,
runtime manifest and exact crypto dependency inventory are unchanged. The copied
private controller differs only in its description and proposal schema label;
normalized AST comparison verifies unchanged executable behavior.

The fresh package is `.private/ot244-trial`; its `control.py prepare` succeeds.
The existing maintained operator's preparation path independently verifies the
binding and request. Negative admission calls reject a wrong binding digest,
stale candidate request and altered operator-source pin. They use prepare only,
which cannot issue a grant or open hardware. A separate read-only review found
no preparation blocker. See the [sanitized preparation receipt](../../tests/benchmarks/crypto/OT-244-TRIAL-PREPARATION-2026-09-18.json).
The consumed OT242 package and its execution directory remain untouched.

## Capture, custody and restoration

The baseline's 589824-byte original application prefixes identify the intended
originals; they are not recovery images. Immediately before execution, resolve
both intended boards freshly and refuse changed identity/image/partition state.
Before any candidate write, capture both complete 733184-byte application spans
at 0x10000 and complete 12288-byte ordinary-NVS spans at 0xd000, plus protected
bootloader/partition/OTA comparisons. Require evaluation namespace absence.
Never reuse old ports, grants, opaque bindings or state snapshots.

Operate the nodes sequentially. Preserve the controller and its in-memory
binding until custody closes. On every exit, independently compare all original
application/NVS/protected spans and reset the verified originals. An unsuccessful
restoration keeps custody; at most two explicit restore-only attempts are scoped,
with no second candidate trial. A user normal-screen observation is separate
from verified restored bytes and reset-command completion.

Existing tested guards remain: expiry and identity before claim/write/boot/
observation; exact source/image/runtime/dependency binding; closed passive handles;
replay and active-custody rejection; precise radio grant actions; continued
restoration of the other role if one fails. Full operator tests are reused from
the unchanged current-source matrix; no redundant firmware build or full rerun.

## Operator readiness and stop conditions

Before execution, the user must be nearby with both nodes USB-connected, antennas
attached and normal Trail screens showing, and approve this exact temporary
candidate/radio/restoration scope. Wait for the controller comparison prompt;
if the displayed codes match, hold and release BOOT/user for about one second on
each device. Do not type or retain comparison codes. No host confirmation bypass.

Stop on ambiguous identity/port/image, failed preflight, unexpected target state,
missed confirmation window or transport/protocol refusal. Preserve the first
sanitized failure point and both available diagnostic snapshots, restore, and
close custody. Do not automatically retry the candidate. Physical acceptance
requires the complete exchange, exact final counters, cleanup and independently
verified restoration, followed by normal-screen confirmation.

V1 and public website status remain unchanged. Preparation is not physical
acceptance; the previous failure's unique cause remains unproven.

## Physical result

One approved attempt used two Heltec WiFi LoRa32 V4.2 ESP32-S3/16MiB nodes
with antennas attached and the exact candidate above. Both write/readbacks and
all three handshake transfers passed. The bridge observed local-confirmed
STATUS state4 for both nodes before entering activation; the user reported done.
Exact comparison/button timestamps were not saved, and the comparison alert and
failure were collected together by the host. Do not infer human reaction time.

Activation step1 failed at B/RFPOLL. Both diagnostic snapshots report fault3
(authority/clock); expiry is consistent with this category, but other authority
checks share it. This does not uniquely establish expiry or its physical cause.
A completed3/3 TX, B1/1. Four-status exchange was not reached; final RX/loss/
duplicate statistics are unavailable. No per-packet latency or range was measured.

Measured cumulative NVS diagnostics: A50603 reads/10714521us, B118982 reads/
24682778us. Maximum recorded ticks were347864us/335231us; service gaps30960ms/
9970ms. These are cumulative/maxima, not a stage-by-stage wall-clock budget.
Protocol cleanup was unverified, while passive handles closed. Automatic original
restoration independently compared application733184B, ordinary NVS12288B and
all protected spans, reset both originals and closed custody. The user confirmed both normal Trail screens after closure. See the [sanitized physical receipt](../../tests/benchmarks/crypto/OT-244-PHYSICAL-2026-09-18.json).

The next discriminating host case must include independently scheduled per-node
idle ticks: app_main ticks on idle iterations, button edges and before commands,
whereas OT243's serialized model explicitly omitted concurrent idle loops.
Replay actual bridge ordering with charged SDK durations and record REVIEW,
each first state4, activation send/receive and the specific rejecting guard.
Do not sum concurrent node costs into one wall clock or repeat this unchanged
hardware attempt. No source/build change in this trial; accepted host/build
evidence is reused. Changes remain local/uncommitted; publication is pending
separate authorization. V1 and public website status remain unchanged.
