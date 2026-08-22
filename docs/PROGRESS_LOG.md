# OpenTrail Progress Log

Progress is grouped by calendar day, newest first. Detailed acceptance criteria
remain in [the engineering backlog](../tasks/BACKLOG.md); this log is the concise
public chronology.

## 2026-08-22

### OT-117 complete libsodium API/configuration admission

- Accepted exact host-only `OTCAPIOE0/v0`, `OTCAPI0/v2`, and append-only `OTLAPIA0/v0` evidence for Espressif libsodium 1.0.22 under the frozen OT-108 and OT-116 boundaries.
- Two fresh configuration-only runs reproduce the candidate-specific OT-107 sdkconfig. All eight fixed operations are structurally eligible, including the separately hash-bound benchmark-only `OTNXK0/v0` Noise XK composition; its strict C11/C++20 fixture passes 4/4 groups, and the admission suite passes 10/10 adversarial groups.
- Counts advance to `3/2/0`. Libsodium is structurally selection eligible but is not selected and execution remains unauthorized. Phase 0 remains incomplete pending Monocypher API/configuration and the second node's exact profile; all retained import/build admissions and fresh benchmark authority remain absent.
- No retained candidate import/build, benchmark, device, flash, radio, production key/entropy operation, suite/wire selection, secure-LoRa implementation, physical evidence, or score credit is added. See [Decision 0055](decisions/0055-host-only-libsodium-api-configuration-admission.md) and [OT-117 evidence](../tests/hardware/OT-117-2026-08-22.md).

### OT-116 successor crypto readiness review and phased benchmark plan

- Accepted append-only `OTCBR1/v0`: all six historical OT-094 requirements have exact accepted closure evidence, while benchmark measurement remains blocked rather than being inferred ready.
- Froze immutable `OTCBX1/v1` as a phased, fail-closed procedure: candidate-specific API/configuration admission, retained import/build admission, then exact-target measurement only under fresh separate authority.
- Counts remain `3/1/0`. The current mbedTLS/PSA five-of-eight evidence is comparison-only and structurally nonselectable; libsodium and Monocypher still need candidate-specific evidence, and every executable candidate still needs accepted import/build evidence, and the second measurement node still needs exact-profile admission.
- Host validation covers parent pins, candidate/configuration/radio invariants, partial-comparison boundaries, authority and claim denial, privacy, parser limits, and deterministic plan semantics. No candidate was imported or built, no hardware or radio was used, no cryptography was selected, and no score changed. See [Decision 0054](decisions/0054-successor-crypto-benchmark-readiness-and-execution-plan.md) and [OT-116 evidence](../tests/hardware/OT-116-2026-08-22.md).

## 2026-08-21
### OT-115 two-device compact-footer install and acceptance

- Added content-aware view ownership so a footer pixel change redraws under an unchanged BLE phase while an identical complete view remains suppressed; invalid footer frames fail closed.
- The pinned ESP-IDF 6.0.2 build produced a 473,152-byte application with SHA-256 `0C40AEB6C95ADE9940AA21065CBC73A72DCD82E96ADE9D13126693147FEB5741` and 91% of the smallest application slot free.
- Automatic full-image write, hash verification, and hard reset passed on both Heltec test units. Both emitted bounded OpenTrail heartbeats. Owner-provided views show both OLEDs lit, and a clear close view accepts `BAT:--% GPS:-- BLE:A` with no traffic arrow; the views were not copied into the repository.
- BLE advertising is the only live footer field accepted here. Battery, GNSS satellites, and LoRa activity remain unbound placeholders. OT-114 remains historical radio evidence; no support, regulatory, production, end-to-end, or score claim changes. See [OT-115 evidence](../tests/hardware/OT-115-2026-08-21.md).
### OT-114 US915 direct-radio profile evidence admission

- Accepted strict `OTRPE1/v1` physical evidence and append-only `OTRPA1/v1` admission under the corrected OT-113 executable successor contract.
- The exact two-node close-bench run reconciles 242/242 probe/data frames and 240/240 acknowledgements with zero loss, duplication, corruption, or unexpected traffic; per-frame DATA/ACK hashes, RSSI/SNR, device-monotonic RTT, 220 exact 2,318 ms bounds for 163-byte DATA, 20 exact 2,452 ms bounds for 255-byte DATA, local 256-byte no-transmit rejection, and post-restart profile retention all pass.
- The final 216,112-byte image passes the ESP-IDF 6.0.2 build and nine focused source groups. Strict validators, independent reconstruction, eight OT-113 adversarial groups, and six OT-114 adversarial groups pass.
- This closes only `direct_radio_mtu_phy_region_unresolved`. Counts remain `3/1/0`; readiness and progress values do not advance pending a successor readiness decision and new executable benchmark plan. No EIRP, regulatory, range, support, compatibility, secure-LoRa, Packet V1, production, benchmark, or selection claim is added. See [Decision 0053](decisions/0053-admit-us915-direct-radio-profile-evidence-v1.md) and [OT-114 evidence](../tests/hardware/OT-114-2026-08-21.md).

### OT-113 executable US915 direct-radio profile contract

- Froze corrected strict `OTRPX1/v1`, replacing only OT-110 execution preconditions that were unsuitable for disposable test nodes while preserving all historical evidence and claim boundaries.
- Added explicit session lifecycle, exact DATA/ACK receipt reconciliation, device-monotonic RTT, fixed timeout components, bounded recovery through the ROM bootloader plus committed source/image, and privacy-safe two-node execution requirements. See [Decision 0052](decisions/0052-host-only-us915-direct-radio-profile-execution-contract-v1.md) and [OT-113 evidence](../tests/hardware/OT-113-2026-08-21.md).

### OT-112 identical-node Heltec V4 direct-radio diagnostic

- Corrected the live-boot SPI lifecycle defect by binding RadioLib HAL `init()`/`term()` to the target SPI begin/end operations; the full ESP-IDF build and 10 focused source groups pass.
- Flashed the same 210,816-byte image (`A50B6F19C728CC8FE40256CF5786385BDA0F34AF35EA3BD65326A03CFF7D3E6D`) to both V4.2 bench nodes. Both booted receive-only with `ready=yes`, `armed=no`, and the fixed 915 MHz profile.
- Controlled OTD1 frames passed A→B and B→A at 17 total wire bytes and exactly 163 total wire bytes; each node ended at `rx=2`, `tx=2`, `armed=no`.
- This closes the bounded OT-112 diagnostic, not the OT-110 acceptance contract. Stress, measured ceiling, rejection, latency, RSSI/SNR recording, restart persistence, RF output/EIRP, regulatory acceptance, compatibility, and support remain open; readiness and progress values do not change. See [OT-112 evidence](../tests/hardware/OT-112-2026-08-21.md).

### OT-110 US915 direct-radio profile evidence contract

- Accepted strict host-only `OTRPF0/v0`, canonical SHA-256 `d5b44cea761b12ad6422be250bf0a827469441643d6f5e944932a91cc92b68d9` / raw SHA-256 `8af36e000d5cd0478d1a829fb5a1f2b330cdf09bad188445d30579c348f7e2e1`, as the contract for future evidence resolving the US915 direct-radio region/MTU/full-PHY requirement.
- Froze a two-node evidence sequence covering exact per-node firmware/configuration/radio-adapter/antenna/profile readback, successor recovery evidence, bidirectional 1-byte, 163-byte protocol-test, measured-ceiling, 256-byte local-rejection, restart-persistence, packet-integrity, latency, RSSI/SNR, close-bench, timeout, and airtime evidence. The peer identity must be resolved independently before execution.
- All profile values remain unmeasured. A privacy-safe read-only USB preflight observed two connected ESP32 candidates; the owner confirmed the generic candidate is the other Heltec and both antennas are attached. No device state change, firmware build, flash, radio transmission, physical radio evidence, regulatory claim, benchmark authority, or selection authority was added. Fresh authority and regulatory preflight remain mandatory before future hardware work.
- `direct_radio_mtu_phy_region_unresolved` remains the sole blocker; counts stay `3/1/0`; readiness remains blocked; and every progress value remains unchanged. See [Decision 0051](decisions/0051-host-only-us915-direct-radio-profile-evidence-contract.md), the [contract](../tests/benchmarks/crypto/OT-110-OT005-US915-DIRECT-RADIO-PROFILE-EVIDENCE-CONTRACT-V0.json), and [OT-110 evidence](../tests/hardware/OT-110-2026-08-21.md).

### OT-109 mbedTLS/PSA API/configuration evidence admission

- Accepted exact generated host-only `OTCAPIOE0/v0` operation evidence, canonical SHA-256 `6a17a6f5a753a19d2d78d7cb6f0c757ef9791e0bf2e953e27afc3eccb04f27ed` / raw SHA-256 `ea85f548deee36ca34241747cdf567036febfb9eecd88d9e134d3383edf2379e`, and `OTCAPI0/v2` candidate evidence, canonical SHA-256 `22975ac7fbd3c9faab1ae0c9fa952a58dc4a7a893de3cc74604182b3492fe1f8` / raw SHA-256 `67532e10704d02489b72a72ef55607743c00a5bd8504276750931b5d986f6155`.
- Two fresh, initially absent, component-manager-disabled configuration-only runs reproduced the exact 106,921-byte OT-107 mbedTLS/PSA sdkconfig SHA-256 `9fc68f61f2fd5ce5f277c3050bdb33e520038349100d60ac142df9fe37d91686`. No candidate source was copied or compiled.
- Accepted append-only `OTCAPIA0/v0`, canonical SHA-256 `fed7b009a97a60678b2dfbba23d933b974aa0fec46d0b460ac7eb221e91931dd` / raw SHA-256 `0311b8b264d264d4a005ac9be8531c06175521362ed54ac9f40bdef1fd7a5df0`. X25519, SHA-256, HKDF-SHA256, and ChaCha20-Poly1305 encrypt/decrypt are admitted for comparison measurement; Ed25519 sign/verify and Noise XK remain unavailable. Five-of-eight partial coverage is structurally nonselectable.
- Counts become `3/1/0`; the mbedTLS/PSA API/configuration requirement closes, while `direct_radio_mtu_phy_region_unresolved` remains the sole readiness blocker. Readiness remains blocked, no execution or selection is authorized, and all progress values remain unchanged. See [Decision 0050](decisions/0050-host-only-mbedtls-psa-api-configuration-admission.md), the [operation bundle](../tests/benchmarks/crypto/OT-109-OT005-MBEDTLS-PSA-API-CONFIG-OPERATION-EVIDENCE-V0.json), [candidate evidence](../tests/benchmarks/crypto/OT-109-OT005-MBEDTLS-PSA-API-CONFIG-EVIDENCE-V2.json), [admission](../tests/benchmarks/crypto/OT-109-OT005-MBEDTLS-PSA-API-CONFIG-ADMISSION-DELTA-V0.json), and [OT-109 evidence](../tests/hardware/OT-109-2026-08-21.md).
- Nine focused adversarial groups, exact raw-byte tamper rejection, independent review, publication-safety checks, and the complete host gate (exit zero, session `22143`) pass. The aggregate gate's existing privacy-safe USB loader precheck is separate from OT-109 and added no device, flash, radio, or physical evidence to this increment.

### OT-108 versioned per-candidate API/configuration acceptance contract

- Accepted append-only `OTCAC0/v1`, canonical SHA-256 `ccdb11e19031a4a01c717e30c172332c5e71ca79ee2b36b721277e55ca9a6c22`, raw SHA-256 `575e8747cdba643f2abb6f3252a62adbf3f12a9faaa46ca1d4bf4ce8bd9d23f3`.
- Bound each candidate to its distinct OT-107 generated sdkconfig and accepted source evidence. Future complete eight-operation evidence may be structurally selection-eligible without granting selection; strict nonempty partial evidence is comparison-only, measurable only for evidenced operations, and nonselectable.
- Nine focused adversarial groups cover exact identity/counts, per-candidate configuration substitution, complete/partial partitions, partial-primary and selection/execution rejection, unsupported-operation evidence, empty independent registries, readiness integration, and historical OT-094 through OT-107 preservation.
- Accepted no API/configuration evidence and granted no import/build/benchmark/device/radio/key/selection authority. Counts remain `3/0/0`, OT-096 remains 5/8, the same two blockers remain, readiness remains blocked, and all progress values remain unchanged. See [Decision 0049](decisions/0049-versioned-per-candidate-api-configuration-acceptance-contract.md) and [OT-108 evidence](../tests/hardware/OT-108-2026-08-21.md).


### OT-107 final per-candidate build-configuration admission

- Accepted owner-approved proposal raw SHA-256 `f9072a602a9c139b1e7728735db04cc270720bc37e0429c22bcdb0cd56202a15`, exact configuration-generation evidence raw SHA-256 `0c1b8cb574a210c6123b82b565e6ea8e12cee59bacd6ab4b94b293ddf9d2dfbc`, and append-only admission raw SHA-256 `3d71dfb02b6fd25e0881ac63cc085174c2cdd7e5ac2cd1e12c320ec34928f5a2`.
- Two fresh, initially absent, component-manager-disabled configuration-only runs per candidate reproduced exact generated sdkconfig pairs for native-SHA libsodium, ChaCha20/ChaChaPoly-enabled mbedTLS/PSA, and the Monocypher core-plus-optional-Ed25519 source requirement. No candidate source was copied or compiled.
- Closed only `final_candidate_build_configuration_unresolved`. The mbedTLS/PSA API/configuration and direct-radio region/MTU/full-PHY requirements remain open; history retains six/prior five/prior four/prior three/current two states, counts remain `3/0/0`, OT-096 remains 5/8, and readiness remains blocked.
- No candidate import/build, benchmark, hardware/device/radio/key operation, selection, packet-v1, support, regulatory, physical, continuing-authority, or score claim was added. Android stays 60%, V1 exact 43.75%/displayed 44%, baseline exact 31.75%/displayed 32%, and V1.5/V2 remain unmeasured. See [Decision 0048](decisions/0048-host-only-final-candidate-build-configuration-admission.md) and [OT-107 evidence](../tests/hardware/OT-107-2026-08-21.md).


### OT-106 Heltec compact-footer build integration

- Promoted the accepted OT-101/OT-101A implementation into the production UI component and linked it into the Heltec V4 bench target. The build-linked footer is `BAT:--% GPS:-- BLE:<S/A/C/R/E>` with a blank five-column direction field; terminal BLE errors retain compact `BLE:E`, while the startup logo and explicit full-screen `SELF CHECK FAIL` path remain separate.
- No live battery, GNSS, or LoRa-activity source is bound. Two fresh, initially absent, cache-isolated pinned ESP-IDF v6.0.2 builds used the exact 309-file staged input, exited zero with zero warnings, reproduced the same ordered seven artifacts, built a 473,024-byte application, and left 4,704,320 bytes free in the verified 5,177,344-byte smallest application slot. Aggregate SHA-256 is `3ba1145cf56cdad3447ce4a1a01c1098e2e78f2c2ffaa8e5378bc4f911b59dc9`.
- Preserved all five accepted OT-093 files byte-for-byte. Its immutable historical mixed-EOL raw digest `3837dbce...` is non-reconstructible because no per-line map was retained; the successor's deterministic Git-blob transforms separately reproduce distinct checkout aggregate `c84ba0e3...` and do not replace or reconstruct `3837dbce...`.
- Focused footer, adapter, startup, target, successor, evidence, and tamper gates passed; independent audit cleared the slice and full Test-Host session 99300 passed. No hardware/device access, flash/erase, radio/BLE/key/entropy/benchmark operation, physical-display/live-telemetry/support/readiness/configuration/crypto/regulatory/score claim was added. Three current readiness requirements remain; Android stays 60%, V1 exact 43.75%/displayed 44%, baseline exact 31.75%/displayed 32%. See [OT-106 evidence](../tests/hardware/OT-106-2026-08-21.md).
### OT-105 pinned ESP-IDF mbedTLS/PSA source-lock admission

- Accepted exact `OTCSLE0/v1` evidence SHA-256 `ae12ad7da6702ac85092e9cb8ad793b749871153fadee8b1a276e5a46b036e49`, strict `OTMPSLA0/v0` admission SHA-256 `26b6acdc9928eb9510a0baed53c609a4f9a23288155636c6462747745f28ac85`, and project-lock SHA-256 `12f8699d8d286a484e054df186fb0e8c97b75263d23caf4bd77ed48082e9c7ab`.
- Covered all 3,551 source and 198 glue files exactly once. Zero patches means only zero OpenTrail-applied patches after the pinned Espressif gitlink; Espressif/upstream divergence is unassessed.
- Counts become `3/0/0`; no blocker closes. Historical six/prior five/prior four/prior three/current three states remain; OT-096 stays 5/8 and the composite API/configuration blocker remains open.
- Direct, focused 9, generic 17, static 17, deterministic nine-output, publication, independent-review, and full Test-Host session 76051 gates pass. No acquisition/copy/import/build/API eligibility/device/radio/key/benchmark/selection/legal/support/physical/score claim was added. V1 exact43.75%/display44%; baseline31.75%/display32%. See [Decision 0047](decisions/0047-host-only-mbedtls-psa-source-lock-admission-delta.md) and [OT-105 evidence](../tests/hardware/OT-105-2026-08-21.md).

### OT-101A host-only compact-footer adapters

- Added pure target-neutral adapters under `tests/host_support` from the existing power assessment and BLE runtime status into the accepted OT-101 footer contract; OT-101 remains `partial`.
- Valid battery percentages reconstruct the original sample time from the assessment time, while a separate render time controls freshness. Delayed composition preserves and expires the old sample; unavailable or invalid assessments, out-of-range percentages, unsafe unsigned subtraction, and render time before assessment time fail closed.
- BLE maps dormant, waiting-for-host-sync, advertising, connected, restart-wait, and contained to `-`, `S`, `A`, `C`, `R`, and `E`. Terminal errors take precedence; unknown phases fail closed to unavailable. Composition intentionally provides no GPS source and uses an unsupported activity owner, so it renders `GPS:--` with a blank direction field.
- Ten C++17 warnings-as-errors adapter groups, the existing twelve footer groups, and the corrected complete host gate all pass (exit zero, session 17704). There is no firmware or target wiring, renderer/OLED, battery sensor, GNSS, BLE transport, radio-event or accepted real-traffic binding, target build, device access, flash, telemetry, traffic, physical-display, support, regulatory, or score evidence. V1 progress is unchanged. See [OT-101A evidence](../tests/hardware/OT-101A-2026-08-21.md).

### OT-101 host-only compact status footer prototype

- Advanced OT-101 from planned to partial with a target-neutral prototype/contract under `tests/host_support`; it remains deliberately outside the firmware tree.
- Formatted the owner-finalized `BAT:100% GPS:12 BLE:C` fields in one exact 128-column by 8-row page. Invalid, unavailable, stale, future, and out-of-range battery/GPS observations fail closed to `--`; BLE is limited to documented `-`, `S`, `A`, `C`, `R`, and `E` codes.
- Added one arrow field with host-injected latest-event semantics: `↑` for TX, `↓` for RX, equal-time replacement accepted, regressive time rejected, and blank while idle, unsupported, future-dated, expired, or cleared. No accepted real-transport source is wired.
- Twelve focused C++17 warnings-as-errors groups and the complete host gate pass (exit zero, session 4000), including both publication-safety gates and the Windows tail. There is no target renderer/OLED, battery/GNSS/BLE/radio binding, firmware or target build, device access, flash, telemetry, traffic, physical-display, support, regulatory, or score evidence. V1 progress is unchanged. See [OT-101 evidence](../tests/hardware/OT-101-2026-08-21.md).

## 2026-08-20

### OT-103 exact received target-profile admission

- Accepted exact `OTRTPE0/v0` received-target evidence, SHA-256 `517809caf31250d126cc3619f9d05386a92811a594dca0087d9acbf1b671147e`, and strict append-only `OTRTPA0/v0`, raw SHA-256 `98cce120cadc1bddf5851f1480ae181488e17277ba0a2c8c8c38a70a062be105`, for `OT-DEV-001`.
- Bound Heltec Automation WiFi LoRa 32 V4 / `HTIT-WB32LAF` / received `V4.2` / ESP32-S3R2 v0.2 / 16 MiB flash / 2 MiB PSRAM from five owner-supplied photos, existing OT-059/OT-061 evidence, and the official V4.2.0 datasheet. Raw photos/PDF, local paths, EXIF/location data, and private identifiers are not retained.
- Preserved official Table 1.5 `868-928 MHz`, Table 3.5.1 `863-928 MHz`, and package `HF 863-928` literals separately; no checkbox state, electrical front-end/antenna proof, region selection, regulatory acceptance, compatibility, or support claim follows.
- Closed only `exact_received_target_profile_unresolved`. Historical six-blocker and prior current four-blocker states remain recorded; three current requirements remain, readiness stays blocked, and crypto source/API-config/import counts remain `2/0/0`.
- Seven focused groups, the full host gate (exit zero, session 19027), publication-safety scans, and independent review pass. No final configuration, firmware/device/flash/radio/key action, benchmark, selection, packet-v1, continuing authority, or score was added. Android 60%; V1 exact 43.75%/display44%; baseline exact31.75%/display32%; V1.5/V2 unmeasured. See [Decision 0046](decisions/0046-exact-received-target-profile-admission-delta.md) and [OT-103 evidence](../tests/hardware/OT-103-2026-08-20.md).

### OT-102 exact Monocypher source-lock admission

- Accepted exact `OTCSLE0/v1` source evidence, SHA-256 `fe037820304103f7ca2253665076e4dc41740598ca9742ba8d45f6ec64ebc06f`, and strict host-only `OTMSLA0/v0`, raw SHA-256 `6dbeeac0266f9e6dd90265cdd71a721acfd36b4308dcb87180bd9d7c24c77e52`.
- Retained all 161 Monocypher 4.0.3 upstream Git blobs under a complete canonical acquisition/tree/license/SPDX/transitive/zero-patch/project-lock chain. The owner selected `BSD-2-Clause`; no legal-clearance or compatibility claim follows.
- Accepted one Monocypher source lock and closed only `monocypher_source_lock_absent`. Historical six-blocker and prior five-blocker states remain recorded; four current requirements remain. The accepted source count is two, while every API/configuration and candidate-import anchor remains empty.
- Seven focused groups, exact 161/161 staged source comparison, publication-safety checks, the complete host gate, and independent review pass. No firmware import/build, crypto/device/radio/key action, benchmark, selection, packet-v1, physical evidence, authority, or score was added. Android 60%; V1 exact 43.75%/display44%; baseline exact31.75%/display32%; V1.5/V2 unmeasured. See [Decision 0045](decisions/0045-monocypher-source-lock-admission-delta.md) and [OT-102 evidence](../tests/hardware/OT-102-2026-08-20.md).

### OT-100 exact libsodium source-lock admission

- Accepted host-only `OTCSLA0/v0`, raw SHA-256 `df595f2d07ba1b5d0a9bdf70237b1f0ea5a01fe8cb5a63ffb3575fe484faede0`, and result `LIBSODIUM-1.0.22-SOURCE-LOCK-ADMITTED-HOST-ONLY; FIVE-OTCBR0-REQUIREMENTS-REMAIN; NO-API-CONFIG-OR-IMPORT-ACCEPTANCE; OTCBR0-READINESS-BLOCKED`.
- Bound unchanged OT-097 policy and OT-099 evidence and accepted only exact libsodium 1.0.22 source evidence. API/configuration and import anchors remain empty.
- Preserved historical OT-094/OT-097 six-blocker evidence while recording five current unresolved requirements. Readiness remains blocked; no target/final-config/device/flash/radio/key/crypto execution/benchmark/selection/packet-v1/legal/physical/authority/score claim changed.
- Focused validation passes 5 groups and independent review is clear. Android 60%; V1 exact 43.75%/display44%; baseline exact31.75%/display32%; V1.5/V2 unmeasured. See [Decision 0044](decisions/0044-libsodium-source-lock-admission-delta.md) and [OT-100 evidence](../tests/hardware/OT-100-2026-08-20.md).

### OT-099 libsodium managed-import evidence

- Accepted host-only `OTLMI0/v0`, raw SHA-256 `8285fa7308bfc83a5d55503a7a3e1fa4c21895a42b095197b3ec75f634411ec9`, and result `ESPRESSIF-LIBSODIUM-1.0.22-MANAGED-IMPORT-EVIDENCE-COMPLETE; SOURCE-LOCK-ADMISSION-PENDING; ISOLATED-COMPUTER-BUILD-PASSED; NO-DEVICE-OR-CRYPTO-EXECUTION; OTCBR0-READINESS-BLOCKED`.
- Exact managed lock/source/license/SPDX/dependency/patch evidence is complete, but OTCSL0/v1 admission remains pending.
- The isolated generic ESP32-S3 build passed; the archive entered the link graph but probe symbols were not retained. No exact-target, final-config, crypto-execution, benchmark, or selection claim follows.
- Focused validation passes 4 groups and independent review is clear. All six blockers remain open. Android 60%; V1 exact 43.75%/display44%; baseline exact31.75%/display32%; V1.5/V2 unmeasured. See [Decision 0043](decisions/0043-libsodium-managed-import-evidence.md) and [OT-099 evidence](../tests/hardware/OT-099-2026-08-20.md).

### OT-098 external candidate acquisition and static inspection

- Accepted host-only `OTCAI0/v0`, raw SHA-256 `b7be03e305c6253e10f69f624132a736cce5aea3f559760cde4f948ae79abad6`, and result `EXTERNAL-CANDIDATE-SOURCES-ACQUIRED-AND-STATICALLY-INSPECTED; ZERO-SOURCES-IMPORTED; ZERO-SOURCE-LOCKS-ACCEPTED; OTCBR0-READINESS-BLOCKED`.
- Exact clean libsodium 1.0.22 and Monocypher 4.0.3 trees were acquired and statically inspected. Source paths cover 7/8 and 5/8 fixed operations; neither contains Noise XK.
- Signature trust, complete inventories, Monocypher's project license choice, project locks, final configuration, import, build, benchmark, and selection remain unresolved or absent. All six blockers remain open.
- Focused validation passes 3 scenario groups and independent review is clear. Android remains 60%; V1 exact 43.75%/displayed 44%; historical baseline exact 31.75%/displayed 32%; V1.5/V2 remain unmeasured. See [Decision 0042](decisions/0042-external-candidate-acquisition-static-inspection.md) and [OT-098 evidence](../tests/hardware/OT-098-2026-08-20.md).

### OT-097 license-aware source-lock admission v1

- Accepted strict host-only `OTCSL0/v1`, admission ID `OT-097-OT005-LICENSE-AWARE-SOURCE-LOCK-ADMISSION-V1`, canonical/policy SHA-256 `51639e1b9342dc9e501fb0682d044c0f7c05e691e1a26f463358a753f28a123a`, and result `LICENSE-AWARE-SOURCE-LOCK-ADMISSION-V1-FROZEN-HOST-ONLY; ZERO-SOURCES-ACQUIRED-OR-IMPORTED; OTCBR0-READINESS-BLOCKED`.
- Future acceptance now requires separate upstream SPDX expression, project choice, complete license inventory, and inventory digest. OTCSL0/v0 remains valid historical evidence but permanently non-admitting.
- Zero sources were accepted/acquired/imported; all six blockers remain open. No legal clearance, compatibility determination, authority, readiness, benchmark, selection, support, implementation, physical evidence, or score was added.
- Focused validation passes 17 scenario groups. Android remains 60%; V1 exact 43.75%/displayed 44%; historical baseline exact 31.75%/displayed 32%; V1.5/V2 remain unmeasured. See [Decision 0041](decisions/0041-license-aware-source-lock-admission-v1.md) and [OT-097 evidence](../tests/hardware/OT-097-2026-08-20.md).

### OT-096 host-only mbedTLS/PSA static eligibility

- Accepted strict `OTCMSE0/v0`, canonical SHA-256
  `3034da5a9f21ed663f82dc45ba976f8b5d6ec4ff353c2f96a3d5de4b586c013e`,
  and result `MBEDTLS-STATIC-ELIGIBILITY-FROZEN-HOST-ONLY; FIXED-OT005-OPERATION-SET-INELIGIBLE; OTCBR0-BLOCKER4-REMAINS-OPEN`.
- The pinned clean source has concrete paths for 5/8 operations; Ed25519
  sign/verify and Noise XK are absent, final configuration is unresolved, and
  generic APIs/identifiers/defaults cannot close those gaps.
- OT-096 acquired/imported zero source. No source lock, readiness, benchmark,
  selection, support, implementation, physical evidence, authority, or score
  changed; all six blockers remain open.
- Focused validation passes 17 scenario groups. Android remains 60%; V1 exact
  43.75%/displayed 44%; historical baseline exact 31.75%/displayed 32%;
  V1.5/V2 remain unmeasured. See [Decision 0040](decisions/0040-host-only-mbedtls-psa-static-eligibility.md)
  and [OT-096 evidence](../tests/hardware/OT-096-2026-08-20.md).

### OT-095 host-only candidate source-lock admission contract

- Accepted strict `OTCSL0/v0`, admission ID
  `OT-095-OT005-CANDIDATE-SOURCE-LOCK-ADMISSION-V0`, canonical and policy
  SHA-256 `c0bd923782d0977f8b375cbd2fe8cde5ff132a26b8b6a7ea34a62111bd101f1f`,
  status `admission_contract_frozen_host_only`, and result
  `SOURCE-LOCK-ADMISSION-CONTRACT-FROZEN-HOST-ONLY; ZERO-SOURCES-ACQUIRED-OR-IMPORTED; OTCBR0-READINESS-BLOCKED`.
- Distinguished acquisition receipts, immutable source trees, project
  dependency locks, API/configuration eligibility, candidate import, and
  benchmark execution. Admission semantics are defined for the first five;
  benchmark-execution admission remains undefined and blocked. No evidence
  layer is sufficient alone.
- Kept the source-lock, API/configuration, and candidate-import accepted-digest
  registries separate, candidate-specific, and empty. Anchor mutation alone
  cannot advance the current zero-source blocked contract.
- Accepted, acquired, and imported zero sources. The installed mbedTLS/PSA
  observation remains neither a project dependency lock nor proof of required
  API, Ed25519, or final-configuration eligibility.
- Kept all six OTCBR0 requirements open and OTCB0 `draft_blocked`. No acquisition,
  import, benchmark, build, target support, cryptographic selection,
  implementation, device/radio/key action, physical evidence, authority, or
  score credit was added.
- Focused validation passes 14 OTCSL0, 16 OTCBR0, 10 OTCB0, and 13 OTCBL0
  scenario groups; independent adversarial machine-contract audit is clear.
  The complete host gate exits zero, including both publication-safety layers,
  59 loader scenarios, and simulator groups 33/23/15/10 plus 13/13 UI scenarios.
- Android remains 60%; V1 exact 43.75%/displayed 44%; the historical baseline
  exact 31.75%/displayed 32%; V1.5 and V2 remain unmeasured. See
  [Decision 0039](decisions/0039-host-only-candidate-source-lock-admission-contract.md)
  and [OT-095 evidence](../tests/hardware/OT-095-2026-08-20.md).

### OT-094 host-only OT-005 candidate-readiness contract

- Accepted the strict `OTCBR0/v0` ledger, readiness ID
  `OT-094-OT005-CANDIDATE-READINESS-V0`, canonical SHA-256
  `705b30693196e2f46d8bda7c17acb1e04d7b9092c4a3817286c14d189001b9d3`,
  status `readiness_blocked`, and result
  `CANDIDATE-READINESS-CONTRACT-FROZEN-HOST-ONLY; OTCB0-EXECUTION-BLOCKED`.
- Bound the unchanged historical OT-005 plan and accepted OT-093 baseline,
  baseline-only target/tool/configuration facts, fixed comparison order, and
  the exact fields required for a later direct-radio benchmark profile.
- Preserved six ordered blockers: exact received target profile, final common
  and candidate build configuration, libsodium project lock, mbedTLS/PSA
  dependency lock plus API/config eligibility, Monocypher project lock, and
  direct-radio region/MTU/full-PHY. Every closure digest remains null.
