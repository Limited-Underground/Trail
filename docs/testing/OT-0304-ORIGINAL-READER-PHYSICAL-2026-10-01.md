# OT-0304 Original-only Trail Bench reader confirmation

## Result

VERIFIED, 2026-10-01T20:10:17.53376Z: one separately authorized read-only check
passed on Trail Bench (OT-DEV-001). Both 733,184-byte factory-prefix reads agree
with the retained original. Matching partition and erased OTA metadata passed
before those reads. One guarded original restart succeeded; the operator exited
0, durable evidence passed offline audit, and device custody closed.

OBSERVED: the owner separately answered "Yes, usual Trail screen" after the
operator finished. No manual restart was needed. There were no flash/settings
writes, candidate installation, phone operations or commanded radio tests.
Cases and batteries were unchanged; the S24 and other Heltec were untouched.
The grant and reader reservation are consumed. Do not replay this operator.

## Exact scope and measured bytes

Inventory identifies Heltec WiFi LoRa32 V4.2 / ESP32-S3 / 16 MB. A fresh unique
USB route and independent ROM identity/capacity checks preceded access. Private
identifiers/routes were used only in memory and are absent from public records.
Radio region/power were neither changed nor measured: no RF operation was scoped.

| Region / offset | Bytes | Fresh SHA-256 |
| --- | --- | --- |
| Partition / 0x8000 | 4096 | `b7bbaf702afd377973aa2371f288bcea50548865d10e2cdada4d5e7f98a91601` |
| OTA metadata / 0x9000 | 8192 | `7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f` |
| Factory prefix / 0x10000, repeated | 733184 | `0b86589ef8ea4d4883bcf535b5d3732bf24189237b5b02803961af7c22115bf7` |

All captures equal retained original files, which were reused without duplicate
backups. No NVS, bootloader or `ot_state` capture was added. Application reads
took 47.497 and 48.359 seconds; cleanup took 6.639 seconds. These are controller
intervals including prerequisite checks, not isolated flash-transfer/reset times.
Total command duration was 131.599 seconds, including frozen runtime preflight.

The freshly matched application prefix now binds the existing offline image
analysis to resident factory bytes: project `opentrail_heltec_v4_bench`, descriptor
`ot178-phone-v1`, valid 586,736-byte image, rounded extent 589,824 bytes and erased
remainder within the 733,184-byte capture. This is the older retained image,
different from the prepared 602,096-byte OT-0302 standard settings candidate.
It is not proof of the currently executing slot, negotiated runtime profile or
the exclusive cause of the earlier unavailable public-settings readback.

## Reviewed procedure and evidence

The [fresh reader preparation](OT-0304-READONLY-READER-PREPARATION-2026-10-01.md)
owns the validated flow and unchanged commands. Before execution the approval
was independently bound to the exact preparation receipt, runner and inputs.
Runner SHA-256: `c1130c781b9149f53cf75172650c8b6657ba6c5a6c1380438f112ad72749796d`.
Inputs SHA-256: `8812e0248733ad3b71984aba4eb60f19e4ed6b7212e3b283e6cda2bfcea86a66`.
The isolated Python 3.14.6/esptool 5.3.1/pyserial 3.5 capsule was reused.

The write-once reservation binds one inventory/runner/input attempt. Fixed layout
guards stop mismatched metadata before application reads. Primary outcome was
saved before one finally restart; cleanup outcome and final result were saved
separately. Both diagnostic histories and the evidence-error list are empty.
Independent result review and offline audit verify sequence, receipt/capture
hashes, successful restart and absence of active custody. The owner confirmation
is a separate receipt; machine outcomes were not rewritten to insert it.

Private physical receipts remain in `.private/ot0304-reader-preparation-20261001`.
The separate execution command, grant, audit, owner confirmation, reviews,
checklist/tracker updates and final inventory are under
`.private/ot0304-original-reader-execution-20261001`; `closeout.json` owns exact
commands, hashes, preservation and final checks. The prepared host closeout and
all earlier closed device receipts remain unchanged.

The prior 28 inert methods, isolated preparation check, maintained diagnostics
matrix and source review are reused unchanged. No product source changed or
firmware/Android rebuild was needed. Documentation checker/regression and diff
results are recorded in the execution closeout.

## Remaining gates

The [earlier USB refusal](OT-0304-READONLY-FIRMWARE-CHECK-2026-10-01.md) was not
reproduced; its physical cause remains UNKNOWN. One successful run does not
prove an intermittent problem fixed or retrospectively recover its missing
diagnostics. The diagnostics correction changed no commands/acceptance guards.

Resident factory-prefix identity is now measured; executing firmware/profile,
bootloader behavior and complete current saved-state/recovery custody remain
unmeasured. Erased OTA metadata alone does not prove the running slot.
Next, complete the bounded original/settings custody plan for the
[prepared standard firmware test](OT-0304-FIRMWARE-RECOVERY-PREPARATION-2026-10-01.md),
including current NVS and custom `ot_state`, before requesting its separate
application-only write/test grant. Do not clear the passed Note20 pairing to
simulate fresh setup, infer a full flash backup from this prefix, or force
public settings by saving default name/region values.

Full first-use, settings, warm-board and production acceptance remain incomplete.
No V1 credit or public website capability change. Work is local/uncommitted;
no Git network operation, publication or website deployment occurred.
