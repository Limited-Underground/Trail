# OT-223 Corrected capture and release-path regression

## Result

One approved role-A attempt used the unchanged OT-216 candidate, the corrected panic parser and the proposed no-reset release. Capture completed its15007ms window with zero bytes and no markers. This supplies no new firmware reset evidence; it must not be classified as a firmware boot failure. Original application733184/NVS12288 were restored/read back and protected regions verified, but the no-reset release did not establish original application operation. See [sanitized evidence](../../tests/hardware/OT-223-CORRECTED-CAPTURE-2026-09-14.json).

## Correction to the previous diagnosis

The prior claim that esptool's final hard reset was a confirmed redundant reset is withdrawn. It inferred application execution from `run` comments rather than establishing ESP32-S3 behavior. The pinned implementation calls `flash_finish(reboot=False)` and sends FLASH_END payload1. Official protocol descriptions differ across versions; the source call chain did not prove that removing the final reset would preserve startup on this setup. OT-223 did not validate that change.

The accepted `--after hard-reset` release was restored for restart operations; ordinary ROM reads/writes retain `--after no-reset`. The independently justified padded-panic parser fix remains. The final72 affected tests passed; they validate host behavior, not physical candidate boot. Exact executed transport bytes and the original binding are retained privately before reverting the release change. The earlier OT-222 software CPU reset0x0C remains valid evidence with an unknown caller; this failed observation does not explain it.

## Original-only completion

Under the existing approved restoration scope, a separate one-use original-only release used the maintained recovery-only `Transport.reset_original` method. Independent review verified source binding, fresh role-A identity, exact retained captures/protected hashes/original prefix/NVS reset guard, process exclusion and absence of candidate/write calls. All five live original regions were independently read and compared before the accepted hard reset. No flash writes or candidate retry occurred.

The phone then reached AUTHORIZATION_ACCEPTED, SNAPSHOT_ACCEPTED and READY_REACHED in the same session24, with its original app data and bond. No app restart, clear, reinstall or new pairing was needed. Custody is closed; the candidate grant and original-only release are consumed. Trail Bench2 remains untouched. No LoRa operation or region change; screen illumination was not independently observed.

## Remaining gate

Reset cause remains unknown. A later explicitly scoped capture must combine the accepted hard-reset release with the padded-panic parser; do not reuse the consumed OT-223 controller or claim that the reverted no-reset path was accepted. Preserve the same full-original restoration and phone Ready checks. No firmware rebuild, V1 credit or website change. Source/evidence remain local pending publication.
