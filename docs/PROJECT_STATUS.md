# OpenTrail Project Status, Assumptions, and Open Questions

Status date: 2026-08-08

## Conceptual goals

- Offline group communication and location awareness using ESP32 and LoRa
- Portable, vehicle, repeater, and larger touchscreen configurations
- Priority emergency/status messages, store-forward where useful, and graceful disconnection
- Offline local maps and a normalized OpenGauge critical-alert input

The close-range MeshCore path now has bounded transport, experimental OpenTrail packet-v0, and three-node MeshCore repeater hardware evidence including a software-forced route with a repeat-off negative control. Fixed-capacity C++ radio, codec, identity lifecycle, group-access policy, non-secret configuration persistence, acknowledgement/retry/expiry, duplicate suppression, controlled forwarding, priority admission, GPS fix validation/age handling, compact position encoding, LoRa airtime calculation, and redacted diagnostics components have deterministic host tests. Cryptographic joining, persistent secret/group/counter state, wire-level authenticated acknowledgements/priority, physical field repeater behavior, physical GPS compatibility/performance, position scheduling/hardware transmission, maps, store-forward behavior, OpenGauge integration, a direct SX1262 binding, rendered UI, and field performance remain unvalidated.

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
| Two Heltec V4 LoRa-capable boards | Both units are runtime-identified as **Heltec V4 OLED** and run MeshCore USB Companion `v1.16.0-07a3ca9` with matching USA/Canada settings (910.525 MHz, BW 62.5 kHz, SF7, CR5, 10 dBm). Antennas were user-confirmed attached. Raw-RX evidence established channel match, MAC validation, decryption, queue notification, and application retrieval. A temporary private-channel sample delivered 5/5 numbered messages each direction with 0 loss, 0 duplicates, 233.3-247.2 ms latency, 11.25-12.25 dB SNR, and zero receive/core errors. A separate packet-v0 run delivered 3/3 C++-encoded and decoded frames each direction with no loss, duplicates, or errors. Both temporary channels were erased and verified empty. See `tests/hardware/OT-007A-2026-08-08.md` and `tests/hardware/OT-007-2026-08-08.md`. `OT-DEV-001` has ROM-level ESP32-S3/2 MB PSRAM/16 MB flash evidence; `OT-DEV-002` does not. | OT-004, OT-006, and the bounded OT-007 proof use this bench evidence. Usable RSSI, fine-grained airtime, field range/mobility, regulatory constraints, and exact SKU/RF/antenna/pinout/power questions remain. |
| Seeed SenseCAP solar node | Runtime-identified as **Seeed SenseCap Solar**, USB `VID 2886:0059`, running MeshCore Repeater `v1.16.0-07a3ca9` at 910.525 MHz/BW 62.5/SF7/CR5/22 dBm with repeat enabled. Both Heltecs received its advert and remotely read its synchronized clock. A temporary private-channel run produced exactly +2 flood RX/+2 flood TX. Explicit one-hop direct routes then succeeded both ways; with repeat off, the same route failed with +1 direct RX/+0 direct TX and no destination message, proving the repeater was required. See `tests/hardware/OT-009-2026-08-08.md`. | Exact P1/P1 Pro SKU and internals, battery/GPS/antenna details, solar endurance, physical field behavior/range, and regulatory validation remain. |
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

- Exact commercial Heltec and Seeed SKUs, regulatory authorization, antenna/RF details, and power-source characteristics
- Reference MCU/radio/display/GPS/storage hardware and minimum supported resource tier
- Portable, vehicle, fixed-relay, and touchscreen power/environmental requirements
- Whether a single ESP32 can meet the chosen large-display map workload

### Protocol and security

- Direct/repeater topology, modulation profiles, airtime budget, broadcast cadence, and congestion policy
- Identity/name/alias/membership boundaries and the OT-013 invitation/promotion/revoke/rekey/recovery policy are defined and host-tested. Exact Node-ID/alias derivation, production administrator quorum, authenticated join-handshake instantiation, encryption, key storage, rollback protection, persistent recovery, rendered UX, and physical lifecycle evidence remain under partial OT-005 and later gates
- Packet-v0 encoding/budget, position payload, host-only acknowledgement/retry/expiry/duplicate/forwarding/priority policies, and OT-014 non-secret configuration persistence are bounded and tested. Authenticated ACK/routing/priority wire encoding, measured deployed timing, persistent message/duplicate counter integrity and secure rollback, realistic contention, and final queue/cache limits remain
- Firmware compatibility policy and OTA/update architecture

### Maps and interface

- Map data source/license, package/container, renderer, storage medium, transfer method, and update workflow
- Touchscreen UI framework and distracted-driving/safe-use constraints
- OpenGauge alert schema transport, trust boundary, rate limits, and physical/wireless connection

### Governance

- Open-source license, contribution policy, code of conduct, security reporting, CI, release/signing, and supported-hardware evidence policy

## Next decision checkpoint

OT-004, OT-006, OT-007, OT-008, OT-011, OT-012, OT-013, OT-014, and OT-015's bounded foundations are complete. OT-009 now has its three-node host simulation, close-range MeshCore relay evidence, and a software-forced one-hop route with a repeat-off negative control, but still needs authenticated routing fields, a direct SX1262/OpenTrail binding, and field evidence; OT-010's priority queue passes but needs authenticated wire priority, realistic mixed traffic, and rendered failure UX. Next specify and host-test OT-017's normalized OpenGauge critical-alert input while OT-005 cryptography, OT-009 field/direct-radio work, regulatory, physical inventory, secret/counter persistence, GPS hardware, position scheduling, and direct-radio airtime gates remain explicit.
