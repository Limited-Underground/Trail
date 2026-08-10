# Hardware and US Regulatory Inventory

Date: 2026-08-10  
Backlog gate: OT-003A (`partial`)

This document reconciles connected-device evidence with current official
manufacturer information and US Part 15 operating conditions. It is an
engineering inventory, not a certification or legal opinion. A runtime model
name or USA radio preset does not establish the exact SKU, FCC authorization,
approved antenna, or lawful operating mode.

## Evidence layers

| Device | Confirmed on this unit | Official family information | Still required |
| --- | --- | --- | --- |
| `OT-DEV-001` | MeshCore reports `Heltec V4 OLED`; USB Companion `v1.16.0-07a3ca9`; ESP32-S3 ROM evidence; 2 MB PSRAM; 16 MB flash; 910.525 MHz/BW 62.5 kHz/SF7/CR5/10 dBm during recorded bench work | Heltec's WiFi LoRa 32 V4 documentation describes ESP32-S3R2, external 16 MB flash, SX1262, OLED, solar input, an up-to-28 dBm LoRa path, and GNSS connector | Exterior product/revision label, exact frequency variant, FCC ID/grant, installed antenna model/gain/connector, cable loss, pin map, and power-system revision |
| `OT-DEV-002` | MeshCore reports `Heltec V4 OLED`; USB Companion `v1.16.0-07a3ca9`; same recorded radio settings; clean serial/runtime checks | Same likely WiFi LoRa 32 V4 family match | Everything listed for `OT-DEV-001`, plus independent ROM/flash/security evidence |
| `OT-DEV-003` | MeshCore reports `Seeed SenseCap Solar`; Repeater `v1.16.0-07a3ca9`; 910.525 MHz/BW 62.5 kHz/SF7/CR5/22 dBm; repeating and bounded bench forwarding proved | Seeed documents the SenseCAP Solar Node family as nRF52840 plus SX1262, 862-930 MHz, up to 22 dBm, 868-915 MHz 2 dBi antenna, USB-C/Grove, solar charging, and P1/P1-Pro variants; GNSS is a P1-Pro feature | Exact P1/P1-Pro SKU and label, FCC ID/grant, GNSS presence, battery/enclosure revision, installed antenna/cable/gain, solar/endurance evidence, and authorization for the exact configuration |
| `OT-CAND-004` Wio Tracker L1 Pro | Ordered; no received-unit evidence | Seeed identifies nRF52840, Wio-SX1262, L76K GNSS, OLED, USB-C, solar input, and an I-PEX-to-SMA antenna path for this family | Preserve shipping state and follow the arrival plan; do not promote vendor specifications to tested compatibility |

The family matches narrow the inspection work but do not close it. The Heltec
runtime string is consistent with the V4 family; it is not a substitute for the
product label or equipment grant.

## Current radio snapshot is not a compliance finding

| Role | Frequency | Bandwidth | SF / CR | Recorded power |
| --- | ---: | ---: | --- | ---: |
| Heltec companions | 910.525 MHz | 62.5 kHz | SF7 / CR5 | 10 dBm |
| SenseCAP repeater | 910.525 MHz | 62.5 kHz | SF7 / CR5 | 22 dBm |

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
   hardware revision, and FCC ID. Exclude serial numbers, MAC addresses, pairing
   identifiers, keys, and precise location.
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
- [Seeed SenseCAP Solar Node specification](https://wiki.seeedstudio.com/meshtastic_solar_node/)
- [Seeed LoRa antenna selection guide](https://wiki.seeedstudio.com/lora_antenna_selection_guide/)
- [47 CFR 15.5 — General conditions of operation](https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-A/section-15.5)
- [47 CFR 15.247 — spread-spectrum and digital-modulation provisions](https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/section-15.247)
