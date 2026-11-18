# Firmware source, build and recovery

This page describes retained evidence and recipes, not a command to execute. Read the [freeze and current handoff](../PROJECT_STATUS.md#current-recovery-handoff). Use the selected source/candidate, exact dependency versions and operation-specific authority; preserve dirty work. [Development](../DEVELOPMENT.md) and the [Heltec target guide](../../firmware/targets/heltec_v4_bench/README.md) describe the checked-in host/target build surfaces. Older target-guide statements are dated evidence, not current product acceptance.

## Select the source before a recipe

| Source boundary | What it supplies | What it cannot establish |
| --- | --- | --- |
| Repository source | Portable C++ components, ESP-IDF bench targets, host tests and historical contracts. | A clone or dirty canonical tree alone is not the exact successful diagnostic source. |
| Preserved diagnostic source and configuration overlays | Exact source/recipe lineage for the November 2 diagnostic firmware; retained by the recovery owner. | No protected Packet-v1 selection, production acceptance or new physical authority. |
| Isolated Packet-v1 host candidate | Conditional architecture and host slice based on a separately preserved candidate. | No production GATT registration/activation, protected device runtime or equivalence to diagnostic firmware. |

The source owner preserves the declared diagnostic source descriptor, configuration overlay and recipe with the accepted artifact. Do not substitute a historical checkout or copy candidate files into canonical source. If the selected private source cannot be retrieved, report that access gap rather than treating public main as equivalent.

## Declared reconstruction environment

The frozen reconstruction used ESP-IDF **6.0.2**, SDK commit `7101770dc6db2667b3c477cc31365dd1acd6db4e` with 25 pinned submodules, and the Xtensa ESP toolchain `esp-15.2.0_20251204`. It configured the ESP32-S3 Heltec bench target with the diagnostic option enabled, confirmation evaluation disabled, retained sdkconfig/reproducibility defaults and `SOURCE_DATE_EPOCH=1792973531`. Compilation was capped at two parallel jobs; signing and device access were absent.

The preserved recipe binds source, tool/dependency closure, environment, CMake/Ninja/Python versions and command logs. The in-repository [dependency lock](../../firmware/targets/heltec_v4_bench/dependencies.lock) records ESP-IDF 6.0.2; it is not a complete stand-alone offline dependency archive. Reuse installed tools where applicable and identify any changed prerequisite before reconstruction. Do not install a new SDK or bootstrap an OS merely to read these docs.

For the checked-in source, the existing compile-only helper is [Build-HeltecV4BenchTarget.ps1](../../tools/Build-HeltecV4BenchTarget.ps1). From the selected repository root in an already exported ESP-IDF 6.0.2 environment, its documented command is:

```powershell
.\tools\Build-HeltecV4BenchTarget.ps1
```

It checks the ESP32-S3 target/flash/PSRAM/partition configuration and records artifact hashes under ignored build output; its receipt remains `NOT-FLASHED`. A general source-validation recipe is [Test-Host.ps1](../../tools/Test-Host.ps1), explained in the [development guide](../DEVELOPMENT.md#run-deterministic-host-tests). These recipes describe the checked-in surfaces. Reconstructing the accepted diagnostic lineage instead requires its exact preserved source/configuration/recipe inputs; do not claim that running the generic helper reproduces that image. Documentation changes use the focused documentation checks in [AGENTS.md](../../AGENTS.md#documentation-roles), without running this product matrix.

## Frozen result and exact artifact comparison

Independent review outcome: **qualified pass for declared-host firmware source-build and focused-test evidence**, with equality and runtime limits. Receipt identifier `trail-reconstruction-001/receipt.json`, SHA-256 `b0575b69d795513aed746243aee67578d50087f6aedc72b10686cb02a5ad3055`, is retained in private foundation review custody. The reviewer verified 112 compact evidence seals and reused the unchanged full source postcheck; this was not a new whole-machine file-access trace.

Configure/build passed. Four native suites/110 groups, eight SDK fact tests and eight composed scenarios passed. The optional negative control was skipped because its unfixed source was undeclared; it must not be counted as passed. These are synthetic host results, not phone/RF/device observations.

| Artifact | Bytes | SHA-256 |
| --- | --- | --- |
| Rebuilt application | 661,504 | `aae8104a44e5e34be87858f2988b017a9c7451b3edaeaab5cb15c262ea57c673` |
| Rebuilt padded image | 663,552 | `fbc5b44ed3abd3823ca66f5b870d1bfc8c4d06523f8f37a5eba91256d66c56ce` |
| Accepted diagnostic reference image | See retained artifact receipt | `553ece87073b81ee9045b9704e204a52dcf9d931e274e22952b0411735052283` |

Raw padded-image equality **failed**: 164,913 differing bytes, first offset 28 and last offset 661,519. An embedded source-path dependency is confirmed: `ESP_ERROR_CHECK` passes `__FILE__` to the SDK error handler; the sibling radio-adapter path retained its distinct source root despite the bench-root/SDK prefix maps. The later focused review below attributes these image differences without changing the measured inequality or accepted reference.

## Reviewed attribution of the image differences

A focused ELF/image comparison, accepted by independent review on November 3, accounts for every differing byte in these two exact application images. The **4,503 nonzero loaded ELF function symbol ranges** retain identical names, addresses, sizes and range bytes. The differences are the shortened embedded diagnostic source filename, 2,200 corresponding data pointers, segment packaging and derived ELF/image integrity metadata. Changed words in executable sections were classified as literal/data entries using bound disassembly, linker maps and original SDK relocation initializers, including 39 original relocation records and the final relaxed initializer. An independently applied systematic address transformation and image packaging reproduced the retained rebuilt image exactly in memory, with zero unattributed output bytes; no replacement image was written.

This is complete attribution for the compared **image bytes**, not matching raw firmware, a general reproducible-build claim or formal runtime equivalence. The symbol-range result does not establish every executable instruction or reachable behavior; nonloaded/debug ELF differences are not fully attributed. It supplies no new hardware acceptance or permission to substitute the rebuild. The accepted reference remains authoritative and separate. Android's qualified 43 host tests and equality of 145 non-signing APK entries remain separate from signed-APK/signer and phone/device-state restoration.

The bound forensic report and independent review remain with the recovery custodian, unavailable from a public clone. Image integrity fields follow the [official Espressif image format](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/firmware-image-format.html); those corruption checks do not establish authenticated production admission.

## Recovery claims

| Claim | Latest bounded evidence | Remaining limit |
| --- | --- | --- |
| Source/history retrieval | Preserved source, history and exact overlays were retrieved and checked. | Public main alone does not contain every retained overlay/private receipt. |
| Exact accepted binary retrieval | Firmware/APK identities and isolated retrieval were verified by preservation/recovery records. | Artifact custody does not establish source reproducibility or running-device state. |
| Source rebuild | Declared-host firmware configure/build and focused native tests passed. | No clean new-OS bootstrap, external dependency refetch, hermeticity or full file-access trace. |
| Byte equality and difference attribution | Raw firmware equality failed; independent review accounts for every differing byte in the two compared application images. | Accepted and rebuilt images remain separate. Symbol-range equality and exact-image attribution do not establish runtime equivalence or a general reproducible build; nonloaded/debug ELF attribution remains incomplete. |
| Runtime/data restoration | No new device/runtime/NVS restoration was performed in reconstruction. | Live state, original data, power interruption, phone/RF behavior and product acceptance remain separate. |

The forensic transformation explains the retained image comparison; it does not qualify a byte-identical source-build recipe or produce an accepted replacement. Earlier historical-path or stable-prefix recipe options remain proposals, not current execution decisions. Any future equality attempt requires its own authorized isolated recipe and new comparison. Select the live task and applicable owner authority through the maintained handoff before acting; development and hardware remain frozen.