- Hardened legacy admission so a caller-declared `ready` OTCB0/v0 plan is
  structural only. Without an independently accepted, fully resolved readiness
  artifact it cannot create a result template or yield pass. The accepted-ready
  trust-anchor set remains empty.
- Focused OTCB0 10, OTCBR0 13, OTCBL0 13, Heltec target 12, and V1/V1.5 scope
  16 scenario groups pass; both publication-safety layers pass and the complete
  `tools/Test-Host.ps1` run exits `0`.
- No dependency acquisition, candidate import, benchmark build/execution,
  target support, suite/wire selection, implementation, device/radio/key action,
  physical evidence, or score credit occurred. Android remains 60%; V1 exact
  43.75%/displayed 44%; the historical baseline exact 31.75%/displayed 32%;
  V1.5 and V2 remain unmeasured.
- Next: close all six readiness requirements, accept a new immutable executable
  benchmark plan, run the exact comparison under separate authority, and make a
  later explicit suite/wire decision. See
  [Decision 0038](decisions/0038-host-only-ot005-candidate-readiness-contract.md)
  and [OT-094 evidence](../tests/hardware/OT-094-2026-08-20.md).

### OT-093 deterministic pre-crypto OT-005 build baseline

- Accepted `OTCBL0/v0`, baseline ID
  `OT-093-OT005-BUILD-BASELINE-V0`, canonical SHA-256
  `16ffe83af7e3c1f00b5d123eae30e3ac4a0ea2dea0cb08bcc60b990d3e881733`,
  and result `BUILD-BASELINE-FROZEN; OTCB0-EXECUTION-BLOCKED`.
- Ran two independent, initially absent, cache-disabled build profiles with
  stable project version `ot093-precrypto-v0`, exact source/raw-byte,
  configuration, ESP-IDF, tool-executable, and isolated-Python locks. Both
  exited zero, produced zero warnings, and yielded identical ordered
  application BIN, ELF, map, bootloader, partition-table, sdkconfig, and
  partition-CSV tuples. Their normalized receipt SHA-256 values both equal
  `265ee99c47784100c8a00dd021c3f10a29ca71cc97361639f7f85e6ea13d10df`.
- Preserved aggregate-only authority: each run receipt remained
  `BUILD-RUN-CAPTURED` and reconciliation-pending; only the aggregate validator
  derived exact receipt and artifact equality.
- Focused OTCBL0 12, Heltec target 12, historical OTCB0 8, and V1/V1.5 scope 16
  groups pass. Independent aggregate audit is clear, and the fresh complete
  `tools/Test-Host.ps1` run exits `0`.
- Preserved Decision 0003. The OT-005 plan remains `draft_blocked`, execution
  authority false, and final candidate-ready target/toolchain/sdkconfig
  applicability unresolved. No candidate or secure-LoRa adapter was imported
  or executed; no suite/library, handshake/KDF, packet-v1 wire, radio profile,
  benchmark result, device operation, implementation, or physical evidence is
  accepted.
- This build-only result adds no score. Android remains 60%; V1 remains exact
  43.75%/displayed 44%; the historical baseline remains exact 31.75%/displayed
  32%; V1.5 remains unmeasured. Next: make the OT-005 plan final-candidate
  ready, execute the exact candidate benchmark, then explicitly accept the
  suite/wire selection. See [Decision 0037](decisions/0037-pre-crypto-build-baseline.md)
  and [OT-093 evidence](../tests/hardware/OT-093-2026-08-20.md).

## 2026-08-19

### OT-092 post-V2 future-concepts register and Public Assistance direction

- Created the durable [future-concepts register](FUTURE_CONCEPTS.md) with one
  required field set and explicit separation between accepted direction,
  scheduling, implementation, support, and progress evidence.
- Accepted a provisioning-independent regional public lane and independently
  configurable Public Assistance Broadcast as direction only, deferred until
  V2 is fully functional and accepted. One LoRa radio time-shares logical lanes;
  no simultaneous-profile capability is claimed.
- Required deliberate public-content and location confirmation, fresh/stale-
  location handling, private-text isolation, nonconfidential public packets,
  bounded radio/abuse controls, and truthful receipt/dispatch language.
- Preserved the existing boundaries: `OTQ0/v0` is not the Public Assistance
  packet, `OTSL0/v0` grants no broadcast authority, and exact packet, radio,
  security, regulatory, and physical-acceptance designs remain future work.
- This is planning/governance evidence only. No hardware, phone, key, location,
  radio, firmware, install, account, upload, or distribution action ran.
  Android remains 60%; V1 remains exact 43.75%/displayed 44%; V1.5 and V2 remain
  unmeasured. The next V1 security checkpoint remains the exact OT-005 target
  benchmark and explicit suite/wire selection. See [Decision 0036](decisions/0036-post-v2-public-lane-and-assistance-direction.md).

### OT-091 host-tested secure-LoRa lifecycle/admission contract

- Accepted `OTSL0/v0`, the algorithm-neutral Decision 0033 contract for V1's
  exact two-node pairwise-unicast key lifecycle and protected-radio admission.
- Froze one secret-free authenticated invitation per candidate/attempt, mutual
  device-authentication and complete-transcript obligations, matching local
  confirmation, exact candidate commit/readback, and exact peer activation
  before routine traffic.
- Required epoch replacement to advance by exactly one with fresh material for
  retained exact identities, block traffic while unresolved, and forbid old-
  epoch fallback after possible/new activation. Known no-change failure may
  retain the prior coherent epoch; ambiguity requires reconciliation with no
  traffic.
- Bound group/epoch, both full identities, ordered direction, and purpose before
  the existing traffic-context, durable counter-lease, and nonce-composition
  obligations. Packet v0, plaintext downgrade, random-nonce fallback, counter
  reuse, alias/name/caller-Boolean trust, and phone-held LoRa keys are denied.
- Required authentication before replay mutation, durable cryptographic replay
  and receive admission before plaintext release, and protected acknowledgement
  afterward. Exact retries reuse the same sealed bytes; byte-identical retries
  are not redelivered. A positive LoRa acknowledgement means peer-device durable
  admission, not phone display or user read.
- Preserved Decision 0003: the OT-005 public benchmark plan remains blocked, so
  no suite/library, handshake/KDF, packet-v1 wire, target storage, MTU/PHY, or
  production retry values are selected.
- No hardware/phone access, device/storage write, entropy/key/signer operation,
  BLE action, radio transmission, physical input/display, installation, account,
  upload, or distribution operation ran. No secure-LoRa implementation or
  physical acceptance is claimed.
- Android remains 60%; V1 remains exact 43.75%/displayed 44%; the historical
  baseline remains exact 31.75%/displayed 32%; V1.5 remains unmeasured. The next
  gate is the exact OT-005 target benchmark and explicit suite/wire selection.
  See [OT-091 evidence](../tests/hardware/OT-091-2026-08-19.md).

### OT-090 host-tested BLE pairing/replacement contract

- Accepted `OTBP0/v0`, the exact normally closed physical-presence pairing,
  saved-bond reconnect, and confirmed phone-replacement contract required by
  Decision 0033.
- Froze the target-neutral physical gesture: hold the designated local input for
  at least 3000 ms and release it to open one exact 30-second, one-attempt,
  single-candidate window. No GPIO/button mapping is selected. Each admitted
  window receives one fresh uniformly sampled, locally displayed six-decimal-
  digit passkey.
- Required Bluetooth LE Secure Connections-only, MITM-authenticated passkey
  pairing, bonding, and an exact 16-byte/128-bit key; legacy pairing, `Just Works`, and
  static/debug passkeys are denied. Reconnect rechecks the exact saved bond,
  link security, and separate application authorization without rewriting
  ownership.
- Required replacement confirmation by a second qualifying 3000-ms hold/release
  after the candidate secure bond and before the original deadline. The
  candidate owner commit and exact readback precede old-authorization
  invalidation; old-bond removal and verified absence precede publication of
  the new controller.
- Required timeout, mismatch, authentication/bond failure, cancellation,
  disconnect, stale/replayed events, second candidates, clock failure/rollback,
  restart, and ambiguous persistence to fail closed. Restart never restores a
  passkey or pending window. Abort, expiry, interruption, or known pre-mutation
  failure preserves the exact prior owner only after candidate-bond removal and
  verified absence; ambiguous commit, readback, candidate cleanup, or old-bond
  cleanup publishes neither controller.
- Kept PINs, bond keys, phone identifiers, BLE addresses, device-specific
  identifiers, private owner bindings, and storage contents out of ordinary
  logs and public evidence.
- No target, Android, storage, phone, Bluetooth, pairing, PIN, bond, key, signer,
  radio, installation, write, account, upload, or distribution operation ran.
  Implementation and physical acceptance remain open.
- Android remains 60%; V1 remains exact 43.75%/displayed 44%; V1.5 remains
  unmeasured. At OT-090 acceptance the separate secure-LoRa contract was the
  next host gate; OT-091 later froze it without implementation or score credit.
  See [OT-090 evidence](../tests/hardware/OT-090-2026-08-19.md).

### OT-089 permanent V1/V1.5 scope and security boundary

- Adopted the owner-approved permanent V1 topology: two supported Heltecs, two
  approved Android phones, one current phone per Heltec, and exact
  bidirectional BLE/direct-LoRa/BLE message acceptance with no relay, server, or
  internet dependency.
- Adopted practical physical-presence authorization: normally closed pairing,
  a short deliberate window, one fresh locally displayed six-digit PIN,
  authenticated BLE Secure Connections pairing/bonding, saved-bond reconnect,
  and confirmed phone replacement. V1 discloses that reset, reflash, invasive
  access, or old-flash restoration may reset/roll back ownership; no secure
  element or independent monotonic floor is required.
- Kept LoRa security separate: authentication, encryption, sender/destination
  identity, unique IDs, integrity, replay/duplicate rejection, acknowledgement,
  bounded retry, key provisioning/replacement, implementation, and physical
  acceptance remain open.
- Defined V1.5 as four supported nodes with mixed hardware allowed and
  preferred but not mandatory. Four phones are not required; any relay claim
  requires a physical three-radio path.
- Preserved earlier decisions/evidence and superseded only their incompatible
  four-pair, four-phone, and mandatory-authorization-floor clauses. No hardware,
  phone, pairing, key, signer, radio, installation, account, upload, or
  distribution operation occurred.
- Result: `OWNER-APPROVED-SCOPE-ADOPTED`. Owner-approved V1 scope and security
  boundary adopted; implementation and physical acceptance remain open.
  Android remains 60%; V1 remains exact 43.75%/displayed 44%; V1.5 remains
  unmeasured. See [OT-089 evidence](../tests/hardware/OT-089-2026-08-19.md).

### OT-088 Android private-pilot operational policy freeze

- Froze one policy revision for offline/account-free/transient privacy and
  data safety, complete backup/transfer exclusion, privacy-safe public
  evidence, and mandatory later inspection of logs, notifications, recent
  tasks, screenshots, crash output, storage, and cleanup.
- Froze first-release rollback as disconnect, service stop, uninstall and
  verified app-data removal with no downgrade. A retry may use only the same
  accepted version, artifact digest, signer, and private source; changed output
  starts a new evidence set.
- Froze private-pilot support as best-effort/no-SLA and unavailable until a
  complete OTAR pass. Support uses an owner-provided private-pilot channel;
  `SECURITY.md` remains the sensitive-report route.
- Satisfied exactly five of eight OTAR prerequisites. Physical matrix, release
  identity, and signer/custody remain blocked; the release gate stays
  `NOT-EVALUATED`, plan outcome stays `PLAN-ACCEPTED-EXECUTION-BLOCKED`, and
  execution authority stays false. No phone, install, signing, account, upload,
  or distribution operation occurred.
- Android remains 60%, and V1 Companion remains exact 43.75%/displayed 44%.
  See [OT-088 evidence](../tests/hardware/OT-088-2026-08-19.md).

### OT-087 Android unsigned release-build foundation

- Froze Android version code/name `1` / `1.0.0`, retained a `-dev` debug
  suffix, and configured an explicit non-debuggable unsigned release build
  type with no signing configuration.
- Passed 248 JVM test executions across protocol/debug/release suites, both
  warning-as-error lint variants, debug and instrumentation assembly, and
  unsigned release assembly.
- Inspected the packaged disposable APK for exact identity/version/SDK, the
  exact six-permission union, backup and transfer exclusion, non-debuggable/
  non-test-only state, no instrumentation or OT-085 helper classes, valid DEX
  checksums, and the exact unsigned verification result. No phone, hardware,
  key/certificate, account/store, installation, upload, or distribution
  operation occurred.
- Satisfied exactly two of eight OTAR prerequisites. Physical matrix, privacy/
  data-safety, release identity, rollback, signer/custody, and support remain
  blocked; the release gate stays `NOT-EVALUATED` and plan outcome stays
  `PLAN-ACCEPTED-EXECUTION-BLOCKED`.
- Android remains 60%, and V1 Companion remains exact 43.75%/displayed 44%.
  See [OT-087 evidence](../tests/hardware/OT-087-2026-08-19.md).

### OT-086 Android operational-release admission plan

- Accepted the machine-checkable `OTAR0/v0` operational-release contract,
  canonical plan, fail-closed validator, and deterministic denial cases.
- Froze `private-sideload-v1-pilot` as the only current distribution scope:
  controlled private installation on four V1 pilot phones that must each be
  approved and frozen before execution, not public or store distribution. Any
  future Play path requires a separate current target-API policy recheck and
  required toolchain/target-SDK update.
- The exact accepted result is `PLAN-ACCEPTED-EXECUTION-BLOCKED`. No production
  variant, signer/custody policy, immutable artifact, supported-device matrix,
  lifecycle/endurance/privacy/support result, install, distribution, or release
  pass exists, and no signing, phone, store/account, or device authority was
  exercised.
- Android remains 60%, and V1 Companion remains exact 43.75%/displayed 44%.
  The next release-track gate is to satisfy and execute the accepted private-
  sideload plan as one coherent candidate evidence set. Protected one-phone
  authorization/`Ready` and the four-pair field proof remain separate gates.
  See [OT-086 evidence](../tests/hardware/OT-086-2026-08-19.md).

### OT-085B physical automatic BLE link-termination acceptance

- Reused the exact installed and verified OT-085 image without a target write,
  reset, recovery action, protected access, or unit-2 access.
- One Android 13 phone found exactly one compatible service advertiser, read
  the exact fixed 16-byte value from the READ-only suffix-`0x04`
  characteristic, and made no disconnect request. The target disconnected the
  bound GATT inside the frozen timing window.
- The owner observed `BLE CONNECTED` followed by `BLE ADVERTISING`; the phone
  then found exactly one compatible service advertiser without requiring or
  inferring a stable address identity.
- Rejected an earlier RPA-sensitive address-equality postcondition, registered
  the exact custom test-only runner, and added instrumentation assembly to the
  standard gate. The Android gate passes 139 JVM tests across fifteen suites,
  lint, debug assembly, and instrumentation assembly; all twelve target-
  admission groups pass.
- Heltec remains 25%, Android remains 60%, and V1 Companion remains exact
  43.75%/displayed 44%. Protected authorization, Ready, operational release,
  support, and field evidence remain open. See
  [OT-085B evidence](../tests/hardware/OT-085B-2026-08-19.md).

### OT-085A physical bounded public BLE link/status acceptance

- Installed and read-back verified the exact OT-085 factory application on the
  selected experimental Heltec target under one consumed owner authorization.
- One Android 13 phone selected the only compatible advertiser, connected, read
  the exact fixed 16-byte zero-capability public value from the READ-only
  suffix-`0x04` characteristic, disconnected, and rediscovered the same
  in-memory endpoint. No device name, address, or private binding was retained.
- The owner observed `BLE CONNECTED` during the link and return to
  `BLE ADVERTISING` after phone disconnect. The firmware's independent
  15-second automatic termination was not physically exercised.
- Removed the superseded exported debug Activity. The retained one-use
  instrumentation is test-only, correlates one exact GATT and phase, fails
  closed on stale or unexpected callbacks, and emits only fixed pass/deny
  fields. The Android gate passes 138 JVM tests across fourteen suites, lint,
  debug assembly, and instrumentation compilation.
- Accepted the third of five equal Android evidence gates, raising V1 Companion
  from exact 39.75%/displayed 40% to exact 43.75%/displayed 44%. Protected
  authorization, Ready, production support, and field evidence remain open. See
  [OT-085A evidence](../tests/hardware/OT-085A-2026-08-19.md).

## 2026-08-18

### OT-085 bounded public BLE link/status build

- Added one fixed 16-byte public `OTB0/v0` read under UUID suffix `0x04`.
  It carries zero capability bits and no unit, phone, owner, group, radio,
  location, key, path, or operation identity; no write property exists.
- Retained every protected Protocol Info, Command, and Stream permission and
  kept authorization claims plus normal commands closed. One connection may
  remain for 15 seconds, followed by exact termination and a two-second
  disconnect-acknowledgement bound before fail-closed containment.
- Five fixed-value groups, fifteen BLE-owner groups, the complete 148-executable
  native host matrix, eleven target-admission groups, and two identical pinned
  ESP-IDF v6.0.2 builds pass. No hardware was accessed or flashed. See
  [OT-085 evidence](../tests/hardware/OT-085-2026-08-18.md).

### Decision 0028 current-Heltec authorization deferral

- Deferred rollback-protected ownership/control beyond current Heltec V1 after
  both on-chip floor candidates were rejected. Protected foundations remain
  dormant; no control is relabeled secure and V1 scoring is unchanged.

### OT-084 SECURE_VERSION rollback-floor viability review

- Rechecked ESP32-S3 `SECURE_VERSION` against seven pinned ESP-IDF 6.0.2
  sources. It is a 16-step firmware anti-rollback field, not an independent
  companion-authorization namespace.
- The native anti-rollback layout requires OTA slots without factory/test,
  while OpenTrail's accepted layout and recovery route retain and restore the
  exact factory application.
- A pure source-bound evaluator, fixed sanitized outcome, and six plan groups
  pass. No provider or external part is selected, no device or target build
  input changed, and V1 completion is unchanged. See
  [OT-084 evidence](../tests/hardware/OT-084-2026-08-18.md).

### OT-083 rollback-floor descriptor viability review

- Rechecked the conditional custom USER_DATA eFuse thermometer against pinned
  ESP-IDF 6.0.2 source and proved its Reed-Solomon coding unit is writable only
  once, so it cannot implement repeated independent floor advances.
- Added a pure source-bound viability evaluator, fixed sanitized outcome, and
  fail-closed plan tests; existing physical-provider admission now remains
  false even when all old factual fields are populated.
- The complete 146-executable host gate and eleven Heltec target-admission
  groups pass. No target build input, device, eFuse, or runtime changed. The
  two HMAC roles remain selected by type, the rollback-floor provider is now
  unselected, and V1 completion is unchanged. See
  [OT-083 evidence](../tests/hardware/OT-083-2026-08-18.md).

### OT-082 build-only configuration/security metadata source

- Added a target-local, one-use source for the default NVS build configuration
  and four distinct decoded ESP32-S3 security-state values.
- Strict synthetic tests cover NVS configuration permutations, all 16 security-
  state combinations, exact call order, all-or-nothing publication, re-entry,
  reuse, and deterministic fresh instances. The complete 145-executable host
  gate, eleven target-admission groups, and two identical pinned target builds
  pass.
- The source is build-compiled but has no runtime call path and was not executed
  on a device. It grants no read or inventory authority and cannot resolve
  runtime NVS overrides, configured-key conflict, or the rollback-floor
  descriptor. V1 completion is unchanged. See
  [OT-082 evidence](../tests/hardware/OT-082-2026-08-18.md).

### OT-081 build-only protected-root key-roster adapter

- Implemented the target-local one-use six-slot adapter using only the five
  decoded read-only ESP-IDF APIs accepted by OT-080.
- Strict synthetic tests prove exact call ordering, fail-fast invalid-purpose
  handling, all-or-nothing publication, contradiction denial, and re-entry/
  reuse containment. The complete 144-executable host gate, ten target-
  admission groups, and two identical pinned target builds pass.
- The adapter is build-compiled but not runtime-injected or device-executed. It
  does not infer provisioning or reservation and cannot produce complete
  inventory evidence or provider authority. V1 completion is unchanged. See
  [OT-081 evidence](../tests/hardware/OT-081-2026-08-18.md).

### OT-080 offline protected-root reader-route admission

- Rejected the host Python eFuse inventory path because it materializes raw key
  blocks in host memory.
- Accepted a pure target-side metadata-interface contract pinned to ESP-IDF
  6.0.2 and five decoded key-purpose/protection/unused APIs. Raw key/block
  reads, HMAC, writes, burns, protection changes, and unlisted APIs remain
  denied.
- No adapter was created, the floor descriptor and physical read remain
  unavailable, no hardware was accessed, and every deployment/read/write/
  provisioning/provider/runtime authority stays false. V1 completion is
  unchanged. See [OT-080 evidence](../tests/hardware/OT-080-2026-08-18.md).

### OT-079 offline protected-root inventory admission

- Accepted a pure, supplied-evidence inventory verifier and an exact offline
  plan for a possible later read-only ESP32-S3 protected-root inventory.
- A complete future inventory must cover all six key slots, configured-NVS
  conflict state, complete floor-candidate facts, cleanup, and the disabled
  security-feature state required by OT-077. Complete unfavorable evidence is
  reviewable but cannot select or admit a provider.
- At OT-079 acceptance no complete inventory reader/orchestrator was created, no device was accessed, all allocation fields remain
  absent, and every read/write/eFuse/provisioning/runtime authority stays false.
  V1 Companion remains 40%. See
  [OT-079 evidence](../tests/hardware/OT-079-2026-08-18.md).

### OT-078 offline protected-root provider selection

- Selected distinct ESP32-S3 `HMAC_UP` provider types for `ot_auth` NVS
  encryption and private bond-binding PRF use without selecting physical blocks.
- Conditionally selected a dedicated custom user-eFuse thermometer provider
  class for the independent authorization floor. Exact allocation, capacity,
  provisioning, protection, and target behavior remain unknown.
- Pure fail-closed host contracts cover absent/unprovisioned key evidence,
  role/purpose/distinctness/read-protection/self-test binding, canonical floor
  encoding, one-step advance, uncertainty, exhaustion, and reboot reconciliation.
- No device was accessed and no key generation, eFuse write, protected-storage
  activation, rollback advance, partition transition, or physical authority was
  added. V1 Companion remains 40%. See
  [OT-078 evidence](../tests/hardware/OT-078-2026-08-18.md).

### OT-077 offline exact recovery-route acceptance

- Accepted one exact ESP32-S3 ROM source-restore contract: unconditional exact
  OT-064 application first, exact `OTHP0/v0` source partition table last, then
  closed-connection independent readback and separately bounded boot evidence.
- The source-table recipe, no-stub 115,200-baud/no-reset tooling, strict denial
  cases, two distinct protected-key roles, and independent monotonic rollback-
  floor requirements are frozen offline. One private application copy is
  retained; a second independently hashed staged copy remains a physical gate.
- Future admission also requires a fresh same-operation/evidence-set read-only
  proof that secure boot, flash encryption, and secure download are disabled.
  Unknown or mismatch denies. Any failure after the first write stays in ROM as
  `RECOVERY-UNCERTAIN`, preserves private evidence before transient cleanup,
  makes no boot-success claim, never auto-retries, and needs fresh authority.
- No device was accessed and no physical recovery, write, erase, reset, key,
  eFuse, rollback, or partition-transition authority was added. V1 Companion
  remains 40%. See
  [OT-077 evidence](../tests/hardware/OT-077-2026-08-18.md).

## 2026-08-17

### OT-076 exact installed application recovery capture

- One owner-authorized, one-use read-only operation captured the exact
  470,928-byte OT-064 factory application and independently reread it after
  closing the device connection; its SHA-256 matches the accepted installed
  application identity.
- The exact binary is retained only as a private ignored recovery artifact.
  The temporary reader/tests/bytecode were deleted, and no private device or
  operation identity or detailed transport output is retained publicly.
- No recovery route or write authority was accepted. The partition transition
  remains denied. After manual RST, the owner observed the Trail logo followed
  by `BLE ADVERTISING`. V1 Companion remains 40%. See
  [OT-076 evidence](../tests/hardware/OT-076-2026-08-17.md).

### OT-075 offline candidate partition and recovery gate

- Pinned ESP-IDF v6.0.2 generated and decoded the exact 3,072-byte `OTPS0/v0`
  candidate partition artifact; focused tests reject any source, row, checksum,
  length, padding, toolchain, or physical-surface mismatch.
- A clean rebuild from the recorded OT-064 source commit did not match the
  application installed on the unit. The rebuild was rejected and its
  temporary worktree removed, leaving the exact recovery application absent.
- The recovery bundle and transition remain denied. Active target/runtime and
  device state are unchanged; no device access or physical action occurred.
  V1 Companion remains 40%. See
  [OT-075 evidence](../tests/hardware/OT-075-2026-08-17.md).

### OT-074 protected-storage source proof

- A bounded read-only operation matched the exact installed 3,072-byte
  partition table and verified that the complete 1 MiB protected-storage
  source region was all `0xFF`.
- The fixed result was `OTPSTE1/v0` / `SOURCE-PROOF-SATISFIED-ONLY`. No raw
  bytes, paths, port, device identifier, private operation/evidence identity,
  or detailed transport output was retained; the temporary executor and
  bytecode were deleted with cleanup verified.
- After manual RST, the owner observed the Trail logo followed by
  `BLE ADVERTISING`. This satisfies only OT-070's source prerequisite and does
  not authorize a partition transition or protected runtime. V1 Companion
  remains 40%. See
  [OT-074 evidence](../tests/hardware/OT-074-2026-08-17.md).

### OT-073 protected-storage source-read attempt

- Consumed one owner-authorized, one-use OT-DEV-001 read-only attempt using
  esptool 5.3.1, one no-reset ROM connection attempt, no RAM stub, and only the
  two OT-071 allowlisted ranges.
- The attempt returned only `OTPSTE0/v0` / `DENY-READ-FAILURE`; it was not
  repeated. No raw bytes, paths, port, device identifier, private operation
  identity, or detailed transport output was retained, and the ephemeral
  executor files were deleted with cleanup verified.
- After manual RST, the owner observed the Trail logo followed by
  `BLE ADVERTISING` status.
  Source proof remains absent; no partition transition or protected-runtime
  authority is added, and V1 Companion remains 40%. See
  [OT-073 evidence](../tests/hardware/OT-073-2026-08-17.md).

### OT-072 V1 Companion release measurement

- Established a distinct six-milestone V1 Companion track whose weights total
  100. The exact evidence-weighted result is 39.75% and displays as 40%.
- Android Companion is 40% because two of five equal gates are accepted:
  tested application plus physical install/lifecycle/artwork observations, and
  exact-service discovery. Physical GATT, protected authorization and Ready,
  operational release acceptance, and four-person field proof remain open.
- The historical standalone baseline remains exact 31.75%/displayed 32%, V2
  Integrated remains unmeasured, and no implementation or physical evidence
  changed. See [OT-072 evidence](../tests/hardware/OT-072-2026-08-17.md).

### OT-071 read-only protected-storage transition evidence

- Added a streaming offline verifier for the exact installed partition table
  and complete 1 MiB all-`0xFF` source region. It emits only a fixed sanitized
  result and does not retain raw bytes, paths, identities, or nonblank details.
- Added a separate denied Heltec read plan for a future exact owner-authorized
  session. It contains no executable hardware reader; no unit, port, command,
  or authority is selected now, and no hardware read occurred.
- Eight verifier groups, six transition-manifest groups, and the existing 9/9
  target admission pass. Active target, runtime, image, and device state remain
  unchanged; source proof is still absent and V1 remains 32%. See
  [OT-071 evidence](../tests/hardware/OT-071-2026-08-17.md).

### OT-070 protected-storage partition transition admission

- Added a pure host guard for the exact `OTHP0/v0` to `OTPS0/v0` transition.
  It requires fresh installed-layout evidence, verified blank media or a
  separately verified migration, recovery artifacts, isolated operation scope,
  and exact operation authority; it performs no I/O.
- Added a design-only Heltec manifest that binds both table hashes and requires
  a future all-`0xFF` proof across the complete 1 MiB source region without
  retaining bytes. It remains denied and grants no read or write authority.
- Thirteen strict C++ groups, 100 repeated runs, five manifest groups, the
  existing 9/9 target admission, and the complete host gate pass. Active target
  configuration, runtime, image, and device remain unchanged. Historical V1
  remains 32%. See [OT-070 evidence](../tests/hardware/OT-070-2026-08-17.md).

### OT-069 inactive Heltec authorization NVS context

- Added one target-local owner for the exact candidate encrypted NVS context.
  It consumes existing security configuration only, performs one exact open,
  zeroes temporary configuration, and contains native ambiguity and reentry.
- Ten strict lifecycle groups, a separate disabled-configuration zero-I/O
  check, and nine target-admission groups pass. Two pinned ESP-IDF v6.0.2
  builds reproduce the same 470,928-byte BIN with SHA-256
  `9D4EBCD8BB68183798BF47267252A1B2A94A114FACD16E8CF975AEBE43314EEF`.
- The owner is build-compiled but not runtime-injected. Active partition,
  configuration, denied authorities, installed firmware, and devices remain
  unchanged. Historical V1 remains 32%. See
  [OT-069 evidence](../tests/hardware/OT-069-2026-08-17.md).

### OT-068 inactive Heltec authorization NVS backend

- Added one target-local ESP-IDF NVS adapter for the exact OT-067 protected-KV
  slot contract. It accepts only an already-opened handle and exposes exact
  get/set/commit behavior; it cannot initialize, open, erase, reset, retry,
  provision, or log.
- Eight strict backend groups, the unchanged ten-group slot-media regression,
  and nine target-admission groups pass. Two pinned ESP-IDF v6.0.2 builds
  reproduce the same 470,928-byte BIN with SHA-256
  `9F5AFB320A015E3BFFD866A9EE31F76198739521FA7519845ACDA12B9B52BAE5`.
- The adapter is build-compiled but not runtime-injected. The active partition,
  sdkconfig, denied authorities, installed firmware, and physical device remain
  unchanged. Historical V1 remains 32%. See
  [OT-068 evidence](../tests/hardware/OT-068-2026-08-17.md).

### OT-067 protected authorization KV slot media

- Added one exact key/value record-media adapter for the two accepted
  authorization slots, with fixed private binding names and exact 32-byte
  values.
- Ten strict groups prove missing and malformed values, typed backend failures,
  commit durability, reentry containment, alternating-slot rotation, reboot
  restore, and fail-closed rollback, prepared-ahead, and ambiguous media.
- The backend remains injected. No target partition/config/runtime/image/device,
  key, rollback authority, bond, pairing, GATT authorization, or Ready state
  changed. Historical V1 remains 32%. See
  [OT-067 evidence](../tests/hardware/OT-067-2026-08-17.md).

### OT-066 trusted phone GATT authority composition

- Added one target-neutral composition from an exact private bond reference and
  device-secret binding resolver to the durable one-phone and GATT authorities.
- Eight strict groups prove exact-tuple caching, changed-reference and stale-
  generation denial, re-pair identity separation, physical-gated first claim,
  exact-owner reconnect, wrong-phone denial, explicit replacement, release,
  reentry containment, and persistence uncertainty.
- The target still injects denied authorities. No private target bond store,
  keys, physical control, pairing, GATT exchange, Ready state, image, or device
  changed. Historical V1 remains 32%. See
  [OT-066 evidence](../tests/hardware/OT-066-2026-08-17.md).

### OT-065 reversible protected authorization-store foundation

- Added a target-neutral two-slot store that accepts only the exact record at a
  freshly read independent generation floor. New records are written and
  exactly verified in the inactive slot before the floor advances, then both
  authorities are reread before publication.
