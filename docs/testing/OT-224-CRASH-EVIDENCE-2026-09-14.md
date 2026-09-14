# OT-224 Actionable firmware crash evidence

## Diagnostic boundary

[OT-222](OT-222-RESET-REASON-2026-09-14.md) established software CPU reset 0x0C but not its caller. [OT-223](OT-223-CORRECTED-CAPTURE-2026-09-14.md) rejected the no-reset host change and recovered original Ready through the proven hard-reset release. This increment retains that accepted release and unchanged candidate. It addresses collector information loss before spending another physical attempt.

The firmware-fault-diagnosis skill was applied with the OpenTrail bench/connection procedures. Expected outcomes: a panic/task/backtrace can identify a source path through the exact ELF; a reset without those markers leaves explicit application restart paths unresolved; silent/failed capture remains inconclusive. No negative marker proves absence unless the relevant emitter/collector coverage is established.

## Collector and host validation

The OT224 schema adds bounded fixed panic reasons, known task identities, executable program counters, at most 20 backtrace instruction addresses and numeric exception cause. Stack pointers, general register values, arbitrary task names, private identifiers and raw lines are discarded. Exact SDK format composition covers padded cores, register columns, ANSI, CRLF, fragmented reads and truncation. Long backtraces retain complete bounded PCs and explicit truncation rather than silently losing all crash addresses.

The existing 15-second / 16 KiB / 512-byte-line / 128-marker limits, fresh USB identity/handle lifecycle, DTR/RTS policy, isolation and worker watchdog remain. Root review checked actual SDK register/backtrace emitters as well as agent tests. All 76 affected tests passed. New source binding and actual isolated runtime probe passed without hardware access; Windows command length fits the platform limit.

The exact OT-216 ELF SHA256 cdfd453df87fb19b1b21048fc532d59eb7303ec79c3cbd03598ffad6a323f00f is embedded in the unchanged firmware at 0xb0. The retained build command explicitly generates that image from the ELF. GNU addr2line 2.45 successfully mapped a known executable address; decoding captured PCs therefore requires no firmware rebuild. Receipt times remain collection times; the phone log lacks a wall-clock anchor, so only its session-relative ordering can be claimed.

## Physical scope and prerequisites

One role-A Trail Bench Heltec WiFi LoRa32 V4.2 / ESP32-S3 / 16 MB candidate attempt, unchanged OT-216 firmware/APK, no Group/confirmation action. Same SM-N986U/SDK33 app/data/bond; comparison Trail Bench 2 untouched. Fresh exact identity, protected layout and actual full application (733184 bytes) and NVS (12288 bytes) recovery captures are required before write. Maintain independent restoration/readback of all regions, accepted original hard reset and protected Ready verification. No LoRa operation or region change; new target build is inapplicable because firmware is unchanged. No app clear/reinstall/bond deletion or cold-power procedure.

No V1 credit or website status changed. The completed physical result follows; publication remains pending.

## Completed physical result

One explicitly approved attempt completed on 2026-09-14 with the unchanged candidate and accepted hard-reset release. The 15014 ms capture received 7101 bytes and retained 15 sanitized markers. Initial self-check and runtime startup passed; three heartbeats preceded an explicit stack-overflow report naming `ot_ble_host` at receipt time 11803 ms. A backtrace followed at 11804 ms, then software CPU reset reason 12 at 11812 ms and another startup sequence. Receipt timing may lag the device event because of buffering.

The failure mechanism is confirmed: the NimBLE host task overflowed its stack. Symbolization against the exact matched ELF identifies `vApplicationStackOverflowHook` and `vTaskSwitchContext`, followed by the abort path. The backtrace is explicitly corrupted; its `ble_hs_unlock` frame does not establish the exact operation or call chain that exhausted the stack. Available app-task stack headroom does not describe the separate BLE host task.

Full original application and NVS restoration/readback, protected-region verification and accepted original reset completed. The journal is closed and the custody lock is absent. No restoration retry, app installation, app-data clear, bond deletion, firmware rebuild, Group action or comparison-board mutation occurred. See the [sanitized hardware result](../../tests/hardware/OT-224-CRASH-EVIDENCE-2026-09-14.json).

A stack correction and its physical acceptance remain open. This finding does not establish resolved BLE behavior and does not increase V1 completion.

### Restored phone acceptance

The first phone observation after original restoration was FAILED. A V1-Test process restart preserved app data and bond; the normal Find device / Start Bluetooth device service flow then restored the connection. Actual typed session 25 recorded AUTHORIZATION_ACCEPTED at 38103 ms, followed by SNAPSHOT_ACCEPTED and READY_REACHED at 38300 ms. These session-relative times do not provide a wall-clock correlation with the candidate capture. No app-data clear or bond change was used.
