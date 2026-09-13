# OT-211 invitation-candidate trial — 2026-09-12

The independently audited trial status is `evaluation_failed`. A: evaluation `capture_failed`; strict receipt accepted `false`; BEGIN observed `true`; matching receipt observed `true`; host read bytes `65`; capture at deadline (reported elapsed ms `30000`, clamped); inner endpoint error `endpoint_read_late`; durable stage/error `send_return/none`. B remained untouched by the candidate.
Final custody: `released_originals_restarted`. Separate recovery used: `False`.

| Role | Candidate write attempted | Evaluation | Strict receipt accepted | BEGIN observed | Preamble bytes | Reported capture elapsed ms | Capture error | Originals verified / reset |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| A | True | capture_failed | False | True | 64 | 30000 | read_failed | True / True |
| B | False | not_attempted | False | False | 0 | 0 | none | True / True |

- A: observation available `True`, stage `send_return`, error `none`, input status `stage_result_recorded`, custody `intent_claim_capture_receipt_verified`. Input: `{"discarded_bytes": 26, "first_frame_reason": "none", "first_read_delay_bucket": "<1ms", "first_rejected_frame_bytes": 0, "flags": ["discarded_present", "discarded_nul", "discarded_slip_c0", "discarded_non_ascii", "full_frame_accepted_after_discard"], "terminal_reason": "none"}`.
- B: observation available `False`, stage `None`, error `None`, input status `None`, custody `not_captured`. Input: `null`.

The fixed host diagnostics distinguish a matching receipt being observed from strict receipt acceptance. A matching observation does not override an endpoint deadline error. The reported elapsed value is clamped at the 30000 ms deadline. `endpoint_read_late` records the deadline being reached before a subsequent raw read; retained diagnostics do not identify which deadline check or provide subread timestamps.

The [audited outcome](../../tests/hardware/OT-211-INVITATION-TRIAL-2026-09-12.json) retains complete categorical observations,
timing, receipt-boundary counters and custody. Where observation occurred, confirmed
serial closure and durable restore intent preceded its one claimed full-NVS read.
Every candidate-touched original application/full NVS and protected region was
independently verified before original restart. Untouched roles received guarded
original verification/reset release. Active locks are absent and authorities are consumed.

## Exact setup

- Existing Heltec WiFi LoRa32 V4.2 / ESP32-S3 / 16 MB roles A and B;
  current routes, ROM identities and geometry were guarded during operations.
- Target `heltec_v4_invitation_eval`, version `ot208-invitation-v1`, 445248 bytes,
  SHA-256 `4526209643bbfb51ccf95d04a992eed41c877dd72cc0b03f415783a63d9036d2`.
- Isolated runtime manifest SHA-256 `fbbc3854de3b4c78f7fe607f1d9b55d33b9f336abd87b59dfe929888ef601129`;
  3561 files, 23 executable source pins and 48 firmware source pins reverified.
- Fresh original NVS was checked for all eight physical diagnostic, invitation and
  evaluation namespaces. No retained state was erased to manufacture admission.
- Application span 589824 bytes at `0x10000`; full NVS 12288 at `0xd000`;
  protected bootloader 32768 / partition 4096 / OTA 8192 bytes.

The [tested OT-210 operator](OT-210-INVITATION-OPERATOR-2026-09-12.md) preserves the
OT-203 BEGIN boundary and strict receipt/deadline behavior. The
[OT-208 procedure](OT-208-INVITATION-HARDWARE-PROCEDURE-2026-09-12.md) defines the scope.
The auditor verifies retained journals/custody and fixed diagnostics; original
serial bytes were not retained for a second independent wire parse. Host reset
timing and firmware first-read buckets do not share a physical boot clock.

## Software reproduction

Eight simulated-clock cases passed against five unchanged actual host modules:
ordinary receipt and no-receipt controls; both pre-read deadline crossings with
and without an early matching receipt; a late-returned byte; and an early trailing
byte. Either pre-read crossing reproduced `endpoint_read_late` and refused an
already observed matching receipt. Late and trailing bytes remained rejected,
and no raw read began after the deadline.

- Reproduction program SHA-256: `ddd5f34122ab948f44eb7a8d91e195d5be7c0fe96f60b5a513ad46e2bf8824c7`.
- Eight-case result SHA-256: `d9c59a48200c327a86c088fbd68b74a33738770caa19ce6960675dbb4861ba15`.

The clock, serial-like handle and admission guard were simulated; no hardware was
accessed. This establishes a host software mechanism consistent with the retained
trial diagnostics. It does not identify which physical deadline check crossed,
reconstruct original serial bytes, or validate a corrected operator or another
physical attempt. Exact software evidence is retained privately.

## Acceptance boundary

This attempt does not confirm the invitation candidate on both boards. A durable stage or successful send alone cannot substitute for strict receipt acceptance.
This is a same-chip evaluation on each board, not a cross-node product join or
human-confirmation proof. `send_return/none` proves only that receipt sending
returned successfully. Normal-path success does not establish interrupted-write,
brownout, cold-power, entropy uniqueness, complete product rekey/reset or selection.
No LoRa transmission, region change, phone/bond operation or factory reset was
performed. Phone UI/OLED and physical markings/cable condition were not newly
independently inspected; USB/ROM reachability was observed.

Next: Implement the successor host endpoint correction so deadline exhaustion before a raw read ends observation normally. Preserve admission failures, the full 30-second observation horizon, and late/trailing-output rejection. Regression-test both clock crossings, then rebuild and pin the affected operator. No firmware change is indicated by this reproduction; no physical retry is authorized.
No V1 score or public website status changed. Exact approval, commands, audit and
custody are retained privately under `.private/ot211-admission`. Work is local and
uncommitted; publication requires separate scope.
