# OT-226 Post-confirmation Bluetooth stack measurement

## Result and scope

The unchanged OT-225 evaluation firmware completed protected Ready, the bounded local synthetic confirmation flow, and the previously missing passive post-confirmation stack measurement. The operator accepted `local_confirmed`; the complete original application and NVS were restored and independently read back, protected regions verified, custody closed, and original firmware reached protected Ready on the same phone session. This closes the narrow stack-correction acceptance gate for this exercised flow.

The [OT-225 attempt](OT-225-BLE-STACK-CORRECTION-2026-09-14.md) remains a historical operator timeout with no post-confirmation capture. OT-226 is a separate, freshly authorized single attempt. Its only executed source change from OT-225 is the host's post-Ready reporting allowance from 60 to 180 seconds and an explanatory comment. The device's 60-second confirmation expiry, firmware and Android APK are unchanged.

## Exact setup and provenance

- Hardware: role-A Trail Bench, Heltec WiFi LoRa32 V4.2 / ESP32-S3 / 16 MB.
- Phone: SM-N986U, Android 13 / SDK 33, existing V1-Test data and bond preserved. No reinstall, app clear, new pairing or app process restart.
- Image: `build/ot216-ble-ot225-final-a/opentrail_heltec_v4_bench.bin`, 730928 bytes; SHA-256 `444591760db347c9ce395287ce0cad315d6f2fe736e633170e2c1064b58da3d6`.
- ELF SHA-256: `a2c469e6bcbb459d80339f28c2cf7a4d59935217cae722a2a8a4a0668a9b6959`.
- APK SHA-256: `9f91a3f361221d19089e685b492b5e772e5cf7d658298ff955c4feb2508694a2`.
- Operator binding SHA-256: `28ed44a639ee824ca78db28599cdd2bc17dff71b5567b0e3393b3aeff4a746e5`; all nine executed source pins matched before and after execution. Frozen runtime manifest and ADB pins matched preflight.
- Application offset `0x10000`, complete padded span 733184 bytes; original NVS offset `0xd000`, span 12288 bytes. Original captures cover the actual complete regions. Bootloader, partition and OTA data were verified without candidate writes.

The unchanged [OT-225 build audit](../../tests/benchmarks/crypto/OT-225-BLE-STACK-BUILD-2026-09-14.json) supplies the reproducible A/B artifacts, exact source closure, evaluation-only 8192-byte NimBLE host stack configuration and ordinary-build isolation. The reporting correction passed 17 operator and 29 transport tests. No firmware rebuild was needed for this unchanged artifact. Execution retained the physically accepted hard-reset release and a fresh one-use grant; no candidate retry or recovery attempt occurred.

## Observed sequence

The [sanitized hardware record](../../tests/hardware/OT-226-POST-CONFIRMATION-STACK-2026-09-14.json) contains the complete categorical and numeric captures.

| Window | Duration | Bytes read | BLE host minimum-free-stack samples |
| --- | ---: | ---: | --- |
| Startup | 15003 ms | 2840 | 6060, 6060, 6060 bytes |
| After local confirmation | 15015 ms | 504 | 3980, 3980, 3980 bytes |

Startup retained self-check PASS and runtime-started markers. The candidate reached protected Ready in phone session 25 at elapsed 3655919 ms, following authorization acceptance at 3655675 ms and snapshot acceptance at 3655919 ms. The actual Group UI then displayed "Your device confirmed this step. Group joining is not complete." The operator received the categorical `local_confirmed` observation within its reporting window.

The post-confirmation collector opened a fresh uniquely matched passive serial handle and completed before restoration. It did not enter ROM, reset, or write during this window. Three continuing heartbeats accompanied the 3980-byte host-stack samples. These samples report the task's minimum free stack recorded by the stack-watermark mechanism; they are not an exhaustive proof for every possible call path. No overflow, panic or reset marker was retained in either window. Early output may be missing, and serial output was not continuously captured between the two windows.

The closed journal records verified application and NVS restoration followed by the original reset. Protected regions were independently checked and the active custody lock was absent after closure. Original firmware automatically reached protected Ready in the same phone session 25 at elapsed 4213796 ms, with authorization acceptance at 4213552 ms and snapshot acceptance at 4213796 ms. App data and bond remained intact.

## Acceptance boundary

The 8192-byte evaluation host-stack correction is validated for this bounded protected reconnect and local synthetic confirmation flow, including post-confirmation headroom and full original restoration. This does not establish remote group membership, two-node radio behavior, long-duration stability or all-path stack sufficiency. Comparison Trail Bench 2 was untouched; no LoRa operation or region change occurred. V1 completion and public website status are unchanged.

Private approval, exact binding, controller, phone observations, captures and closed custody evidence remain under `.private/ot226-final-stack` and the private custody records. This report introduces no further physical authority. Publication is tracked separately.