- Twelve strict host groups cover rotation, reboot, corrupt/stale/conflicting
  media, prepared-ahead state, safe pre-write failure, ambiguous writes, floor
  conflicts, and post-advance uncertainty.
- Added an exact inactive Heltec candidate partition/provisioning plan. It
  selects no key or rollback provider, grants no physical authority, and does
  not alter the active partition table, sdkconfig, runtime, or device.
- This is host/build planning evidence, not target encryption, eFuse or key
  provisioning, physical rollback protection, durable bond storage, GATT
  authorization, Ready, or device-write evidence. Historical V1 remains 32%.
  See [OT-065 evidence](../tests/hardware/OT-065-2026-08-17.md).

### OT-064 physical Trail OLED startup and BLE status

- Added a target-local 128 x 64 one-bit startup/status display owner, a
  fail-contained SSD1315-compatible Heltec V4 adapter, and a deterministic
  Limited Underground Trail bitmap.
- Two pinned ESP-IDF v6.0.2 builds were byte-identical. One owner-authorized
  factory-app-only update wrote the exact 470,928-byte image at `0x010000`;
  no full erase or other partition write occurred. Exact read-only verification
  passed before reset.
- The owner observed the recognizable Trail logo followed by `BLE ADVERTISING`.
  A 16-second privacy-safe USB observation found boot self-check PASS, four
  heartbeats, and no failure/panic marker. Android found exactly one compatible
  service candidate without selection, connection, pairing, or identifier retention.
- This advances only the Heltec target milestone from 20% to 25%. Exact
  evidence-weighted V1 is 31.75% and displays as 32%; protected storage, secure
  GATT/Ready, LoRa, GNSS, interactive display/input, complete-unit, and field
  evidence remain open. See
  [OT-064 evidence](../tests/hardware/OT-064-2026-08-17.md).

## 2026-08-16

### OT-063 read-only protected-storage admission probe

- Replaced target placeholder security booleans with a typed, ordered,
  target-linked probe for configuration, named NVS-partition presence, selected
  HMAC_UP purpose, key read protection, and a private operational HMAC
  self-test.
- The current configuration short-circuits at
  `nvs_encryption_not_configured` before any partition, eFuse, or HMAC read.
  The probe contains no NVS initialization/open/write, key generation, eFuse
  programming, pairing, bonding, or GATT-admission path.
- Six strict host groups, the deterministic companion boot self-check at
  100/100, five target-only static admission groups, and two pinned ESP-IDF
  v6.0.2 builds pass. The builds reproduce a 440,240-byte BIN with SHA-256
  `0D064045D44D7F4D1120D164912CEAE9E1103ECE159E226B1CEEE2B11489B650`.
- This is `BUILD-LINKED-NOT-RUN` evidence. No build was flashed and no device,
  phone, port, emulator, or simulator was accessed. The physical OT-DEV-001
  remains on the accepted OT-061 image; secure GATT, protected storage, keys,
  bonds, authorization, and Ready remain denied. Historical V1 remains 31%;
  current V1 Companion and V2 Integrated remain unmeasured. See
  [OT-063 evidence](../tests/hardware/OT-063-2026-08-16.md).

### OT-062 exact Trail artwork and physical Android visual acceptance

- Added the owner-supplied 1774 x 887 Limited Underground Trail PNG
  byte-for-byte to the real Compose entry surface above the existing accessible
  brand text. It is fitted without a timed splash, sleep, delayed post, new
  navigation state, permission, service, or Bluetooth behavior.
- The isolated Android gate passes 136 JVM tests across thirteen suites
  (protocol 29; application 107), lint with `No issues found.`, manifest
  inspection, and debug assembly. The 12,236,702-byte APK has SHA-256
  `0E3A9C91E4AB68F0D6C45FB1D5A613CED7EE33154155AB2D0E76CE453F52918E`;
  the packaged 2,559,044-byte artwork matches the approved source SHA-256
  `A3024504BA261ADDAFD2A85F49F6BCE630D1E9AB994EEA348D5842A6D2AB7422`.
- That APK replaced the prior debug build in place on the physical Android
  13/API 33 phone. Cold launch succeeded, the owner visually accepted the
  artwork and framing, and Home/background plus reopen succeeded. No screenshot
  or device identifier was retained.
- This is Android entry-artwork evidence only—not an Android system splash,
  clean install, broad accessibility, secure GATT/Ready, Heltec OLED,
  touchscreen, release, endurance, or field proof. The historical V1
  calculation remains 31%; current V1 Companion and V2 Integrated scores remain
  unmeasured. See [OT-062 evidence](../tests/hardware/OT-062-2026-08-16.md).

### OT-061 first physical OpenTrail target and BLE advertisement

- Selected only `OT-DEV-001`; `OT-DEV-002` remained disconnected and
  untouched. Manual ESP32-S3 ROM entry/exit returned to the unchanged public
  MeshCore runtime before the owner authorized one full-chip erase and one
  write of the four frozen OpenTrail regions. No private flash backup was
  requested.
- Exact input hashes matched. The single erase/write completed, a separate
  `verify-flash` pass compared the four public written regions successfully,
  and the board remained in ROM until verification finished. No retry occurred
  and no additional write or recovery authority remains.
- One manual reset reached the deterministic boot self-checks, NimBLE runtime,
  and at least two five-second USB heartbeat records with no self-check, runtime,
  panic, abort, or assertion failure. The blank OLED is expected.
- The exact 9,677,165-byte Android APK (SHA-256
  `9CE206EEEAE2B13FC5C1092CEF41C226607FD3A9905A5797D4EBE31F3DC7F01C`)
  was rebuilt with 135/135 JVM tests and clean lint, installed on one physical
  Android 13/API 33 phone, and reported exactly one nearby compatible OpenTrail
  service. No candidate was selected, connected, paired, or identified.
- This is experimental physical target, bounded runtime, and BLE advertisement
  visibility evidence—not protected storage, GATT exchange, authorization,
  Ready, LoRa, GNSS, display, GPIO, support, or field evidence. The historical
  phone-independent evidence calculation advances to 31%; current V1 Companion
  and V2 Integrated release scores remain unmeasured. See
  [OT-061 evidence](../tests/hardware/OT-061-2026-08-16.md).

### OT-060 Android foreground screen retention and physical install

- Added Activity-window `FLAG_KEEP_SCREEN_ON` immediately after
  `MainActivity` superclass creation. It is active only while the Trail window
  is visible and adds no wake lock, permission, lock-screen behavior, brightness
  override, or foreground-service coupling.
- The Android-only gate passes 135 JVM tests across thirteen suites, lint with
  `No issues found.`, manifest inspection, and debug assembly. The 9,677,165-byte
  APK has SHA-256
  `9CE206EEEAE2B13FC5C1092CEF41C226607FD3A9905A5797D4EBE31F3DC7F01C`.
- One owner-authorized physical Android 16/API 36 handset installed and launched
  that exact APK. With the original USB stay-awake setting restored and a
  30-second timeout, the untouched visible Trail Activity remained awake,
  interactive, and at normal active brightness for 40 seconds. Backgrounding
  released focus and reopening succeeded.
- The same APK reached visibly fake Local test mode without a Bluetooth prompt
  or real BLE service. This is not BLE, LoRa, target-firmware, authorization,
  Ready, release, endurance, or field evidence. V1 remains 30%.
  See [OT-060 evidence](../tests/hardware/OT-060-2026-08-16.md).
## 2026-08-15

### OT-059 exact Heltec memory and recovery-layout build profile

- Replaced the stale generic 2 MiB/single-app/no-PSRAM build profile with an
  OT-DEV-001-bound configuration selecting 16 MiB flash, QIO/80 MHz, embedded
  2 MiB quad PSRAM at 80 MHz, boot initialization/memory test, and explicit
  capability allocation.
- Added an exact five-row partition layout: otadata, a 5,177,344-byte factory
  slot, two 5,242,880-byte OTA slots, and a 1 MiB application-owned state row
  ending at 16 MiB. This does not implement storage, OTA, updater, or recovery
  authority.
- Target-only admission passes 4/4. Two pinned ESP-IDF v6.0.2 builds reproduce
  exact configuration, partition binary, and artifacts. The 437,552-byte BIN
  has SHA-256
  `F0E81310C62CA0C17CA2531AF9B0D5BD5E6E115E1649F84C97514F72D51D6A3A`.
- No simulator gate was run, and no hardware, port, or flash was accessed.
  Physical profile behavior, recovery, authorization, and runtime remain open.
  OT-059 is `done`, OT-034 remains `partial`, and V1 remains 30%.

### OT-058 bounded simulator native-session pump

- Replaced the simulator UI's unbounded native-session notification behavior
  with bounded coalescing while preserving exact request ordering and the
  existing five-second failure timeout.
- The focused simulator gate passes 33 Core, 23 Windows bridge, 15 private
  helper, 10 native-protocol, and 13 integrated WPF groups. Full Test-Host exits
  0. This is local simulator evidence, not physical LCD, phone, BLE-device, or
  radio evidence. V1 remains 30%.

### OT-057 Android Group / Location presentation

- Added a renderer-neutral Group / Location presentation model with one card
  per bounded peer observation. Production Bluetooth truthfully reports
  coordinates unavailable because no accepted real BLE coordinate feed exists.
  Local test mode uses separate, visibly synthetic deterministic card data.
- No phone GPS, map, tile, network, location permission, or storage permission
  was added, and private device correlation remains outside presentation.
- The accepted gate passes 134 JVM tests across thirteen suites (protocol 29;
  application 105), lint with `No issues found.`, and debug assembly. The
  9,677,165-byte APK has SHA-256
  `697D73A6E48F1850A2756FB0886A8201C653804FB5A2B9628DD26790C8EC65B1`.
  No phone/device/install/live-location proof exists. V1 remains 30%.

### OT-056 bounded NimBLE runtime owner

- Added a fixed-memory one-connection runtime owner and real ESP-IDF v6.0.2
  adapter for exact startup ordering, protected GATT registration, service-only
  advertising-data fields, queued host callbacks, disconnect cleanup, bounded
  delayed re-advertising, watchdog containment, and exact host stop/deinit.
- OT-054 protected authorization storage remains denied. SC/MITM/bonding
  configuration is not usable-bond evidence; claims and normal commands remain
  closed, and every connection is immediately terminated so an unauthenticated
  peer cannot monopolize the sole connection.
- Thirteen strict owner groups pass at 100/100, target self-check passes
  100/100, static admission passes 3/3, pinned NimBLE indication/stop ordering
  passes, and two builds reproduce the 433,104-byte BIN with SHA-256
  `8A25508B50B29FE2A09CF3390AE53473BBA0BF04F60AE9A6366B930D516FCE2A`.
  All 123 native entries and full Test-Host pass. This is
  `CODED-BUILD-LINKED-NOT-RUN`, `NOT-FLASHED`, no-device evidence. See
  [OT-056 evidence](../tests/hardware/OT-056-2026-08-15.md). V1 remains 30%.

### OT-055 Android connected-device foreground-service owner

- Moved the production BLE facade, runtime, authorization controller, GATT
  leases, and timers into one non-exported `connectedDevice` foreground
  service. Only a visible explicit Bluetooth action can start it; it calls
  foreground disclosure before constructing BLE, returns `START_NOT_STICKY`,
  has no boot receiver/background auto-start, and never retries a claim
  automatically.
- The Activity now owns Local test state separately and observes the service
  through a bounded binder generation. Stop, rotation, and unbind release only
  that observation; explicit Bluetooth-mode exit stops the service, and service
  destruction closes the production graph exactly once. Notification denial on
  Android 13+ is reported as reduced drawer visibility rather than invented
  service failure or invisibility.
- The accepted gate passes 124 JVM tests across twelve suites (protocol 3, 10,
  6, and 10; application 8, 15, 17, 11, 2, 21, 1, and 20), lint with `No issues
  found.`, manifest inspection, and debug assembly. The 9,660,781-byte APK has
  SHA-256
  `33174B72792E2AFC0D03AB52DFAC6613BAE48618BF268C3197D7E04105897722`.
  It was not installed or run on Android, and the firmware service remains
  dormant, so no OS-service, notification, pairing, authorization, Ready, or
  live BLE evidence exists. V1 remains 30%.

### OT-054 protected authorization persistence prerequisite

- Added the exact fixed 32-byte `OAP0/v0` owner/tombstone record and an injected
  protected-store contract requiring fresh expected-generation comparison,
  atomic complete-record plus independently rollback-resistant floor commit,
  and exact readback. CRC is corruption detection only; post-write ambiguity is
  uncertain and keeps authorization faulted closed.
- Added an opaque private bond-reference plus generation boundary and a separate
  device-secret PRF seam for the 128-bit controller token. Public BLE address,
  public ID, peer name/value, and raw key material do not enter this path;
  re-pairing must allocate a new private reference or generation.
- Seventeen strict groups pass at 100/100, the deterministic target reboot
  self-check passes 100/100, static admission passes 3/3, all 122 native host
  executables pass, and two pinned builds are identical. The image is 175,701
  bytes; the 175,824-byte BIN has SHA-256
  `D39430096B7BEDD0F69D9ECCDE2424EDCD635C0BEA904EB2E4FCA3EEED307080`.
  Current target preflight denies because protected NVS/key/private-bond/PRF/
  rollback-floor prerequisites are unavailable. No target production code opens
  NVS or accesses eFuse/HMAC state, and nothing was flashed. See
  [OT-054 evidence](../tests/hardware/OT-054-2026-08-15.md). V1 remains 30%.

### OT-053 Android protected-read production composition

- Wired the accepted provisional authorization client into the explicit
  Bluetooth-mode production composition. There is no fake/local fallback.
  Android bond state remains only a prerequisite; successful protected exact
  20-byte `OTB0/v0.1` Protocol Info access is the device-enforced current-link
  security evidence.
- The composed path performs Protocol Info, advertised MTU 151, exact Stream
  indication subscription, one Claim Start, correlated Pending/terminal, and
  an explicit Snapshot Request only after Accepted/Replaced. Permission,
  disconnect, malformed/stale result, or lifecycle loss closes without
  manufacturing security or authorization success.
- The combined gate passes 101 JVM tests across ten suites (protocol 29;
  application 8, 15, 17, 11, 1, and 20), with zero failures, errors, or skips.
  Lint reports `No issues found.` The 9,644,209-byte debug APK has SHA-256
  `BE385FEB8966210C4C09027388C3F560745F6A075B9CBB1ABF25DC0893C0033C`.
  The target service remains dormant and unregistered, so no phone/device
  pairing, authorization, Ready, install, signing, or live BLE evidence exists;
  V1 remains 30%.

### OT-052 real NimBLE provisional callback adapter

- Added a fixed-memory callback owner and wired the real ESP-IDF v6.0.2 GATT
  registration, connection, security/MTU, exact Stream subscription, AUTHOR,
  indication completion/timeout, disconnect, and physical-decision seams to
  the accepted provisional lifecycle. The generated CCCD is discovered
  independently; no handle arithmetic is used.
- Protected Protocol Info and Command access re-read current device-side
  encryption, authentication, bond, 16-byte key, MTU, and private trusted
  binding evidence. Only authorization claims pass before promotion. Response
  capacity precedes mutation, and immutable connection/generation/session/
  exchange/value/token correlation plus pinned NimBLE teardown ordering closes
  stale completion after handle reuse.
- Ten strict callback-adapter groups at 100/100, target self-check 100/100,
  static admission 3/3, the pinned teardown-order test, all 121 native host
  executables, and two identical pinned builds pass. The image is 170,313
  bytes; the 170,432-byte BIN has SHA-256
  `22CAE43F7AEA9D980602C41E1ACEB49CA1174315EE87598D15E6717A27A1E4D4`.
  The service is `NOT-REGISTERED`, controller `NOT-STARTED`, advertising absent,
  and nothing was flashed. See
  [OT-052 evidence](../tests/hardware/OT-052-2026-08-15.md). V1 remains 30%.

### OT-051 Android provisional-authorization orchestration

- Added the separate exact 20-byte `OTB0/v0.1` decoder and a production-shaped
  runtime claim client. It requires encrypted/authenticated-bond evidence, reads
  Protocol Info at the default MTU, requests the advertised normal MTU, enables
  exact Stream indications, writes one Claim Start, and accepts only correlated
  Pending then terminal authorization frames. A claim-capable payload limit must
  be 28 through 128 bytes.
- Accepted/Replaced permits an explicit Snapshot Request; normal state is never
  inferred before promotion. Denial, timeout, permission loss, disconnect,
  malformed/stale input, and lifecycle release close the exact lease without
  automatic claim retry. Transport timeout remains local uncertainty, not an
  invented device denial.
- The combined gate passes 90 JVM tests across ten suites (protocol 6, 10, 10,
  and 3; application 7, 9, 17, 11, 1, and 16), with zero failures, errors, or
  skips. Lint reports `No issues found.` The 9,644,209-byte debug APK has
  SHA-256
  `28ED3014ACE420F8C531625211D26BD3FB9D522F1349BACA0878F94726534D8A`.
  `MainActivity` still injects the disabled claim client and the default
  security authority remains deny-all. No emulator, phone, peripheral, ADB,
  install, signing, or live authorization evidence exists; V1 remains 30%.

### OT-050 restricted device provisional authorization lifecycle

- Added exact `OTB0/v0.1` claim-capability evidence and a fixed-memory
  one-connection lifecycle binding trusted encrypted/authenticated-bond state,
  exact registered Protocol Info/Command/Stream/CCCD handles, MTU, provisional
  session, exchange, purpose, correlation, and distinct delivery tokens.
- Claim admission requires MTU at least 51 for the fixed 48-byte terminal
  envelope. Pending is reserved and confirmed before later physical/device-
  authority resolution; terminal capacity is reserved before authority
  mutation, and Accepted/Replaced promotes only after exact terminal indication
  confirmation. Normal traffic requires MTU 151. Exact 27-reject/28-accept
  payload-limit boundaries close Protocol Info coherence.
- Twenty strict groups and 100/100 repeats, the complete 120-executable host
  matrix, target self-check 100/100, static admission 3/3, and two pinned ESP-IDF
  v6.0.2 builds pass. The image is 165,349 bytes; the 165,472-byte BIN has
  SHA-256
  `E2ACF6672925D2FF298BD58E7C7BCBA564D46F1B7A6853D67865CE62F09D12B9`.
  The lifecycle is `BUILD-LINKED-NOT-RUN`; real NimBLE provisional access stays
  AUTHOR-denied, the controller/service/advertiser never starts, and nothing was
  flashed. See [OT-050 evidence](../tests/hardware/OT-050-2026-08-15.md). V1
  remains 30%.

### OT-049 Android authorization-wire parity

- Added pure Kotlin parity for the frozen `OTL0/v0` claim Start,
  `OTP0/v0` correlated Pending, and `OTF0/v0` terminal authorization records
  plus their dedicated `OTC0` kinds. Ten shared C++ vectors fix exact payload
  and envelope bytes.
- The externally serialized Kotlin tracker mirrors encrypted/authenticated and
  explicitly negotiated provisional evidence, exact transport generation,
  session, exchange, purpose, and correlation checks, pending-before-terminal,
  one terminal maximum, and exact-close-only connection release. Accepted or
  Replaced is only a client observation of an exact device result; it is not
  device authority or live transport evidence.
- The combined gate passes 77 JVM tests across eight suites (protocol suites 6,
  10, and 10; application suites 6, 17, 11, 1, and 16) and lint with no issues. The
  9,627,825-byte debug APK has SHA-256
  `967FCD7A032ECED63789378F5B3C0F6AC86D06CE9CF3B6B16205E7C49B8093A3`.
  The production claim client remains disabled. No MainActivity/BLE-runtime/
  GATT transport integration, emulator, phone, peripheral, ADB, install,
  signing, or field evidence exists; V1 remains 30%.

### OT-048 fixed authorization wire and response tracker

- Added exact fixed `OTL0/v0` (8-byte Start), `OTP0/v0` (24-byte Pending),
  and `OTF0/v0` (28-byte terminal) records under dedicated `OTC0` kinds
  `0x03`, `0x84`, and `0x85`. The device-issued 128-bit correlation is
  nonzero, boot-privately bound to the exact session/exchange/purpose, and is
  never an identity, address, credential, secret, display value, log value, or
  persisted value.
- A fixed-memory C++ tracker requires explicit future negotiated-support evidence
  plus an encrypted authenticated bond before provisional admission. It
  requires Pending before one exact Accepted/Denied/Replaced terminal result,
  rejects stale/wrong/duplicate/out-of-order input, keeps timeout as a local
  unknown-authority state rather than the wire Unknown denial enum, and requires
  its owner to call exact connection close before another transport generation
  may open.
- Fourteen strict groups, 100/100 repeats, ten shared vectors, and the complete
  119-executable host matrix pass. At OT-048 acceptance `OTB0/v0` had no claim
  capability, the GATT path required application authorization before
  negotiation, and no
  provisional GATT, authorization target/runtime integration, device access,
  or physical evidence exists. Because three shared sources are already target-
  linked, two pinned ESP-IDF v6.0.2 builds also reproduced the generic
  157,957-byte image (158,080-byte BIN) after adding exact kind recognition and
  normal-path rejection. The new wire/tracker source is absent from the link
  map and remains host-only; see
  [OT-048 target evidence](../tests/hardware/OT-048-2026-08-15.md). V1 remains
  30%.

### OT-047 device-authoritative Android authorization UX

- Added explicit authorize-this-phone and replace-lost-phone flows to Bluetooth
  mode. The UI instructs the user to operate the physical device within 30
  seconds and distinguishes Pending, Accepted, authoritative Denied, Replaced,
  local invalid-result, expired/unknown, and unavailable outcomes.
- Device-issued opaque claim tokens are bounded before any callback queue,
  matched to the exact purpose and controller generation, and never displayed
  or persisted. Timeout or malformed/lost result reports unknown device
  authority and requires reconnect/resync; it cannot claim rollback.
- Permission loss, mode switch, lifecycle stop/close, observer re-entry, stale
  callbacks, queue pressure, and synchronous timer completion release owned
  leases. If injected claim cleanup throws, the attempt is contained and local
  claim authority plus timer/runtime ownership still close independently. The
  production claim client remains disabled, so no bonding or live authorization
  occurs.
- The exact gate passes 66 JVM tests and lint. The 9,611,441-byte debug APK has
  SHA-256
  `3EB3986BD17F3DFC918936CF8978E44A36089E38D9CDCD877336BB9A16024C44`.
  No phone, emulator, ADB, BLE peripheral, or LoRa device was accessed; V1
  remains 30%.

### OT-046 one-phone authorization authority

- Added a fixed-memory, target-neutral device authority for one bonded phone.
  A trusted lower layer supplies a stable opaque 128-bit bond token that cannot
  be an address, public/client identifier, client value, or cryptographic key.
- Encrypted authenticated claims bind exact boot, strictly increasing session,
  and private controller challenges. Explicit physical claim, revoke, replace,
  and reset windows expire after 30 seconds and reject replay, clock rollback,
  invalid actions, a second controller, and stale sessions.
- An injected backend owns atomic compare/commit/exact-readback plus a rollback-
  resistant generation floor. Failed commit guarantees no durable change;
  uncertain/conflicted/rollback evidence latches closed. Status/results are
  redacted.
- Sixteen strict groups, 100/100 repeats, and the complete 118-executable host
  matrix pass. This is host-only, target-neutral, not build-linked, and has no
  BLE bond-store, persistence, physical-input, target, or device evidence; V1
  remains 30%.

### OT-045 explicit Android Bluetooth mode and lifecycle wiring

- Replaced the fake-only entry point with an explicit choice between Local test
  mode and Bluetooth-device mode. Bluetooth mode requests Android 12+ Nearby
  Devices permission, provides settings recovery, scans, selects one candidate,
  connects/disconnects, and exposes the accepted typed actions. It never falls
  back silently to local simulation.
- One controller/lifecycle binding owns runtime and facade startup, stop,
  resume, destruction, late permission callbacks, mode switches, observer re-
  entry, and exactly-once final cleanup. The production application-security
  authority remains deny-all, so the real path cannot reach Ready yet.
- Six envelope, ten semantic, six Android-policy, 17 BLE-runtime, 11 fake-
  controller, and nine new mode/lifecycle tests pass with clean lint. The
  9,595,057-byte debug APK has SHA-256
  `0CCD4DECAAAE712A587DB97BC744B515E97008532FAA834BBF3D7BE4C715D76C`
  and declares only Scan with `neverForLocation`, Connect, and AndroidX's
  generated same-app permission. No phone, emulator, ADB, BLE peripheral, or
  LoRa device was accessed; V1 remains 30%.

### OT-044 response-safe GATT session lifecycle

- Added a fixed-memory, target-neutral one-connection session owner with exact
  Command, Stream, and registered CCCD handles; ATT MTU; encrypted,
  authenticated, and application-authorized evidence; indication-only
  subscription; exact coordinator session; and one outstanding response token.
- The indication sink reserves the complete response before coordinator
  mutation. Congestion and guarded callback re-entry reject without mutation or
  state advance; wrong/stale completion preserves the exact pending response.
  Unsubscribe, security loss, submit failure, negative exact completion, or
  timeout contain and block; exact disconnect clears the terminal tombstone.
- Fifteen strict groups and 100/100 repeats pass. Target boot self-check and
  static admission pass 100/100; two pinned ESP-IDF v6.0.2 builds reproduce a
  157,957-byte generic image with a 158,080-byte BIN. Exact hashes are recorded
  in [OT-044 evidence](../tests/hardware/OT-044-2026-08-15.md).
- The result remains generic 2 MB, `BUILD-LINKED-NOT-RUN`, `NOT-FLASHED`,
  controller `NOT-STARTED`, advertising `NOT-IMPLEMENTED`, authorization
  `NOT-INJECTED`, and real NimBLE Command dispatch denied. No device or port was
  accessed; V1 remains 30%.

### OT-043 unwired Android BluetoothGatt facade

- Added a concrete Android 12+ Bluetooth facade behind the accepted lifecycle
  owner. It filters the exact service, bounds scan duration/results and opaque
  tokens, keeps callbacks on the main thread, validates the complete GATT
  profile, and uses indication-only Stream subscription.
- API 31/32 legacy and API 33+ value-safe GATT calls are version-gated. Every
  active state- or data-bearing queued scan/GATT callback rechecks its
  operation-specific Nearby Devices permission; disconnected cleanup remains
  best-effort. Revocation and contained `SecurityException` clear local
  ownership and emit typed privacy-safe failure without leaking an address or
  platform exception.
- Six base-protocol, ten semantic, six Android-policy, 17 BLE-runtime, and 11
  controller tests pass with clean lint. The 9,656,378-byte debug APK has
  SHA-256
  `CAAC3922EBC2BD011F12EE4A334DA98FBA1AE9467228C23174ED658F3F650AFE`
  and declares only Scan with `neverForLocation`, Connect, and AndroidX's
  generated same-app permission. The activity remains fake-only/unwired and
  cannot request Nearby Devices access; no emulator, ADB, or BLE device was
  accessed. V1 remains 30%.

### OT-042 dormant NimBLE GATT definition linked into the target

- Added the exact BLE Companion GATT v0 service and three characteristics plus
  one-connection Secure Connections-only NimBLE peripheral/GATT-server
  configuration to the generic ESP32-S3 target candidate.
- Registration requires injected application authorization and coordinator
  authority. The application does not register the service, start NimBLE or the
  controller, or advertise. Command writes are denied before coordinator
  mutation until exact registered CCCD handles and per-connection indication
  subscription/disconnect ownership exist.
- Static admission passes 3/3 and 100/100 repeats. Two pinned ESP-IDF v6.0.2
  builds reproduce a 155,061-byte image and retain all five companion/GATT/self-
  check objects. Exact hashes are recorded in
  [OT-042 evidence](../tests/hardware/OT-042-2026-08-15.md).
- The result remains generic 2 MB, `BUILD-LINKED-NOT-RUN`, `NOT-FLASHED`,
  controller `NOT-STARTED`, advertising `NOT-IMPLEMENTED`, authorization
  `NOT-INJECTED`, and runtime logging `UNREVIEWED`. No device or port was
  accessed; V1 remains 30%.

## 2026-08-14

### OT-041 lifecycle-safe unwired Android BLE runtime boundary

- Added a pure Kotlin owner for one scan/GATT/reconnect generation behind
  injected Android Bluetooth and timer facades. It fixes the four v0 GATT
  identifiers and orders encrypted/authenticated bond plus application
  authorization before MTU, Protocol Info, indication-only Stream subscription,
  and the initial authoritative snapshot.
- Global and negotiated fragment bounds, active session/action correlation,
  owner-thread callbacks, phase and result timeouts, reconnect exhaustion,
  stale generations, observer re-entrancy, and lifecycle stop/destroy fail
  closed; stop/destroy releases every scan, GATT, reconnect, and timer lease.
  A throwing presentation observer is detached without interrupting or
  orphaning the coherently owned transport.
- Six base-protocol, ten semantic, 17 BLE-runtime, and eleven controller tests
  pass in a clean isolated build; lint reports zero issues. The 9,545,745-byte
  debug APK has SHA-256
  `54C79FA4773A25704D1D33619B0AA93EED3CB7EA78E8B921A2D31FADEDD072BB`.
  The visible app remains fake-only and the manifest remains Bluetooth,
  location, internet, and storage permission-free. No Android Bluetooth facade,
  phone/device/emulator/ADB/BLE access, install, signing, or field evidence
  exists; V1 remains 30%.

### OT-040 companion coordinator linked into the build-only target

- Linked the accepted fixed-memory request coordinator into the generic
  `heltec_v4_bench` candidate and added a deterministic target-local boot
  self-check over fixed fake authorities.
- Exact action/result and snapshot/status envelopes, one prepare/commit
  application, response correlation, and byte-identical duplicate replay
  without a second authority call pass strict native checks and 100/100 repeats.
  Static target admission and the complete 116-executable host matrix pass.
- ESP-IDF v6.0.2 reproducibly builds a 148,949-byte application image with the
  protocol, semantics, coordinator, and boot-self-check objects retained. Exact
  artifacts are recorded in
  [OT-040 evidence](../tests/hardware/OT-040-2026-08-14.md).
- The result remains generic 2 MB, `BUILD-LINKED-NOT-RUN`, `NOT-FLASHED`, and
  `UNREVIEWED-RUNTIME`. The target path did not open or write a device; fixed
  fake authorities provide no NimBLE/GATT, real authority, radio, GNSS,
  storage, delivery, or physical evidence. OT-034 stays `partial`; V1 remains
  30%.

### OT-039 duplicate-safe device companion request coordinator

- Added one fixed-memory C++ owner above the accepted session guard and
  semantic codecs. It accepts only one complete client request, invokes
  injected snapshot/action authority, and returns an exactly correlated
  snapshot or action-result envelope.
- Actions use a pure prepare followed by one atomic commit. Output capacity and
  exact semantic/envelope response bytes are established before commit;
  non-successful commit guarantees no action was applied. Exact duplicate
  requests replay the cached response without calling authority again, while
  same-ID byte conflicts, stale/old sessions, and exhausted IDs fail closed.
- Sixteen strict groups plus 100/100 repeats cover response vectors, aliasing,
  duplicates/conflicts, queue-full/stale-alert results, terminal authority
  failures, unchanged output, and close/reopen isolation. The complete host
  matrix is now 115 executed C++ test binaries. No BLE stack, target adapter,
  radio delivery, hardware access, or physical result exists; V1 remains 30%.

### OT-038 Android semantic parity and typed fake workflow

- Added strict Kotlin `OTX0/v0`, `OTN0/v0`, `OTA0/v0`, and `OTR0/v0`
  codecs using nine shared C++ golden rows and exact single-fragment `OTC0`
  kind binding.
- The connected fake-only Compose surface now presents typed test status and
  submits all four quick statuses, an exact pending-alert acknowledgement, and
  explicit position-sharing Start/Stop. Fake session/exchange IDs are bounded
  and monotonic; result envelopes are correlated and round-tripped; queue,
  revision, session, and exchange exhaustion reject before mutation.
