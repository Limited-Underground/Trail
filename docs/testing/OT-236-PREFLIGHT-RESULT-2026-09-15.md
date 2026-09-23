# OT-236 physical preflight result

The later [host correction](OT-236-NVS-INTERVAL-CORRECTION-2026-09-15.md)
implements the interval described below. This report preserves the failed
physical attempt; it does not imply that a corrected physical trial has run.

## Observed result

The authorized no-RF/no-phone preflight ran from 2026-09-15T09:11:53Z to
09:13:14Z on the two Heltec WiFi LoRa32 V4.2 / ESP32-S3 / 16MB bench nodes.
It returned incomplete before issuing a grant or starting a candidate journal.
No candidate flash, erase, NVS write, radio transmission or phone action occurred.
Python 3.14.6, esptool 5.3.1 and pyserial 3.5 were used. The exact candidate,
original versions, sizes, hashes and offsets remain in the
[prepared scope](OT-236-LIBSODIUM-CAPTURE-PREPARATION-2026-09-15.md).

The caller did not retain the initial exception stage. A separate bounded read
from 09:15:03Z to 09:15:51Z isolated a mismatch in A's full 12288-byte NVS region.
A's bootloader, partition table, OTA metadata and original application matched.
The expected NVS hash came from the older OT-234 restoration capture. The fresh
hash differed. This proves snapshot disagreement; neither corruption nor the
reason for the change is established. B's NVS was not diagnosed after A failed.
Raw NVS bytes and private device identifiers were not retained by this attempt.

Cleanup independently checked each 589824-byte padded original application and
reset each original. A was checked/reset again after the diagnosis. The user
then confirmed both normal Trail screens. This is verified original firmware
and observed screen recovery, not new acceptance of captures or NVS preservation.

## Required correction before another trial

NVS is mutable application state. An old snapshot cannot serve as the current
preservation baseline merely because the installed firmware is unchanged.
Replacing that hash alone is insufficient: the current workflow boots originals
after admission, during coordinator preflight and before later checks. Original
runtime can legitimately modify storage between these checks.

Implement and host-test a separate preservation interval for each role:

1. Verify fresh exact identity, static protected regions, original application
   and recovery artifacts in ROM. Read and durably bind the current full NVS
   hash to that role, package and attempt; independently reread it before write.
2. Keep original application execution prohibited from baseline acquisition
   through the candidate write. Any intervening original boot, uncertain serial
   lease, changed identity or changed baseline invalidates admission.
3. Compare that same NVS baseline after candidate execution and after exact
   original restoration/readback, before releasing the original application.
4. Record the preservation interval as closed, then boot the verified original.
   Post-release original runtime is a separate observation; it cannot silently
   replace the baseline or retroactively prove candidate preservation.

Tests must exercise actual Session/Backend composition with a simulated original
that changes NVS on boot, candidate NVS mutation, between-read mutation, changed
identity, interrupted baseline persistence and restoration failures. Preserve
static-region checks, one-use authority, sequential A-before-B restoration and
restoration-only recovery. Do not add NVS writes or broad erase capability.

This correction is designed, not implemented or physically accepted. The failed
preflight namespace remains consumed; its manifest must not be edited and its
grant-preparation script must not be run. A corrected caller/package requires a
new reviewed namespace and current authorization before physical execution.

## Evidence and limits

The private OT-236 evidence folder retains the frozen caller/manifest, 18 passing
mock caller tests, failed preflight proof, bounded diagnosis, exact commands and
hash inventory. The [sanitized record](../../tests/benchmarks/crypto/OT-236-PREFLIGHT-2026-09-15.json)
records artifact hashes and operation outcomes without settings or identifiers.
The earlier host suites are unchanged; documentation checks are rerun for this
closeout. No capture was obtained, no firmware was rebuilt and no remote operation
occurred. OT-236 remains local/uncommitted. No V1 milestone or public website
status changed.
