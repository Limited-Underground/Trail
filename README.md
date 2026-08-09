# OpenTrail

OpenTrail is a proposed free/open-source, ESP32-based off-road communication, location-awareness, and safety platform designed to keep a group useful when cellular service is unavailable.

## Project status

Architecture and bounded proof-of-concept phase. Two Heltec V4 OLED companions and a Seeed SenseCAP Solar repeater have close-range transport, packet-v0, flood-relay, and software-forced one-hop evidence. The OpenGauge critical-alert v0 semantic codec, OpenTrail trust/freshness/duplicate/rate ingress policy, and mirrored `OGK0` acknowledgement codec also have deterministic host evidence in both projects. A Wio Tracker L1 Pro for MeshCore is reported ordered and has a non-destructive arrival plan, but no evidence yet. These are bounded bench, host, and planning results—not a production/security protocol, authenticated physical alert link, direct-radio binding, field-range result, or supported-hardware declaration. No production firmware, selected display, or tested-compatible hardware list exists yet.

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

Read [the architecture](docs/ARCHITECTURE.md), [project status and assumptions](docs/PROJECT_STATUS.md), [the hardware inventory](hardware/INVENTORY.md), [the Wio Tracker arrival plan](hardware/WIO_TRACKER_L1_PRO_BRINGUP.md), [the critical-alert v0 contract](docs/integration/OPENGAUGE_CRITICAL_ALERT_V0.md), [the mirrored critical-alert ACK contract](docs/integration/OPENGAUGE_CRITICAL_ALERT_ACK_V0.md), [the identity/group threat model](docs/security/THREAT_MODEL_V0.md), [the group lifecycle and recovery UX](docs/security/GROUP_LIFECYCLE_V0.md), [the persistent-configuration envelope](docs/persistence/PERSISTENT_CONFIGURATION_V0.md), [the packet envelope](docs/protocol/EXPERIMENTAL_PACKET_V0.md), [the experimental delivery policy](docs/protocol/DELIVERY_POLICY_V0.md), [the controlled-forwarding policy](docs/protocol/FORWARDING_POLICY_V0.md), [the GPS/location abstraction](docs/location/GPS_ABSTRACTION.md), [the position-broadcast format and budget](docs/protocol/POSITION_BROADCAST_V0.md), [the diagnostics foundation](docs/DIAGNOSTICS.md), [the repeater proof](tests/hardware/OT-009-2026-08-08.md), and [the backlog](tasks/BACKLOG.md). OT-017 and OT-017A are complete for their bounded semantic/host scopes. Hardware identity, cryptography, physical alert transport, secret storage, and regulatory questions continue in parallel.

## License and contributions

OpenTrail is free/open-source software licensed under the
[Apache License 2.0](LICENSE). Contributions are welcome through GitHub issues
and pull requests; read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting
code or hardware evidence and use [SECURITY.md](SECURITY.md) for sensitive
security reports.