- Six envelope, ten semantic, and eleven app-state tests pass with clean lint
  and debug APK assembly. The current local APK is 9,914,201 bytes with SHA-256
  `8ED7B6C4789160CB7AD6BBC8BC43E914F73CB1F928BE23EE42D7D10A4A840F75`.
  UI outcome copy is explicitly fake/test-only and the main surface scrolls.
  No Bluetooth permission, adapter, device/emulator access, lifecycle-safe
  session, install, signing, or field evidence exists; V1 remains 30%.

### OT-037 companion codecs linked into the build-only target

- Linked the accepted `OTB0/v0`, `OTC0/v0`, `OTX0/v0`, `OTN0/v0`,
  `OTA0/v0`, and `OTR0/v0` C++ sources into the generic
  `heltec_v4_bench` ESP-IDF candidate.
- Added a boot-path self-check that compares exact fixed Protocol Info,
  action, and full envelope-plus-action vectors, decodes them, and requires the
  semantic dispatcher to accept the exact action-request binding. Success and
  failure logs are fixed; failure suspends before startup or heartbeat.
- Static admission passes 3/3 and 100/100 repeats. The pinned ESP-IDF v6.0.2
  build and a hash-stable incremental rebuild pass. The application image is
  145,657 bytes, 3,692 bytes larger than OT-034, and both companion source
  objects are present in the link map. Exact artifacts are recorded in
  [OT-037 evidence](../tests/hardware/OT-037-2026-08-14.md).
- The result remains `BUILD-LINKED-NOT-RUN`, `NOT-FLASHED`, and
  `UNREVIEWED-RUNTIME`. No device was accessed. The generic 2 MB/DIO/80 MHz
  profile, exact-board authority, recovery, BLE/GATT runtime, radio, GNSS,
  storage, and physical evidence remain open. OT-034 stays `partial`; V1
  remains 30%.

### OT-036 buildable Android client foundation

- Added a native Kotlin/Jetpack Compose Android project with a stable technical
  application/package identity and the visible working name `Limited
  Underground Trail`. The shell presents explicit Disconnected, Selecting,
  Connecting, Connected, and Failed states over a deterministic fake transport;
  it performs no Bluetooth discovery or device I/O.
- Added a pure Kotlin strict `OTB0/v0` and `OTC0/v0` codec. Its six tests consume
  the shared C++ golden bytes and cover exact vectors, every known frame kind,
  maximum fragmentation, malformed/version/reserve/enum rejection, invalid
  limits and identifiers, and rejection of payloads over 128 bytes before copy.
  Four application-state tests cover selection, connection, failure,
  disconnect, single-active-connection, and invalid-transition containment.
- Pinned Java 17, Gradle 8.11.1 with checksum, Android platform 35/build-tools
  35.0.0, AGP 8.7.3, and Kotlin/Compose 2.0.21. The focused gate passes all 10
  JVM tests, warning-as-error lint with no issue, and debug APK assembly. An
  independent audit confirmed the package/label/SDK bounds and no Bluetooth,
  nearby-device, location, internet, storage, or management permission.
- The controller is not Android lifecycle-safe, errors are fake-only strings,
  and there is no live BLE, real GATT/security session, semantic device binding,
  install, signing, store package, or physical evidence. No device was accessed,
  and V1 remains 30%.

### OT-035 fixed companion semantic payloads

- Added four exact brand-neutral, fixed-capacity semantic records above OT-033:
  the 8-byte `OTX0/v0` snapshot request, 32-byte `OTN0/v0` status snapshot,
  20-byte `OTA0/v0` user intent, and 20-byte `OTR0/v0` action result. A strict
  dispatcher rejects every record under the wrong `OTC0` frame kind.
- The snapshot carries only typed device-owned radio/GNSS/power/position state,
  queue count, revision, and optional exact pending critical-alert ID. Actions
  admit the four canonical quick-status IDs, exact-alert acknowledgement, and
  distinct position-sharing Start/Stop. Results separate local admission,
  device queue admission, and rejection; queued is never radio-delivery proof.
- Thirteen focused warning-free groups, 100/100 repeats, and the complete
  114-executable host/publication-safety gate pass. OT-035 itself contains no
  BLE stack, Android binding, target runtime owner, result cache, coordinates,
  text, message history, group secret, or device evidence. OT-037 later links
  these codecs into a build-only target without executing them. No hardware was
  accessed or written, and V1 remains 30%.

### OT-033 one-controller BLE companion protocol foundation

- Added brand-neutral `OTB0/v0` Protocol Info and `OTC0/v0` fragment codecs for
  a three-characteristic production GATT boundary. Fixed arrays, exact lengths,
  a 128-byte fragment payload, 16-fragment ceiling, and strict version/role/
  capability/kind/reserve validation keep the surface bounded.
- Added a one-controller session guard that requires injected encrypted-link,
  authenticated-bond, and application-authorization evidence. Exact private
  controller/session binding, monotonic non-wrapping session/request IDs,
  duplicate classification, stale rejection, and direction/single-fragment
  action rules fail closed.
- Fifteen focused warning-free host groups, 100/100 repeats, and the complete
  113-executable host/publication-safety gate pass. This is not a BLE stack,
  pairing method, Android app, state/action payload, target binding, radio/GNSS
  path, or physical-device result. No device was accessed or written, and V1 is
  unchanged.

### OT-034 build-only Heltec V4 ESP32-S3 target candidate

- Added a separate `firmware/targets/heltec_v4_bench` ESP-IDF candidate with a
  machine-readable `OTTB0` contract pinned to ESP-IDF v6.0.2 and `esp32s3`.
  The exact received revision remains unknown, support is false, and device
  writing is explicitly unauthorized.
- The application emits one fixed startup line plus a recurring USB
  Serial/JTAG heartbeat. Its only application-owned dynamic value is boot-local
  elapsed milliseconds, and it reads no device-specific identifier. ESP-IDF
  boot/runtime logs and the generated default partition table remain unreviewed
  build/runtime surfaces. The application does not initialize, access, or bind
  radio, BLE, Wi-Fi, GNSS, persistence, identity, secrets, GPIO, display,
  power, or the complete portable-client composition.
- Three host admission groups enforce those boundaries and reject device-write
  tooling. PowerShell, Python, JSON, and publication-safety checks pass. The
  pinned ESP-IDF v6.0.2 native build/size gate also passes: 141,965 application
  bytes, 86% of the application partition free, exact artifact hashes, and a
  hash-stable 8.05-second incremental rerun. The result is explicitly
  `NOT-FLASHED`; no device was discovered, opened, or changed. Its generic
  2 MB/DIO/80 MHz image header and NVS/PHY/factory table are not the observed
  16 MB/2 MB-PSRAM Heltec profile. Exact-board authority, profile rebuild,
  runtime-log review, manual ROM recovery, physical behavior, and support remain
  open. OT-034 is `partial`; V1 is 30%.

### Provisional Limited Underground Trail product-family names

- Accepted `Limited Underground` as the parent working identity and `Limited
  Underground Trail` as both the Android application and umbrella family.
  Essential identifies the screenless phone-required LoRa companion; Gold the
  one-touchscreen client; Platinum the two-display client; and `Limited
  Underground Trail Repeater` the optional repeater.
- Accepted `Limited Underground Firmware Loader` for the shared desktop utility.
  It remains visibly `Preview` and `Inspection only` until real firmware writing
  and recovery have passed their applicable physical acceptance gates.
- Decision 0008 keeps all names provisional pending professional clearance,
  prohibits `®`, and preserves the existing OpenTrail repository/folders,
  namespaces, `OT-*` records, protocols, GATT/schema/crypto/compatibility/board
  identifiers, and device IDs. No V1 evidence, support, firmware, hardware, or
  release claim changes.
- The warning-free 59-scenario Windows loader suite passed with the new title.
  A temporary self-contained 464-file package then built under the new archive
  and manifest identity, passed independent inspection-only verification, and
  was removed; no public release artifact was created.

### OT-030C shared bounded messages, Compose, and critical acknowledgement

- Extended the renderer-neutral C++ `PortableUiShell` and render plan with
  Message center, Inbox, Outbox, detail, fixed-template Compose, and
  confirmation screens. The compact `UiFrame` remains exactly 24 bytes; copied
  message/text presentation lives in a shell-owned `UiPresentationSidecar` for
  the exact offered frame, preserving the legacy embedded ABI and result-object
  memory budgets. The shared boundary owns newest-first ordering, selection,
  epoch-scoped read markers, read-on-successful-detail-presentation, eight fixed
  templates, and held acknowledgement of an exact active inbound critical
  alert; WPF remains only a renderer and action-slot adapter.
- Fixed the presentation snapshot at 12 messages and 96 printable-ASCII bytes
  per message. Four pointer-free owned-text slots carry visible copies. Text
  that cannot be represented has an explicit mutually exclusive truncated or
  unavailable state and canonical C++ presentation rather than an invented or
  partially trusted host string.
- Added native UI protocol v2 with a 4096-byte command and 8192-byte reply
  limit, newline completion, exact fields, uppercase hexadecimal text, one
  pending offer, and generation/revision/request/session/template/sequence
  correlation. Version 1, embedded NUL, partial EOF, oversized input, malformed
  hex or exceptional state, stale input, replayed evidence, and contradictory
  completion fail without applying portable state.
- Preserved transport authority below the shared presentation model. Queue
  admission returns the applied bridge-session epoch and message sequence;
  fixed-template completion requires exact newer matching evidence. Alert ACK
  requires an exact newer acknowledgement for the selected active critical
  message. Local loopback supports both flows. Live USB template chat and
  acknowledgement requests fail closed. The helper protocol can decode an
  inbound correlated `OTS0:A` observation, but it can advance only an already-
  outbound matching critical alert and provides no authentication or
  authoritative delivery evidence.
- The accepted warning-free focused gate passes strict C++ shell/render tests, 33
  Core groups, 23 Windows bridge groups, 15 private USB-helper groups, ten
  native-protocol groups, and 11 WPF groups. The integrated WPF run includes
  exact-once request handling and two native LCD sessions crossing one exact
  fixed-template message over local loopback.
- The Home, Compose, post-send Client A, and inbound-detail Client B renders
  passed representative visual review. After message presentation moved to the
  exact-offer sidecar, the complete expanded 112-executable host matrix and both
  publication-safety layers passed. Remote publication verification remains
  pending. OT-030C is `done` for this bounded host/shared-model increment;
  parent OT-030 remains `partial`, V1 stays at 29%, and no production packet,
  real LoRa, authenticated peer, physical LCD/input, target-firmware, clean-
  machine, installer, or supported-hardware result follows.

### OT-030B shared portable UI and bounded companion bridge

- Retired the OT-030A generic WPF application screens and moved portable Home,
  Status, Quick, Critical, Archive, recovery, and fault behavior into the shared
  C++ `PortableUiShell` and canonical fixed logical render plan. WPF renders
  offered primitives inside the 466 x 466 circle and maps input to exact action
  slots; host connection/evidence controls remain outside that surface.
- Added two-phase offer/present/commit with one pending offer, strict generation
  and revision checks, typed request completion, render rejection, recovery/
  fault arbitration, and close-time service draining. A failed presentation or
  stale input cannot mutate portable state or emit a request.
- Added passive VID/PID-only USB-candidate discovery. A recognized candidate is
  never opened or queried until explicit selection; open rechecks its private
  binding and exact allowlisted companion runtime. One endpoint cannot belong to
  both clients, assignment changes publish coherent A/B snapshots, and Forget
  removes the private association. The SenseCAP repeater role is excluded.
- Bounded helper/native process input, output, stderr, timeouts, cancellation,
  teardown, and restart. Ambiguous post-write failure is terminal. The live USB
  application admits only fixed quick-status and critical-alert requests;
  acknowledgement, arbitrary chat, archive, and position requests fail closed.
  The helper can decode an inbound correlated `OTS0:A` observation, which can
  advance only an already-outbound matching alert. This remains unauthenticated
  simulator-test evidence.
- The accepted focused gate passed 32 Core, 23 Windows bridge, 15 private
  helper, and 11 integrated WPF groups with warning-free Release builds,
  deterministic scale renders, and visual review. No hardware or serial port
  was accessed. The result proves shared host presentation and bounded bridge
  behavior, not a production OpenTrail packet, radio/peer delivery, target
  firmware, physical LCD/input, or supported hardware.

### OT-030A isolated dual virtual-LCD simulator shell

- Added one laptop-only .NET 8 WPF simulator process that opens separate
  Client A and Client B virtual-LCD windows over independently owned connection,
  outgoing-queue, message-history, alert-history, freshness, and error state.
- The current injected transport is explicitly local synthetic loopback. Two
  allowlisted simulated companions use private opaque endpoints with exclusive
  assignment; no USB enumeration/open, MeshCore command, reset, flash,
  configuration, network service, cloud, account, or persistent assignment is
  present.
- Fixed limits are 32 outgoing commands per client, 128 message entries, 32
  alert entries, and 64 pending observations per synthetic peer. Text is
  bounded/printable, full queues reject, stale state is visible, clock rollback
  faults, and one client disconnect/reconnect or close does not change the
  other.
- The LCD copy separates queued-local, accepted-by-local-bridge, observed-at-
  peer-simulator, and peer-simulator-acknowledgement evidence. The test-alert
  action is explicitly synthetic and does not claim the canonical held critical
  confirmation. A failed message request retains its draft.
- Focused Release runs pass 17 core groups and ten integrated WPF groups,
  including two-window construction, real core-presenter traffic, minimum 44 x
  44 target size at the 560 x 640 window minimum, theme round trip, classic
  disabled contrast, navigation, failed-draft retention, and one-window close
  isolation, plus close-time draining of an in-flight periodic service tick.
  Core repeats pass 100/100; the overlapping-command group passes 100 serialized
  Connect/Reconnect races with explicit post-await Dispatcher marshalling.
  Deterministic
  100%/125%/150%/200% bitmap checks pass; the 100% Client A home render received
  visual review with no layout blocker. The complete local `tools/Test-Host.ps1`
  gate exits 0 with the existing 111 C++ executables, 59 loader groups, both
  publication-safety layers, and this warning-free 17-core/10-WPF simulator
  gate. The aggregate loader roster is separate, pre-existing loader evidence;
  remote CI confirmation remains a per-push gate.
- OT-030 remains `partial`: live two-device USB/MeshCore binding, generation-
  safe reconnect, private remembered assignments, actual protocol/firmware,
  real LoRa, physical LCD/touch/readability/power/thermal/boot/recovery, real-
  monitor DPI/theme/assistive-technology acceptance, packaging, installer,
  signing, clean-machine, and supported-hardware evidence remain open. V1 stays
  at 29%.

### Shared Android-companion and standalone-touchscreen direction

- Accepted one-repository, shared-protocol direction for a future affordable
  Android companion using a separately approved mesh device and the original
  self-contained touchscreen client. The simulator is a shared behavior
  reference; platform and hardware adapters retain separate evidence gates.
- The Android path does not introduce a phone dependency into the existing
  four-self-contained-unit V1, and no current milestone changes. Android
  lifecycle/permissions/accessibility/signing/distribution and device approval
  remain open; iPhone and app-store direction are undecided.

### OT-020B host-only Wio GNSS Windows authority

- Added the Windows authority required below a future supervised Wio adapter.
  A bounded, no-overwrite DPAPI CurrentUser store owns one 32-byte HMAC key;
  separate HMAC domains derive an opaque target token and a session-scoped
  continuity token. The latter is not a device boot identity.
- A nonblocking per-target named mutex covers same-process and cross-process
  ownership. Busy, abandoned, raced, wait-failed, release-uncertain, or handle-
  close-uncertain paths fail closed; uncertain cleanup poisons the target for
  the process rather than permitting a second owner.
- Two reduced observations span a requested five-second quiet interval and
  require unchanged transport generation, plausible monotonic uptime, zero
  pending work, and no packet/airtime/flood/direct change. The prior-window
  authority gap is exactly bounded: 180 seconds passes and 180.001 seconds
  fails. Clock/read/wait failures and raw exception tracebacks remain private
  and latch the authority closed.
- Exact focused outputs were `PASS: 15 host-only Wio GNSS authority scenario
  groups` and, separately, `PASS: 15 host-only Wio GNSS lifecycle scenario
  groups`. The hardened complete host gate exited 0 with publication safety;
  Windows PowerShell parsing and the final diff check passed.
- OT-020B contains no serial, PnP, BLE, MeshCore, discovery, live-device, or
  mutable device-command surface. The complete gate's pre-existing read-only
  loader precheck separately observed the public one-Heltec/one-SenseCAP/one-
  Wio roster; it is not OT-020B device evidence.
- OT-020 remains `partial`, the Wio remains `experimented`, its missing
  shipping/pre-write state and entire live GNSS phase remain open, and V1 stays
  at 29%. No hardware compatibility, support, or regulatory claim follows.

### OT-020A host-only Wio GNSS lifecycle/recovery coordinator

- Added a deterministic host-only coordinator for a future supervised Wio GNSS
  pass. It requires an exclusive lease, exactly bound public target/firmware/
  role, location-safe policy, GPS read back off, zero pending work, and no
  existing recovery journal before it can request GPS on.
- The coordinator creates durable no-overwrite recovery authority before
  enable, resolves enable acknowledgement by readback, exposes only telemetry
  presence, and always attempts GPS-off restoration. It deletes the journal only
  after off readback and post-restore target rebinding; expected-record
  replacement/deletion rejects journal conflicts.
- Two settled post-restore counter reads must match the baseline with the same
  session-scoped continuity token, zero pending work, and no traffic delta. Reset,
  continuity loss, pending work, transmission, or an unsettled read invalidates
  the guard. An existing journal forces recovery-only handling that can verify/
  request off but cannot enable GPS.
- Exact focused output was `PASS: 15 host-only Wio GNSS lifecycle scenario
  groups`. The complete `tools/Test-Host.ps1` gate then exited 0 with that exact
  result, publication safety, all host matrices, and existing read-only loader
  acceptance. The tests use synthetic injected adapters only; no live adapter
  or device I/O, GNSS change, transmission, coordinate capture, BLE/DFU action,
  reset, or flash occurred.
- OT-020 remains `partial`, the Wio remains `experimented`, its missing
  shipping/pre-write state is preserved, and the entire live GNSS phase remains
  open. This adds no compatibility/support claim and leaves V1 at 29%.

### OT-019AI source-free minimum-window accessibility probe

- An independent source-free probe passed against the exact retained package
  and the public `Heltec V4 OLED`, `SenseCAP Solar`, and `Wio Tracker L1`
  roster. At 96 DPI, UI Automation Transform resized the packaged production
  window to exactly 900 x 620.
- The Wio card began offscreen at x=425, y=1085, width=342, height=635. Its real
  out-of-process `ScrollItem` provider moved it onscreen to x=425, y=254 without
  changing its size. Refresh focus, empty selection, disabled bundle/Flash
  authority, enabled Refresh, and the exact three-found/three-runtime-
  identified/zero-ready summary held.
- No `inspection-error` peer was present before or after. Cleanup left zero
  owned processes and zero owned temporary directories.
- The first integrated combined assertion exposed a verifier timing race: it
  could sample post-refresh state before the complete UI had settled. That
  failure did not establish a product defect. The verifier now waits for the
  complete roster, summary, selection, action, Refresh, and error state before
  evaluating the assertions.
- Three consecutive hardened runs—a two-repeat batch and one final reviewer-
  hardened run—then each passed exact package/roster verification, three UI
  Automation Refresh cycles, minimum-window `ScrollItem`, initial/selected/
  post-refresh no-error checks, privacy, manifest/hash/launch, and zero-residue
  cleanup. The complete `tools/Test-Host.ps1` gate and Windows PowerShell 5.1
  parse/publication-safety checks pass. OT-019AI is `done`.
- This adds no physical-input, Narrator, clean-machine, writer/recovery,
  hardware-compatibility, or V1-progress claim.

### Wio loader recognition and failed-refresh accessibility recovery

- Extended both current-tree privacy-safe loader paths to recognize the public
  Wio USB/runtime family without emitting local ports, private identity, raw
  replies, channel data, PINs, or coordinates. The warning-free C# suite now
  passes 59 groups.
- Three consecutive built-in production refreshes returned the expected current
  roster of one Heltec V4 companion, one SenseCAP Solar repeater, and one Wio
  Tracker L1 companion. All three were runtime-identified, zero were ready to
  flash, each refresh cleared selection/action state, and no mutation occurred.
- The new failure-to-recovery gate found a real accessibility defect: after an
  inspection failure recovered, the collapsed error peer could retain stale
  assertive text. Hidden error state now clears its text, Automation ID, and
  help, sets its live setting Off, and disappears from the recovered automation
  tree; only an actual current visible failure is assertive.
- Replaced the second-Heltec visual fixture with the current Wio card and
  regenerated all nine production-XAML images. Desktop, 900×620 minimum,
  scrolled/focused, deterministic high-contrast, and 125%/150%/200% review
  shows the longer Wio profile copy wrapping within its card with no horizontal
  clipping; at minimum width the third card remains vertically reachable.
- The complete 111-executable native host matrix, seven fixture-driven Wio
  preflight scenarios, updated Python loader/runtime/discovery groups,
  publication-safety scans, cross-tool signature vector, and warning-free
  59-group Windows suite pass from the integrated tree.
- RID-restored the accepted source into a replacement 464-file self-contained
  package. Independent verification matched every payload hash, enforced the
  inspection-only boundary, launched a fresh extraction, and completed three
  external UI Automation selection/Refresh/live-event/heading cycles while
  requiring the exact public roster `Heltec V4 OLED`, `SenseCAP Solar`, and
  `Wio Tracker L1`. The retained ZIP is 72,103,538 bytes with SHA-256
  `133A4E133A78D0CE789873B6E43226EAA455B59EDFF81D8DCF4369C172DED2C5`.
  A non-remediating Windows Defender custom scan inspected the exact archive
  with exclusions ignored and found no threats.
- This remains local engineering/package evidence. OT-020 stays
  `partial`/`experimented`; packaged failure injection, installer/clean-machine,
  code-signing, distribution, and physical update remain open, and the
  evidence-weighted V1 percentage is unchanged.

## 2026-08-13

### First privacy-safe Wio Tracker L1 USB/runtime pass

- Advanced OT-020 from `planned` to `partial` and classified the arrived owner-
  reported Wio Tracker L1 Pro only as `experimented`. Windows correlated its
  public USB model as `Seeed Wio Tracker L1`, family `2886:1667`, without
  retaining a transient COM assignment, device path, serial number, identity,
  channel value, PIN, BLE address, or coordinate. No `TRACKER L1` DFU volume was
  present in normal runtime; DFU was not entered.
- Recorded the owner's boundary that the unit was already flashed as MeshCore
  USB Companion and configured for a USA frequency plan before inspection.
  Shipping/pre-write firmware, role, settings, exact flasher target, and erase
  history therefore were not preserved or independently verified.
- A fixed read-only MeshCLI pass returned firmware `v1.17.0-727fc05`, build
  09-Aug-2026, repeat false, 910.525 MHz/BW 62.5 kHz/SF7/CR5/configured and
  maximum 22 dBm, a transient 4.111 V battery snapshot, and zero errors, queue,
  packets, airtime, and receive errors. Three additional cycles kept the public
  model, firmware, profile, and zero error/traffic state stable while uptime
  increased.
- GNSS was detected but inactive with no GPS telemetry, and no setting changed.
  A non-transmitting comparison with the connected Heltec reduced channel 0 to
  in-memory match booleans, found both default scopes unconfigured, confirmed
  distinct identities without emitting them, and found clocks within one
  second. No transmission, BLE, reset, flash, recovery, coordinate capture, or
  clean-machine validation occurred.
- Over-air interoperability, GNSS activation/fix/loss, exterior label/SKU/
  revision, antenna/RF/regulatory, power/endurance, BLE, and DFU/recovery remain
  open. Current-tree loader recognition subsequently passed locally on
  2026-08-14; replacement-package/external verification remains open. No
  supported/validated hardware or field-legality claim follows, and the
  evidence-weighted V1 percentage is unchanged.

See [the dated OT-020 evidence](../tests/hardware/OT-020-2026-08-13.md).

### Every disabled production button accepted through its rendered surface

- Replaced the palette-only/one-footer sampling with a shown-window six-state
  matrix: classic and deterministic high contrast, each in ready/unselected,
  ready/selected, and held busy-refresh states. It enumerates the exact disabled
  controls—three card Flash actions, footer Flash, conditional bundle selection,
  and busy Refresh—and measures each resolved foreground against its actual
  rendered `ButtonBorder` background.
- The first instance-level run exposed a real style-precedence defect: local
  button foreground values could outrank the shared disabled-state trigger even
  though the semantic resource pair passed. Removing those local values leaves
  one template responsible for enabled and disabled colors. Every measured brush
  is now opaque and matches the semantic pair at >=4.5:1 classic and >=7:1
  deterministic high contrast; refresh completion restores three unselected
  cards and keeps bundle/Flash authority blocked.
- Regenerating the retained evidence also exposed a harness-ordering artifact:
  captures taken after the shared fixture had been shown and resized could keep
  its 1120-pixel layout inside a 900-pixel bitmap. Baseline bitmap capture now
  occurs before that fixture is shown; fresh classic and high-contrast review
  again shows the intended two-plus-one minimum-width wrap without clipping.
- The warning-free suite remains 58 groups because this strengthens the existing
  contrast group. It proves resolved WPF control/template colors, not final pixel
  antialiasing, live switching through every Windows contrast theme, Narrator or
  Braille UX, installer lifecycle, or clean-machine operation.
- The production-XAML change was RID-restored into a fresh 464-file self-contained
  package. Its independent extraction, manifest/hash, source-free launch, and
  three Windows PowerShell 5.1 external UI Automation selection/Refresh/live-
  event/heading cycles pass. The retained archive is 72,103,021 bytes with
  SHA-256 `A6D60D0188F5DD7BA51B9792187080EEF4575F0B30653EF6AAC9AFE448CACAC2`;
  it remains Git-ignored and is not a public release. The contrast matrix itself
  remains local shown-window/template evidence, not packaged pixel evidence.

### Packaged heading hierarchy and reverse focus traversal

- Added semantic headings to the production XAML without changing its visible
  layout: the full utility title is level 1; current inspection, bundle, and
  safe-mode status are level 2; and each validated public device name is level
  3. The exact seven-heading sequence contains no internal candidate or
  transport identifiers and remains current through selection and refresh.
- Added an independent 900×620 reverse-focus path. With device three selected,
  Previous traversal is Refresh → enabled bundle action → the exact selected
  card → Refresh. With no selection, it skips the disabled bundle action,
  enters and leaves the list without selecting a device, and returns to
  Refresh. All three card Flash buttons and the footer Flash action remain
  disabled. This is deterministic WPF traversal, not physical Shift+Tab.
- The warning-free suite now passes 58 groups. A new 464-file self-contained
  package passed independent extraction, manifest/hash checks, launch, and
  three Windows PowerShell 5.1 external UI Automation selection/Refresh/live-
  event cycles. The external client also required the exact heading sequence
  before selection, after selection, and after every refresh. The retained
  archive is 72,103,101 bytes with SHA-256
  `0D1E82B978CA33DBAF912448D81936EB98FBBD4F51751F784F91AE9EEC715137`;
  it remains Git-ignored and is not a public release.
- Narrator/Braille heading commands, spoken order/verbosity, physical keyboard
  input, installer lifecycle, and clean-machine operation remain separate
  gates.

### Functional UI Automation scroll reachability

- Invoked the actual third device item's `ScrollItem` provider in a shown
  900×620 production window instead of only asserting that the pattern exists.
  The card begins wholly outside the outer content viewport with its UI
  Automation peer offscreen; the provider moves the real vertical offset into
  range, reveals the card, and changes the peer to onscreen.
- Scrolling leaves Refresh keyboard-focused, device selection empty, the
  privacy-safe item name/help unchanged, the zero-ready summary intact, and
  bundle selection plus the footer Flash action disabled. The warning-free suite
  now passes 56 groups.
- This is deterministic in-process production-peer invocation. Narrator,
  Braille/other assistive-technology UX, packaged out-of-process scrolling,
  physical wheel/touch/mouse input, virtualization or larger candidate sets,
  and clean-machine operation remain separate gates.

### Shown-window wide/minimum/wide resize transition

- Added a deterministic transition on the real shown production window from an
  effective wide size to the 900×620 minimum and back. Geometry must match the
  realized host capacity, and both window width and content viewport must change
  materially then restore. On the local 1920×1080 display, the exact
  1120×760 → 900×620 → 1120×760 run moves three 330-pixel cards from one row to
  the expected two-plus-one wrap and then returns to one row.
- The same last-card model, selected index, and generated keyboard-focused
  `ListBoxItem` must survive both transitions. The stricter unassisted run found
  minimum-size reflow could leave it below the viewport and drop keyboard
  focus. Resize now defers until layout settles, verifies the captured selection
  is unchanged, brings that item into view, and restores its focus. Focus moves
  to Refresh or a different nonselected card before settlement are preserved;
  the queued update does not reclaim them, and an inactive, hidden, or
  minimized utility never restores focus.
- Selection status, the item's public UI Automation name, bounded bundle-action
  availability, disabled Flash state, and all three exposed item peers remain
  intact without test-side scrolling after either resize; the vertical offset
  remains finite and within the current scrollable range. The warning-free
  suite now passes 55 groups. This does not prove physical resize input, a real
  monitor/DPI transition, Narrator behavior, or clean-machine operation.
- Rebuilt the corrected production source as a 464-file self-contained package.
  The builder launched an independent extraction, then Windows PowerShell 5.1
  repeated manifest/hash/capability checks, launch, three external UI Automation
  selection/Refresh cycles, and native live-region event acceptance. The new
  local archive is 72,103,016 bytes with SHA-256
  `6D6A487B23B44E67E8CCBC37F1FD61B001C2514C31600400613FE2609E5AB5F7`;
  it remains Git-ignored and is not a public release.

### Cross-process live-region delivery and Refresh focus continuity

- Subscribed a compiled native UI Automation client to the exact packaged
  window's `LiveRegionChanged` subtree. A compiled handler is required because
  callbacks arrive on a worker thread without a PowerShell runspace; the exact
  handler is removed before the owned process is closed.
- Three source-free cycles delivered the same privacy-safe sequence. Selection
  raised current bundle then current selection status. Refresh raised selection
  cleared, bundle waiting, inspection busy, bundle reset, the settled zero-ready
  summary, then the selection prompt. Event names matched the settled properties;
  unexpected IDs, missing/wrong order, callback errors, private identifiers, and
  stale selection events fail the verifier.
- Windows PowerShell 5.1 exposed one verifier encoding defect: its source-file
  ellipsis literal did not match the packaged Unicode event. The expectation is
  now built from code point U+2026, and all three cycles pass on 5.1.
- Extended routed keyboard acceptance to invoke F5 from the focused last wrapped
  device rather than from Refresh. Because refresh destroys the focused item,
  the window now restores focus to Refresh after success or failure. Success
  permits the next Tab into the newly generated list; failure leaves the error,
  empty list, disabled bundle/Flash actions, and enabled focused Refresh.
- The exact accepted 464-file package is 72,102,589 bytes with SHA-256
  `D065E1259A81803DC2BD535FC066E844E99864176FACCB770EE20C4BD0A5F734`.
  The warning-free 55-group suite and three live USB refresh cycles pass. This
  still is not physical keyboard input or Narrator speech acceptance.

### Source-free external UI Automation and refresh acceptance

- Added stable privacy-safe Automation IDs for the packaged window, Refresh,
  dynamic summary/selection/bundle regions, device list, bounded bundle action,
  and disabled Flash action. The in-process production-peer acceptance locks
  those IDs to their existing names, help, control types, and patterns.
- Extended the independent package verifier with an opt-in native Windows UI
  Automation client. From a unique source-free extraction it found the real
  packaged window, three ListItem/SelectionItem/ScrollItem device peers, public
  summary and blocker help, and disabled card/footer Flash actions. Candidate,
  COM-port, and MAC-like identifiers remained absent.
