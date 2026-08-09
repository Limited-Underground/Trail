# OpenTrail

OpenTrail is a proposed free/open-source, ESP32-based off-road communication, location-awareness, and safety platform designed to keep a group useful when cellular service is unavailable.

## Project status

Architecture and bounded proof-of-concept phase. Two Heltec V4 OLED boards running MeshCore USB Companion completed a close-range private-channel transport characterization and a separate OpenTrail packet-v0 proof. The v0 run delivered three C++-encoded/checksummed frames in each direction with no loss, duplicates, or errors. This is bench evidence through an experimental MeshCore text adapter, not a production/security protocol, direct-radio binding, field-range result, or supported-hardware declaration. No production firmware, selected display, or tested-compatible hardware list exists yet.

## Intended capabilities

- Compact LoRa messaging, location/status broadcasts, priority alerts, and controlled relaying
- GPS-backed group awareness with graceful operation when GPS or peers disappear
- Portable, vehicle-mounted, repeater, and approximately 7–10 inch touchscreen roles
- Offline maps transferred locally from a phone or computer using a licensed, replaceable package format
- Quick actions such as SOS, medical, recovery, disabled vehicle, fuel/tools, wildlife, and group-defined alerts
- A versioned external interface for normalized critical alerts from OpenGauge or other telemetry producers

These are product goals, not verified capabilities.

## Repository layout

| Path | Purpose |
| --- | --- |
| `docs/` | Architecture, assumptions, decisions, and specifications |
| `firmware/components/` | Hardware-independent and reusable firmware components |
| `firmware/targets/` | Deployable applications for a defined board/role |
| `hardware/` | Board inventories, wiring, power, enclosure, and compatibility evidence |
| `tests/` | Host, integration, protocol, and hardware test assets |
| `tools/` | Development, packaging, provisioning, and diagnostic tools |
| `prototypes/` | Time-bounded experiments that are not production architecture |
| `tasks/` | Prioritized engineering backlog and acceptance criteria |

## Design boundary

OpenTrail owns trail networking, group/location behavior, messaging, maps, and alert presentation/relay. It does not decode raw CAN/J1939. OpenGauge integration occurs only through a documented, normalized, versioned alert interface.

## Start here

Read [the architecture](docs/ARCHITECTURE.md), [project status and assumptions](docs/PROJECT_STATUS.md), [the identity/group threat model](docs/security/THREAT_MODEL_V0.md), [the transport evidence](tests/hardware/OT-007A-2026-08-08.md), [the packet-v0 proof](tests/hardware/OT-007-2026-08-08.md), and [the backlog](tasks/BACKLOG.md). OT-005's identity boundaries and lifecycle model are now host-tested, while its cryptographic and physical lifecycle gates remain. The next implementation step is OT-008 acknowledgement, retry/expiry, and duplicate handling against the fake transport. Hardware identity and regulatory questions continue in parallel.

## License and contributions

The project is intended to be free/open source and hosted on GitHub, but a license and contribution policy have not yet been selected. Do not accept external code until those governance decisions are recorded.
