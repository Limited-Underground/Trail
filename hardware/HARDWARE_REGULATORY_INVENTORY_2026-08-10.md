# Hardware and US Regulatory Inventory

Initial reconciliation: 2026-08-10

Last updated: 2026-08-20

Backlog gate: OT-003A (`partial`)

This document reconciles connected-device evidence with current official
manufacturer information and US Part 15 operating conditions. It is an
engineering inventory, not a certification or legal opinion. A runtime model
name or USA radio preset does not establish the exact SKU, FCC authorization,
approved antenna, or lawful operating mode.

## Evidence layers

| Device | Confirmed on this unit | Official family information | Still required |
| --- | --- | --- | --- |
| `OT-DEV-001` | Exact received Heltec Automation `WiFi LoRa 32 V4`, PCB/RF-variant model `HTIT-WB32LAF`, revision `V4.2`, documented-high-band profile; owner photos show ESP32-S3 / ESP32-S3R2 revision v0.2, 40 MHz, 16 MiB flash, 2 MiB PSRAM, and a separate Quectel L76K GNSS module; historical MeshCore/OpenTrail bench evidence records the bounded runtime and prior 910.525 MHz/BW 62.5 kHz/SF7/CR5/10 dBm configuration | Official V4.2 datasheet Table 1.5 records `HTIT-WB32LAF` at `868-928 MHz` and `28 +/- 1 dBm`; Table 3.5.1 separately records `863-928 MHz` and `28 +/- 1 dBm`; Table 3.1 identifies ESP32-S3R2, SX1262, 16 MiB flash, and 2 MiB PSRAM. The two band ranges remain distinct and family-level chip/power facts are not received-unit electrical proof | FCC ID/grant and exhibits, electrical radio/front-end verification, installed antenna model/gain/connector and cable loss, exact OpenTrail region/MTU/PHY/power configuration, pin map and GNSS wiring/fix/loss evidence, power-system revision, and authorization for the exact configuration |
| `OT-DEV-002` | MeshCore reports `Heltec V4 OLED`; USB Companion `v1.16.0-07a3ca9`; same recorded radio settings; clean serial/runtime checks; same two-unit purchase record as `OT-DEV-001`; firmware detected/activated GNSS and emitted a GPS telemetry field | Same likely WiFi LoRa 32 V4 family and bundle match | Everything listed for `OT-DEV-001`, plus independent ROM/flash/security evidence |
| `OT-DEV-003` | MeshCore reports `Seeed SenseCap Solar`; Repeater `v1.16.0-07a3ca9`; 910.525 MHz/BW 62.5 kHz/SF7/CR5/22 dBm; repeating and bounded bench forwarding proved; owner purchase record is P1-Pro ASIN `B0FMDHBWX8`; GNSS became active and reached a live fix, with later checks at 4, 7, and 8 satellites | Seeed's current MeshCore product identifies P1-Pro SKU `100023690`, XIAO nRF52840 Plus, Wio-SX1262, L76K GNSS, battery, and solar enclosure | Exact received label/revision, FCC ID/grant, GNSS accuracy/loss/power behavior, battery/enclosure revision, installed antenna/cable/gain, solar/endurance evidence, and authorization for the exact configuration |
| `OT-CAND-004` Wio Tracker L1 Pro | Owner-reported Pro unit; Windows public USB model `Seeed Wio Tracker L1`, family `2886:1667`; owner-flashed USB Companion `v1.17.0-727fc05` build 09-Aug-2026; repeat false; read-only 910.525 MHz/BW 62.5 kHz/SF7/CR5/22 dBm configuration; four stable zero-error/zero-traffic cycles; GNSS detected but inactive with no GPS telemetry | Seeed identifies nRF52840, Wio-SX1262, L76K GNSS, OLED, USB-C, solar input, and an I-PEX-to-SMA antenna path for this family | Shipping/pre-write state was not preserved. Record exact label/SKU/revision, RF variant, FCC ID/grant, antenna/cable/gain, GNSS fix/loss, power/endurance, BLE, DFU/recovery, over-air interoperability, and authorization for the exact configuration; do not promote this bounded experiment to compatibility |

