# OpenTrail

[![Host validation](https://github.com/nbjelanovic/OpenTrail/actions/workflows/host-validation.yml/badge.svg)](https://github.com/nbjelanovic/OpenTrail/actions/workflows/host-validation.yml)

OpenTrail is a free and open-source ESP32/LoRa platform for off-grid group communication, location awareness, and safety alerts. It is intended to keep a small group useful when cellular service and internet access are unavailable.

The base design is a self-contained portable client with its own power, display, input, radio, and GNSS-aware group status. Repeaters, remote archives, vehicle alerts, larger displays, and offline maps are optional additions—not requirements for basic operation.

> **Working names:** `Limited Underground` is the parent identity and `Limited Underground Trail` is the Android application and product family. The provisional tiers are `Trail Essential` (screenless, phone-required LoRa companion), `Trail Gold` (one touchscreen), `Trail Platinum` (two displays), and `Trail Repeater`. The shared desktop utility is `Limited Underground Firmware Loader`, visibly marked `Preview` and `Inspection only` until real writing and recovery pass. All names await professional clearance; no `®` is used. `OpenTrail` remains the repository/engineering name, and existing folders, namespaces, `OT-*` records, protocols, GATT/schema/crypto identifiers, and device IDs remain stable. See [Decision 0008](docs/decisions/0008-limited-underground-trail-working-product-family.md).

## Project status

| Area | Current state |
| --- | --- |
| Phase | Architecture, host-tested components, bounded bench proofs, and one experimentally flashed Heltec target |
| Latest increment | OT-082 build-compiles and host-tests a bounded target-side source for the default NVS build configuration and four decoded security-state values; it remains unreachable from runtime, was not device-executed, and creates no complete inventory or read authority |
| Proven so far | The 145-executable C++ host matrix passes; OT-065 through OT-071 add protected-store, trusted-authority composition, KV slot-media, target NVS-backend/context, transition admission, and privacy-safe source-evidence tooling. OT-074 physically verified the exact installed 3,072-byte partition table and complete blank 1 MiB protected-storage source region. OT-075 deterministically froze the exact offline candidate partition artifact. OT-076 retained the exact installed application as a private verified recovery artifact. OT-077 freezes a fail-closed exact ROM recovery contract. OT-078 adds pure fail-closed provider-class and monotonic-floor admission. OT-079 adds an offline-only complete-inventory verifier. OT-080 rejects unsafe host-side raw-key materialization and freezes five decoded ESP-IDF metadata APIs. OT-081 implements only the six-slot coarse key-roster leaf. OT-082 adds only the default-build NVS configuration and decoded security-state source. Both leaves are build-compiled, host-tested, unreachable from runtime, and unexecuted on a device; together they still cannot prove provisioning/reservation, configured-NVS conflict, runtime overrides, complete inventory, or provider suitability. The Heltec target-only admission suite passes eleven groups, and `OT-DEV-001` still runs the separately accepted OT-064 app with experimentally observed startup/status OLED and privacy-safe BLE advertising. Android passes 136 JVM tests across thirteen suites, clean lint, debug assembly, manifest inspection, bounded physical install/lifecycle/artwork observations, and exact-service discovery. V1 Companion has an evidence-weighted canonical release track. There is still no physically proved recovery, authorized partition transition, active protected partition, physical inventory, selected/provisioned key block or rollback-floor field, runtime storage/key/bond backend, GATT exchange, successful app/device authorization or Ready state, real LoRa/GNSS path, release signing/store package, power/endurance, field, regulatory, or support evidence |
| Planned first release | Four Trail Essential device-and-Android-phone pairs, with later capacity evidence up to eight active clients and at most one optional authorized repeater |
| Not yet proven | Production firmware, supported client hardware, authenticated on-device transport, protected keys, complete-client GNSS/UI, field range, power endurance, or regulatory acceptance |

OpenTrail is not production-ready, and no hardware is currently listed as supported. See the [dated progress log](docs/PROGRESS_LOG.md) for recent work and the [engineering backlog](tasks/BACKLOG.md) for exact acceptance evidence and remaining gates.

## Start here

- [Documentation guide](docs/README.md) — organized entry point for every project area
- [Architecture](docs/ARCHITECTURE.md) — system layers, roles, interfaces, and failure boundaries
- [Product boundaries](docs/PRODUCT_BOUNDARIES_V0.md) — base system versus optional additions
- [Project status and open decisions](docs/PROJECT_STATUS.md) — current assumptions, evidence, and unresolved choices
- [Dated progress log](docs/PROGRESS_LOG.md) — public chronology, newest day first
- [Engineering backlog](tasks/BACKLOG.md) — work-item status and acceptance evidence
- [Hardware inventory](hardware/INVENTORY.md) — available, ordered, missing, and unverified equipment
- [Contributing](CONTRIBUTING.md) and [security reporting](SECURITY.md)

## Release boundary

The current release goal is V1 Companion: a Trail Essential LoRa device uses
one privately authorized Android phone as its interface. The first release must
prove four frozen device-phone pairs, direct group operation, recovery, range,
endurance, and usability without depending on a server or internet connection.
The phone is an intentional V1 interface dependency; a repeater remains
optional.

The future V2 Integrated goal moves the interface onto a dedicated touchscreen
client. Its standalone no-phone acceptance path and the later four-plus-
repeater and eight-plus-repeater capacity steps remain separate evidence gates.

The existing [capacity policy](docs/testing/FIRST_RELEASE_CAPACITY_V0.md),
[four-person pilot plan](docs/testing/FOUR_PERSON_PILOT_V0.md), and
[result evaluator](docs/testing/FOUR_PERSON_PILOT_RESULT_V0.md) still govern the
standalone path. A Companion-specific pilot amendment remains required before
V1 field evidence can be accepted.

## How it fits together

```text
       self-contained client(s)
   display + input + GNSS + LoRa
               |
       direct group traffic
               |
      optional single repeater

Optional, fail-independent additions:
archive service | OpenGauge alerts | offline maps / larger display
```

- **Clients** originate and receive compact messages, positions, status, and alerts.
- An optional **repeater** may forward eligible immutable traffic once; it cannot become a base-system dependency.
- An optional **archive** may retain selected breadcrumbs only while explicitly enabled; base radio operation remains independent.
- **OpenGauge** may provide normalized critical events, never raw CAN/J1939 traffic.
- **Offline maps and larger displays** add local context and never travel over LoRa.

## Intended capabilities

- Compact LoRa messaging, position/status sharing, priority alerts, and controlled relaying
- Explicit start/stop privacy controls and graceful behavior when GPS or peers disappear
- Portable, vehicle-mounted, fixed-repeater, and larger touchscreen forms over shared protocols
- Locally transferred offline maps from a licensed, replaceable package source
- Quick group-defined alerts for emergencies, medical needs, recovery, disabled vehicles, fuel/tools, and other field events
- A versioned interface for normalized critical alerts from OpenGauge or other approved producers

These are product goals unless the linked evidence explicitly says otherwise.

## Hardware status

The available bench inventory contains two assembled Heltec V4 OLED units: `OT-DEV-001` now runs the experimental OT-064 OpenTrail target and physically showed the Trail startup logo followed by `BLE ADVERTISING`; untouched `OT-DEV-002` remains a MeshCore USB Companion. It also contains one packaged Seeed SenseCAP Solar P1-Pro MeshCore repeater and one owner-reported Wio Tracker L1 Pro candidate. A privacy-safe USB check shows both Heltecs detect and activate their connected GNSS hardware and emit GPS telemetry; the SenseCAP reached a live fix, with subsequent checks increasing through four, seven, and eight satellites. The Wio's first read-only pass identified public USB model `Seeed Wio Tracker L1`, USB Companion `v1.17.0-727fc05`, the configured 910.525 MHz/BW 62.5 kHz/SF7/CR5/22 dBm profile, zero error/traffic counters across four cycles, and GNSS detected but inactive. No coordinates, identities, channel values, or transient ports were published, and the Wio did not transmit. Exact Wio label/revision and pre-write state, over-air interoperability, GNSS fix/loss, RF/regulatory fit, power/endurance, BLE, and recovery remain open. The accepted OLED evidence is startup/status only—not interactive UI or supported-hardware evidence. The Heltec kits and Wio are bench candidates, not the frozen board-level parts for the first complete touchscreen client. The integrated solar SenseCAP may be evaluated as the optional packaged repeater; exact complete-client hardware for the four-person pilot remains unfrozen.

See the [hardware inventory](hardware/INVENTORY.md), [regulatory reconciliation](hardware/HARDWARE_REGULATORY_INVENTORY_2026-08-10.md), and partially executed [Wio Tracker bring-up procedure](hardware/WIO_TRACKER_L1_PRO_BRINGUP.md). A radio preset or in-band frequency alone is not proof of legal operation.

## Repository layout

| Path | Purpose |
| --- | --- |
| `docs/` | Architecture, product boundaries, decisions, specifications, and dated project records |
| `android/` | Native Android Local-test shell plus production-shaped BLE ownership, authorization, and renderer-neutral presentation layers |
| `firmware/components/` | Hardware-independent, host-testable protocol and state components |
| `firmware/targets/` | Separately composed applications for defined boards and roles |
| `hardware/` | Inventory, bring-up procedures, power/RF details, and compatibility evidence |
| `tests/` | Host, fixture, integration, and physical hardware evidence |
| `tools/` | Validation, planning, diagnostics, and evidence utilities |
| `prototypes/` | Time-bounded experiments that are not production architecture |
| `tasks/` | Prioritized engineering backlog and acceptance criteria |

## Safety and privacy boundary

OpenTrail is a supplemental communication and awareness aid, not a guaranteed rescue system. Missing GPS, maps, UI, peers, repeaters, archives, or OpenGauge data must degrade independently. Real location sharing and any archive capture require explicit user control, and public evidence must remain privacy-safe.

## License and contributions

OpenTrail is licensed under the [Apache License 2.0](LICENSE). Contributions are welcome through GitHub issues and pull requests; read [CONTRIBUTING.md](CONTRIBUTING.md) first and use [SECURITY.md](SECURITY.md) for sensitive reports.
