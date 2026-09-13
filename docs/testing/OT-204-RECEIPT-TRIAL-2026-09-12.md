# OT-204 receipt-boundary confirmation — 2026-09-12

## Physical result

Both nodes accepted matching BEGIN markers and strict pass receipts.
The audited trial status is `pass` and final custody is
`released_originals_restarted`. One newly approved trial was performed.

| Role | Evaluation | Strict receipt accepted | BEGIN observed | Preamble discarded | Wire bytes read | Capture error | Originals verified / reset |
| --- | --- | --- | --- | --- | --- | --- | --- |
| A | pass | yes | yes | 64 | 174 | none | yes / yes |
| B | pass | yes | yes | 64 | 174 | none | yes / yes |

Serial closure preceded the claimed NVS observation and independent restoration.
Every candidate-touched node's original application/full NVS and protected regions
were verified before restarting its original image. Any untouched node received
guarded original readback/reset release. Both active locks are absent; issued
authorities are consumed and snapshots are stale. Separate recovery used:
`False`. No physical retry occurred.

The [independently audited outcome](../../tests/hardware/OT-204-RECEIPT-TRIAL-2026-09-12.json)
records per-role stage/input observations, wire-boundary diagnostics and custody.
The [OT-203 correction and approved scope](OT-203-RECEIPT-BOUNDARY-2026-09-12.md)
retains its earlier software/preparation checkpoint.

## Exact setup and evidence

- Existing Heltec WiFi LoRa32 V4.2 / ESP32-S3 / 16 MB roles A and B. Current
  routes, ROM identity and geometry were independently guarded during operations.
- Image `heltec_v4_security_receipt_sync`, version `ot203-receipt-sync-v1`,
  440,480 bytes; SHA-256 `d47b09dae0aa1ae878769410a5daaabdac07af2d4c95c9f9d3436a1e03321cee`.
- Runtime manifest SHA-256 `55430360c11ff7113fb55008d6fdc0f67e5ef313c1164455346b98f0dee12e8a`.
  All 3,561 runtime files, 23 executable source pins and 44 firmware source pins
  reverified. Existing builds and affected test evidence were reused unchanged.
- Fresh application spans: 589,824 bytes at `0x10000`; full NVS: 12,288 bytes
  at `0xd000`. Bootloader 32,768 / partition 4,096 / OTA 8,192 bytes were protected.
- Fresh approval, grants, backups and exact physical package were used. Synthetic
  fixtures and historical snapshots supplied no executable custody.

The fixed operator records acceptance only after the exact solicited receipt and
its trailing-output deadline pass. The auditor independently checked the saved
journal/diagnostic consistency, BEGIN counters, image-bound observation claims,
NVS capture hashes/projection and restoration/release evidence. Raw receipt bytes
were not retained; the audit does not claim a second parse of original wire bytes.

## Acceptance boundary

This confirms the receipt-boundary correction on both physical nodes in this trial.
The exact contents and origin of OT-202's historical 129 bytes remain unknown.
`send_return/none` alone is not a policy result; use strict receipt acceptance and
the evaluation field above. The candidate is a bounded same-chip security
evaluation, not production provisioning, rekey, crypto selection or product radio.

No LoRa transmission, region change, phone/bond operation or factory reset was
performed. Original full NVS preserves the prior region/settings. Cold-power,
phone UI and OLED behavior were not independently tested. USB/ROM reachability was
observed; board markings and cable condition were not newly visually inspected.

Next: Reconcile this bounded target-policy result against the eight named security gates and identify the next missing proof before Phase 3 admission and explicit crypto selection.
No weighted V1 milestone or public website status changed. Work remains local and
uncommitted; publication requires separate scope. Exact commands, logs, approval,
claims and custody are retained under `.private/ot204-admission` and adjacent
private OT-204 artifacts in the active worktree.
