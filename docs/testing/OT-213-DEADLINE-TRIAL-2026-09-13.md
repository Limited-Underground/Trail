# OT-213 corrected-operator invitation trial — 2026-09-13

The independently audited trial status is `pass`. Both roles accepted matching BEGIN markers and strict pass receipts for the invitation candidate through the corrected OT-212 host operator.
Final custody: `released_originals_restarted`. Separate recovery used: `False`.

| Role | Candidate write attempted | Evaluation | Strict receipt accepted | BEGIN observed | Preamble bytes | Reported capture elapsed ms | Capture error | Originals verified / reset |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| A | True | pass | True | True | 64 | 30000 | none | True / True |
| B | True | pass | True | True | 64 | 30000 | none | True / True |

- A: observation available `True`, stage `send_return`, error `none`, input status `stage_result_recorded`, custody `intent_claim_capture_receipt_verified`. Input: `{"discarded_bytes": 26, "first_frame_reason": "none", "first_read_delay_bucket": "<1ms", "first_rejected_frame_bytes": 0, "flags": ["discarded_present", "discarded_nul", "discarded_slip_c0", "discarded_non_ascii", "full_frame_accepted_after_discard"], "terminal_reason": "none"}`.
- B: observation available `True`, stage `send_return`, error `none`, input status `stage_result_recorded`, custody `intent_claim_capture_receipt_verified`. Input: `{"discarded_bytes": 26, "first_frame_reason": "none", "first_read_delay_bucket": "<1ms", "first_rejected_frame_bytes": 0, "flags": ["discarded_present", "discarded_nul", "discarded_slip_c0", "discarded_non_ascii", "full_frame_accepted_after_discard"], "terminal_reason": "none"}`.



The [audited outcome](../../tests/hardware/OT-213-DEADLINE-TRIAL-2026-09-13.json) retains complete categorical observations,
timing, receipt-boundary counters and custody. Where observation occurred, confirmed
serial closure and durable restore intent preceded its one claimed full-NVS read.
Every candidate-touched original application/full NVS and protected region was
independently verified before original restart. Per-role restoration or guarded
release is recorded above. Active locks are absent and authorities are consumed.

## Exact setup

- Existing Heltec WiFi LoRa32 V4.2 / ESP32-S3 / 16 MB roles A and B;
  current routes, ROM identities and geometry were guarded during operations.
- Target `heltec_v4_invitation_eval`, version `ot208-invitation-v1`, 445248 bytes,
  SHA-256 `4526209643bbfb51ccf95d04a992eed41c877dd72cc0b03f415783a63d9036d2`.
- Isolated runtime manifest SHA-256 `9e0eedc9b0c421862253d993f9d6b26450344d34128c73790f53a8eaf87d0e1c`;
  3563 files, 25 executable source pins and 48 firmware source pins reverified.
- Fresh original NVS was checked for all eight physical diagnostic, invitation and
  evaluation namespaces. No retained state was erased to manufacture admission.
- Application span 589824 bytes at `0x10000`; full NVS 12288 at `0xd000`;
  protected bootloader 32768 / partition 4096 / OTA 8192 bytes.

The [tested OT-212 operator](OT-212-DEADLINE-OPERATOR-2026-09-13.md) selects the
corrected host endpoint under the unchanged OT-203 BEGIN boundary, strict receipt
grammar and full observation horizon. The
[OT-208 procedure](OT-208-INVITATION-HARDWARE-PROCEDURE-2026-09-12.md) defines the scope.
The auditor verifies retained journals/custody and fixed diagnostics; original
serial bytes were not retained for a second independent wire parse. Host reset
timing and firmware first-read buckets do not share a physical boot clock.

## Acceptance boundary

This confirms the bounded normal-path invitation evaluation independently on both boards. Its accepted receipt depends on the target's durable boot/role consume, reconstruction refusal, authenticated exchange, duplicate refusal, cancellation and retirement checks.
This is a same-chip evaluation on each board, not a cross-node product join or
human-confirmation proof. `send_return/none` proves only that receipt sending
returned successfully. Normal-path success does not establish interrupted-write,
brownout, cold-power, entropy uniqueness, complete product rekey/reset or selection.
No LoRa transmission, region change, phone/bond operation or factory reset was
performed. Phone UI/OLED and physical markings/cable condition were not newly
independently inspected; USB/ROM reachability was observed.

Next: Continue product trust provisioning and human confirmation in the actual two-node join/message/revoke/rekey workflow. Retain applicable interruption, entropy, reset and corpus gates before Phase 3 admission and crypto selection.
No V1 score or public website status changed. Exact approval, commands, audit and
custody are retained privately under `.private/ot213-admission`. Work is local and
uncommitted; publication requires separate scope.
