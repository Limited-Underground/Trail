# OT-197 controlled durable-stage trial

Status: **bounded trial complete with input refusal; A restored and B guarded-released. No policy pass.**

## Scope and exact inputs

This controlled nonradio trial used the [OT-196 stage operator](OT-196-STAGE-OPERATOR-2026-09-11.md) and the unchanged OT-195 diagnostic candidate on the Heltec WiFi LoRa32 V4.2 / ESP32-S3 setup. Both roles received fresh, verified original application and full-NVS backups before the held-custody handoff. Application snapshots covered 589,824 bytes; full NVS covered 12,288 bytes. Protected bootloader, partition and OTA baselines were also checked. Earlier snapshots and consumed grants were not substituted for this custody.

| Input | Exact identity |
| --- | --- |
| Candidate | `ot195-policy-diag-v0`, 438,784 bytes |
| Candidate SHA-256 | `449e3249bf6d92e5bcfa394244bd8b15544ee2f5aa8f0f4eded13321eea06164` |
| Isolated runtime manifest SHA-256 | `37e666d74ee5f8cdb740bd1491abf5be3a5fa2fac14eae5cd72b7d9f2c34f3c4` |
| Solicited wire protocol | `SEC_EVAL1 ot187-policy-v0` |

The [sanitized outcome record](../../tests/hardware/OT-197-STAGE-TRIAL-2026-09-11.json) owns the machine-readable result. This report contains no device identities, command challenge, grants or raw NVS.

## Preflight and validation boundary

Current passive enumeration matched both retained roles. ROM checks verified
ESP32-S3 and 16 MB flash before each admitted operation. The isolated parent and
child probe passed with all 3,557 manifest files verified. Independent rehashing
matched all 14 artifacts in seven build pairs and all 37 source pins from the
accepted build report; no target source or toolchain changed, so those builds
and the OT-196 full host matrix were reused. This increment changes evidence
and documentation only.

The firmware uses application offset `0x10000` with a 589,824-byte padded span,
and NVS offset `0xd000` with a 12,288-byte span. Bootloader, partition and OTA
comparison spans were 32,768, 4,096 and 8,192 bytes. Current original bytes and
namespace absence passed before fresh finite custody and trial admission. The
existing no-stub 115,200-baud transport and exact per-role restore artifacts were
retained. Pre-console NVS initialization/stage writes and one diagnostic capture
were included in the new scope. No earlier grant was reused.

Radio, GNSS, display, phone installation and bond changes were outside this
nonradio diagnostic; their acceptance was not tested. Cold-power disassembly
remained deferred. Repository documentation checks and the 17 documentation tests
pass; protected publication requires the configured GitHub checks.

## Observed result and restoration

A's candidate was verified and booted. The serial transport accepted 63 command bytes, then made 104 reads over 30,000 ms and received zero bytes. Capture ended with `receipt_timeout`; no matching receipt was observed or accepted. Transport acceptance does not prove firmware received or executed the command.

After confirmed serial closure, the authorized pre-restoration full-NVS observation produced a valid `input_result / input_refused` marker. The observation used the separate durable intent and private raw-capture custody path. Its fixed projection is diagnostic evidence, not a policy receipt.

The executor reported `evaluation_failed`, with A's evaluation `capture_failed` and restoration true. A's original application, full NVS and protected regions passed the restoration checks; its journal records `original_booted`. Successful restoration does not change the failed evaluation result. The saved original snapshots become stale after the original application is reset into execution.

B was not flashed and its serial evaluation endpoint was not opened. Its separate guarded original-region readback and reset completed with `reset_nvs_stale`. Both active locks are absent. All issued grants remain consumed; both original snapshots are stale for any later handoff. No retry or separate recovery was needed. Phone UI and OLED state were not independently observed.

## What the marker establishes

The [record contract](../../firmware/components/security_diagnostics/stage_record_v1.json) defines the last structurally valid stored stage/error. It does not prove persistence acknowledgment or that a subsequent operation began. CRC detects corruption; it is not authentication or rollback protection.

In the [actual diagnostic entry point](../../firmware/targets/heltec_v4_security_policy_diag/main/app_main.cpp), `input_refused` records a false return from `receive_control`, then exits before evaluation. A successful receive followed by session-admission failure would instead record `session_refused`.

The [receive loop](../../firmware/targets/heltec_v4_security_policy_eval/main/policy_control_loop.hpp) and [parser](../../firmware/components/security_evaluation/include/opentrail/evaluation_control.hpp) can refuse because of the initial 60-second idle deadline, the five-second assembly deadline after the first byte, clock regression, malformed or overlong input, loop exhaustion, or console health loss. The stored marker does not distinguish these causes.

The retained build configuration has `CONFIG_FREERTOS_HZ=100`; its SHA-256 is `1a77d64cab633b862c4a8ffa5493321e6ac039ae3edb94a99fda0a332c6d66d1`, matching the [accepted build report](../../tests/benchmarks/crypto/OT-195-DURABLE-STAGE-BUILD-2026-09-11.json). The loop uses `vTaskDelay(1)`: 7,000 one-tick pauses are nominally 70 seconds, subject to tick alignment and scheduling. During ordinary empty-input waiting, the explicit 60-second deadline should therefore fire before that loop cap.

The [console implementation](../../firmware/targets/heltec_v4_security_policy_eval/main/policy_console.cpp) returns false on empty RX without latching a fault. The pinned SDK's low-level RX function returns a byte count of zero or one for this call, not minus one. Console health means the ownership phase remains `owned`; it does not establish that the host is connected or input is available.

The device begins its input window when it constructs the parser. The host begins its 30-second receipt deadline only after candidate reset returns and the serial endpoint opens. These clocks have no demonstrated shared boot-to-command bound. Child validation occurs before the intended hard reset, so total ROM-child overhead alone does not prove the input window expired. Neither a timeout cause nor a USB/input fault is established by this trial.

## Next consolidated gate

Prepare an additive diagnostic successor with fixed refusal reasons and bounded timing/count observations that distinguish idle expiry, assembly expiry, parser refusal, loop exhaustion and console fault, including whether any input byte was seen. Pair this with host boot-to-command timing. Keep command contents and raw input private, preserve strict challenge/framing rules, and avoid selecting a longer timeout without evidence.

Validate the actual parser/console/application composition for those cases, build and pin the affected target, and verify the operator/runtime package before proposing another fresh controlled trial. Preserve independent original restoration and untouched-role release. This result establishes no policy pass, physical security acceptance, V1 completion increase or public website capability change.
