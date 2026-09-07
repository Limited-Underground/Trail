# OT-171 setup-label target preflight - 2026-09-07

Scope: six-character public boot-local setup alias, shown on the actual pairing OLED and D1 advertising only. No ownership, PIN, stored name, region, protocol authority or radio transmission changes. Existing owned devices are not reset for first-use testing.

Mandatory porting checklist reviewed before implementation:

1. Target boundary: retained Heltec V4.2 HTIT-WB32LAF ESP32-S3R2, 16 MB flash/2 MB PSRAM, SSD1306 128x64, existing USB console/reset/pins/partition/application offset 0x10000; US915 selected, radio TX disabled. Reuse current accepted board definition. No GNSS/battery/radio work; those measurements are outside this change.
2. Reproducibility: retain pinned ESP-IDF 6.0.2 / 7101770dc6db2667b3c477cc31365dd1acd6db4e and existing cache-off double-build path. Two initially absent builds and exact image/ELF/map/config/partition/boot hashes remain pending; no hardware execution before this gate.
3. Boot/transport: label generated once before the existing unowned pairing window and retained through that boot. D0 marker and reset-receipt response preserved. USB/reset transport unchanged; any later hardware execution must re-enumerate exact registered role and recovery image.
4. Ordering: random generation and OLED work stay in startup/runtime-owner context, never GATT callbacks. Label must exist and be shown with the PIN before D1 becomes visible. Test invalid entropy, window expiry/concealment, and immutable within-boot value. Label is not authorization or a pairing secret.
5. Persistence: no new NVS keys, migration, hardware-derived identifiers or writes. Existing owner/bond/name/region/reset domain untouched; persistent-label and power-loss migration checks are inapplicable by construction.
6. Composition: validate fixed-memory code/alphabet/advertising budget and actual pairing display owner/OLED path, retain authority/concealment tests, cross-check Android decoding. Focused tests pending. Complete affected host matrix and target builds remain final gates.
7. Hardware: not authorized in this subtask. Current both-owned board baseline must be preserved. New/unowned first-use physical acceptance requires an unowned device or separately authorized destructive reset. Current per-role readback/rollback images must be bound before any future flash.

Website updates and cold-power enclosure disassembly remain deferred. This preflight is implementation planning, not build or hardware acceptance.

## Focused software evidence

Implemented generation on unowned host-sync after BLE startup, before local PIN display and D1 advertisement. Existing alphabet independently counted as 32 characters and compile-time asserted. D1 uses exactly six bytes of complete local name (29-byte complete primary advertisement); D0 carries no alias and reset-receipt scan response remains untouched. Actual OLED shows Trail- plus the same code below the PIN.

Focused GCC C++17 -Wall -Wextra -Werror builds execute the real startup owner and actual OLED port against host I/O fixtures: 14 startup groups and 8 OLED groups pass. `python tests/host/heltec_v4_bench_target_tests.py` passes all 17 target admission groups; its prior blanket local-name prohibition is narrowed to the public D1 setup alias while preserving hardware identity/manufacturer-data/runtime-log prohibitions. Complete affected matrix, double target build and physical first-use acceptance remain pending.