The family matches narrow the inspection work but do not close it. OT-103 now
admits exact privacy-safe received-unit identity for `OT-DEV-001`; it does not
supply an FCC ID or equipment grant, verify the electrical radio path or
antenna, establish a legal region/configuration, or make the unit compatible or
supported. Runtime-only identity remains the boundary for `OT-DEV-002`.

## Current radio snapshot is not a compliance finding

| Role | Frequency | Bandwidth | SF / CR | Recorded power |
| --- | ---: | ---: | --- | ---: |
| Heltec companions | 910.525 MHz | 62.5 kHz | SF7 / CR5 | 10 dBm |
| SenseCAP repeater | 910.525 MHz | 62.5 kHz | SF7 / CR5 | 22 dBm |
| Wio candidate, repeat off | 910.525 MHz | 62.5 kHz | SF7 / CR5 | 22 dBm |

The Wio values were read without transmitting. Its reported `USA` setup and
matching center-frequency/profile comparison are not an over-air or compliance
result.

The center frequency is inside the US 902-928 MHz ISM band, but that alone is
insufficient. FCC Part 15 operation is subject to 47 CFR 15.5, including
avoiding harmful interference and accepting interference. Authorization also
depends on the exact transmitter, modulation/firmware mode, occupied bandwidth,
conducted power, antenna type and gain, cable loss, and grant conditions.

The commonly cited digital-modulation provisions of 47 CFR 15.247 include a
500 kHz minimum 6 dB bandwidth condition. The recorded MeshCore bandwidth is
62.5 kHz, so the project must not assume that selecting `USA` or staying under
a headline power limit proves compliance under that provision. The exact FCC
grant, exhibits, instructions, approved antennas, and rule path for each
physical variant must be reviewed before public field deployment.

## Fail-closed field gate

Before any OpenTrail field test is described as an authorized US deployment:

1. Record a privacy-safe exterior photograph and transcription of model,
   hardware revision, and FCC ID. OT-103 satisfies only the model/revision portion
   for `OT-DEV-001`; its FCC ID remains absent. Exclude serial numbers, MAC
   addresses, pairing identifiers, keys, and precise location.
2. Match each FCC ID to the official equipment authorization and retain the
   grant plus relevant operational, antenna, and installation exhibits.
3. Record the installed LoRa antenna model, connector, supported band, gain,
   feed-cable type, and cable loss. Treat an antenna change as a new review.
4. Freeze the firmware build, role, region, frequency, bandwidth, spreading
   factor, coding rate, transmit power, and repeater setting in the test plan.
5. Verify that the grant and manufacturer instructions cover that exact mode,
   power, antenna type/gain, and installation. Lower transmit power by itself
   is not proof of authorization.
6. Keep early work bounded and controlled with the correct antenna attached;
   log configuration without publishing private device identity or claiming
   field legality until this gate closes.

Until then, existing results remain bounded close-bench engineering evidence.
OT-003A stays `partial`, and no device enters a tested-compatible or
regulatory-approved list.

## Official sources

- [Heltec WiFi LoRa 32 V4 documentation](https://docs.heltec.org/en/node/esp32/wifi_lora_32/index.html)
- [Heltec WiFi LoRa 32 V4.2 datasheet](https://resource.heltec.cn/download/WiFi_LoRa_32_V4/datasheet/WiFi_LoRa_32_V4.2.0.pdf)
- [Owner's two-unit V4 GPS bundle purchase listing](https://www.amazon.com/dp/B0FS1WQWKF)
- [Seeed SenseCAP Solar Node specification](https://wiki.seeedstudio.com/meshtastic_solar_node/)
- [Owner's SenseCAP P1-Pro purchase listing](https://www.amazon.com/dp/B0FMDHBWX8)
- [Seeed SenseCAP P1-Pro for MeshCore product](https://www.seeedstudio.com/SenseCAP-Solar-Node-P1-Pro-for-Meshcore-p-6741.html)
- [Seeed Wio Tracker L1 Pro for MeshCore product](https://www.seeedstudio.com/Wio-Tracker-L1-Pro-for-Meshcore-p-6717.html)
- [Seeed LoRa antenna selection guide](https://wiki.seeedstudio.com/lora_antenna_selection_guide/)
- [47 CFR 15.5 — General conditions of operation](https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-A/section-15.5)
- [47 CFR 15.247 — spread-spectrum and digital-modulation provisions](https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/section-15.247)