- The two same-model Heltec cards have intentionally identical public names,
  but WPF exposes their privacy-safe set positions as 1/3 and 2/3; the SenseCAP
  is 3/3. The verifier now requires every position and set size so assistive
  technology can distinguish otherwise identical peers without a private ID.
- Three external cycles selected a current item through SelectionItem, enabled
  only bounded bundle inspection, invoked the read-only Refresh action, then
  required empty selection, three republished devices, zero ready to flash,
  bundle selection disabled, and every Flash action still disabled. No file
  picker or mutation action was invoked. Each refreshed public-name multiset
  matches the initial privacy-safe roster, and owned process/temp cleanup passed.
- Windows PowerShell 5.1 compatibility reruns found that its legacy UI
  Automation client reports the newer WPF live-setting property as unsupported.
  The external verifier requires the Polite settings when its client exposes
  them and otherwise relies on the mandatory dynamic-name checks plus the .NET
  8 in-process live-setting regression; cleanup ran after both failed probes.
- The exact accepted 464-file package is 72,102,589 bytes with SHA-256
  `D065E1259A81803DC2BD535FC066E844E99864176FACCB770EE20C4BD0A5F734`.
  This is cross-process Windows UI Automation evidence on the current host,
  not physical keyboard/mouse, Narrator speech, installer, or clean-machine
  acceptance.

### Hardened source-free Windows utility package refresh

- Rebuilt the self-contained `win-x64` engineering ZIP from the source after
  the resize, scale, contrast, live-refresh, keyboard, automation-peer, and
  in-window theme-transition fixes. The builder independently extracted its
  own output, matched all 464 manifest-bound payload files, and launched the
  exact source-free executable.
- A separate verifier then repeated clean extraction, complete length/SHA-256
  matching, capability-boundary checks, forbidden-payload checks, and hidden
  launch verification. The retained archive is 72,102,589 bytes with SHA-256
  `D065E1259A81803DC2BD535FC066E844E99864176FACCB770EE20C4BD0A5F734`.
- The package remains local and Git-ignored. It is inspection-only, has no
  production signer, firmware, writer, erase/reset/DFU/recovery authority,
  private key, or debug/source payload, and is not an installer, signed build,
  clean-machine result, public release, or distribution claim.

### In-window theme-transition state and focus acceptance

- Exercised the shown 900×620 production `MainWindow` through classic →
  deterministic high contrast → classic using the same dynamic-resource path
  as the application theme owner. The last wrapped device remained selected
  and keyboard-focused at the same vertical scroll offset; its UI Automation
  name, enabled bounded bundle action, disabled Flash action, and zero-ready
  summary also remained intact.
- Assigned the explicit accessible focus visual to each selectable device
  container. Pixel review of the first transition render found that yellow was
  serving both as the high-contrast focus color and the selected-card outline.
  Focus is now white while selection remains yellow, preserving two distinct
  semantic cues in the deterministic palette.
- The first opt-in live-refresh rerun then found that an eager focus-style
  resource prevented the supported pre-application fixture window from loading.
  The item style now resolves that application resource dynamically, retaining
  the production visual while keeping both construction paths valid.
- Fixed classic and high-contrast transition renders were reviewed at the
  minimum layout, and the warning-free Windows suite now passes 54 groups.
  This is in-window WPF resource/state evidence; no Windows theme setting was
  changed, so real accessibility notification timing, every system/custom
  theme, physical input, and Narrator remain separate gates.

### Wrapped-layout device keyboard navigation

- Exercised routed production-window keyboard events against both the wide
  three-card row and the 900×620 two-plus-one wrapped layout. Native Right/Left
  and Down/Up navigation followed the actual card geometry while keeping focus,
  selected-device status, and bounded bundle availability synchronized.
- The first minimum-width run found native WPF End could remain on the first
  card because the horizontal wrap panel treated End as a row-edge operation.
  The device list now owns only Home/End: each selects, scrolls to, and focuses
  the true first or last current candidate. Native spatial arrows are unchanged.
- Home/End, wide Right/Left, wrapped Down/Up, and the previously accepted Tab
  cycle pass through the real shown `MainWindow`. Routed F5 then executes the
  same bounded refresh command, republishes all three cards, clears selection,
  disables bundle selection, and restores Refresh. This is routed WPF input
  evidence, not physical keyboard injection or Narrator acceptance.

### Production-window automation-peer semantics

- Inspected the actual UI Automation peers exposed by the production WPF
  window. The first run found device list items announcing the CLR model type
  with no blocker help, while mutable status regions exposed only fixed labels
  instead of their current visible messages.
- Moved validated device summary/help bindings onto each real `ListBoxItem`.
  The three item peers now expose the expected list-item and selection-item
  contracts, omit internal candidate ordinals from their name/help, and UI
  Automation selection reaches only the existing bounded device-selection path.
- Summary, safety notice, selection, bundle, and error peers now expose their
  current visible messages with explicit supporting help. Refresh exposes the
  Button/Invoke contract; the list exposes single optional selection; disabled
  Flash remains disabled while exposing its name and blocker help.
- The warning-free Windows suite now passes 53 groups. This is real
  production-peer evidence, not proof of Narrator wording, timing, verbosity,
  Braille output, or another assistive technology on a physical desktop.

### Production-window Tab focus acceptance

- Opened a second real production `MainWindow` in the STA acceptance runner and
  exercised WPF keyboard focus traversal after publishing and selecting one of
  the three validated fixture cards.
- The first run found that Tab entered noninteractive blocker `ItemsControl`
  content instead of leaving the device list. The blocker list is now removed
  from the Tab sequence and the connected-device list is one Tab group.
- The accepted order is Refresh, the current connected-device list selection,
  the enabled bounded bundle action, then back to Refresh. Both disabled Flash
  actions are skipped. The warning-free Windows suite now passes 52 groups.
- This is production-window focus-manager evidence. Physical Tab/arrow input,
  focus appearance under every Windows theme, Narrator announcements, and
  assistive-technology review remain separate gates.

### Focused Windows utility visual and live-refresh follow-up

- Rechecked the production `MainWindow` at 900×620 and 1600×900, its scrolled
  minimum view, and 125%, 150%, and 200% deterministic DPI profiles. Wrapping,
  fixed footers, selection state, and scroll access remained intact.
- The classic disabled-button palette measured only 3.79:1 against its actual
  button face. Darkening the semantic disabled-text brush from `#5A5A5A` to
  `#404040` raises that production pairing above 5.5:1; the acceptance runner
  now enforces at least 4.5:1 on the real disabled Flash control.
- Added an opt-in production-window live-refresh run. Three consecutive reads
  through the packaged Windows USB/runtime adapter each republished the three
  connected bench candidates, kept zero ready to flash, cleared selection,
  disabled bundle selection, and restored Refresh without exposing private
  device data or adding mutation authority.
- The direct Windows interaction bridge could not enumerate windows on this
  host because of an OS permission error. Source focus semantics and automation
  properties plus production-window Tab focus-manager traversal now pass, but
  physical Tab/arrow input, Narrator, live theme switching, and physical
  monitor-to-monitor DPI acceptance remain open.

### System-aware contrast palette and deterministic theme acceptance

- Replaced hard-coded production control colors with named dynamic brush
  resources while preserving the approved classic Windows 95 palette as normal
  mode. Header, information, warning, critical, selection, success, focus,
  disabled, and button surfaces now have explicit semantic pairings.
- Added a bounded theme owner that maps high-contrast mode to the current WPF
  `SystemColors` window/text, control/text, highlight/highlight-text, and
  disabled-text pairs. Application startup reads `SystemParameters.HighContrast`;
  accessibility, color, visual-style, window, and general preference changes
  cause the current system palette to be read again on the UI dispatcher.
- Added one deterministic black/white/yellow contrast profile through the same
  production resource-application path. The test requires at least 7:1 text
  and disabled-label contrast, proves resources reach the real Refresh and
  disabled Flash controls, retains three cards plus the current selection and
  scroll viewport, and rejects a blank/collapsed render.
- Pixel review accepted the 900×620 contrast render: selected state remains
  visible through a yellow border, every key status/safety line is readable,
  the bundle and safe-mode boundaries remain intact, and both footer buttons
  retain legible labels. The classic render is restored afterward.
- The warning-free Windows suite now passes 51 groups. Live activation of each
  built-in/custom Windows contrast theme, keyboard/Narrator, and clean-machine
  acceptance remain open rather than inferred from deterministic rendering.
- A fresh 464-file self-contained `win-x64` engineering package independently
  passed manifest/hash and source-free launch verification. It is 72,101,915
  bytes with SHA-256
  `CC47BB6BB3BDD952B6717861F71AA1A067ED08D486314FC98888B86614C2395E`;
  it remains local, ignored, inspection-only, and not a public release.

### Explicit per-monitor DPI configuration and scaled-render acceptance

- Added a production application manifest that declares Windows
  `PerMonitorV2` awareness plus the older `true/pm` fallback. The project now
  embeds that exact manifest instead of relying on an implicit runtime default.
- Extended the real-`MainWindow` STA acceptance path with minimum-window
  renders at 125%, 150%, and 200% pixel density. Each profile preserves the
  900×620 logical layout, produces the exact scaled bitmap dimensions and DPI,
  retains nonblank production pixels, and keeps Refresh, firmware selection,
  and the scroll viewport measurably available.
- Pixel review accepted all three scaled renders. Header and inspection state,
  selected-device copy, two reachable device cards, bundle blocker, safe-mode
  boundary, and disabled Flash label remain readable. This is deterministic
  high-DPI evidence only; real monitor-to-monitor movement, Windows theme/high-
  contrast switching, keyboard/Narrator, and clean-machine acceptance remain
  open.
- The warning-free Windows suite now passes 50 scenario groups. Its live
  read-only precheck remains two Heltec companions plus one SenseCAP repeater,
  three runtime-identified and zero ready to flash; no maintenance or radio
  mutation occurred.
- A fresh 464-file self-contained `win-x64` engineering package independently
  passed manifest/hash and source-free launch verification. It is 72,098,493
  bytes with SHA-256
  `DBB746434C8EABF1DE772913091A353502DCE9D8609427A6B51DD117BE4D0EC4`;
  it remains local, ignored, inspection-only, and not a public release.

### Production-window repeated refresh/selection state acceptance

- Added an internal dependency seam that preserves the public WPF constructor
  while allowing the production window to consume a controlled, validated,
  read-only inspection source during host acceptance. No test-only transport,
  writer, reset, or device authority enters the application.
- The real `MainWindow` now completes three refresh/selection cycles in one STA
  session. Each refresh republishes three cards, clears the prior selection,
  disables bundle selection until one current card is selected, restores the
  Refresh control, and preserves the zero-ready-to-flash summary. Each new
  selection re-enables only bounded local bundle inspection and binds its
  visible status to that current card.
- The cycle exposed one stale footer message: after inspection succeeded but
  before a card was selected, it still said device inspection was pending. The
  production window now shows `No firmware bundle selected`, then switches to
  current-device wording only after selection.
- The warning-free Windows suite now passes 49 scenario groups, and the live
  read-only precheck still reports two Heltec companions plus one SenseCAP
  repeater, three runtime-identified and zero ready to flash. Real clicking,
  keyboard traversal, and live repeated-refresh acceptance remain explicitly
  open rather than inferred from this deterministic result.
- A fresh 464-file self-contained `win-x64` engineering package independently
  passed manifest/hash and source-free launch verification. It is 72,098,580
  bytes with SHA-256
  `A47CAE1B13F99598927FE26D6F5D5EA61BE2950674ABD599FBC9468E6ED3CC60`;
  it remains local, ignored, inspection-only, and not a public release.

### Deterministic Windows loader rendered-layout acceptance

- Added an opt-in STA WPF renderer to the existing warning-free loader test
  executable. It composes the real production `MainWindow` with a validated,
  privacy-safe three-device document and captures desktop, minimum-window, and
  scrolled minimum-window pixels without opening a device transport.
- Pixel review at 1600×900 and 900×620 exposed and fixed two production-XAML
  defects: a transparent content root could hide the summary against a black
  backing surface, and device cards stayed in one clipped horizontal row at
  minimum width. The root now owns the classic gray surface and cards wrap to
  the constrained visible width.
- The accepted renders show all three cards at desktop size, two-plus-one
  wrapping at minimum size, vertical access to the third card, a clear selected
  state, fixed bundle/safe-mode footers, and readable disabled Flash labels.
  This is rendered layout/resize evidence only; Narrator, keyboard traversal,
  high-DPI/system-theme, repeated live refresh, and clean-machine acceptance
  remain open.
- The full 111-executable host matrix, Python/publication-safety checks,
  cross-tool signature vector, and 48 Windows loader groups pass. The read-only
  runtime check still reports three inspected, three runtime-identified, and
  zero ready to flash; no radio was reset, written, or placed in maintenance
  mode.
- A fresh 464-file self-contained `win-x64` engineering package independently
  passed manifest/hash and source-free launch verification. It is 72,098,432
  bytes with SHA-256
  `96720DD25C63151F303522A07D93D65315CC2CC0ED380BEF62D159A47B92CDC6`;
  it remains local, ignored, inspection-only, and not a public release.

### Exact selected-device bundle-match boundary

- Added one pure matcher between the explicitly selected current device and the
  inspected firmware manifest. It compares only exact hardware-profile ID,
  processor, product role, received board revision, bootloader schema, and
  image-size capacity from a separate authoritative device profile.
- Runtime model names, USB family, installed MeshCore role, vendor-family
  baseline, and the visible profile-candidate label cannot manufacture that
  authority. The two Heltec cards and SenseCAP card therefore remain
  `Exact-device match unavailable`.
- Exact field mismatches fail independently. A complete match proves only that
  the selected received-unit profile fits the signed manifest; it does not
  imply signer approval, release-generation admission, destructive consent,
  writer availability, or Flash permission.
- The warning-free Windows suite now passes 48 scenario groups against the live
  three-device inspection result. No device was reset, erased, written,
  rebooted, or placed into maintenance mode; V1 progress is unchanged.
- A fresh 464-file self-contained `win-x64` engineering package independently
  passed manifest/hash and source-free launch verification. It is 72,098,162
  bytes with SHA-256
  `8AB5CA9FF9FE0348AB23E5ACDE84BF2CD84F9F82B9EB5D48338F92BE6F7A3510`;
  it remains local, ignored, inspection-only, and not a public release.

### Explicit current-device selection boundary

- Replaced the passive connected-card list with a keyboard-accessible,
  single-selection list. The selected Windows 95-style card receives a visible
  navy border and a live status message that explicitly says selection is not
  Flash permission.
- Added a pure selection authority that accepts only reduced generic candidate
  ordinals from the current validated snapshot. It stores no COM port, hardware
  identifier, serial number, device identity, or pairing data; unknown or stale
  ordinals fail closed.
- Every refresh and window close clears selection. Changing the selected card
  invalidates an in-flight or displayed bundle result, and local bundle
  selection remains disabled until both the current device snapshot and one
  explicit selection exist.
- All Flash controls remain disabled, and no admission consumer, writer, reset,
  erase, reboot, or recovery adapter was added. The
  warning-free Windows suite now passes 47 scenario groups; V1 progress is
  unchanged.
- A fresh 464-file self-contained `win-x64` engineering package independently
  passed manifest/hash and source-free launch verification. It is 72,094,172
  bytes with SHA-256
  `A38946C82CCC8F55A3BEAAE3083DF3EC923E700F0F7798EE45B5F4C90FEC8BDE`;
  it remains local, ignored, inspection-only, and not a public release.

### Device-snapshot-bound bundle inspection

- Added a separate revision authority that binds every local firmware-bundle
  inspection to one current connected-device snapshot. Device refresh begins
  by discarding the prior bundle display and invalidating any in-flight result;
  only the current inspection can publish and complete once.
- Bundle selection now starts disabled, becomes available only after a valid
  device inspection, and remains blocked after a failed/timed-out refresh.
  Window close invalidates both snapshot and bundle authority.
- This closes a stale-state UI boundary only. Exact-device matching, firmware
  admission, signer approval, and every writer/reset/recovery path remain
  absent. The warning-free Windows suite now passes 46 scenario groups.
- A fresh 464-file self-contained `win-x64` engineering package independently
  passed manifest/hash and source-free launch verification. It is 72,092,522
  bytes with SHA-256
  `364B32BA1431DC79BDEEF43579817F009C537769A0619C908001F5B9BCD522B9`;
  it remains local, ignored, inspection-only, and not a public release.
- The preceding public Windows run passed the complete C++/Python/privacy
  matrix, then exposed one checkout-only defect: Windows converted the strict
  public signature-vector fixture to CRLF. A narrow repository attribute now
  forces that exact fixture to LF on every checkout; the canonical-vector test
  remains strict rather than accepting changed bytes.

### Heltec maintenance-failure recovery observation

- Performed one supervised `--no-stub` read-only ESP32 `chip-id` attempt on
  assembled bench client `OT-DEV-002`. Host serial configuration failed before
  the ROM handshake; no stub, flash read, erase, write, eFuse, or firmware
  action occurred, and the attempt was not repeated.
- The selected Heltec temporarily stopped answering its normal MeshCore runtime
  query. After USB reconnection/re-enumeration, all three bench candidates were
  again runtime-identified and zero were ready to flash.
- The recovered unit retained its MeshCore firmware, companion role, repeat
  setting, and complete radio configuration; it reported 0 errors, an empty
  queue, and 0 receive errors. This is manual recovery evidence for one unit on
  this host, not a working automatic ROM-entry or supported-board claim.
- See [OT-019O](../tests/hardware/OT-019O-2026-08-13.md). Exact low-level
  hardware identity remains unresolved, and V1 progress is unchanged.

### Fail-closed maintenance-attempt safety gate

- Added an explicit one-attempt-per-session maintenance policy for the future
  low-level board profiler. A recognized device cannot enter that future path
  until the operator confirms disruption, and a consumed attempt can never be
  retried in the same session. A failed attempt requires normal runtime
  recovery to be verified before any later session is considered.
- Updated both inspection producers and the Windows 95-style device cards with
  a visible recovery warning. Heltec guidance now says to stop after an
  automatic-reset failure and reconnect USB when needed; SenseCAP guidance
  similarly requires both normal enumeration and runtime inspection after a
  failed maintenance entry. Unknown hardware receives zero attempt authority.
- This is a pure policy and presentation boundary. The utility still has no
  reset, line-toggle, esptool, DFU, erase, write, reboot, or recovery adapter,
  and every Flash action remains disabled.
- Four Python loader-view groups and all 45 warning-free Windows loader groups
  pass. The canonical V1 percentage is unchanged because no authoritative
  received-board profile, physical update, rollback, or recovery gate closed.

### Candidate-only hardware profile guidance

- Added a strict hardware-profile evidence object to both the development and
  source-free Windows inspection paths. A recognized card now separates what
  the connected runtime proved, the published vendor-family baseline, and the
  deliberate maintenance step still required.
- The two live Heltec cards resolve only to the `Heltec WiFi LoRa 32 V4 family`
  candidate; the SenseCAP resolves only to the `SenseCAP Solar Node family`
  candidate. Every result is visibly labeled `Runtime candidate only` and
  `authoritative_for_flash=false`. Unknown USB devices receive no automatic
  restart offer.
- Official Heltec documentation supplies the V4 ESP32-S3R2/16 MB/SX1262/OLED
  family baseline. Official Seeed documentation supplies the Solar Node XIAO
  nRF52840 Plus/Wio-SX1262 baseline and notes that P1-Pro adds L76K GNSS. These
  are reference specifications, not measurements of the enclosed received
  units.
- Repeated the live privacy-safe run with both Heltec companions and the
  SenseCAP repeater connected: `3 found · 3 inspected · 0 ready to flash`.
  No reset, line toggle, bootloader entry, erase, write, identity capture, or
  coordinate capture occurred. A future low-level probe remains an explicit
  operator-approved maintenance action.
- The warning-free Windows suite now passes 45 scenario groups and the four
  Python loader-view groups pass. The Windows 95-style cards compile with the
  new profile panel and accessible summary.
- Fixed the package builder/verifier to run under both Windows PowerShell 5.1
  and PowerShell 7 by using portable BOM-free UTF-8, relative-path, and process-
  stop APIs. A fresh source-free package independently passed manifest/hash and
  launch verification: 464 payload files, 72,089,805 bytes, SHA-256
  `49351D2B963BB5E765D8EC20B4D20A5252664193E2C5CF70DBEF029F8419E15D`.
- A direct rendered acceptance attempt was not counted: the Windows app-control
  helper failed before app selection with `EPERM` on the Codex installation
  path. Its exact temporary extraction was removed. Final layout, resize, DPI,
  and assistive-technology acceptance remain open, and V1 progress is
  unchanged.

### Fixed cross-tool firmware-signature vector

- Added one public-only verification fixture containing an exact 435-byte
  canonical manifest, RSA-3072 public SubjectPublicKeyInfo, and 384-byte
  signature. Its private key was generated only in memory and was never written
  to the repository, package, logs, or retained evidence.
- The real Windows loader verifies that exact vector through its pinned public-
  key catalog. The same manifest/signature passes OpenSSL 3.5.6 and Espressif
  `espsecure` 5.3.1 Secure Boot v2 verification with a 32-byte PSS salt;
  `cryptography` 50.0.0 supplies the pinned Espressif runtime dependency.
- Both OpenSSL and Espressif reject a changed manifest. The cross-tool verifier
  reconstructs only temporary public files and a Secure Boot v2 signature block
  under one automatically removed system-temporary directory.
- Added the fixed vector to the warning-free C# loader run, raising that suite
  to 44 groups. Added pinned cross-tool requirements and the cross-tool command
  to the complete Windows host-validation workflow. Public CI confirmation
  still requires a later commit/push and successful Actions run.
- This closes the initial .NET/OpenSSL/Espressif interoperability and explicit
  salt-length evidence gate. Independent security review, production signing-
  key custody, approved public-key pinning, revocation/generation state,
  authoritative board matching, admission composition, and every write/
  recovery/target gate remain open. The canonical V1 progress record is
  unchanged.

### RSA-PSS firmware-bundle verification boundary

- Selected exact RSA-PSS-3072/SHA-256 signatures over the canonical firmware-
  bundle manifest. The signed manifest already binds image length/SHA-256,
  hardware profile, processor, target role, revision range, bootloader schema,
  and release generation; the detached signature is exactly 384 bytes.
- Added an immutable public-key catalog capped at three RSA-3072 keys with
  exponent 65537. Its 16-character signer ID is derived from the public-key
  SubjectPublicKeyInfo for lookup only; verification always uses the complete
  pinned public key.
- Kept firmware release, radio identity/group transport, and target Secure Boot
  key roles explicitly separate. No private signing key is present anywhere in
  the repository, utility, package, log, or test artifact.
- The packaged signer catalog is deliberately empty pending approved offline
  key custody, rotation/revocation, and release procedure. Therefore the app can
  inspect a candidate but cannot trust, admit, or write it.
- Ephemeral-key tests prove matching verification plus fail-closed empty/
  unrelated catalogs, changed signature, changed signed manifest, and wrong
  RSA key size. The warning-free Windows suite initially passed 43 scenario
  groups and now passes 44 with the fixed vector. Production key material, admission composition,
  exact-board authority, and all write/recovery evidence remain open. This does
  not change the evidence-weighted V1 percentage.
- Built and independently reverified a new 464-file self-contained Windows ZIP
  from a fresh extraction. It is 72,087,792 bytes with SHA-256
  `9B6847E6E2DA1569B499551F3F6E542233BF96CB2338097DDD2656F20B54D754`.
  Its capability manifest declares the verifier, no configured production
  signer, no protected revocation state, no signature admission, and no write
  or recovery authority. The launch smoke test passed; this remains a local
  engineering package rather than an installer or public release.
- Strengthened the publication-safety command to scan both tracked content and
  nonignored untracked content. A newly written document or source file is now
  checked before its first commit instead of becoming visible to this guard
  only after Git starts tracking it.

### Bounded Windows firmware-bundle candidate inspection

- Enabled the Windows 95-style shell's `Select firmware bundle` action for
  local `.fwbundle` files. Selection is inspection-only: the chosen path and
  filename are not displayed or retained, no archive content is extracted, and
  every Flash action remains disabled.
- Added a bounded three-entry ZIP adapter requiring exactly `manifest.json`,
  `image.bin`, and `manifest.sig`. It caps the archive at 20 MiB, the canonical
  manifest at 4 KiB, the image at 16 MiB, and the RSA-3072 signature at exactly
  384 bytes; duplicates, extra entries, alternate names, and all-zero
  signature bytes fail closed.
- The manifest uses exact compact UTF-8 bytes, fixed property order/types,
  strict processor/role values, nonzero profile/release/image fields, lowercase
  digest/signer hex, and its own exact canonical byte count. The complete image
  is streamed through SHA-256 and must match both its declared length and
  manifest digest.
- A positive packaged result says only that candidate structure and image
  SHA-256 were verified because no production signer is configured. The
  subsequent RSA-PSS increment adds verifier/test evidence without production
  trust, admission, exact-device matching, or device mutation authority.
- Seven initial candidate-format groups raised the Windows suite to 38 C#
  scenario groups; the subsequent signature increment raises it to 43. The
  warning-free live run still identifies both Heltec V4
  OLED companions and the SenseCAP Solar repeater, with zero ready to flash.
  This does not change the evidence-weighted V1 percentage.

### Source-free Windows MeshCore runtime identification

- Replaced the packaged utility's generic-only serial-map view with direct
  Windows SetupAPI Ports-class discovery. This path does not use the laptop's
  access-denied CIM/PnP inventory query, Python, MeshCLI, a shell, or a network
  service. Exact allowlisted USB VID/PID pairs and COM names remain private to
  the probe and are never serialized or shown.
- Added bounded native 115200-baud inspection for two fixed runtime shapes. The
  companion path sends only MeshCore app-start and device-info requests and
  skips the private BLE-PIN bytes without decoding them. The repeater path sends
  only `board`, `ver`, and `get role`. There is no arbitrary serial command,
  line-toggle, reset, erase, bootloader, or firmware-write API.
- The live built-in C# path independently reports three connected candidates,
  three runtime identities, and zero ready to flash: two `Heltec V4 OLED`
  MeshCore USB companions and one `SenseCAP Solar` MeshCore repeater. The public
  result contains no ports, serials, hardware-instance paths, raw replies,
  pairing data, device identities, keys, PINs, or coordinates.
- Runtime identity is explicitly non-authoritative for the enclosed received
  hardware. Exact processor/memory, hardware profile, product target role,
  board revision, bootloader schema, RF variant, antenna, and regulatory gates
  remain blocked. The current 45-group C# suite and warning-free build pass.

### Local self-contained Windows utility package

- Added a repeatable `win-x64` package builder that performs the required
  runtime-specific restore before a self-contained .NET 8 WPF publish. Its
  repository-scoped NuGet configuration clears external sources, and all build,
  staging, extraction, and launch work occurs under one exact temporary tree.
- The retained ZIP contains 464 manifested payload files. The manifest records
  each exact length and SHA-256 hash, states that the display name is a working
  identity pending attorney review, and fixes the capability boundary to
  read-only connected-device inspection plus bounded local bundle-candidate
  structure/image-SHA-256 inspection. It explicitly denies signature trust,
  authoritative hardware-profile status, and firmware writes. Package admission
  rejects source, firmware images, writer/recovery tools, private keys, debug
  symbols, and filenames containing repository engineering names.
- A separately runnable verifier extracted the ZIP into a new source-free
  directory, matched all 464 files to the manifest, found no prohibited entry,
  launched `DeviceUtility.exe`, and cleaned up its exact process and temporary
  directory. The current verified archive is 72,085,475 bytes with SHA-256
  `B9E04B536CA39061B124616E67AC05317CEE4D887A8EB275BFF6DA2AE3E3507F`.
- This is a local engineering ZIP, not an installer, signed artifact,
  clean-machine result, supported product, or public release. It adds no
  signature trust, write, erase, reset, DFU, recovery, or device authority, and
  does not change the evidence-weighted V1 percentage.

### Replaceable Windows utility identity

- Removed the `OpenTrail` engineering name from the Windows application's
  visible title/header and from loader presentation/blocker copy. The current
  local working display is `Limited Underground Trail Device Utility`, with an
  explicit attorney-review-pending status.
- Added one replaceable identity object for parent, family, and utility-role
  copy. Existing `OpenTrail.Loader` namespaces, repository/script paths,
  protocol schemas, `OT-*` records, board identifiers, and compatibility fields
  did not change.
- The identity boundary rejects standalone `LU`, `LU Link`, `LU Studio`, every
  `LU`-plus-number form, retired `TLU` / `LUT` / `LUTrail` compact names, control
  characters, oversized values, and `®`. The inspection JSON retains the
  brand-neutral role title `Device Utility`.
- Thirty-eight C# scenario groups and four Python loader-view groups pass with a
  zero-warning/error build. This is local source/test evidence and a working
  presentation pending attorney review; it does not authorize public-product
  adoption, repository renaming, public distribution, hardware marking, or a V1
  progress increase.

### Initial built-in Windows USB inspection fallback

- Added the initial dependency-free inspection path for loader builds that
  cannot find
  the OpenTrail source tree. It reads the Windows serial map locally, admits
  only entries whose private transport label indicates USB, validates and
  deduplicates their COM values, then discards both the labels and port names.
- This initial revision emitted only generic candidate cards and could not claim a board,
  runtime, or firmware identity, and leaves signed-bundle selection, Flash,
  clean install, and recovery disabled. It does not call Python, CIM, a shell,
  a device writer, or a network service.
- At that stage, the laptop exposed seven serial-map entries, three of which met the
  private USB-only filter. Twenty C# scenario groups now include live registry
  exercise, port-name omission, deduplication, bounded candidate count, and
  source-tree-absent backend selection; the application builds with zero
  warnings and errors.
- That revision removed Python/source-tree dependence only for generic transport
  discovery. Exact board probing, public publication, installer and
  clean-machine lifecycle, and physical write/recovery evidence remain open,
  so the evidence-weighted V1 percentage is unchanged.

### Windows loader validation isolation

- Reproduced two independent Windows access-denied failures against an old WPF
  markup cache and a prior loader executable even after the .NET build servers
  were stopped; source compilation succeeded when those artifacts were not
  reused.
- Added conditional, project-specific intermediate and output roots for the
  loader validation workflow. Each run now uses one unique directory beneath
  the resolved system temporary directory and deletes only that exact tree in
  its `finally` path.
- The isolated workflow restores both projects, builds the WPF application and
  independent test executable warning-free, and passes all 11 existing loader
  document, refresh, and process-boundary scenario groups.
- This is build-reliability evidence only. It adds no firmware picker, writer,
  device mutation authority, packaging claim, or visible UI acceptance, so the
  evidence-weighted V1 percentage is unchanged.

### Windows loader keyboard and accessibility boundary

- Added F5 as a second route to the exact existing read-only Refresh command,
  with a visible orange focus treatment for keyboard users.
- Marked the changing summary and error copy as automation live regions and
  raise the corresponding event after inspection progress, success, or
  failure changes.
- Device cards now bind their connection and inspection status instead of
  displaying hard-coded values. Their accessible summaries use only validated
  public fields, omit the internal candidate reference, and explain why Flash
  remains unavailable.
- Blocker text now rejects empty values, more than 240 characters, and control
  characters before reaching the view. Fifteen C# document, accessibility,
  refresh, and process-boundary scenario groups pass warning-free.
- No screen-reader/high-DPI/resize session has been accepted yet, and the
  controls still have no firmware selection or mutation authority.

### Windows 95-style loader presentation

- Replaced the dark rounded presentation with the owner-requested classic
  Windows utility direction: gray work surfaces, navy headings, square panels,
  compact Microsoft Sans Serif typography, pixel-aligned edges, and beveled
  buttons. No Windows logo or copied system artwork is included.
