# OpenTrail

[![Host validation](https://github.com/nbjelanovic/OpenTrail/actions/workflows/host-validation.yml/badge.svg)](https://github.com/nbjelanovic/OpenTrail/actions/workflows/host-validation.yml)

OpenTrail is a free and open-source ESP32/LoRa platform for off-grid group communication, location awareness, and safety alerts. It is intended to keep a small group useful when cellular service and internet access are unavailable.

The base design is a self-contained portable client with its own power, display, input, radio, and GNSS-aware group status. Repeaters, remote archives, vehicle alerts, larger displays, and offline maps are optional additions—not requirements for basic operation.

> **Naming:** OpenTrail remains the repository and engineering name. The local Windows utility now uses the preliminary, attorney-review-pending working display `Limited Underground Trail`; one replaceable identity boundary keeps that wording out of `OT-*` records, protocols, compatibility identifiers, and namespaces. This is not a clearance or registration claim.

## Project status

| Area | Current state |
| --- | --- |
| Phase | Architecture, host-tested components, and bounded close-range bench proofs |
| Latest increment | The Windows 95-style loader now uses semantic dynamic brushes instead of hard-coded control colors. Normal mode retains the classic palette; high-contrast mode maps paired foreground/background, selection, disabled, focus, and button colors to the user's Windows system palette and refreshes when accessibility/color/theme preferences change. A deterministic black/white/yellow production-window render is readable, but a real Windows contrast-theme switch remains a separate acceptance gate |
| Proven so far | A 111-executable C++ host matrix, 51 C# loader document/identity/accessibility/production-window-refresh/selection/high-DPI/contrast-theme/snapshot-binding/device-match/process-boundary/USB-runtime/hardware-profile/fixed-vector bundle-signature/packaged-inspection scenarios, deterministic classic, 125%/150%/200%, and contrast loader renders, cross-tool signature evidence, and Python evidence checks; two Heltec clients and one SenseCAP repeater have completed limited GNSS, transport, soak, burst, alert/acknowledgement, and bounded USB recovery tests |
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

The current bench contains two assembled Heltec V4 OLED MeshCore companions and one packaged Seeed SenseCAP Solar P1-Pro MeshCore repeater. A privacy-safe USB check now shows both Heltecs detect and activate their connected GNSS hardware and emit GPS telemetry; the SenseCAP reached a live fix, with subsequent checks increasing through four, seven, and eight satellites. No coordinates or device identities were published. Heltec fix/satellite status, exact received revisions, accuracy, loss behavior, and field performance remain open. The Heltec kits are bench clients for USB, recovery, radio, GNSS, and protocol work—not the board-level parts for the first complete touchscreen client. The integrated solar SenseCAP may be evaluated as the optional packaged repeater. A Wio Tracker L1 Pro is reported ordered but not received or tested, and exact complete-client hardware for the four-person pilot remains unfrozen.

See the [hardware inventory](hardware/INVENTORY.md), [regulatory reconciliation](hardware/HARDWARE_REGULATORY_INVENTORY_2026-08-10.md), and prepared [Wio Tracker arrival procedure](hardware/WIO_TRACKER_L1_PRO_BRINGUP.md). A radio preset or in-band frequency alone is not proof of legal operation.

## Repository layout

| Path | Purpose |
| --- | --- |
| `docs/` | Architecture, product boundaries, decisions, specifications, and dated project records |
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
