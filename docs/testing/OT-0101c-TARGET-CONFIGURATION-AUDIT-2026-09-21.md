# OT-0101c Target and regional RF evidence baseline

VERIFIED 2026-09-21: repository evidence review only. The approved task reconciles
existing evidence; it does not authorize device access or certify a release.

## Result

The exact two received Heltecs can be identified and a bounded evaluation RF
configuration can be frozen as an evidence baseline. **Production support and
field/regulatory acceptance remain BLOCKED.** Neither board is promoted to
supported hardware. Owner acceptance of this report is pending.

## Exact target boundary

| Item | Evidence and permitted conclusion |
| --- | --- |
| OT-DEV-001 | [OT-103](../../tests/hardware/OT-103-2026-08-20.md): Heltec WiFi LoRa 32 V4, HTIT-WB32LAF, V4.2; ESP32-S3/S3R2 revision v0.2, 40 MHz, 16 MiB flash, 2 MiB PSRAM. Exact received identity admitted; not electrical/RF support. |
| OT-DEV-002 | [OT-119](../../tests/hardware/OT-119-2026-08-22.md): independently identified distinct unit, same model/revision and ROM memory/crystal profile. Do not infer this from the shared purchase bundle. |
| Radio/front end | SX1262 is documented family information. Current enrolled source uses front-end control GPIO 7/2/46. Neither source configuration nor family specifications close received-unit electrical validation. |
| Build boundary | [OT-246](OT-246-FAILURE-CAPTURE-2026-09-18.md) records enrolled/pair evaluation builds using Heltec V4.2 ESP32-S3, 16 MiB QIO 80 MHz, no-PSRAM build configuration. Physical PSRAM presence does not mean the evaluation uses it. No new firmware was built here. |
| Production target | The bench [target contract](../../firmware/targets/heltec_v4_bench/target-contract.json) retains supported=false. Evaluation firmware and restored Companion applications are distinct artifacts; neither is a complete accepted V1 radio product. |

## Bounded RF configuration

Current [enrolled driver](../../firmware/targets/heltec_v4_enrolled_eval/main/enrolled_radio_driver.cpp)
configures 915.0 MHz, 125 kHz bandwidth, SF7, CR4/5, explicit header, CRC,
LDRO disabled, sync word 0x12, preamble 8, and a **2 dBm command setpoint**.
The [driver interface](../../firmware/targets/heltec_v4_enrolled_eval/main/enrolled_radio_driver.hpp)
limits frames to 158 bytes. This is source verification, not calibrated power or
current physical measurement. [OT-247](OT-247-END-TO-END-RADIO-AUDIT-2026-09-20.md)
owns the current exchange/timing limits and unfinished activation gate.

The historical [OT-114 profile](../../tests/benchmarks/crypto/OT-114-OT005-US915-DIRECT-RADIO-PROFILE-EVIDENCE-V1.json)
records US915 and the same basic PHY with bounded over-air evidence, explicitly
uncalibrated. Its different historical frame sizes do not establish the current
158-byte enrolled path. [OT-234 pair evaluation](OT-234-PAIR-RADIO-TARGET-2026-09-14.md)
has a separate 154-byte bound. Earlier MeshCore 910.525 MHz / 62.5 kHz settings are
historical configuration, not this OpenTrail baseline.

The twelve saved region choices are configuration labels, not twelve accepted
channel plans. [Decision 0110](../decisions/0110-protected-region-selection-without-transmit-authority.md)
explicitly withholds transmit authority from selection alone. No new region,
channel plan, power allowance or legal conclusion is introduced here.

## Installation, recovery and remaining release gates

[OT-246 restoration evidence](OT-246-FAILURE-CAPTURE-2026-09-18.md) records both
original application spans (733184 bytes) and NVS spans (12288 bytes) restored,
protected/final spans independently verified, resets completed, custody closed
and normal screens confirmed. This is historical successful restoration, not a
standing write authorization or a verified current USB roster. Each later trial
must bind the exact device, current original, candidate/hash, offsets, protected
regions and recovery procedure under [target preflight](../firmware-porting-lessons.md).
No release candidate or new install image is selected by this audit.

| Open gate | Evidence needed before the affected claim |
| --- | --- |
| Antenna/RF path | Per-unit installed antenna model, band, gain, connector, feed cable/loss and received-unit radio/front-end evidence. Supplied high-band antennas were confirmed attached in OT-114, but gain and electrical identity were not established. |
| Regulatory/field operation | Exact unit FCC ID, applicable grant and antenna/installation/operational exhibits, and review against exact firmware/PHY/power/antenna configuration. Existing records contain no accepted closure; this audit is not current legal verification. |
| Output power | Calibrated output/EIRP evidence where required. A 2 dBm software command is not a measured or certified radiated output. |
| Product support | Accepted production target and artifacts, complete two-phone/two-node protected messaging, recovery/fault coverage, field limits and endurance. The enrolled activation/timing defect remains tracked under OT-0247a and successors. |
| Power/GNSS | Target-specific power/battery and GNSS evidence remains required by current project status; identity and display observations do not close it. |

## Stale-record reconciliation

The 2026-08-20 [regulatory inventory](../../hardware/HARDWARE_REGULATORY_INVENTORY_2026-08-10.md)
claimed OT-DEV-002 had runtime-only identity. OT-119 supersedes that identity
statement; it does not close the regulatory/antenna gaps. An explicit dated
correction now points to OT-119 without rewriting its historical table.

The bench target contract still contains null exact_received_revision/rf_variant.
Treat these as an older contract boundary, not evidence that OT-103/OT-119 are
absent. This audit does not rewrite hash-bound firmware inputs. A release
manifest must reconcile those fields to per-unit evidence before claiming support.

## Validation and scope

Independent source/evidence review agrees with this boundary. Repository document
checks passed; all 18 repository-doc regression tests passed; git diff --check passed.
The checker previously rejected the approved lowercase child-ID convention. Its
three suffix recognizers now accept historical uppercase and lowercase suffixes,
with regression coverage that still rejects an unregistered child. No firmware,
protocol, manifest, hardware, external legal source, signing or public capability
claim changed. No V1 progress credit. The hosted checklist receives the report
for owner review; OpenTrail changes remain local and uncommitted.
