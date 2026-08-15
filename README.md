# OpenTrail

[![Host validation](https://github.com/nbjelanovic/OpenTrail/actions/workflows/host-validation.yml/badge.svg)](https://github.com/nbjelanovic/OpenTrail/actions/workflows/host-validation.yml)

OpenTrail is a free and open-source ESP32/LoRa platform for off-grid group communication, location awareness, and safety alerts. It is intended to keep a small group useful when cellular service and internet access are unavailable.

The base design is a self-contained portable client with its own power, display, input, radio, and GNSS-aware group status. Repeaters, remote archives, vehicle alerts, larger displays, and offline maps are optional additions—not requirements for basic operation.

> **Working names:** `Limited Underground` is the parent identity and `Limited Underground Trail` is the Android application and product family. The provisional tiers are `Trail Essential` (screenless, phone-required LoRa companion), `Trail Gold` (one touchscreen), `Trail Platinum` (two displays), and `Trail Repeater`. The shared desktop utility is `Limited Underground Firmware Loader`, visibly marked `Preview` and `Inspection only` until real writing and recovery pass. All names await professional clearance; no `®` is used. `OpenTrail` remains the repository/engineering name, and existing folders, namespaces, `OT-*` records, protocols, GATT/schema/crypto identifiers, and device IDs remain stable. See [Decision 0008](docs/decisions/0008-limited-underground-trail-working-product-family.md).

## Project status

| Area | Current state |
| --- | --- |
| Phase | Architecture, host-tested components, and bounded close-range bench proofs |
| Latest increment | OT-059 freezes an evidence-bound build-only profile for the OT-DEV-001 Heltec candidate: 16 MiB flash, configured QIO/80 MHz, configured 2 MiB quad PSRAM at 80 MHz, and an exact five-partition factory/dual-OTA/application-state layout ending at the 16 MiB boundary. Target admission passes 4/4 and two pinned ESP-IDF v6.0.2 builds reproduce a 437,552-byte BIN with SHA-256 `F0E81310C62CA0C17CA2531AF9B0D5BD5E6E115E1649F84C97514F72D51D6A3A`. This is configuration/build evidence, not physical mode/frequency/PSRAM, recovery, flash, or runtime proof. OT-034 remains `partial`, and V1 remains 30% |
| Proven so far | A 123-executable C++ host matrix, including 13 bounded BLE-runtime-owner groups plus the accepted companion suites and target-local boot self-checks; 59 C# loader scenarios; and four build-only Heltec target admission groups. OT-059 passes exact default/generated-profile, image-header, partition-binary, size, link-map, and reproducibility checks twice under pinned ESP-IDF v6.0.2. Its DIO bootstrap header is expected while QIO is selected for post-boot operation; the profile itself has not run on hardware. Android passes 134 JVM tests across thirteen suites (protocol 29; application 105), warning-as-error lint, debug APK assembly, and manifest inspection; the 9,677,165-byte APK has SHA-256 `697D73A6E48F1850A2756FB0886A8201C653804FB5A2B9628DD26790C8EC65B1`. There is still no physical target runtime, admitted protected target storage/key/bond backend, successful app/device authorization or Ready state, real BLE coordinate source, phone GPS/map/tile path, Android OS/device/install proof, release signing/store package, production packet/radio target, physical display, or support evidence. Two Heltec clients and one SenseCAP repeater separately completed limited MeshCore-era bench evidence; one Wio candidate has partial USB/runtime/configuration and loader-recognition evidence only |
| Planned first release | Up to eight active clients in one group with at most one optional authorized repeater |
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

## Planned first-release boundary

The v0 target is intentionally small and staged:

1. Four identical standalone clients with no repeater or infrastructure dependency.
2. Four clients plus one optional authorized repeater.
3. Eight clients plus one optional authorized repeater.

Each stage must pass on frozen hardware and firmware before the next stage becomes a support claim. A client must remain useful without a server, internet connection, phone, laptop, vehicle connection, or repeater.

Read the [capacity policy](docs/testing/FIRST_RELEASE_CAPACITY_V0.md), [four-person pilot plan](docs/testing/FOUR_PERSON_PILOT_V0.md), and [result evaluator](docs/testing/FOUR_PERSON_PILOT_RESULT_V0.md) for the exact evidence sequence. The first pilot remains blocked until four complete client units satisfy the hardware and recovery freeze.

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

The available bench inventory contains two assembled Heltec V4 OLED MeshCore companions, one packaged Seeed SenseCAP Solar P1-Pro MeshCore repeater, and one owner-reported Wio Tracker L1 Pro candidate. A privacy-safe USB check shows both Heltecs detect and activate their connected GNSS hardware and emit GPS telemetry; the SenseCAP reached a live fix, with subsequent checks increasing through four, seven, and eight satellites. The Wio's first read-only pass identified public USB model `Seeed Wio Tracker L1`, USB Companion `v1.17.0-727fc05`, the configured 910.525 MHz/BW 62.5 kHz/SF7/CR5/22 dBm profile, zero error/traffic counters across four cycles, and GNSS detected but inactive. No coordinates, identities, channel values, or transient ports were published, and the Wio did not transmit. Exact Wio label/revision and pre-write state, over-air interoperability, GNSS fix/loss, RF/regulatory fit, power/endurance, BLE, and recovery remain open. The Heltec kits and Wio are bench candidates—not the frozen board-level parts for the first complete touchscreen client. The integrated solar SenseCAP may be evaluated as the optional packaged repeater; exact complete-client hardware for the four-person pilot remains unfrozen.

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