- Fixed a user-observed disabled-button defect where the platform template
  produced nearly white labels on white backgrounds. The application now owns
  the complete button template and explicitly renders disabled labels in dark
  gray on the classic gray face while retaining a visible dotted focus cue.
- The revised application builds warning-free and a fresh connected-device
  preview was launched. Final layout, resize, DPI, keyboard, screen-reader, and
  disabled-state visual acceptance remain open until the new window is
  reviewed; no device or firmware authority changed.

## 2026-08-12

### First real Windows loader desktop shell

- Added a .NET 8 WPF development application that invokes the existing
  privacy-safe inspection pipeline, revalidates the reduced document in C#,
  and renders the connected candidates as familiar device cards.
- The application and an independent dependency-free console test project
  build warning-free. Eleven C# scenario groups reject malformed counts, a
  nonzero ready-to-flash count, enabled global Flash authority, and private
  local-port disclosure; they also ensure only the newest active refresh may
  publish, window close invalidates pending output, and helper output remains
  bounded.
- Cancellation, timeout, invalid output, or an excessive helper response
  triggers best-effort termination of the exact process tree owned by that
  inspection. Raw stderr remains discarded.
- Refresh is the only active application action. Firmware selection and every
  device mutation control remain absent or disabled; no writer, reset, erase,
  DFU, recovery, or firmware-file path exists.
- This is source/build evidence only. Visible launch, layout, resize,
  accessibility, repeated live refresh, packaged operation, and clean-machine
  acceptance remain.

### Live Windows loader inspection view model

- Added the future Windows loader's first presentation model on top of the
  privacy-safe USB/runtime adapters. It emits fixed screen copy, candidate
  cards, blocker explanations, action state, and aggregate counts without
  exposing transport or device identity.
- The live three-device result reads `3 found · 3 inspected · 0 ready to flash`
  and presents one SenseCAP Solar MeshCore repeater plus two Heltec V4 OLED
  MeshCore companions, all on the observed firmware build.
- Refresh and Inspect are enabled. Select Firmware, Flash, Clean Install, and
  Recovery are disabled with fixed reasons. The five unresolved board-evidence
  gates are shown in plain language on each card.
- Four scenario groups cover the live shape, private-field omission, generic
  failure presentation, and fail-closed schema/permission handling. A rendered
  application shell now exists under OT-019H, and OT-019I subsequently adds a
  separate bounded local bundle-candidate inspector. Signature trust, final
  admission, a writer, and visual/physical evidence remain.

### Final firmware-write admission composition

- Composed independent bundle admission and board/install preflight results
  into one final pure `ready_to_write` decision. Neither half can authorize the
  future writer by itself.
- The bundle policy and install requirements must agree exactly on hardware
  profile, processor, target role, supported revision range, minimum bootloader
  schema, and maximum image size. The signed image length must equal the
  board-preflight candidate length.
- Bundle failure, disconnected/incompatible board, or any cross-gate mismatch
  blocks readiness. Clean/recovery erase confirmation and physical recovery
  authorization remain intact.
- Eight scenario groups pass. The result performs no I/O; exclusive one-use
  ownership/invalidation, concrete parsers/probes, writer/readback, target boot,
  rollback/recovery, Windows UI, and physical evidence remain.

### Fail-closed signed firmware-bundle admission

- Added a pure pre-device admission policy for the future loader. It keeps
  container/manifest parsing, manifest digest, signature verification, signer
  trust, image read/digest, hardware binding, and rollback evidence distinct.
- Exact hardware profile, processor, target role, board-revision range,
  minimum bootloader schema, nonzero release generation, image length/capacity,
  SHA-256 digest, and opaque signer ID must agree with owner-approved policy.
- A digest or signer ID merely being present never counts as verification.
  Unknown roles/processors, untrusted/wrong signers, truncated or oversized
  images, and generations below the trusted floor fail closed.
- Twelve scenario groups pass. Admission only permits evaluation by the
  separate board/install preflight; it cannot authorize erase/write. Canonical
  container parsing, reviewed SHA-256/signature adapters, key custody/rotation,
  signed release bundles, protected trust storage, and physical update evidence
  remain.

### Privacy-safe Windows USB candidate discovery

- Added the future loader's first Windows-facing adapter. It enumerates USB
  serial runtimes without opening them, reduces records to coarse transport
  families, and treats USB VID/PID as non-authoritative for flashing.
- The default live run found exactly three current bench candidates: two
  Espressif application USB runtimes and one Seeed TinyUSB serial runtime.
  It omitted COM names, serial numbers, hardware-instance paths, device
  locations, runtime identity, and raw enumerator data.
- All three candidates remained inspectable but explicitly blocked from Flash
  because low-level processor/memory, exact profile/revision, target role, and
  bootloader evidence remain unresolved. Discovery performs no serial I/O,
  erase, write, reset, DFU, firmware, settings, or recovery action.
- Added a second read-only layer that strictly reduces MeshCore `ver`, `board`,
  and runtime-role responses. The live run identified two Heltec V4 OLED
  MeshCore companions and one Seeed SenseCAP Solar MeshCore repeater on the
  same firmware build. Raw replies, pairing fields, identity, local ports, and
  unexpected values are never emitted.
- Runtime role remains separate from the unresolved OpenTrail target role and
  is explicitly non-authoritative for Flash. Four discovery groups, four
  runtime-reduction groups, and the publication-safety scan pass. Low-level
  probes, approved physical profiles, signed bundle verification, UI/packaging,
  and physical flash/recovery evidence remain.

### Privacy-safe three-device GNSS evidence

- Confirmed through official MeshCore source that Heltec V4 USB Companion
  includes the GNSS pins/sensor manager and exposes its custom `gps` variable
  only after detecting serial GNSS data. Both connected Heltecs exposed that
  variable, changed from disabled to enabled under authorized bench testing,
  verified enabled on readback, and returned a GPS telemetry field.
- Used the repeater CLI's coordinate-free `gps` status command on the packaged
  SenseCAP. It began powered off, accepted explicit enablement, progressed from
  active/no-fix/0 satellites to a live fix, with later checks at 4, 7, and 8
  satellites.
- Added a reusable read-only snapshot tool that reduces coordinate-bearing
  companion telemetry in memory to one boolean and emits only role-labeled
  detection/active/fix/satellite evidence. Default output omits local ports,
  raw responses, coordinates, identities, keys, and PINs.
- Four parser/redaction scenario groups pass, followed by a successful live
  three-device snapshot. GPS remained enabled; no firmware, radio, repeater,
  channel, identity, or location-advertising setting was otherwise changed.
- The result closes basic GNSS detection/activation for the two assembled bench
  clients and live-fix capability for this packaged repeater. Heltec current
  fix/satellites, exact modules/wiring, accuracy, cold-start/loss/power behavior,
  complete-client binding, and field evidence remain open.

### Fail-closed firmware-install board preflight

- Added one pure loader-facing policy that keeps read-only inspection separate
  from permission to flash. A connected device may be described even while
  incomplete or conflicting evidence blocks every install mode.
- Exact processor, flash, required PSRAM, hardware profile, revision,
  target role, bootloader schema, and image-size evidence must agree. Runtime
  board names alone never unlock Flash. A bench-client profile cannot accept a
  complete-client image, and a packaged repeater cannot accept a client image.
- Clean install requires explicit destructive-erase confirmation; recovery
  requires that plus separate physical recovery authorization. The policy
  performs no USB, signature, erase, write, reboot, or recovery operation.
- Thirteen focused scenario groups pass. Windows probe adapters, signed bundle
  verification, approved physical profiles, UI/packaging, and on-device
  interruption/recovery evidence remain.

### Connected GPS/GNSS inventory clarification

- The owner's report that GPS/GNSS was connected to both Heltec clients and the
  SenseCAP repeater established the physical test setup. The redacted electronic
  detection/activation and SenseCAP live-fix evidence recorded above later
  strengthened that report without resolving exact module/wiring identity or
  OpenTrail target compatibility.
- GNSS is deliberately excluded from flash identity. A board profile and
  post-flash check must handle initialization and no-fix behavior separately.
- Owner-provided purchase records identify the two clients as the Meshnology
  V4 GPS two-unit bundle (ASIN `B0FS1WQWKF`) and the repeater as SenseCAP Solar
  Node P1-Pro (ASIN `B0FMDHBWX8`). Seeed currently maps its MeshCore P1-Pro to
  SKU `100023690`. Received-unit labels/minor revisions remain open; GNSS
  performance evidence remains only partial under the bounded result above.
- The assembled Heltecs are bench clients, not parts for the first complete
  touchscreen build. The integrated solar P1-Pro is the packaged-repeater
  candidate and may be validated physically in that role. The complete client
  remains a separate board/display/control/power/enclosure hardware freeze.

### Quick-status parent page and restored selection handoff

- Added one narrow semantic status page with exactly Quick status and Back. It
  enters the two-page chooser at the exact next revision and restores itself at
  the menu's returned newer revision.
- A typed choice is retained privately until parent restoration succeeds.
  Display-not-ready exposes no early selection and retries without polling a
  second input; deferred menu opening leaves the existing parent retryable.
- The result means only a local selection after truthful UI restoration. The
  owner has no queue/radio/identity/storage/GPS/archive/server/alert reference
  and cannot claim selected means sent, received, or delivered.
- Ten groups plus 100/100 focused repeats and the complete 109-executable host
  matrix pass. Broader shell, authenticated outbound/outcome UX, target
  renderer/input, and physical-device evidence remain later gates.

### Revision-safe generic quick-status menu

- Added two canonical four-action pages so all four generic statuses retain a
  visible Back action within the portable interface limit. Page one is I'm OK,
  Need assistance, Next, Back; page two is Anyone online?, Available to help,
  Previous, Back.
- Exact-revision transitions own their next frame. Display-not-ready during a
  page change retries the pending revision without reading a second input;
  stale/invalid input cannot produce a selection.
- A valid choice returns only one typed local request plus the minimum newer
  parent revision. No queue/radio/delivery dependency exists, and no local
  selection is presented as sent or delivered.
- Ten menu groups and the now-thirteen-group local-interface suite pass 100/100
  focused repeats and the complete 109-executable matrix. Parent-shell,
  authenticated outbound, renderer/target, and physical UX remain later gates.

### Generic quick-status payload

- Added one exact 12-byte `OTQ0/v0` payload for the four generic meanings
  selected for the first small-group experience: I'm OK, Need assistance,
  Anyone online?, and Available to help.
- The payload contains only its fixed semantic value plus canonical framing and
  CRC-32. Participant/device/group/location/time/message/ACK/text/key/routing
  data are structurally absent; CRC does not claim authentication.
- Ten groups plus 100/100 focused repeats cover the independent canonical
  vector, all four round trips, strict failure classes, output preservation,
  and corruption at every byte. The complete 109-executable matrix passes.
- Authenticated packet-v1 binding, priority/replay/expiry/ACK policy, outbound
  admission, menu/confirmation UI, renderer, target/radio, and physical
  delivery remain explicit later gates.

### Optional archive parent page and restoration

- Added one non-copyable semantic `status` page with exactly Archive controls
  and Back. Explicit activation presents the parent without reading archive
  storage, touching runtime, or allocating a lease.
- Exact local Open enters the navigation/bootstrap chain. Nested Cancel
  restores this page at the returned newer revision; Back exits to the still-
  future broader application shell. Re-entry reuses the one boot lease.
- Initial display deferral retries safely. Deferred restoration retains its
  pending revision and does not re-enter navigation/storage. Invalid summary,
  input/display failure, or lease/navigation failure stays inside this
  optional page and cannot directly affect base messaging.
- Nine groups plus 100/100 focused repeats and the complete 109-executable host
  matrix pass. This is deliberately not a full client home/menu, renderer,
  physical input, target task/backend, or device claim.

### Exact-revision archive parent/workflow handoff

- Added one non-copyable local router that accepts only an already resolved
  `open_archive_controls` action matching the exact active parent revision.
  First entry derives the next workflow revision and initializes the durable
  lease bootstrap before controls can be presented.
- Cancel now returns an explicit minimum newer parent revision without
  inventing a home/status frame. A later exact active parent action re-enters
  the same boot workflow and lease without another storage allocation.
- Invalid/stale/already-active/exhausted entry rejects before storage;
  allocation failure latches before runtime/display access. Open remains only
  navigation and cannot Start capture.
- Eight groups plus 100/100 focused repeats and the complete 109-executable
  host matrix pass. The complete application shell, exact target bindings,
  renderer/physical input, concurrency, recovery UX, and on-device evidence
  remain.

### Durable archive lease-to-workflow bootstrap

- Added a fixed-memory, explicit-initialization owner that commits and reads
  back one restart-safe archive session range before constructing the local
  archive workflow. Dormant service/entry cannot read storage, poll input,
  present UI, or touch the archive runtime.
- Successful same-boot reinitialization is idempotent. Any invalid,
  corrupt/uncommitted, conflicting, exhausted, failed, or uncertain lease
  result latches the optional path without a same-object retry or reset; a
  committed-but-uncertain range is skipped after restart.
- Made the consent controller, workflow coordinator, and bootstrap explicitly
  non-copyable/non-movable so a leased cursor cannot be cloned accidentally.
- Eight bootstrap groups plus 100/100 focused repeats and the complete
  109-executable host matrix pass. Exact target backend/seed composition,
  recovery UX, parent navigation, renderer/physical input, concurrent stress,
  reset/brownout/endurance, and on-device evidence remain.

### Restart-safe breadcrumb archive session leases

- Added a two-slot, 64-byte, commit-last `OTBL/v1` store that durably reserves
  an inclusive nonoverlapping session-ID range before any ID is returned.
  Unused IDs are abandoned on reboot; a committed-but-uncertain range is
  skipped rather than reused.
- Isolated the record in a fifth persistent-storage domain and exact
  `ot_archive` key/value namespace. The record contains only generation and
  range bounds—no participant, device, group, location, endpoint, account,
  credential, or key identity.
- Tightened local consent and the complete workflow to require explicit
  first/final lease bounds. Start refuses to cross the final ID and never wraps
  or silently allocates another range.
- Nine store groups, five real key/value-composition groups, the eleven-group
  consent suite, 100/100 focused repeats, and the complete 109-executable host
  matrix pass. ESP-IDF/NVS binding, secure blank-state entropy, authenticated
  integrity/rollback resistance, recovery UX, target boot composition,
  physical interruption, and on-device evidence remain.

### Complete local breadcrumb archive workflow

- Added one cooperative revision owner for snapshot-backed archive controls,
  Start/Stop confirmation, Cancel, consent application, and post-action status
  refresh. Every control action resolves against the exact displayed revision.
- A coherent stopped snapshot offers Start; active state offers Stop. Failed,
  unknown, or incoherent state can offer only the privacy-safe Stop path, never
  Start. Start still requires hold while Stop remains immediate.
- Snapshot contention prevents input polling. Runtime contention retains the
  confirmation for an explicit retry. If active state cannot be shown after a
  successful Start, the coordinator attempts Stop and latches the workflow;
  revision exhaustion also fails privacy-safe by stopping.
- Fourteen deterministic groups plus 100/100 focused repeats and the complete
  109-executable host matrix pass. The workflow now requires explicit durable
  session-range bounds and refuses to cross the inclusive final ID. A parent
  menu/navigation owner, renderer, physical input, target lease composition,
  ESP-IDF binding, concurrent target stress, and on-device evidence remain.

### Revision-bound local breadcrumb archive consent

- Added canonical local Start and Stop confirmation frames. Start requires a
  hold on the exact active revision; Stop resolves immediately from its exact
  local frame. Archive actions are invalid on every other screen.
- The controller accepts only an already resolved local action. Stale, cancel,
  unsupported, failed-input, and clock-failed paths make no archive runtime
  call; Stop remains clock-independent for privacy control.
- Confirmed Start uses one checked monotonic sample and one nonzero ID inside
  its explicit inclusive lease. Contention preserves the candidate for a new
  explicit hold; uncertain post-operation state consumes it so it cannot be
  reused, and the final lease ID permanently exhausts that controller.
- Eleven deterministic groups plus 100/100 focused repeats pass within the
  current 109-executable host matrix. No radio/server/automatic start input,
  renderer, physical input, target lease composition, ESP-IDF binding, or
  on-device evidence exists.

### Private serialized breadcrumb archive runtime owner

- Added one target-shaped owner that privately constructs the capture session,
  bounded outbox, uploader, retry coordinator, and snapshot adapter.
- Start, stop, capture, upload service, and snapshots now share one injected
  lock without exposing a direct mutable-owner reference. Temporary contention
  attempts nothing; component rejection remains separate from lock failure.
- Acquire uncertainty latches before mutation. Release uncertainty after a call
  marks the outcome uncertain and blocks later archive operations without an
  unsafe compensating mutation; base radio remains outside this owner.
- Ten deterministic groups plus 100/100 focused repeats and the complete
  98-executable host matrix pass. Local consent/authorization, real ESP-IDF
  synchronization, concurrent target stress, and physical evidence remain.

### Serialized breadcrumb archive snapshot adapter

- Added one target-shaped source over the concrete capture session, bounded
  outbox, and retry coordinator under a single injected nonblocking lock.
- Ready output requires one acquire, all three copies while held, and one
  successful release. Contention redacts output and defers; lock/unlock failure
  or unknown state redacts and latches this optional observation path closed.
- The source has no archive mutation, upload, input, storage, or base-radio
  authority and composes directly with the single-owner privacy-safe UI.
- Ten deterministic groups plus 100/100 focused repeats and the complete
  97-executable host matrix pass. A real ESP-IDF primitive, common writer
  discipline, concurrent target proof, resource measurement, and physical
  behavior remain absent.

### Single-owner breadcrumb archive UI coordinator

- Added one cooperative UI owner that performs exactly one complete archive
  snapshot read per valid service call and owns all semantic-frame revisions.
- Unchanged state performs no display write and consumes no revision. Temporary
  snapshot/display unavailability and display failure retain the last truthful
  frame; recovery retries the same candidate revision after a fresh snapshot.
- Failed, unknown, or incoherent source state remains the redacted action-free
  archive warning. Maximum revision permits unchanged service but refuses a
  changed frame because no strictly newer revision can be represented.
- Ten deterministic groups plus 100/100 focused repeats and the complete
  96-executable host matrix pass. No target task/lock, renderer, physical
  display, resource measurement, or archive/base-radio execution authority was
  added.

### Single-read breadcrumb archive status snapshot

- Added one target-facing source contract for a complete capture-session,
  bounded-outbox, and retry-coordinator status tuple; common code performs
  exactly one source call per presentation capture.
- Temporary not-ready produces no frame. Failed or unknown source state ignores
  partial output and emits only the generic action-free archive warning with a
  redacted zero queue count; revision zero is refused before source access.
- The tuple is trivially copyable, contains no breadcrumb records or
  coordinates, measures 200 bytes on the current 64-bit host toolchain, and has
  a compile-time ceiling of 208 bytes.
- Ten deterministic groups plus 100/100 focused repeats pass through the
  checked local-interface boundary. The exact ESP-IDF task/lock, target adapter,
  concurrent copy proof, renderer, physical display, and archive execution
  authority remain absent.

### Privacy-safe breadcrumb archive presentation

- Added a fixed-memory, pure adapter from copied archive session, bounded
  outbox, and checked-time retry status into the existing semantic local-
  interface frame.
- Operators can distinguish stopped, active, queued, waiting to retry, full,
  and failed states with only a bounded 0-through-16 queue count. The frame has
  no coordinates, record bytes, endpoint, credentials, participant identity,
  retry deadline, or receipt detail.
- Every archive frame is action-free. Optional archive failure remains an
  ordinary warning rather than a base system-fault claim; incoherent copied
  state fails visibly and an impossible count is redacted to zero.
- Ten deterministic groups plus 100/100 focused repeats pass through the
  checked local-interface boundary. Target snapshotting, renderer, physical
  display, recovery/discard/retention/export/deletion authority, server, and
  real-coordinate evidence remain absent.

### Checked-time breadcrumb archive retry boundary

- Added a fixed-memory coordinator that reads the guarded boot-local monotonic
  clock before any optional archive upload and attempts at most one FIFO head
  per cooperative service call.
- Transient not-ready/failure outcomes retain the exact head and double a
  nonzero retry delay to a fixed maximum. Calls before the exact deadline make
  no remote attempt; durable acknowledgement plus exact local commit restores
  the initial delay for the next record.
- Empty queue performs no clock read. Temporary clock not-ready defers, while
  rollback/source failure, remote rejection, deadline overflow, or uploader
  ambiguity retain the FIFO and latch this optional boot composition closed.
- Ten deterministic groups plus 100/100 focused repeats pass. No target task,
  reviewed field interval, connectivity detector, authenticated network
  adapter, server receipt, persistence, power result, UI, or physical upload
  exists; base radio behavior remains independent.

### Bounded breadcrumb archive outbox and durable-ack handoff

- Added a 16-record fixed-memory FIFO that validates exact `OTBA/v0`, requires
  sequence 1 at each strictly increasing session start, and refuses duplicate,
  gap, rollback, corrupt, or noncanonical input before mutation.
- Full capacity never overwrites or evicts an uncommitted private record. The
  archive session receives typed pressure and retries the same sequence after
  space opens; stopping capture leaves already accepted outbox records intact.
- Added a cooperative uploader that removes only the exact FIFO head after an
  injected `durable_ack`. Not-ready, rejection, and failure retain it; an
  impossible post-ACK local commit mismatch latches upload closed.
- Removed the draft's unnecessary second copy of the latest full record, so
  ordering history retains only opaque session/sequence metadata after a record
  leaves. Ten groups and 100/100 focused repeats pass.
- This queue is volatile RAM and the durable-ACK result is only a host adapter
  contract. No server, endpoint, protected persistence, authentication,
  account/access, retention/export/deletion, target, or physical evidence exists.

### Opt-in breadcrumb archive session boundary

- Added a fixed-memory client-side archive session that composes the existing
  explicit position scheduler with an injected nonblocking transport while
  remaining separate from base radio operation.
- Fixed canonical `OTBA/v0` at 56 bytes: opaque nonzero session ID,
  session-local sequence, boot-local capture time, the existing exact 16-byte
  current-position payload, canonical reserves, and CRC-32.
- Required strictly increasing nonzero session IDs within one object lifetime;
  sequence advances only after local transport acceptance, while retryable
  pressure retains the same record number. Invalid/stale fixes and clock faults
  fail closed.
- Ten deterministic groups and 100/100 focused repeats pass. Local acceptance
  is not remote acknowledgement or persistence; server, account, authorization,
  encryption, retention/export/deletion, target binding, and physical evidence
  remain absent. Precise coordinates remain private.

### Base-versus-optional product boundary

- Added one public capability/dependency map separating the self-contained base
  client from optional repeater, server/archive, OpenGauge vehicle, offline-map
  display, and post-session management roles.
- Defined base v0 around local display/input, battery, GNSS-aware group state,
  messaging/alerts, privacy-safe logging, and USB recovery with no infrastructure
  dependency during operation.
- Required each add-on to fail independently: repeater loss falls back to direct
  behavior, archive loss stops remote retention only, vehicle loss removes
  normalized vehicle alerts only, and map/display loss retains simpler local UI.
- Kept provider accounts, hosting/DNS operations, costs, credentials, private
  routes/participants, and implementation-specific deployment details outside
  the public boundary.

### First-release capacity boundary

- Fixed the planned v0 ceiling at eight active clients in one group plus at
  most one optional authorized repeater. The repeater is not a client and
  cannot become a base-client dependency.
- Required evidence remains deliberately staged: four standalone clients,
  four clients plus one repeater, then eight clients plus one repeater, all on
  frozen hardware/firmware with versioned procedures and privacy-safe results.
- Clarified that host capacities, airtime estimates, close-bench tests, and one
  successful field session do not create a support claim. No phase has passed,
  so the ceiling remains a release target rather than current capability.
- Removed a larger-capacity statement from the public pilot interpretation;
  changing the release ceiling now requires a versioned policy, analysis,
  security/recovery review, and staged physical evidence.

### Cross-project `ORS0` target-shaped storage sync

- Reconciled OpenTrail's public cross-project status with current OpenGauge
  evidence instead of leaving the target adapter described as unimplemented.
- OpenGauge now binds exact 1280-byte `ORS0` slots through a backend-neutral
  `og_state` / `og_recovery` / `ors0_a|b` key/value adapter and composes its
  real boot/save coordinators through restarted adapter/store instances.
- Thirteen adapter groups and 100/100 repeats cover normal one/two-slot boot,
  verified save, applied-uncertain trusted-floor catch-up, and
  unapplied-uncertain prior-generation recovery in the public 43-executable
  matrix. No OpenTrail ESP-IDF binding, protected keys/trust, physical power
  interruption, or on-device recovery result is claimed.

### ACK boot-session allocation through key/value storage

- Composed the real commit-last `OTAS` allocator through the isolated
  multi-domain key/value adapter using only `ot_state` / `ot_proto` /
  `slot_a|b` and exact 64-byte values.
- Proved restart session increment/slot rotation, retry after a confirmed
  unapplied prepared commit, skipping a session whose marker became durable
  despite a reported failure, and explicit durable two-key reset before
  different consumer/authorization reseed.
- Six deterministic groups and 100/100 focused repeats pass in the complete
  90-executable host matrix including publication safety. ESP-IDF binding,
  authenticated integrity, trusted rollback, reset/reseed UI, locking,
  physical interruption/endurance, and on-device evidence remain open.

### Rollback-safe counter leasing through key/value storage

- Composed the real `OTCN` outbound counter lease store through the isolated
  multi-domain key/value adapter using only `ot_state` / `ot_counter` /
  `slot_a|b` and exact 64-byte values.
- Proved restart slot rotation and non-overlap, retry after a confirmed
  unapplied prepared commit, and permanent skipping of a range whose marker
  became durable even though backend commit reported failure.
- Five deterministic groups and 100/100 focused repeats pass in the complete
  89-executable host matrix including publication safety. ESP-IDF binding,
  protected integrity/rollback, target locking, physical interruption,
  endurance, packet-v1, and on-device evidence remain open.

### Non-erasable map trust-domain storage boundary

- Added a backend-neutral key/value adapter for the two exact 80-byte
  `OTMD/v0` slots with fixed `ot_state` / `ot_map_domain` / `otmd_a|b` binding
  and no erase or reset API.
- Preserved prepared-before-marker ordering by rereading the exact prepared
  blob and rewriting it with only byte 75 committed, while keeping failed
  commits uncertain for boot inspection instead of rollback or blind retry.
- Five deterministic groups, all twenty-seven map suites at 100/100, and the
  complete 88-executable host matrix pass including publication safety.
  ESP-IDF binding, protected rollback/authentication, target locking, physical
  interruption/endurance, and on-device evidence remain open.

### NVS-ready multi-domain persistent storage boundary

- Added a backend-neutral adapter for the four existing 64-byte persistence
  domains, with separate `ot_config`, `ot_secret`, `ot_proto`, and `ot_counter`
  namespaces and two fixed keys per namespace.
- Preserved erase-before-program, zero-to-one refusal, partial RAM accumulation,
  full-blob sync/commit, durable-only reads, and fail-latched uncertain writes.
  Configuration body sync, commit-marker sync, rotation, and restart discovery
  after an applied-then-failed final commit pass through the real store.
- Twelve deterministic groups and 100/100 focused repeats pass in the complete
  88-executable host matrix including publication safety. The secret namespace
  is structural only; protected secret storage, ESP-IDF binding, locking,
  authentication, rollback protection, power interruption, and endurance
  remain open.

### NVS-ready replay checkpoint storage boundary

- Added a backend-neutral key/value adapter for the two context-bound
  `ODS0/v1` replay slots with exact `ot_state` / `ot_replay` / `ods_dup_a|b`
  binding and strict 704-byte value validation.
- Required an explicit backend commit after every write or present-key erase,
  preserved missing-key erase as idempotent, and verified that a durable record
  is discovered after an applied-then-failed commit rather than blindly retried.
- Nine deterministic groups and 100/100 focused repeats pass in the complete
  86-executable host matrix including publication safety. Protected ESP-IDF
  namespace access, target locking, authentication, rollback protection,
  physical power interruption, and endurance remain open.

### NVS-ready update checkpoint storage boundary

- Added a backend-neutral key/value adapter for the recoverable two-slot
  `OTU0/v0` store with exact `ot_state` / `ot_update` / `otu_chk_a|b` binding
  and strict 64-byte value validation.
- Made write and erase success depend on an explicit backend commit, retained
  missing-key erase as idempotent, and kept applied-then-failed commits visible
  so a restart can reconcile the durable record through the existing store.
- Nine deterministic groups and 100/100 focused repeats pass in the complete
  85-executable host matrix including publication safety. ESP-IDF binding,
  partition/security configuration, target locking, hardware-backed trust,
  power interruption, and endurance remain open.

### Domain-aware map runtime transitions

- Added the runtime boundary after domain-aware trial boot. It handles healthy
  trial reads and promotion, trial deadline/failure, valid fallback completion,
  and previous-package cleanup without accepting caller-supplied generations.
- Fixed the durable order at private selector generation `N+1`, unchanged-domain
  recheck, exact selector verification, protected-domain advance/readback,
  selector recheck, `OTMD/v0` accepted-generation save/readback, final
  three-owner rechecks, then live publication.
- Expanded restart recovery to canonical active checkpoints left after an
  interrupted promotion, fallback completion, or cleanup. Invalid fallback
  evidence retains protected/domain history for service instead of returning
  to first-use state. Eleven deterministic groups and all twenty-seven map suites
  pass 100/100 focused repeats in the complete 88-executable host matrix,
  including publication safety. Protected target adapters and locking, physical
  package operations, interruption/wear, rendering, and on-device evidence
  remain open.

## 2026-08-11

### Restart-safe domain-aware map trial boot

- Added the restart boundary after domain-aware candidate entry. It accepts
  only exact synchronized durable state or either single-generation gap that
  candidate entry or a prior trial boot can leave after interruption.
- Fixed the recovery order at private selector boot/save, unchanged-domain and
  selector recheck, protected advance/readback, selector recheck, accepted-
  generation save/readback, final three-owner recheck, then live publication.
- Added trial boot-limit persistence, already-persisted fallback restart,
  replacement-domain recovery, generation/race/write/readback containment, and
  degraded-peer repair coverage. Fourteen deterministic groups and all twenty-
  five map suites pass 100/100 focused repeats in the complete 83-executable
  host matrix including publication safety. Promotion, fallback completion,
  cleanup, protected target adapters, and physical evidence remain open.

### Domain-aware map candidate entry

- Added the runtime entry from one stable active map to a privately persisted
  alternate-slot trial. The coordinator derives current generation from active
  `OTMD/v0` and the exact domain-bound protected source rather than caller state.
- Fixed the durable order at trial selector save/readback, domain recheck,
  protected advance/readback, selector recheck, accepted-domain-generation
  save/readback, final three-owner recheck, then live trial publication.
- Added safe candidate-rejection rechecks and fail-visible interruption behavior
  after selector persistence; no path rolls back protected or domain history.
  Thirteen deterministic groups and all twenty-five map suites pass 100/100
  focused repeats in the complete 83-executable host matrix including
  publication safety. Trial boot/recovery is now a separate composition;
  promotion, fallback completion, cleanup, protected target adapters, and
  physical evidence remain open.

### Read-only active map trust-domain boot

- Added the restart boundary for an already-active stable trust domain. It
  requires exact agreement among the active `OTMD/v0` record, protected domain
  and accepted generation, stable `OTM0/v0` selector, policy, and supplied
  package evidence.
- Fixed the read order at domain, protected source, private selector restore,
  domain recheck, exact selector recheck, protected-source recheck, then live
  publication. Boot has no domain/selector write, repair, erase, or protected-
  mutation authority.
