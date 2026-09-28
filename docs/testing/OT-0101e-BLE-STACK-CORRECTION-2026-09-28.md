# OT-0101e BLE host stack correction

## Result and evidence boundary

VERIFIED on 2026-09-28: the ordinary and connection-diagnostic configurations now allocate 8192 bytes to `ot_ble_host`, up from 4096. Two fresh diagnostic A builds match all seven authoritative artifacts, and a separate ordinary build succeeds. This is a host-validated candidate correction. No corrected image was installed or physically exercised in this increment.

The saved diagnostic A capture retained an explicit FreeRTOS stack-overflow marker naming `ot_ble_host`, followed by software reset and a panic reset reason. The phone concurrently reported the protected ProtocolInfo failure. The [physical evidence](../../tests/hardware/OT-0101e-2026-09-25.md) owns that capture and its restoration record.

The capture establishes the fatal category for that instrumented attempt. It does not establish the exact overflowing call chain or prove that the earlier uninstrumented failure had the same cause. Diagnostic recording adds finite stack use and changes timing. The collector received one eight-event drain batch before the fault; later queued events could have been lost. GAP event 34 is the pinned SDK's data-length-change event, so its retained entry alone is not a ProtocolInfo crash-site identification.

## Narrow correction

The shared target defaults explicitly set `CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=8192`. The ordinary sdkconfig seed changes only that setting and its two legacy IDF aliases. The post-project CMake admission check rejects absent, malformed, or smaller settings, including stale sdkconfig files that override defaults during resumed builds. The exact new builds use 8192; the admission floor also permits a separately chosen larger budget.

BLE task priority 21 and core 0, ordinary main-task stack 8192, security policy, callback code, acquisition, and sensor display remain unchanged. Diagnostic A keeps diagnostics and sensor display enabled. Ordinary keeps diagnostics disabled and sensor display enabled. No new B build or physical attempt is introduced. The fixed `PROJECT_VER=ot0101e-conn-diag-v1` avoids changing another candidate input.

[OT-225](OT-225-BLE-STACK-CORRECTION-2026-09-14.md) introduced 8192 for the separate evaluation profile; [OT-226](OT-226-POST-CONFIRMATION-STACK-2026-09-14.md) accepted its bounded reconnect/local-confirmation case with retained post-confirmation headroom. This correction copies only the host-stack allocation. It does not adopt the evaluation profile's 24576-byte main stack or its other overrides.

## Build and validation evidence

Toolchain: ESP-IDF v6.0.2, commit `7101770dc6db2667b3c477cc31365dd1acd6db4e`; Xtensa GCC 15.2.0 / `esp-15.2.0_20251204`; CMake 4.0.3; Ninja 1.12.1. Dependency manager and compiler cache were disabled. The maintained diagnostic launcher was adapted into the new private `.private/build-connection-stack-v2.ps1`, which admits only new version 2 output names and verifies 8192 in both seed and generated header.

| Build | Application bytes | Application SHA-256 |
| --- | ---: | --- |
| `build/ot0101e-stack-v2-a1` | 593200 | `56a1737e5fcdcca0514aba7f615b9934efc0f025d656b393b29633c10ec808b1` |
| `build/ot0101e-stack-v2-a2` | 593200 | `56a1737e5fcdcca0514aba7f615b9934efc0f025d656b393b29633c10ec808b1` |
| `build/ot0101e-stack-v2-ordinary` | 591536 | `a83ebe8e6fc1c43540085c33385641dca3e636dbc2461f6bc76b043257496f96` |

The A1/A2 application ELF SHA-256 is `c39105628c45219f07d1c722589fad89dd6bbcabdae84f3458b6b4599eb5e01e`. Both directories match byte-for-byte for application BIN/ELF/MAP, bootloader BIN, partition-table BIN, sdkconfig, and generated sdkconfig header. Each application is within the retained 733184-byte application restoration span.

The diagnostic map, bootloader, and partition-table hashes match retained diagnostic A. Of 48 compiled application objects, only `companion_nimble_runtime.cpp.obj` changes; its task-creation allocation argument changes while the surrounding implementation is unchanged. All generated configuration values and header definitions compare equal to retained A except the canonical BLE stack value and the corresponding two sdkconfig aliases.

