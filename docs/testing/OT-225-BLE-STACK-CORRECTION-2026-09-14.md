# OT-225 Bluetooth task stack correction

## Correction and evidence boundary

[OT-224](OT-224-CRASH-EVIDENCE-2026-09-14.md) captured a stack overflow in `ot_ble_host` immediately before software CPU reset. The exact ELF resolves the FreeRTOS overflow hook. Its corrupted backtrace does not identify the complete overflowing call chain.

The evaluation configuration now reserves 8192 bytes for the NimBLE host task, up from 4096. The separate main task remains at 24576 bytes. A small owner-guarded observer samples the exact live host task and emits `ble_host_stack minimum_free_bytes` from the existing application heartbeat. It rejects null, current-task and non-owner handles and is invalidated before deletion. The suspended host task remains allocated until owner cleanup. The observer and logging compile only in the existing opt-in evaluation profile. Security, authority, storage and callback behavior remain unchanged.

The ordinary control retains a 4096-byte host stack and has no new observer or diagnostic marker. The Android source and installed OT-216 V1-Test APK are unchanged. The 8192-byte allocation is a candidate correction; physical sufficiency is not inferred from compilation or individual stack frames.

## Validation

- Actual observer: 11 behavioral groups and 4 target-wiring checks.
- Affected target admission: 17 groups; the new header was added to the exact file allowlist after its initial expected refusal.
- Actual runtime fault guard: 13 groups; pinned ESP-IDF NimBLE ordering check passes.
- Final operator matrix: 84 tests across custody (22), transport (29), operator (14) and collector (19).
- Two fresh evaluation builds match all seven artifact pairs. An ordinary control build passes. The [build audit](../../tests/benchmarks/crypto/OT-225-BLE-STACK-BUILD-2026-09-14.json) verifies the actual source closure, 731 library files, generated stack/security configuration, linked observer isolation, ELF digest embedded in the image and executable address ranges. All 21 prior OT-216 build artifacts remain intact.
- Toolchain: ESP-IDF v6.0.2, Xtensa GCC 15.2.0 / esp-15.2.0_20251204, CMake 4.0.3, Ninja 1.12.1, dependency manager and compiler cache disabled. Initial sandbox attempts could not discover the installed compiler and were excluded before compilation; the unchanged maintained launcher succeeded in the host context.

The new `--confirmation-diagnostics` operator mode captures startup, performs the existing Ready / offer / local-confirmed observation, then captures another passive window before restoration. The final window uses a fresh uniquely matched serial handle without entering ROM, resetting or writing. Tests exercise the actual engine, transport and observer composition, including restoration after a capture failure. Historical startup-only behavior remains available.

## Exact executed physical case

One role-A Trail Bench, Heltec WiFi LoRa32 V4.2 / ESP32-S3 / 16 MB. Use the same SM-N986U / Android 13 / SDK 33 and existing app data and bond. Comparison Trail Bench 2 remains untouched. No LoRa operation, region change, factory reset, app clear, reinstall or new pairing is included.

- Image: `build/ot216-ble-ot225-final-a/opentrail_heltec_v4_bench.bin`, 730928 bytes.
- Image SHA-256: `444591760db347c9ce395287ce0cad315d6f2fe736e633170e2c1064b58da3d6`.
- ELF SHA-256: `a2c469e6bcbb459d80339f28c2cf7a4d59935217cae722a2a8a4a0668a9b6959`.
- Application offset `0x10000`; padded span 733184 bytes. The existing exact original application capture still covers the complete write, including its actual tail.
- Full original NVS: 12288 bytes at `0xd000`. Bootloader, partition and OTA data are read and verified, never candidate-written.
- Operator binding SHA-256: `c2811868d12d03df894dbf8a43b02d48cec7c582f92779a7d1c2edff89890a50`.

The approved single-device attempt used fresh device identity, installed APK, original full-region capture and protected-layout checks, then the accepted hard-reset release. The complete result is recorded in the [physical evidence](../../tests/hardware/OT-225-BLE-STACK-2026-09-14.json).

The startup collector completed 15011 ms and read 2840 bytes. It retained self-check PASS, runtime started and three BLE host minimum-free-stack samples of 5916 bytes. No overflow, panic or reset marker was retained in this window. These are bounded observations, and early output may be missing.

The candidate reached protected Ready in phone session 25 at elapsed 2265696 ms, with typed authorization and snapshot acceptance. The actual Group UI subsequently showed "Your device confirmed this step. Group joining is not complete." This establishes the observed local confirmation, not remote membership or radio operation.

The operator result is still `timeout`: the terminal UI was observed, but its host attestation arrived after the reporting deadline. The passive post-confirmation capture therefore did not run and its result is null. Post-confirmation BLE stack headroom remains unknown, so the full combined acceptance case did not pass. The phone observation and the operator timeout are retained as separate facts.

The full original application and NVS were restored and independently read back; protected regions were verified, the original hard reset completed and custody closed. Original firmware then reached protected Ready in session 25 at elapsed 2727479 ms, with authorization and snapshot acceptance. No app process restart, app data clear or bond change was needed. There was one candidate attempt and no automatic retry.

The host reporting allowance after Ready was subsequently corrected from 60 to 180 seconds; the device's existing 60-second confirmation expiry, firmware and APK are unchanged. The focused operator and transport suites passed 17 and 29 tests. The exact executed operator sources were archived and hash-verified before this correction; the historical timeout result has not been rewritten.

## Disposition at the OT-225 checkpoint

Implementation, affected host validation and reproducible firmware builds are complete. The physical attempt establishes startup headroom and observed protected Ready and local confirmation. Post-confirmation headroom remains an open acceptance gate because the host reporting timeout prevented its capture. Original firmware and protected Ready are restored and custody is closed.

Full physical correction acceptance and publication remain pending. No V1 completion or public website status changed. Porting preflight, exact commands, source pins, build logs, reviews, the executed binding and closed custody journal are retained in `.private/ot225-ble-stack` and the active worktree's private custody records. No new grant or retry is authorized by this record.

## Follow-up acceptance

[OT-226](OT-226-POST-CONFIRMATION-STACK-2026-09-14.md) subsequently completed the same-image combined case and retained 3980 bytes of post-confirmation BLE-task headroom. The OT-225 timeout and missing capture above remain unchanged historical evidence.