- Added thirteen deterministic groups covering fresh/replacement boot,
  degraded exact records, invalid or incomplete state, rollback/ahead evidence,
  trial refusal, read failures, and domain/selector/source races. All twenty-
  three map suites pass 100/100 focused repeats, and the complete 81-executable
  host matrix passes locally including publication safety. Candidate/trial/
  fallback/cleanup domain synchronization and physical target boot remain open.

### Recoverable stable map trust-domain activation

- Added the stable-baseline activation coordinator for a provisioned pending
  trust domain. Fresh commissioning uses selector generation 1; replacement
  reseed uses exactly one generation above the retired-domain quarantine floor.
- Fixed the durable order at exact selector save/readback, atomic protected
  source advance/readback, exact selector recheck, pending-to-active
  `OTMD/v0` save/readback, final protected-source recheck, then live-map
  publication. No intermediate state exposes a map.
- Added restart reconciliation after selector persistence, protected advance,
  applied-then-failed source mutation, uncertain domain commit, and an already
  active exact stable baseline. Divergent selector, source, domain, policy, or
  package evidence remains mapless and requires service or reconciliation.
- Added fourteen deterministic scenario groups. All twenty-two map suites pass
  100/100 focused repeats, and the complete 80-executable host matrix passes
  locally including publication safety. Active-domain boot, candidate/trial/
  fallback/cleanup synchronization, protected target adapters, and physical
  power-cut evidence remain open.

### Authorized map trust-domain provisioner

- Added the sole common-code consumer for the protected-domain permit. It burns
  exact binding, boot-session, and checked-time authority before any I/O and
  requires exclusive stopped/mapless ownership throughout the operation.
- Fixed the mutation order at verified pending `OTMD/v0` record, verified-empty
  selector media, then protected-source establishment/readback at generation
  zero. The source boundary can establish only independently uninitialized
  state and exposes no reset or rebind operation.
- Added exact new-permit resume for matching pending state, including recovery
  when a source call applied the domain before reporting failure. Every
  post-pending failure remains mapless and reconciliation-required; successful
  preparation still requires a later baseline/reseed activation.
- Added thirteen deterministic scenario groups. All twenty-one map suites pass
  100/100 focused repeats, and the complete 79-executable host matrix passes
  locally including publication safety. Protected target adapters, concrete
  credentials/continuity/entropy, exclusive target tasking, physical power-cut
  evidence, and active-domain completion remain open.

### Recoverable map trust-domain lifecycle store

- Added a separate two-slot storage boundary for exact 80-byte `OTMD/v0`
  records; it does not share keys, slots, erase, or reset authority with the
  64-byte `OTM0/v0` selector store.
- Required readable empty media plus fresh pending commissioning for generation
  1, then exact next-generation maintenance, pending-to-active, monotonic
  selector-acceptance, or linked replacement transitions. Backward state,
  lowered retirement floors, binding changes, and immediate retired-domain
  reuse fail before writes.
- Preserved the prior committed record across twelve interrupted prepared-write
  boundaries, committed byte 75 last, verified exact readback, reconciled both
  sides of a commit-call error at boot, and repaired known degraded peers while
  refusing unreadable, invalid-only, conflicted, or exhausted media.
- Added ten deterministic store groups. All twenty map suites pass 100/100
  focused repeats, and the complete 78-executable host matrix passes locally
  including publication safety. Protected target storage/rollback, the permit-
  consuming provisioner, cross-store ordering, target locking, and physical
  durability remain open.

### Canonical map trust-domain lifecycle record

- Added fixed 80-byte `OTMD/v0` instead of breaking the existing 64-byte
  `OTM0/v0` selector checkpoint or squeezing a truncated domain into its six
  reserved bytes.
- Bound current and retired 128-bit domains, quarantined selector-generation
  floor, accepted selector generation, domain epoch, record generation,
  commit-last marker, and CRC across fresh/pending-reseed/active lifecycles.
- Tightened protected-domain authorization: same-device replacement must name a
  distinct nonzero retired domain, while first commissioning must name none.
- Added ten deterministic codec groups. All nineteen map suites pass 100/100
  focused repeats, and the complete 77-executable host matrix passes locally
  including publication safety. Recoverable domain-record storage, a permit
  consumer/provisioner, target locking, and physical durability remain open.

### Protected map-domain authorization handoff

- Added separate same-device replacement and blank-new-device commissioning
  scopes derived from the reset/replacement policy before backend access.
- Required an atomically consumed local-USB grant bound to exact lifecycle,
  coherent empty or quarantined selector evidence, a fresh nonzero 128-bit
  domain, boot session, six confirmations, committed local revision, and short
  checked-time window.
- Rejected temporary source failure, retained state on claimed new hardware,
  wireless/radio transport, malformed or mismatched grants, expiry, and replay.
  Retained same-device selector state remains quarantined and cannot be imported.
- Added ten deterministic groups. All eighteen map suites pass 100/100 focused
  repeats, and the complete 76-executable host matrix passes locally including
  publication safety. The move-only permit has no consumer; no domain record,
  reset/provision executor, concrete credentials, target lock, or physical
  result exists.

### Map-selector reset and device-replacement boundary

- Separated ordinary factory reset, authorized selector reseed, same-device
  protected-source recovery, and whole-device commissioning into four explicit
  lifecycle routes.
- Kept ordinary reset outside both map persistence domains. It may reset
  identity/configuration under their own policy, but cannot erase selector
  records or lower protected map history.
- Blocked selector reseed when protected history is unavailable or missing,
  required future independent external authority for same-device source
  replacement, and rejected retained-selector import through new-device
  commissioning.
- Added ten deterministic groups and an accepted architecture decision. All
  seventeen map suites pass 100/100 focused repeats, and the complete
  75-executable host matrix passes locally including publication safety. No
  physical continuity detector, external recovery authority, fresh-domain
  provisioner, target executor, or on-device result is claimed.

### Protected-generation map-selector service reseed

- Added a service-recovery composition that derives the reviewed floor from
  protected history before selector access and requires the existing single-use
  permit to match that exact value.
- Kept verified selector clear and replacement save on a private guard, then
  advanced and exactly read back protected history before publishing the
  recovered map.
- Left a permit retryable after initial temporary trust failure, prevented every
  selector failure from advancing trust, and contained every post-save trust
  conflict or uncertainty as ambiguous-mapless for fresh-boot reconciliation.
- Added twelve deterministic groups and completed 100/100 focused repeats. All
  sixteen map suites and the complete 74-executable host matrix pass locally,
  including publication safety. Concrete credential and protected-generation
  backends, reset/replacement authority, target locking, physical durability,
  and on-device results remain open.

### Protected-generation map-selector first baseline

- Added a first-use composition that inspects protected history before selector
  access and permits only an exact clean `no_selector` owner with zero history.
- Saved canonical stable selector generation 1 against a private guard, then
  advanced protected history from 0 to 1 with exact readback before publishing
  the first active map.
- Kept a retryable initial trust read clean and retryable. Nonzero history blocks
  selector access; selector-save failure never advances trust; and every
  post-save conflict or uncertainty remains ambiguous-mapless for fresh-boot
  reconciliation.
- Added eleven deterministic groups and completed 100/100 focused repeats. All
  fifteen map suites and the complete 73-executable host matrix pass locally,
  including publication-safety checks. Protected reseed composition, a concrete
  protected backend, reset/replacement authority, target/package locking,
  physical durability, and on-device results remain open.

### Protected-generation map-selector candidate replacement

- Added a replacement composition that derives both selector generation values
  from protected history before selector-store access.
- Kept candidate staging and the commit-last selector save private, then
  advanced and exactly read back trust before publishing trial state.
- Required a final exact trust recheck before a rejected candidate may retain
  the active map. Selector-save failure never advances trust; every post-save
  conflict or uncertain advance remains mapless for reconciliation.
- Added eleven deterministic groups and completed 100/100 focused repeats. All
  fourteen map suites and the complete 72-executable host matrix pass locally,
  including publication-safety checks. Baseline/reseed protected composition,
  a concrete protected backend, target/package-slot locking, physical
  durability, and on-device results remain open.

### Protected-generation map-selector runtime transitions

- Added a runtime composition that obtains both the current selector generation
  and rollback floor from the protected-generation source before selector-store
  access.
- Kept trial reads, deadline handling, fallback completion, and prior cleanup on
  a private guard until unchanged trust is rechecked or a saved selector is
  followed by an atomic protected advance and exact readback.
- Contained a currently visible map after trust failure/change and retained
  protected history after invalid-fallback selector clearing, routing that state
  to service reconciliation instead of treating it as clean first use.
- Added eleven deterministic groups and completed 100/100 focused repeats. All
  thirteen map suites and the complete 71-executable host matrix pass locally,
  including publication-safety checks. Candidate/baseline/reseed protected
  composition, a concrete protected backend, target locking, physical
  durability, and on-device results remain open.

### Protected-generation map-selector boot composition

- Added the first map coordinator that derives its rollback floor directly
  from the protected-generation boundary instead of accepting it from an
  ordinary caller.
- Kept restored and boot-updated selector state private until the protected
  value is rechecked or atomically advanced and read back at the exact selector
  generation.
- Made nonzero trusted history with empty selector media service-required,
  retained only fail-visible mapless state for selector rollback, and withheld
  saved selector state after any uncertain protected advance or final conflict.
- Added ten deterministic groups and completed 100/100 focused repeats. All
  twelve map suites and the complete 70-executable host matrix pass locally,
  including publication-safety checks.
  No protected backend, ESP-IDF composition, physical durability, hardware
  counter, reset/replacement authority, or on-device result is claimed.

### Protected map-selector trusted-generation boundary

- Added a backend-neutral source contract whose mutation must atomically match
  the exact current generation before advancing to a strictly greater value.
- Added a boot-local, non-copyable common enforcer that distinguishes source
  rollback from stale conflict, rejects nonincreasing requests without a write,
  and requires exact post-advance readback.
- Treated every advance error as commit-uncertain. Advance failure, failed
  readback, frozen readback, and advanced-past readback latch all later source
  I/O closed until reconciliation through a fresh boot instance.
- Added ten deterministic groups. All eleven map suites pass 100/100 focused
  repeats, and the complete 69-executable host matrix passes locally. No
  protected backend, target composition, reset/replacement authority, hardware
  counter, ESP-IDF implementation, or physical anti-rollback result is claimed.

### Single-use local-service authorization for map reseed

- Replaced the reseed coordinator's five caller-created booleans with a
  non-copyable permit minted only through an injected authorization backend.
- Bound each grant to the exact boot session, selector-reseed scope, local USB
  or independently authenticated local-wireless transport, activation policy,
  baseline package evidence, reviewed trusted floor, five service
  confirmations, and committed local-confirmation revision.
- Enforced a configurable short lifetime under a hard five-minute ceiling,
  exact-expiry rejection, backend handle consumption, move-without-copy permit
  ownership, and burn-before-selector-access behavior. Remote radio, replay,
  mismatch, invalid time, denial, unavailable backend, and failure mint no
  permit.
- Added ten authorization groups and retained twelve reseed groups. All ten map
  suites pass 100/100 focused repeats, and the complete 68-executable host
  matrix passes. No credential verifier, target service UI, protected replay
  store, audit backend, or on-device authentication is claimed.

### NVS-ready map selector key/value boundary

- Fixed one backend-neutral selector binding: `ot_state` partition label,
  `ot_maps` namespace, and exact 64-byte `otm_sel_a` / `otm_sel_b` blobs.
- Required a backend commit after every staged blob write or key erase. Marker
  commit reads the prepared record, changes only byte 59, rewrites the complete
  blob, commits it, and leaves exact readback to the upper store.
- Kept missing keys distinct from I/O, made missing-key erase idempotent, and
  rejected wrong sizes, bindings, offsets, marker values, and already-committed
  blobs without guessing state.
- Added ten deterministic groups including normal two-slot rotation and
  verified selector clear through the real store. The ESP-IDF/NVS plan fixes
  service scope and physical interruption gates without claiming a target
  backend or hardware durability. At this checkpoint nine map suites passed
  100/100 repeats and the complete 67-executable host matrix passed; the newer
  authorization section above supersedes the current matrix count.

### Authorized map selector service reseed

- Added a separate service-only coordinator for dirty or previously used map
  selector state; clean first use and healthy active replacement remain routed
  to their non-destructive coordinators.
- Required five explicit caller acknowledgements, an already-mapless owner,
  exact policy, valid package evidence, and pre-erase generation review.
- Added `reset_and_verify_empty`: both abstract selector records are erased and
  read back, and reported-success erase without exact emptiness fails closed.
  Package bytes and other persistence domains are never touched.
- Advanced the new stable record beyond the greater of observable local and
  reviewed trusted history, rechecked emptiness through `save_if_empty`, and
  allowed exposure only after commit-last exact readback.
- Added twelve reseed groups and raised the selector store to fourteen groups.
  Partial/dishonest erase, exhaustion, write/commit/readback uncertainty, and a
  post-clear selector race remain mapless. All eight map suites pass 100/100
  repeats, and the complete 66-executable host matrix passes.

### Restart-safe first map baseline

- Added a first-use coordinator that accepts only a clean `no_selector`
  mapless guard, exact policy, fully evidenced package, two readable empty
  selector slots, and zero trusted generation history.
- Avoided an unencodable no-prior trial. The initial package becomes private
  stable state and canonical selector record generation 1 is committed/read
  back before map exposure.
- Added `save_if_empty` so a selector appearing between preflight and save is
  never overwritten. Exclusive target ownership is still required; the check
  is not a lock.
- Refused existing, dirty, unreadable, conflicted, previously trusted, and
  changed selector state. This path cannot reset or reseed a used device.
- Added ten deterministic groups covering success/restart, owner/policy/package
  rejection, trusted history, media states, write/commit/readback failure, and
  the inspect/save race. All six affected suites pass 100/100 repeats; the full
  65-executable matrix is the publication gate.

### Safe replacement-map candidate ordering

- Added a typed candidate coordinator for replacing one stable active map with
  externally staged and fully evidenced alternate-slot package bytes.
- Candidate lifecycle changes occur on a private guard; the trial becomes live
  only after commit-last selector save and exact readback.
- Added exact-generation guarded saves to close the preflight-to-save gap for
  boot, runtime-transition, and candidate coordinators. The caller still must
  provide exclusive store ownership; this is not a lock.
- Invalid package evidence leaves the current active map unchanged. Unreadable,
  conflicting, stale, changed-generation, failed-write, uncertain-commit, and
  corrupt-readback states fail mapless with typed service/reconciliation
  outcomes.
- Kept first-ever map installation explicitly out of scope because restart-
  safe trial state requires a real prior-good package rather than invented
  fallback authority.
- Added eleven deterministic candidate groups, including a newer checkpoint
  appearing after preflight but before save. Candidate, transition, boot, and
  store suites each pass 100/100 focused repeats; the full 64-executable matrix
  is the publication gate.

### Verified runtime map transition persistence

- Added read-only exact live-checkpoint verification to the two-slot store; a
  stale or arbitrary in-memory guard cannot become the next durable state.
- Added a transition coordinator that applies trial reads/time, fallback
  completion, and prior cleanup to a private guard copy and publishes durable
  changes only after commit-last save plus exact readback.
- Avoided writes for healthy reads/time that do not change checkpoint state and
  for rejected operations.
- Verified-cleared only the two selector records when fallback evidence was
  invalid; partial clearing, storage uncertainty, rollback, conflicts, policy,
  and generation mismatch remain fail-visible and mapless.
- Passed thirteen store and thirteen transition groups plus 100/100 focused
  repeats. No physical storage, package deletion, renderer, target task, or
  on-device behavior is claimed.

### Persist-before-exposure map selector boot

- Added a typed boot coordinator that restores only into a private map guard
  and releases stable, trial, fallback, or mapless state through explicit
  outcomes.
- Required every resumed-trial boot increment and trial-limit fallback to pass
  a new commit-last store write plus exact readback before the candidate or
  fallback state becomes live.
- Kept failed writes, uncertain commits, bad readback, unreadable media,
  conflicts, rollback-floor rejection, and package mismatch mapless; the
  private candidate is never exposed.
- Preserved map/communications independence and kept physical storage, package
  access, rendering, trusted-floor ownership, and target task binding outside
  the coordinator.
- Passed ten coordinator groups and 100/100 focused repeats under strict C++17
  warnings-as-errors. No physical or on-device boot result is claimed.

### Recoverable map selector store

- Reserved `OTM0` byte 59 as commit marker `0xA5`; CRC covers the committed
  form, so a complete prepared record with zero there is explicitly invalid.
- Added an abstract two-slot store that writes prepared bytes, commits last,
  verifies exact readback, alternates away from prior-good state, and chooses
  only a unique newest committed generation.
- Preserved older state through partial writes, separated known-uncommitted from
  uncertain commit failures, repaired invalid/uncommitted peers, and rejected
  unreadable slots, equal-generation conflict, and generation exhaustion.
- Added optional external trusted-floor comparison without claiming protected
  anti-rollback state. Trial restore exposes its incremented boot count for the
  boot coordinator to persist before candidate use.
- Passed thirteen store groups plus all five map executables 100/100 repeats.
  No physical backend, atomicity/endurance/power-loss result, authentication,
  target filesystem, renderer, display, or on-device behavior is claimed.

### Restart-safe map selector checkpoint

- Added canonical 64-byte `OTM0/v0` for stable, candidate-trial, and fallback-
  required map state with abstract slots/generations, bounded trial policy,
  record generation, canonical reserved bytes, and CRC-32.
- Wired atomic guard export/restore. Every selected package is re-evidenced;
  trial/fallback also require the exact prior package. Restart discards volatile
  reads/time, increments a persisted boot count, and requires full health again.
- Reaching the trial-boot limit enters visible fallback rather than repeatedly
  restarting an unproven candidate. Corrupt, future, incoherent, policy-
  mismatched, active-mismatched, or missing-prior input fails closed.
- Covered ten checkpoint groups and 100/100 focused repeats alongside 100/100
  unchanged activation-guard repeats with strict warnings-as-errors.
- `OTM0` contains no path, geographic content, identity, credential, key, URL,
  or free text. CRC is not authentication/anti-rollback; durable two-slot
  storage, trusted generation, filesystem integration, and hardware remain.

### Fail-safe offline map activation policy

- Added a fixed-memory C++ guard for mapless/active/staged/trial/fallback state
  without selecting a filesystem, selector record, renderer, or target.
- Boot refuses missing, unreadable, ambiguous, or invalid selections. Staging
  leaves the current map untouched, and trial begins only after an adapter
  confirms the exact staged slot/generation selector was committed.
- Retained the prior package until a bounded complete-read threshold passes.
  Trial read failure, deadline, clock regression, and media removal require an
  exact verified prior restore or enter visible mapless state without guessing.
- Covered ten deterministic groups plus 100/100 focused repeats with strict
  C++17 warnings-as-errors. The interface contains no paths, geographic data,
  identity, secrets, free text, radio, message, alert, or USB authority.
- This is lifecycle-policy evidence, not authentication, selector durability,
  storage/renderer integration, received-display, or on-device evidence.

### Offline map package manifest and verifier

- Added strict `OTMP0/v0` metadata for source/licence/offline rights/visible
  attribution, experimental container/encoding/scheme, Web Mercator coverage,
  exact bytes/tile count/SHA-256, and reader/firmware/storage/scratch needs.
- Limited v0 experiments to MBTiles/JPEG/TMS, PMTiles/JPEG/XYZ, and indexed-
  raster JPEG-or-RGB565/XYZ. Unsupported formats, extra fields, bad bounds,
  incompatible pairs, and insufficient storage fail before package reads.
- Added a read-only host verifier that streams the supplied package digest and
  rejects truncation and same-length mutation. It performs no download, write,
  transfer, mount, signing, activation, deletion, rollback, or rendering.
- Passed seven focused groups covering all accepted candidates, strict shape,
  bounds/zooms, coherence, rights/attribution and blocked public OSM endpoints,
  exact-byte verification, mutation/truncation, and rejected-path redaction.
- Hardened the Windows PowerShell host harness so the four intentional negative
  diagnostic-CLI smoke checks capture native stderr and exit status instead of
  being prematurely terminated by the script-wide stop preference.
- The tests use synthetic bytes. No real map data, provider, package approval,
  received display, or target result is claimed.

### Offline-map architecture gate

- Rechecked current OSMF policy and made the public OpenStreetMap tile servers
  an explicit non-source for offline packs. Any package requires a provider or
  self-hosted pipeline with documented offline/redistribution permission plus
  visible attribution.
- Compared MBTiles 1.3, PMTiles v3, and a pre-rendered indexed-raster reference
  without selecting one before target evidence. The first display spike will
  compare incrementally decoded JPEG with RGB565 and may reject every option.
- Fixed an off-device, immutable update boundary: complete staging and
  verification precede a small recoverable activation; normal reads are read-
  only; prior-good data is retained; interruption or removal falls back to a
  mapless UI without stopping messages, alerts, or privacy controls.
- Added the owner-reported pair of incoming Waveshare 1.75-inch round AMOLED
  boards as unreceived candidates only. Exact variant, shipping firmware,
  storage, display/touch behavior, memory, performance, power, and usability
  remain unverified OT-018 evidence gates.
- This increment is current-source research and a measured-test plan. It adds
  no map data, provider selection, package, target build, renderer, or hardware
  compatibility claim.

### One offline diagnostic entry point

- Added `opentrail_diagnostic_cli` as one operator-facing command for the
  existing strict `OTPD0` position-UI and `OTRD0` update-recovery records.
- Kept each original parser authoritative. Exact supported prefixes dispatch to
  their complete validators; malformed records and unknown prefixes fail with
  fixed errors that do not echo rejected content.
- Preserved the narrow authority boundary: one command-line record only, with
  no file, log, device, radio, identity, location, network, retention, export,
  position-control, update, cleanup, reboot, or recovery access.
- Passed both canonical record smoke checks, malformed supported input,
  unsupported input, all 58 C++ executables, and every Python/publication-
  safety gate in the complete local host run. The exact published increment
  passes in GitHub Actions run `31502841481`.

### Strict offline update-recovery diagnostic decoder

- Added a checked host parser for the exact uppercase
  `OTRD0=XXXXXXXX` logger record. Wrong length/prefix/case/hex, unsupported
  versions, reserved bits, unknown categories, altered flags, and incoherent
  recovery combinations fail before a decoded event is returned.
- Added stable names for every v0 operation, operator state, reason, action,
  and parser error plus a one-shot CLI with deterministic `key=value` output.
  Rejected input is never echoed.
- Kept the decoder local and offline: one command-line argument is the entire
  input, with no file, device, log, network, persistence, retention, deletion,
  or export access.
- Covered ten operator-decoding groups, canonical and invalid CLI smoke tests,
  100/100 focused repeats, the complete 58-executable host matrix, and every
  Python/publication-safety check. Target log binding, accessible rendering,
  execution authority, and physical recovery evidence remain separate gates.

### Strict offline position UI diagnostic decoder

- Added a checked host parser for the exact uppercase
  `OTPD0=XXXXXXXX` logger record. It rejects wrong length/prefix/case/hex,
  unsupported versions, reserved bits, unknown categories, and incoherent
  field combinations before returning any decoded event.
- Added stable operator names for every v0 event, outcome, notice, reason, and
  parser error plus a one-shot CLI that prints deterministic `key=value`
  output. Invalid input produces only a fixed error category and is never
  echoed.
- Kept the decoder local and offline: it reads one command-line argument and
  performs no network, file, log, device, identity, location, retention, or
  export work.
- Covered ten operator-decoding groups, canonical and invalid CLI smoke tests,
  100/100 focused repeats, the complete 58-executable host matrix, and every
  Python/publication-safety check. Target log binding and physical service
  capture remain separate gates.

### Privacy-safe position UI diagnostic event

- Added the versioned 32-bit `OTPD0/v0` adapter for one validated
  position-sharing UI coordinator result. It normalizes presentation, observed
  refresh, action, input rejection, and failure into coarse outcome, displayed
  position notice, reason, and safety flags.
- Routed the fixed `OTPD0=XXXXXXXX` message through the existing bounded logger
  with info/warn/error severity. Runtime filtering is an accepted non-write,
  sink rejection remains visible, and normal idle polls are deliberately
  suppressed to prevent bounded-log churn.
- Kept revisions, timestamps, scheduler/runtime counters, coordinates,
  payloads, messages, peer/device identity, addresses, credentials, and free
  text outside the diagnostic payload. The caller-supplied logger timestamp is
  record metadata, not encoded event content.
- Distinguished successful critical-frame publication from UI service failure,
  and distinguished display containment, unavailable presentation, revision
  exhaustion, clock deferral, outbound fault, stale input, and invalid input
  without exporting source detail.
- Covered ten groups, 100/100 focused repeats, the complete 58-executable host
  matrix, and every Python/publication-safety check. Target log binding,
  retention/export/clear policy, persistence, and physical
  service evidence remain open.

### Observed position-state refresh before input

- Extended the single-owner position UI service to remember the last
  successfully presented semantic frame and compare it with current live
  outbound/scheduler state before polling local input.
- A user-visible transition caused outside the UI path—GPS wait or recovery,
  sink pressure/failure, or a permanent outbound clock fault—now publishes a
  higher revision first. An action queued against the superseded revision is
  left unread during refresh and is rejected as stale on the next service call.
- Compared only screen, attention, notice, summary, and all canonical action
  bindings. Service timestamps, attempt deadlines, and counters do not create
  display churn when the visible meaning is unchanged.
- Failed observed-state publication invokes clock-independent Stop and latches
  further UI input closed. Revision exhaustion does the same before either
  refresh or input can proceed.
- Covered ten dedicated groups, 100/100 focused repeats, the complete
  58-executable host matrix, and every Python/publication-safety check. Exact
  ESP-IDF task/lock serialization, renderer behavior, physical display/input,
  and real concurrent service timing remain open.

### Single-owner position-sharing UI coordination

- Added one cooperative owner for position frame revisions, current live
  presentation, one checked input poll, coordinator-owned Start/Stop, and the
  required result-frame refresh. Target callers no longer assemble copied
  scheduler/runtime state or choose revisions/timestamps for this path.
- Kept retry and mutation behavior distinct. Temporary clock-not-ready changes
  no state and retains the current truthful Start frame/revision; every applied
  or permanent-rejection path publishes a higher-revision active, stopped, or
  critical no-action frame.
- Failed closed around uncertain UI state. Initial display failure can retry the
  same unused revision, while post-action display failure immediately stops
  sharing and latches further input closed. Revision exhaustion stops and
  latches before another input is polled or action applied.
- Covered ten groups, 100/100 focused repeats, the complete 58-executable host
  matrix, and every Python/publication-safety check. Exact ESP-IDF task/lock,
  renderer/retry UX, reboot policy, diagnostics, physical input, and concurrency
  remain open.

### Checked-time outbound position commands

- Moved target-facing Start/Stop authority into the outbound coordinator. Start
  now obtains one checked sample when the action is applied; callers cannot
  provide stale or invented `now_ms`. A successful Start only arms scheduling
  and performs no GPS read, payload submission, delivery, or radio service.
- Kept temporary and permanent clock behavior distinct. Not-ready defers with
  no scheduler access and can be retried with a new sample; source failure or
  rollback stops sharing and latches the boot composition closed; a later Start
  consumes no clock sample. Stop remains immediate and clock-independent.
- Routed the semantic UI action adapter through that live command boundary and
  added bounded command counters. The safety presentation now validates clock
  evidence against service plus command operations while copied status is no
  longer action authority.
- Covered ten command groups plus 100/100 focused repeats, repeated the updated
  safety suite 100/100 times, and passed the complete 58-executable host matrix
  plus every Python/publication-safety check. Target task synchronization,
  concrete clock binding, rendered retry UX, reboot behavior, and physical
  input remain open.

### Fail-visible outbound position safety

- Added a target-facing position presentation overload that requires both
  scheduler and outbound-runtime status. A coherent latched clock rollback
  or source failure now overrides the scheduler's ordinary stopped state with a
  critical `position_sharing_failed` frame containing no actions.
- Validated the coarse runtime status before use. Unknown clock states,
  contradictory fault evidence, impossible counters, and faulted-but-active
  scheduler combinations fail closed to the same safe frame instead of
  offering Start.
- Rechecked action authority through the live coordinator: a Start resolved
  from an older healthy frame is rejected after the fault without scheduler
  mutation; a newly presented fault revision invalidates old input; Stop
  remains safe/idempotent.
- Covered ten groups using real coordinator source-failure and rollback paths,
  100/100 focused repeats, the complete 58-executable host matrix, and every
  Python/publication-safety check. Exact renderer, target synchronization,
  reboot recovery, and physical input remain open.

### Checked-time outbound service coordination

- Added one fixed-memory cooperative coordinator that reads the guarded
  boot-local clock once and uses that exact value for active-sharing location,
  position scheduling, priority handoff, delivery, and opaque-radio service in
  a fixed order.
- Preserved privacy and fail-closed behavior: stopped position sharing does not
  read GPS; temporary clock not-ready invokes no downstream component;
  rollback or source failure stops sharing and latches the coordinator closed
  without consuming later clock samples.
- Kept subsystem failure independent. Missing GPS or invalid position policy
  does not block existing queued traffic, handoff rejection does not block
  already accepted delivery, and full delivery retains priority work until a
  later checked cycle frees capacity.
- Covered ten groups including same-cycle exact position packet delivery to a
  fake-radio peer, 100/100 focused repeats, the complete 58-executable host
  matrix, and every Python/publication-safety check. This is cooperative host
  ordering, not ESP-IDF task/concurrency, inbound processing, authentication,
  target adapters, or physical-radio evidence.

### Loss-aware priority-to-delivery handoff

- Added a fixed-memory, single-owner handoff that peeks the strict-priority/FIFO
  head and removes it only after `DeliveryController` accepts the exact frame.
  Delivery capacity or typed rejection leaves the original priority entry
  available instead of losing it during transfer.
- Preserved the original deadline across layers: remaining priority-queue
  lifetime can shorten but never extend the delivery class expiry. Expiry at
  the queue boundary still wins and remains visible through the queue event;
  impossible commit mismatch latches the coordinator closed.
- Covered ten groups for ordering, exact peek/commit, full-queue deferral,
  duplicate/MTU rejection, expiry, unsafe time spans, and the complete
  scheduler-to-packet-to-priority-to-delivery fake-radio path. The focused
  executable passes 100/100 repeats and the complete 58-executable host matrix
  plus every Python/publication-safety check passes. This is host-only,
  unauthenticated packet-v0 composition, not real-coordinate or physical-radio
  evidence.

### Experimental position packet and priority admission

- Added a fixed-memory sink that revalidates only canonical current-position
  payloads, obtains injected ephemeral packet-v0 metadata, encodes the exact
  38-byte frame, and admits it as `MessageClass::position` background traffic.
- Passed the scheduler's actual service timestamp into its sink so queue
  creation and expiry start from real attempted work, not an unrelated clock
  sample. Metadata is consumed before admission and never reused after pressure.
- Kept failure semantics typed: metadata or queue not-ready/rate pressure is
  retryable; reserved/full capacity maps to full; malformed/noncurrent payload,
  invalid metadata, duplicate IDs, invalid policy, and queue failure fail closed.
- Covered ten groups including scheduler composition, decode round-trip,
  priority ordering, exact expiry, pressure, and failure paths; the focused
  executable passes 100/100 repeats and the complete 58-executable host matrix
  plus every Python/publication-safety check passes. This is explicitly
  unauthenticated packet-v0 host evidence, not real-coordinate or radio use.

### Local position-sharing privacy control

- Added fixed semantic notices for stopped, active, waiting-for-fix, deferred,
  and failed position sharing, plus explicit start/stop actions through the
  existing checked local-interface boundary.
- Kept authority narrow: start only arms the scheduler and performs no sink
  submission; stop disables it immediately. Unrelated or unknown UI actions
  cannot mutate scheduler state, and repeated start/stop remains idempotent.
- Preserved fail-visible behavior. Missing fixes and recoverable sink/encoding
  conditions remain warning states with stop available; invalid policy,
  monotonic rollback, and time exhaustion become critical action-free faults.