- 17 existing Heltec V4 target-admission groups pass.
- 7 new configuration tests pass, including execution of the actual CMake guard with missing, malformed, 4096, and 8191 inputs; 8192 and 16384 acceptance; retained 4096 rejection; and actual generated A1/A2/ordinary config/header checks.
- The suite is registered in `tools/Test-Host.ps1`. Its normal source-only run has 4 active cases; 3 private artifact-audit cases are explicitly enabled for this final gate. It discovers an explicit `OPENTRAIL_CMAKE`, then PATH, then the existing pinned user-local CMake installation. The final 7 cases passed in a fresh user shell without the explicit CMake override or build PATH changes.
- Existing diagnostic, runtime, collector/operator, native custody, and Android results are reused because their implementation and frozen inputs are unchanged by this correction. No unaffected full matrix was rerun.

The retained evaluation build is reused within its existing compile-evidence boundary. It already selects host 8192/main 24576, its evaluation defaults and all previously pinned firmware implementation files are unchanged, and the added admission predicate accepts its existing value without adding flags or source. Adding a shared 8192 default cannot change that profile's identical 8192 override. This does not extend its historical physical acceptance to the new ordinary candidate.

All 70 recorded retained artifact and v3 binding-input descriptors still match. Prior A/B/ordinary/GNSS/evaluation outputs, v3 runner/engine/grant schemas and candidate constants, frozen trial plan, registry, and APK remain intact. No grant was issued or altered.

One pre-existing ignored 216-byte IDF-only `dependencies.lock` in the target directory caused the exact file-surface admission test to refuse. Its bytes and hash were preserved by an exact move into private version 2 evidence; the original path and reason are recorded. The production allowlist was not widened. Initial sandbox configure attempts could not discover the installed Ninja/toolchain and ended before compilation; the same maintained commands succeeded in the user host context. Those failed configure logs remain retained.

Private preflight, command/build logs, all artifact/source pins, the generated-file preservation record, and the final audit are under `.private/ot0101e-stack-v2-*`. The detailed audit is `.private/ot0101e-stack-v2-build-audit.json`; an independent seven-artifact audit is `.private/connection-stack-v2-final-audit.json`. Each new build also contains `diagnostic-build.json` with its exact command arguments and seven artifact descriptors.

## Porting preflight and remaining gate

| Applicable preflight | Result or reason for deferral |
| --- | --- |
| Target and optional services | Retained candidate Heltec WiFi LoRa32 V4.2 / ESP32-S3, 16 MB QIO 80 MHz flash and Quad 80 MHz PSRAM settings; no pin, region, sensor, or LoRa change. |
| Clean reproducibility | Two distinct fresh A compilation directories; seven identical artifact pairs; separate ordinary compilation. |
| USB, reset, and serial custody | No physical action in this increment. Retained operator/custody and restoration evidence remain unchanged; their next physical use requires a fresh exact artifact binding. |
| Callback concurrency and resources | Task core/priority and callback/lifecycle implementation unchanged. Only the allocated host stack grows. |
| Persistence and cleanup | Storage, bonds, authorization, partition layout, and restoration engine unchanged; all frozen inputs preserved. |
| Composed validation | Affected target admission, executable CMake negative controls, exact generated-config comparison, object/map review, and reproducible builds pass. Unchanged suites reused. |
| Physical acceptance | Deferred. Corrected startup, protected Ready, sustained clock, post-connection stack headroom, and exact restoration remain unmeasured for this new image. |

The larger dynamic task stack requests 4096 additional bytes from ESP-IDF's internal 8-bit-accessible FreeRTOS heap. The unchanged task-creation path returns failure if allocation fails. Static image/map equality does not measure available runtime heap, heap fragmentation, or worst-case stack demand. 8192 is supported by the earlier evaluation precedent, but sufficiency for this ordinary/GNSS flow must still be measured.

No new hardware execution, retry, publication, V1 completion change, or public website update is authorized or claimed by this record.
