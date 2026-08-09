# OpenTrail Project Status, Assumptions, and Open Questions

Status date: 2026-08-08

## Conceptual goals

- Offline group communication and location awareness using ESP32 and LoRa
- Portable, vehicle, repeater, and larger touchscreen configurations
- Priority emergency/status messages, store-forward where useful, and graceful disconnection
- Offline local maps and a normalized OpenGauge critical-alert input

Only the close-range two-node MeshCore transport path has bounded hardware evidence. Group workflows, location, maps, priorities, store-forward behavior, OpenGauge integration, and field performance remain unvalidated.

## Decisions captured

- OpenTrail and OpenGauge are separate projects and must remain independently operable.
- OpenTrail will not decode raw vehicle CAN/J1939 traffic.
- Hardware-specific code will be isolated behind interfaces.
- LoRa will carry compact state/events/messages, not map packages or high-rate telemetry.
- Forwarding will be controlled and measured before any mesh topology is adopted.
- Protocols and stored configuration will be versioned and defensively decoded.
- Loss of GPS, maps, UI, peers, or OpenGauge must degrade independently.
- Offline-map formats/providers remain replaceable and must permit offline use with correct attribution.

## Available hardware and current evidence

| Item | Current status | Required evidence |
| --- | --- | --- |
| Two Heltec V4 LoRa-capable boards | Both units are runtime-identified as **Heltec V4 OLED** and run MeshCore USB Companion `v1.16.0-07a3ca9` with matching USA/Canada settings (910.525 MHz, BW 62.5 kHz, SF7, CR5, 10 dBm). Antennas were user-confirmed attached. Raw-RX evidence established channel match, MAC validation, decryption, queue notification, and application retrieval. A temporary private-channel sample delivered 5/5 numbered messages each direction with 0 loss, 0 duplicates, 233.3-247.2 ms latency, 11.25-12.25 dB SNR, and zero receive/core errors; the channel was erased and verified empty afterward. Earlier timeouts were harness false negatives caused by exact full-display-text matching. See `tests/hardware/OT-007A-2026-08-08.md`. `OT-DEV-001` has ROM-level ESP32-S3/2 MB PSRAM/16 MB flash evidence; `OT-DEV-002` does not. | Use the bench evidence for OT-004/OT-006; usable RSSI, fine-grained airtime, field range/mobility, regulatory constraints, and exact SKU/RF/antenna/pinout/power questions remain. |
| Seeed solar device, believed Solar Pro/P1-type | Identity and programmability unknown | Exact model, datasheet, interfaces, firmware access, battery/solar characteristics |
| Two approximately 7-inch touchscreens | Original test intent; no exact hardware identified | Board/display/controller, interface, resolution, memory/storage needs, availability |

Hardware is not added to a tested-compatible list until repeatable evidence exists.

## Assumptions to validate

- The Heltec boards are legal/configurable for the user's operating region and can form the first two-node test bed.
- ESP32 resources are sufficient for selected map/UI behavior after benchmarking.
- Local Wi-Fi SoftAP may support setup and map transfer without Internet, subject to UX/security/storage testing.
- GPS modules and suitable antennas/power arrangements will be selected separately.
- Alerting is supplemental and cannot guarantee delivery, location accuracy, or emergency response.

## Unresolved decisions

### Product and hardware

- Exact initial Heltec and Seeed models, regulatory region, operating frequency, antennas, and power sources
- Reference MCU/radio/display/GPS/storage hardware and minimum supported resource tier
- Portable, vehicle, fixed-relay, and touchscreen power/environmental requirements
- Whether a single ESP32 can meet the chosen large-display map workload

### Protocol and security

- Direct/repeater topology, modulation profiles, airtime budget, broadcast cadence, and congestion policy
- Node-ID derivation, group identity, join/revocation/recovery flow, authentication, encryption, and key storage
- Packet encoding, maximum payloads, acknowledgement classes, retries, TTL, duplicate-cache size, and queue limits
- Firmware compatibility policy and OTA/update architecture

### Maps and interface

- Map data source/license, package/container, renderer, storage medium, transfer method, and update workflow
- Touchscreen UI framework and distracted-driving/safe-use constraints
- OpenGauge alert schema transport, trust boundary, rate limits, and physical/wireless connection

### Governance

- Open-source license, contribution policy, code of conduct, security reporting, CI, release/signing, and supported-hardware evidence policy

## Next decision checkpoint

Use the completed OT-007A bench evidence to define OT-004's radio transport contract and OT-006's experimental byte budget without freezing the full network protocol. In parallel, confirm the regulatory constraints applicable to the selected USA/Canada preset and record the remaining physical SKU/RF/antenna details when available.