- Covered ten groups through button and touch capability shapes, 100 focused
  repeats, the complete 58-executable host matrix, and every Python/publication-
  safety check. Exact renderer/text, target synchronization, direct radio/GPS
  composition, and physical privacy UX remain open.

### Start/stop position broadcast scheduling

- Added a fixed-memory scheduler around the existing canonical 16-byte position
  payload. It starts only by explicit command, stops immediately, and treats
  repeated start while active as idempotent rather than a forced send.
- Scheduled the next cadence from the actual accepted time and the next retry
  from actual deferred work. Delayed service submits only the newest snapshot
  once, preventing a stale catch-up queue.
- Allowed only current validated fixes into the injected sink. Unavailable,
  stale, invalid, and malformed-current snapshots are suppressed or rejected
  before sink access; not-ready/full/failure outcomes remain typed.
- Covered ten groups, 100 focused repeats, the complete 58-executable host
  matrix, and every Python/publication-safety check. Exact cadence, rendered UX,
  authenticated packet/priority composition, direct radio/GPS binding, field
  behavior, and regulatory acceptance remain open.

### Fail-visible update recovery presentation

- Connected decoded `OTRD0/v0` outcomes to the existing fixed semantic UI
  frame instead of introducing a parallel renderer or free-form recovery text.
- Mapped nonblocking trial, rejected-transition, and cleanup states to warning
  notices with acknowledgement only. Rollback, safe mode, service, and reboot
  reconciliation become critical system-fault frames with no reboot, cleanup,
  confirmation, or service execution action.
- Made corrupt/unsupported/incoherent diagnostic words fail visibly to a generic
  critical service-required frame when a valid revision exists; revision zero
  cannot create a presentable frame.
- Covered nine groups through the real checked local-interface boundary, 100
  focused repeats, the complete 58-executable host matrix, and every Python/
  publication-safety check. Exact renderer, target task/revision ownership,
  physical recovery execution, and operator workflow remain open.

### Bounded runtime diagnostic ring

- Added a production-facing fixed-capacity `LogSink` that retains the newest 32
  canonical records in RAM, assigns boot-local sequences, and snapshots the
  complete retained set oldest-first without partial caller output.
- Made pressure visible: normal rollover overwrites exactly the oldest entry
  and increments its counter, while malformed direct records are rejected and
  reach the existing logger's sink-drop accounting. Clear erases records but
  preserves boot-local order and lifetime counters.
- Kept the boundary narrow: it allocates no dynamic storage, accepts redacted
  records only as `[REDACTED]`, and is neither a serialized/persistent format
  nor an internally synchronized target service.
- Covered eight groups including actual `OTRD0` capture, 100 focused repeats,
  the complete 58-executable host matrix, and every Python/publication-safety
  check. Exact target composition, measured RAM/timing, persistent audit/export,
  and physical failure capture remain open.

### Versioned redacted update-recovery diagnostic event

- Added canonical `OTRD0/v0`: one 32-bit recovery outcome logged through the
  existing bounded logger as exactly `OTRD0=XXXXXXXX` under the fixed
  `update-recovery` component.
- Omitted observed/trusted generations and all identity, policy, checkpoint,
  key, address, and raw adapter detail. The mandatory redaction bit, fixed magic
  and version, zero reserved bits, enum ranges, flags, and state/action/reason
  coherence all fail closed on encode and decode.
- Kept logger authority intact: info/warn/error severity follows operator state,
  runtime filtering is an accepted non-write, and full-sink rejection remains
  visible rather than becoming false success.
- Covered eight scenario groups, 100 focused repeats, the complete 58-executable
  matrix, and all Python/publication-safety checks. Target sink binding,
  persistent retention/export, display rendering, and physical failure capture
  remain open.

### Redacted update-recovery operator status

- Added one fixed, pointer-free operator record for boot, normal save, and
  trial-time transition outcomes: coarse state, reason, action, two generation
  values, and bounded success/attention/reboot/confirmation/cleanup flags.
- Structurally excluded hardware and candidate identity, checkpoint payloads,
  raw guard/trusted errors, and nested persistence results. The record is
  trivially copyable and bounded to 32 bytes.
- Validated source state, reason, flags, generations, operation, guard outcome,
  lifecycle publication, and nested persistence coherence. Unknown, default,
  incomplete, or contradictory input fails closed to blocked service with no
  inherited continue, reboot, confirmation, or cleanup claim.
- Covered eight scenario groups, 100 focused repeats, the complete 43-executable
  host matrix, and all Python/publication-safety checks. Target logging,
  rendering, retained audit, reboot/cleanup execution, and physical service UX
  remain open.

### Durable trial-time update transitions

- Added a lifecycle-transition coordinator for health reporting, checked ticks,
  confirmation, and explicit rollback. Every operation runs against a private
  guard copy first.
- Boot-local health/time changes publish without flash writes. Confirmation or
  rollback intent becomes live only after the next checkpoint and exact trusted
  generation are verified; failed persistence leaves the attempted state
  unpublished, stops the original guard, and requires the typed recovery path.
- Covered committed confirmation/rollback, both deadline routes, rejected and
  volatile behavior, uncertain writes, generation mismatch, and post-write
  trust failures across ten groups, 100 focused repeats, and the complete 43-
  executable matrix.
- Kept the target claim bounded: staging/install, boot and rollback execution,
  terminal cleanup/reset, scheduling/watchdog behavior, protected backends, and
  physical interruption evidence remain open.

### Verified normal update persistence

- Added read-only two-slot checkpoint inspection so normal persistence can
  compare local state with independent trust without restoring a guard or
  changing storage. Empty, known-degraded, unreadable, invalid-only, and
  equal-generation conflict results remain distinct.
- Added a typed save coordinator that requires a running guard and exact
  local/trusted generation agreement, verifies the next checkpoint before
  advancing trust, and requires exact trust readback before reporting committed.
  Rollback enters safe mode; local-ahead, uncertain-write, and post-write trust
  failures require reboot reconciliation and no same-boot retry.
- Covered ten save-coordinator groups and the expanded 20-group checkpoint store
  in the complete 41-executable matrix and across 100 focused repeats each.
- Kept the claim bounded: this generic coordinator persists an already-mutated
  guard. A target-facing wrapper must still own lifecycle mutation, persistence
  failure shutdown, scheduling, reboot, protected storage/trust, and physical
  interruption evidence.

### Typed update-recovery boot ordering

- Added a boot coordinator that starts/restores only a private guard and exposes
  it to the application only after exact image validation and all required
  persistence steps succeed.
- Persisted each new trial attempt and boot-mismatch/attempt-limit rollback
  decision before release; exact rollback completion is persisted before the
  recovered baseline can operate.
- Advanced and exactly read back the injected trusted generation only after a
  fully verified checkpoint write. Uncertain checkpoint or trust state requires
  reboot reconciliation and leaves the live guard untouched.
- Covered 15 typed baseline/trial/rollback/terminal/failure groups in the
  complete 40-executable matrix and across 100 focused repeats. No ESP-IDF boot
  task, protected backend, authorized cleanup/reset, or physical result is
  claimed.

### Trusted update-generation boundary

- Added an explicit trusted-floor restore path that rejects missing or stale-
  but-valid `OTU0` media before any live boot-guard mutation.
- Added save allocation beyond the greater of the newest local checkpoint and
  caller-supplied last-trusted generation, including fail-closed 64-bit
  exhaustion before export or write.
- Covered absent, rollback, exact-boundary, newer, local-ahead, trust-ahead,
  and exhausted cases. All 16 checkpoint-store groups pass in the complete
  host matrix and across 100 focused repeats.
- Kept the security claim bounded: OpenTrail consumes but does not yet provide
  a hardware-backed trusted source or authenticated target checkpoint store.

## 2026-08-10

### Canonical update-state checkpoint

- Added the fixed 64-byte `OTU0/v0` record for hardware/version/policy binding,
  candidate state, trial-boot count, rollback reason, and a future store-owned
  generation.
- Wired atomic export/restore into the update boot guard; trial restoration
  preserves the attempt count but clears session, clock, and accumulated health
  so a restarted image must prove health again.
- Added an abstract two-slot store that owns generation allocation, preserves
  the previous valid record across partial/corrupt writes, verifies exact
  readback, repairs known invalid redundancy, and refuses unreadable or
  equal-generation-conflicted media.
- Covered deterministic round trip, restart, rollback completion, exact policy
  mismatch, corruption/canonical/version failures, invalid state, and output
  preservation across eight codec/guard groups plus 100 repeats, then ten store
  groups plus 100 repeats; the complete host matrix passes.
- Kept authenticated two-slot target storage, trusted generation persistence,
  ESP-IDF/bootloader adapters, power interruption, wear, and physical recovery
  explicitly open.

### Fail-safe update and recovery boundary

- Defined signed, hardware-bound release bundles and a USB-first transport that
  cannot weaken artifact verification.
- Required inactive-slot write/readback, persisted bounded trial boot,
  independent health confirmation, automatic rollback, and a trusted version
  floor.
- Added a pure lifecycle guard with eight passing host groups for candidate and
  write evidence, stable health, exact deadline, boot/clock failures, bounded
  trials, and exact rollback completion.
- Kept exact target partitions, signer/key custody, implementation, protected
  rollback storage, and physical interruption/recovery evidence explicitly
  open.

### Verified protected-fragment reassembly

- Added a fixed-memory reassembler that accepts only future crypto-adapter-
  produced verified fragment metadata/plaintext and never parses raw radio
  packets or manufactures authentication.
- Bounded state to four concurrent messages, 16 fragments, 103 bytes per
  fragment, and 1,648 bytes per complete message with no dynamic allocation.
- Covered the real 39+25-byte alert shape out of order, exact duplicates,
  changed-byte/count conflicts, invalid context, full capacity, exact timeout,
  clock rollback, and maximum-size completion across ten groups.
- Released no partial plaintext and kept packet-v1, signature scope, AEAD,
  receiver replay persistence, retry behavior, target resources, and physical
  evidence open.

### Critical-alert protected-radio feasibility

- Applied the corrected signed-group profile to the real 64-byte `OGA0` alert
  and `OGK0` ACK instead of assuming either would fit one radio frame.
- Each requires two candidate fragments, 312 transmitted bytes, and 1,025,024
  us theoretical source airtime at the 163-byte example MTU and bench PHY.
- Alert plus ACK is four source fragments; one exact-byte repeater copy raises
  aggregate theoretical transmission airtime to 4,100,096 us before retries,
  contention, or scheduling.
- Added a tenth deterministic budget group and kept fragmentation blocked until
  signature scope, nonce/replay/reassembly, ACK/retry, bounded-resource, target,
  and regulatory gates close.

### Protected-header destination reconciliation

- Stopped before encoding the 36-byte candidate header because it could not
  carry both the forwarding policy's authenticated 64-bit destination alias and
  fragment metadata.
- Corrected the sizing profile to 44 header bytes with explicit destination and
  no mutable TTL; base overhead is now 60 bytes and the signed-group candidate
  leaves 39 plaintext bytes at the 163-byte example MTU.
- Updated all then-existing nine budget groups and current architecture/status/backlog claims;
  the 16-byte signed position is now 140 bytes/461,312 us at the bench PHY.
- Kept final offsets, flags/types, fragment/reassembly rules, signature coverage,
  AEAD, destination privacy, and packet-v1 approval open.

### Canonical traffic-key derivation context

- Added the fixed 52-byte `OTKD/v1` public context for a future audited KDF.
- Bound nonzero group ID, epoch, full authoritative sender fingerprint, and
  distinct AEAD-key, nonce-prefix, and counter-domain purposes in network byte
  order; short aliases and display names are excluded.
- Failed closed with zero output for zero group/epoch/fingerprint or unknown
  purpose across eight deterministic scenario groups.
- Kept epoch secrets, KDF selection, secret outputs, wiping, independent
  vectors, AEAD, and target evidence behind the exact-device benchmark gate.

### Lease-bound AEAD nonce composition

- Added an algorithm-neutral 96-bit nonce composer that requires the durable
  counter lease and traffic key to carry the same nonzero 128-bit domain.
- Fixed the candidate nonce bytes as a crypto-adapter-supplied four-byte prefix
  followed by the nonzero 64-bit counter in network byte order.
- Failed closed with zero output for missing domains, domain mismatch, or
  counter zero; seven deterministic scenario groups cover canonical and
  boundary cases.
- Kept KDF/key/prefix derivation, AEAD, packet-v1, target storage, and library
  selection behind OT-005's exact-target benchmark gate.

### Hardware and US regulatory reconciliation

- Reconciled the two runtime-identified Heltec V4 OLED companions and the Seeed
  SenseCAP Solar repeater against official manufacturer family documentation
  without claiming exact SKUs from runtime strings.
- Recorded family-level MCU/radio/display/GNSS/power/antenna characteristics
  separately from facts observed on the connected units.
- Added a fail-closed US field gate requiring physical model/revision and FCC ID,
  equipment-grant/exhibit review, installed antenna/gain/cable evidence, and a
  frozen firmware/radio configuration before any authorization claim.
- Kept OT-003A partial: a USA preset, 902-928 MHz center frequency, or lower
  transmit power does not independently prove Part 15 authorization for the
  62.5 kHz MeshCore mode.

### Portable-client composition and whole-contract review

- Added a hardware-independent composition preflight for the first self-
  contained portable client. It binds radio, GPS, diagnostics, two distinct
  storage surfaces, entropy, monotonic time, power, display, and local input.
- Audited every abstract target-facing interface. The review caught the separate
  704-byte replay-checkpoint storage obligation instead of incorrectly treating
  the existing 64-byte multi-domain storage surface as sufficient.
- Exposed power-policy and display-capability validation as shared pure checks,
  then reused them in composition so preflight performs no power/display/input
  I/O and cannot drift from component rules.
- Aggregated all missing and incompatible bindings, validated required/observed
  radio MTU plus UI action/hold capability, and kept GPS no-fix and entropy not-
  ready as valid structural states.
- Passed eight composition groups and the complete 33-executable host matrix.
  No ESP-IDF target, concrete adapter, board build, pin/partition map, rendered
  UI, or physical hardware behavior is claimed.

### Local display and input foundation

- Added a production-facing fixed semantic-frame boundary for small OLED/button
  clients, touch displays, and later adapters without exposing pixels, touch
  coordinates, GPIO identities, or private peer/message content to application
  state logic.
- Bound every normalized action-slot event to the exact successfully presented
  boot-local revision; stale, disabled, out-of-range, unknown, not-ready, and
  failed input is explicit and cannot resolve an action.
- Required a canonical critical-confirmation screen and hold gesture before the
  local confirm request resolves; this remains separate from radio delivery.
- Passed twelve capability/frame/failure/revision/input/critical/system-fault/
  bounded-fake groups, the complete 32-executable host matrix, and 100 focused
  repeats.
- Kept renderers, localization, accessibility, readability, target adapters,
  distracted-driving policy, resource/power measurements, and physical display/
  input evidence explicitly open.

### Power-state foundation

- Added a production-facing atomic power observation and evaluator that keeps
  source readiness, external power, battery presence, charge state, optional
  percentage/voltage, and monotonic sample age distinct.
- Required composition to inject low/critical percentage and staleness policy;
  the common component does not infer percentage from voltage or choose a
  hardware threshold.
- Passed eleven normal/low/critical/charging/external-only/missing/fault/time/
  validation/FIFO groups, the complete 31-executable host matrix, and 100
  focused repeats.
- Kept board adapter, charger/shutdown behavior, hardware thresholds, endurance,
  rendered UX, and physical power-failure evidence explicitly open.

### Monotonic clock foundation

- Added a production-facing checked boot-local millisecond boundary with typed
  not-ready, source-failure, rollback, and latched-fault outcomes plus fixed
  saturating status counters.
- Defined equal ticks as valid, temporary not-ready as recoverable, and source
  failure or decreasing time as closed for the current boot composition without
  consuming later samples after the latch.
- Passed eight lifecycle/failure/boundary groups, the complete 30-executable
  host matrix, and 100 focused repeats.
- Kept ESP-IDF timer/task binding, deep-sleep/brownout behavior, accuracy/drift,
  long-run continuity, and physical failure injection explicitly open.

### Secure randomness foundation

- Added an algorithm-neutral production-facing randomness interface with typed
  not-ready, ready, and failed entropy state, bounded 1-64-byte requests, and
  an atomic full-output-or-no-change rule with no weaker fallback.
- Isolated a predictable 512-byte scripted source under test support only; it
  exposes failure injection and exact attempt/success/consumption accounting.
- Passed eight readiness, failure, exhaustion, retry, transition, validation,
  and boundary groups, the complete 29-executable host matrix, and 100 focused
  repeats.
- Kept ESP-IDF entropy/DRBG binding, production key generation, cold-start,
  reboot, brownout, radio/ADC concurrency, and physical evidence explicitly open.

### Funding foundation

- Added a brand-neutral funding packet with a factual project sheet, reusable
  application answers, a vendor-neutral hardware request, preliminary
  milestone budget, qualification/submission checklist, and dated opportunity
  register.
- Kept the public product name replaceable and identified the legal applicant,
  payee, final bill of materials, quotes, and opportunity eligibility as
  required pre-submission decisions. No application or external request was
  sent.
- Preserved local-first operation: the optional server may add recovery and
  convenience but cannot become a requirement for base radio communication.
- Placed every cash, hardware, discount, loan, sponsorship, and service-credit
  request on owner-directed hold. The packet remains preparation material only;
  no opportunity research for outreach, contact, submission, account activity,
  acceptance, shipment, or announcement is authorized.

### Field-test capacity foundation

- Added a deterministic group-load model and runnable CLI for the planned four
  standalone, four-plus-repeater, and eight-plus-repeater field phases.
- Separated logical messages, source attempts, repeater copies, and exact LoRa
  airtime. Under one fixed one-hour profile, scheduled demand is 1.2727%,
  2.5455%, and 5.0911%; the model explicitly makes no RF, collision, range, or
  regulatory claim.
- Added a privacy-safe live-session evidence contract covering loss, duplicates,
  retries, latency distribution, counters, queues, resets, GPS state, power, and
  route/environment context.
- Passed the expanded matrix locally and in public GitHub Actions: 24 strict C++
  executables, three CLI builds, and the four-group Python MeshCore lease suite.

### Four-person pilot definition

- Defined the first live-test boundary as four identical self-contained clients
  with no repeater, server, internet, phone, laptop, or vehicle connection
  required during the session.
- Added a machine-validated one-hour plan with at least three broad environment
  classes, 300 message origins, 900 peer-delivery opportunities, privacy-safe
  evidence requirements, and provisional pass/fail thresholds.
- Kept the plan explicitly `draft_blocked`: it cannot claim readiness until one
  exact client model and firmware are frozen across four units with battery,
  enclosure, GNSS, display, local input, and USB recovery.
- Added two fail-closed host groups for canonical plan validation, traffic math,
  and the blocked-to-ready hardware transition. The complete current-main host
  matrix passes publicly in GitHub Actions.
- Added the strict `OTPR0/v0` result contract and evaluator. It derives expected
  traffic from the plan and distinguishes an eligible pass, eligible measured
  failure, ineligible setup, and malformed/privacy-unsafe record.
- Added six result groups covering exact-match pass, blocked-plan refusal,
  threshold failure, setup mismatch, privacy rejection, canonical shape, and
  impossible delivery counts. They pass publicly in GitHub Actions.
- Added fail-closed result-template generation. It refuses a blocked plan,
  unknown scenario, existing output, or abandoned temporary output; copies only
  frozen public configuration; and leaves evidence claims false. Three added
  groups bring the publicly passing result suite to nine.

### Cryptographic decision gate

- Completed a current primary-source review of the official Espressif libsodium
  component, pinned ESP-IDF mbedTLS/PSA direction, Monocypher, Noise, and
  Noise-C. Made libsodium the first target benchmark, retained mbedTLS/PSA and
  Monocypher comparisons, and limited Noise-C to reference/vector use.
- Selected Noise XK only as the leading invitation prototype when a signed
  invitation pins a separate X25519 Noise key to the Ed25519 device identity;
  refused silent downgrade or plaintext group-key transfer.
- Recorded entropy, sender-specific traffic keys, rollback-safe nonce counters,
  resource/interoperability benchmarks, protected storage, and physical
  lifecycle evidence as hard gates before packet v1 or a security claim.
- Prohibited irreversible Secure Boot/flash-encryption/eFuse experiments on the
  current bench radios; production enablement needs a sacrificial device and a
  separately proven recovery path.
- Added the algorithm-neutral `OTCN` outbound counter lease store. It commits
  nonoverlapping ranges before use, keeps its two slots separate from existing
  protocol/configuration/secret state, and skips unused ranges after restart.
- Added ten host groups covering allocator lifecycle, exact record/domain
  isolation, restart, mismatch, corruption/conflict/version, exhaustion, reads,
  malformed state, and every persistence mutation boundary. They pass locally
  and in public GitHub Actions run `31368305188`; target protected storage,
  physical power loss, nonce packing, and AEAD remain.
- Added the strict `OTCB0/v0` cryptographic benchmark plan/result boundary and a
  public `draft_blocked` ESP32-S3 plan. A ready plan requires exact target,
  toolchain, sdkconfig, candidate commits/locks/licenses, and radio profile.
- Added eight evidence groups covering ready/block transitions, exact candidate
  and gate sets, canonical plan hashing, no-overwrite result templates,
  complete pass/measured fail, config mismatch, privacy rejection, and invalid
  measurements. They pass locally and in public GitHub Actions run
  `31369215213`. No target benchmark or library selection is claimed.
- Added a fixed-memory protected-packet budget model that charges every fragment
  explicit authenticated-header/tag overhead and theoretical LoRa airtime.
- Added eight groups covering the 163/255-byte MTU examples, a 16-byte position,
  a larger layered header, a 300-byte three-fragment message, empty payload,
  exact fragment boundary, fragment-limit refusal, and invalid PHY/capacity.
  They pass locally and in public GitHub Actions run `31369948699`; this initial
  36-byte header estimate was superseded later the same day by the 44-byte
  destination-inclusive reconciliation above.
- Recorded Decision 0004 after comparing OSCORE's inner/outer split, IPsec
  mutable-field handling, COSE countersignatures, and the RFC Editor final-review
  Group OSCORE design. The first release permits one authorized repeater to
  validate and forward exact immutable bytes once; no protected TTL is rewritten.
- Separated group AEAD access from individual source authentication. A 64-byte
  initial Ed25519 signed-group estimate raised the 163-byte-MTU overhead to 116
  bytes and left 47 plaintext bytes. The destination-inclusive reconciliation
  above supersedes those numbers with 124-byte overhead and 39-byte capacity.
  A ninth budget group passes locally; target signature cost,
  exact wire format, and target evidence remain. The expanded budget passes in
  public GitHub Actions run `31371354045`.
- Added the algorithm-neutral immutable single-repeater forwarder. It requires
  adapter-supplied source authentication/authorization, verified context/epoch,
  and immutable forwarding permission before replay observation, then queues
  the exact protected bytes without a TTL rewrite.
- Added nine groups covering exact bytes, fail-before-replay auth/context/
  permission, exactly one authorized repeater, duplicates/reflections, self/
  local destination, queue/rate congestion, non-rescuable replays, exact expiry,
  enqueue/process clock regression, and invalid input. The full 27-executable
  matrix passes locally and in public GitHub Actions run `31371354045`;
  cryptography, target binding, restart persistence, and field evidence remain.
- Added the single-repeater replay coordinator. It requires protected storage-
  namespace evidence matching the expected group/epoch, restores and repairs
  the two-slot duplicate checkpoint before operation, and readback-verifies a
  new checkpoint before a queued exact frame can be released.
- Failed or uncertain saves and unreadable media disable forwarding. Queue/rate
  congestion observations are also persisted, preventing reboot from rescuing
  a consumed forwarding opportunity into amplification.
- Added nine restart/failure groups covering authorized first provisioning,
  missing checkpoint service, restart suppression, failed-save transmit block,
  congestion persistence, known-degraded repair, epoch mismatch, unreadable
  media, binding, clean owner, and one-shot boot. The complete 28-executable
  matrix passes locally and in public GitHub Actions run `31372816356`.
- Accepted the explicit availability tradeoff: because only replay state is
  durable, power loss after verified save but before radio transmission can
  lose the volatile frame. A coordinated durable outbox, protected target
  storage, rollback defense, power cuts, and endurance remain later gates.
- Evolved the same fixed 704-byte checkpoint envelope to context-bound
  `ODS0/v1`. The old eight reserved header bytes now carry the 64-bit group-
  context ID and the old four reserved tail bytes carry the group epoch; every
  inner replay key must match that epoch.
- Added typed refusal for zero bindings, valid records from another group or
  epoch, and structurally valid legacy unbound v0 media. Neither restore nor
  save mutates/overwrites those records, and the repeater coordinator maps
  bound-media and legacy cases to service before operation.
- Expanded the store suite from nine to ten groups while retaining nine
  coordinator groups. The complete 28-executable matrix and 100 consecutive
  focused repeats of each suite pass locally; the exact published matrix passes
  in GitHub Actions run `31374678550`. No target storage, authenticated
  integrity, anti-rollback, physical migration, power-cut, or endurance claim
  is made.

### Privacy-safe hardware evidence

- Added the `OTFL0` converter/validator so raw local captures can be published as
  aggregate role-labeled evidence without transport ports, serials, addresses,
  channels, coordinates, keys, PINs, or secrets.
- Added four deterministic groups covering redaction, count/latency/topology
  coherence, prohibited key/value detection, and atomic overwrite refusal.
- Ran a one-minute alternating two-second three-radio burst: 30/30 delivered,
  zero loss/duplicates/new errors, median 268.4 ms, p95 275.6 ms, maximum
  431.2 ms, exact +30 repeater flood RX/TX, repeat preserved, empty final client
  queues, 2/2 cleanup, and no remaining lease journal.
- Published only the validated aggregate JSON and dated interpretation; the raw
  capture remains in ignored local build state.
- Confirmed the privacy-safe logger expansion in public GitHub Actions before
  adding the separately scoped pilot-plan validation groups.

## 2026-08-09

### Connected hardware

- Used two Heltec V4 OLED USB Companion nodes and one Seeed SenseCAP Solar
  repeater with matching USA/Canada radio settings.
- Completed a five-hour alternating three-radio bench soak: 300/300 deliveries,
  zero loss/duplicates/errors, exact +300 repeater flood RX/TX, repeat preserved,
  and verified cleanup.
- Completed a post-soak packet-v0 regression and role-reversed physical
  OpenGauge alert/ACK cycles through the same three-radio setup.
- Recorded a non-destructive arrival plan for the ordered Wio Tracker L1 Pro;
  it has not arrived or been tested.

### OpenTrail and OpenGauge integration

- Exercised accepted, terminal stale-rejection, retryable rate-limit,
  retry-to-accept, and live-state alert/ACK lifecycles with zero observed radio
  loss, duplicates, or new errors.
- Added OpenGauge restart checkpoints for ACK replay and outbox state, combined
  them in `OCR0`, and added a recoverable two-slot host store with store-owned
  generations and uncertain-commit reconciliation.
- Added OpenGauge's canonical `OPA0` peer-authorization checkpoint as the next
  prerequisite for restoring authorization epochs after reboot. It has a
  passing 33-executable host matrix and 100 focused repeats.
- Added OpenGauge's `OPS0` two-slot host store around `OPA0`. Automatic
  generations, exact readback, ten interrupted-write boundaries, full-write
  error reconciliation, the complete 34-executable matrix, and 100 focused
  repeats pass.
- Added OpenGauge's `ORS0` system checkpoint, binding peer authorization and
  ACK/outbox recovery to one generation. Dependency-correct private candidates
  preflight all three owners before live import; the complete 35-executable
  matrix and 100 focused repeats pass.
- Added a recoverable two-slot host store for exact `ORS0` generations.
  Store-owned allocation, exact readback, eleven interrupted-write boundaries,
  full-write error reconciliation, the complete 36-executable matrix, and 100
  focused repeats pass.
- Added a caller-owned trusted-generation boundary: below-floor restore is
  rejected before live import, and new saves advance beyond trusted/local state.
  Ten focused groups, the unchanged 36-executable matrix, and 100 repeats pass.
- Documented OpenGauge's target recovery-adapter boundary: two-slot persistence,
  protected opaque-handle resolution, a separate trusted-generation source,
  boot/save ordering, coordinated reset, and physical evidence requirements.
  No on-device implementation is claimed.
- Added OpenGauge protected key-handle preflight to direct and stored `ORS0`
  restore. Active handles validate before any live import, revoked peers are
  skipped, and failures remain typed. Eight system and eleven store groups plus
  100 repeats each pass.
- Added OpenGauge's typed system-recovery boot coordinator. It distinguishes
  genuine first boot, restored, degraded, safe-mode, and service-required
  outcomes, and enables transport only after protected-key restore plus exact
  trusted-floor reconciliation. Nine groups, the 37-executable matrix, and 100
  repeats pass.
- Added OpenGauge's verified recovery-save ordering. Local and trusted
  generations must exactly agree before a normal save; checkpoint verification
  precedes trust advancement and exact readback. Missing/behind/ahead/uncertain
  states remain typed. Eight groups, the 38-executable matrix, and 100 repeats
  pass.
- Hardened OpenGauge boot recovery for an unreadable peer slot that could hide a
  newer committed generation. Unlike known empty/invalid media, unreadable state
  now remains service-only with no trust advancement or transport enablement.
  Ten boot groups, the 38-executable matrix, and 100 repeats pass.
- Added OpenGauge known-degraded repair for exactly one valid plus one known
  empty/invalid slot. It revalidates boot evidence, commits the next generation,
  advances/readbacks trust, and proves both slots valid; unreadable/stale/
  uncertain cases fail closed. Five groups, the 39-executable matrix, and 100
  repeats pass.
- Added OpenGauge's redacted recovery-status boundary for boot, save, and
  repair results. It retains operator state/reason/action, slot health,
  generations, protected-key failure class, and transport/repair flags while
  structurally omitting peer IDs and key handles. Unknown/incoherent results
  fail closed. Seven groups, the 40-executable matrix, and 100 focused repeats
  pass locally.
- Added OpenGauge's versioned diagnostics adapter for that status. One atomic
  32-bit ring event carries only the coarse outcome, slot health, protected-key
  failure class, and flags; generations and identity-bearing fields are omitted,
  and malformed/incoherent words fail closed. Eight groups, the 41-executable
  matrix, and 100 focused repeats pass locally.

### Project operations

- Published Apache-2.0 licensing, contribution guidance, and security reporting.
- Reorganized the repository home page so current hardware, results, limitations,
  and this dated log are visible before the deeper architecture material.
- Added OpenTrail's own public GitHub Actions workflow. The commit-pinned Windows
  2025/Python 3.13/UCRT64 job builds both verifier CLIs and passes all 23 C++
  test executables plus the four-group Python MeshCore lease suite. Its
  current-main
  warning-free run returned zero annotations.
- Linked the public OpenGauge GitHub Actions workflow that validates the shared
  Windows host matrix on every `main` push and pull request. Its current-main
  warning-free run passes all 41 executables with zero annotations.

## 2026-08-08

### Foundation

- Bootstrapped OpenTrail as its own repository with architecture, project status,
  hardware inventory, and an evidence-based backlog.
- Identified and configured both Heltec V4 OLED companions over USB without
  persisting private pairing identifiers.
- Added deterministic host foundations for the radio abstraction, packet v0,
  delivery behavior, diagnostics, GPS state, position encoding, group lifecycle,
  and recoverable non-secret configuration.

### Three-radio repeater evidence

- Runtime-identified the Seeed SenseCAP Solar node as a MeshCore repeater.
- Proved bidirectional close-bench relay and then a software-forced one-hop route;
  disabling repeat made the same route fail, providing the negative control.
- Preserved the temporary-channel cleanup evidence and recorded the remaining
  field-range, exact-SKU, power, antenna, and regulatory gates.
