# OT-222 Bounded candidate reset-reason diagnostic

## Scope

Continue the [OT-221 startup finding](OT-221-STARTUP-DIAGNOSTICS-2026-09-13.md): candidate self-check/runtime startup and heartbeats were followed by another startup, but the raw reset reason was not retained. The next discriminating observation uses the unchanged OT-216 candidate on role-A Trail Bench only, for the same 15-second window and independent full-original restoration. The user approved continuation and unlocked the selected SM-N986U for preflight. No app clear, reinstall, bond deletion, new pairing or comparison-board action is included.

## Host implementation and preflight

The maintained capture now emits OT222-STARTUP-1 with a bounded numeric ROM reset_reason (0..255), fixed panic/interrupt-watchdog/task-watchdog categories, a Unix-millisecond capture anchor and monotonically ordered received_ms on every marker. Times describe host receipt, not exact device event time; buffering can delay delivery. Unknown text and raw identifiers remain discarded. Early-log absence stays inconclusive.

Limits remain 15 seconds, 16 KiB, 512 bytes per line and 128 markers, with the existing worker watchdog for blocking OS calls. Fresh USB identity matching and a newly opened DTR/RTS-false handle remain unchanged; no extra serial reset, flush or write is introduced. The engine retains full actual application733184/NVS12288 restoration and independent protected-region readback/reset, including observation failures.

All 70 affected host tests passed. The new operator binding and unchanged firmware/APK hashes were checked, and independent controller review found no stop gate. Historical bindings and consumed grants are preserved. The target is unchanged, so rebuilding and radio-range testing are inapplicable; no LoRa claim or region change is made. Live ROM identity, protected layout and fresh actual recovery captures remain mandatory inside the maintained operator before candidate write.

The physical result and restored phone acceptance are recorded below. No V1 credit or website change follows host preparation; publication remains pending.

## Physical result

One approved attempt completed; originals are restored/read back, protected regions verified and original reset completed. Custody is closed, the fresh grant consumed, and no recovery retry occurred. Capture retained 13 markers from 7098 bytes over 15014 ms. Self-check/runtime startup was followed by heartbeat at628/5678ms, ROM reset_reason12 received8529ms after capture start, then another self-check/runtime startup and heartbeat626/5678ms. [Sanitized physical evidence](../../tests/hardware/OT-222-RESET-REASON-2026-09-14.json).

ESP32-S3 ROM code12 (0x0C) means software CPU reset. It does not identify the precise caller. The pinned IDF6.0.2 `components/esp_rom/esp32s3/include/esp32s3/rom/rtc.h` defines this hardware code; `components/esp_system/port/soc/esp32s3/reset_reason.c` can combine it with retained panic/brownout/watchdog hints. Those hints were not captured. The event sequence supports a later reset after application startup; receipt timing may include buffering.

The restored phone initially remained failed. The previously validated V1-Test-only process restart and normal Find device / Start Bluetooth device service flow preserved app data and bond, then typed session24 recorded AUTHORIZATION_ACCEPTED, SNAPSHOT_ACCEPTED and READY_REACHED. No reinstall, clear, new pairing or comparison-board action occurred. Screen illumination was not independently observed. Existing phone logs are session-relative and expose no wall-clock anchor; exact correlation with the host capture is unavailable.

## Diagnostic defects found and host corrections

Pinned esptool5.3.1 `run` calls `flash_finish(False)` to start the application, after which the previous `--after hard-reset` caused a redundant reset during teardown. The maintained operator now uses `--after no-reset` with `--no-stub`, retaining handle closure and fresh capture reopening. Ordinary ROM operations and restoration checks are unchanged. The redundant reset completes before capture begins and does not directly explain the later software-reset sequence; this correction is not a demonstrated BLE fix.

The SDK's panic formatter uses a padded CPU number (`Core  0`), while the executed parser required one space. Therefore a standard panic line could have been missed. The parser correction accepts the SDK padding and is covered by a source-format regression test. Historical raw text was intentionally discarded and cannot be re-parsed. No inference excluding panic or watchdog is valid from this capture.

Exact executed helper/transport bytes are retained privately beside the original binding before the post-trial corrections. The final affected matrix passed 72 tests (22 engine, 24 transport, 12 operator, 14 capture), plus the repository documentation checker and 17 documentation tests. These corrections have not been physically exercised. No second attempt was made. Next: one corrected diagnostic capture under a fresh exact scope, preserving originals and the current phone/bond; identify a positive panic/watchdog category or targeted reset source before claiming root cause. No V1 or website change. Changes remain local pending publication.

## Correction recorded by OT-223

The above redundant-reset conclusion and proposed no-reset correction were withdrawn after [OT-223](OT-223-CORRECTED-CAPTURE-2026-09-14.md) failed to establish startup with that release path. The accepted hard-reset path was restored and original phone Ready verified. Historical reset0x0C evidence and the padded-panic parser defect remain valid; no firmware root cause was established.
