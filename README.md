# OpenTrail

[![Host validation](https://github.com/nbjelanovic/OpenTrail/actions/workflows/host-validation.yml/badge.svg)](https://github.com/nbjelanovic/OpenTrail/actions/workflows/host-validation.yml)

OpenTrail is a free and open-source ESP32/LoRa platform for off-grid group communication, location awareness, and safety alerts. It is intended to keep a small group useful when cellular service and internet access are unavailable.

The base design is a self-contained portable client with its own power, display, input, radio, and GNSS-aware group status. Repeaters, remote archives, vehicle alerts, larger displays, and offline maps are optional additions—not requirements for basic operation.

> **Working names:** `Limited Underground` is the parent identity and `Limited Underground Trail` is the Android application and product family. The provisional tiers are `Trail Essential` (screenless, phone-required LoRa companion), `Trail Gold` (one touchscreen), `Trail Platinum` (two displays), and `Trail Repeater`. The shared desktop utility is `Limited Underground Firmware Loader`, visibly marked `Preview` and `Inspection only` until real writing and recovery pass. All names await professional clearance; no `®` is used. `OpenTrail` remains the repository/engineering name, and existing folders, namespaces, `OT-*` records, protocols, GATT/schema/crypto identifiers, and device IDs remain stable. See [Decision 0008](docs/decisions/0008-limited-underground-trail-working-product-family.md).

## Project status

| Area | Current state |
| --- | --- |
| Phase | Architecture, host-tested components, bounded bench proofs, and one experimentally flashed Heltec target |
| Latest increment | OT-101 is partial with a target-neutral host-only compact-footer prototype/contract under `tests/host_support`. Twelve warning-free groups pass for the exact 128-column by 8-row `BAT:100% GPS:12 BLE:C` layout, fail-closed battery/GPS freshness and validity, bounded BLE codes, and one injected latest-direction arrow with monotonic expiry. The prototype remains outside the firmware tree and has no target renderer/OLED, sensor, BLE, radio, or accepted real-traffic binding. No firmware or target build, device access, flash, telemetry, traffic, physical-display, support, regulatory, or score claim was added |
| Proven so far | OT-103 adds bounded owner-supplied physical identity evidence for one experimental target without new device access or a support claim. OT-095 adds strict source-evidence admission governance, not a source lock or candidate readiness. OT-094 adds strict blocked readiness governance, not candidate readiness. OT-093 adds reproducible build-only evidence, not crypto performance or target support. OT-091 is deterministic host-contract evidence only; it proves no crypto suite, packet v1, provisioning, key replacement, encrypted radio traffic, replay protection, acknowledgement, or physical delivery. OT-090 separately freezes practical BLE pairing/replacement without implementation. OT-089 permanently fixes the two-pair V1/four-node V1.5 scope and disclosed physical-reflash rollback limit. OT-085A/OT-085B remain bounded physical public-BLE evidence on the same experimental target. No score changes: Android remains 60%, V1 exact 43.75%/displayed 44%, the historical baseline exact 31.75%/displayed 32%, and V1.5 and V2 remain unmeasured |
| Planned first release | Two supported Heltec device-and-Android-phone pairs exchanging authenticated and encrypted messages bidirectionally through BLE, direct LoRa, and BLE; V1.5 later proves four supported nodes with mixed hardware allowed |
| Not yet proven | Production firmware, supported client hardware, authenticated on-device transport, protected keys, complete-client GNSS/UI, field range, power endurance, or regulatory acceptance |

OpenTrail is not production-ready, and no hardware is currently listed as supported. See the [dated progress log](docs/PROGRESS_LOG.md) for recent work and the [engineering backlog](tasks/BACKLOG.md) for exact acceptance evidence and remaining gates.

## Start here

- [Documentation guide](docs/README.md) — organized entry point for every project area
- [Architecture](docs/ARCHITECTURE.md) — system layers, roles, interfaces, and failure boundaries
- [Product boundaries](docs/PRODUCT_BOUNDARIES_V0.md) — base system versus optional additions
- [Project status and open decisions](docs/PROJECT_STATUS.md) — current assumptions, evidence, and unresolved choices
- [Future concepts](docs/FUTURE_CONCEPTS.md) — unscheduled post-release ideas and accepted directions with no progress credit
- [Dated progress log](docs/PROGRESS_LOG.md) — public chronology, newest day first
- [Engineering backlog](tasks/BACKLOG.md) — work-item status and acceptance evidence
- [Hardware inventory](hardware/INVENTORY.md) — available, ordered, missing, and unverified equipment
- [Contributing](CONTRIBUTING.md) and [security reporting](SECURITY.md)

## Release boundary

The permanent V1 Companion goal is two supported Heltec LoRa devices and two
approved Android phones, one physically authorized phone per Heltec. The exact
end-to-end chain is Phone A ⇄ BLE ⇄ Heltec A ⇄ direct LoRa ⇄ Heltec B ⇄ BLE ⇄
Phone B. V1 must physically accept practical PIN-based authorization and phone
replacement, authenticated/encrypted bidirectional messaging, rejection and
bounded retry/recovery, and one exact signed Android artifact on both phones.
It has no relay, server, or internet dependency.

[Decision 0033](docs/decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md)
accepts that factory reset, reflashing, invasive access, or old-flash restore
may reset or roll back ownership. V1 does not require a secure element or claim
rollback-proof authorization against physical firmware-writing access.

[Decision 0035](docs/decisions/0035-host-tested-secure-lora-key-transport-contract.md)
and [`OTSL0/v0`](docs/security/SECURE_LORA_KEY_TRANSPORT_V0.md) freeze the
algorithm-neutral secure-LoRa lifecycle/admission semantics without selecting
cryptography or packet v1. [Decision 0037](docs/decisions/0037-pre-crypto-build-baseline.md)
and the accepted [`OTCBL0/v0` evidence](tests/hardware/OT-093-2026-08-20.md)
freeze the reproducible zero-candidate target build only.
[Decision 0038](docs/decisions/0038-host-only-ot005-candidate-readiness-contract.md)
and the accepted [`OTCBR0/v0` evidence](tests/hardware/OT-094-2026-08-20.md)
now freeze the separate host-only readiness admission contract. All six closure
requirements remain blocked, so the historical OT-005 plan is still
`draft_blocked`; a self-declared legacy `ready` plan grants no template or pass.
[Decision 0039](docs/decisions/0039-host-only-candidate-source-lock-admission-contract.md)
and the accepted [`OTCSL0/v0` evidence](tests/hardware/OT-095-2026-08-20.md)
freeze the source-lock admission rules without accepting, acquiring, or importing
any candidate source. All six readiness requirements remain blocked.
[Decision 0040](docs/decisions/0040-host-only-mbedtls-psa-static-eligibility.md)
and [OT-096 evidence](tests/hardware/OT-096-2026-08-20.md) record the bounded
5/8 static result; final configuration and all six blockers remain unresolved.
[Decision 0041](docs/decisions/0041-license-aware-source-lock-admission-v1.md)
and [OT-097 evidence](tests/hardware/OT-097-2026-08-20.md) freeze the
license-aware `OTCSL0/v1` admission policy. Version 0 remains historical but
cannot admit a future source lock; version 1 requires separate upstream SPDX,
project-choice, complete-inventory, and inventory-digest evidence.
[Decision 0042](docs/decisions/0042-external-candidate-acquisition-static-inspection.md)
and [OT-098 evidence](tests/hardware/OT-098-2026-08-20.md) record exact
acquisition and static inspection of the two external candidates: libsodium
has 7/8 and Monocypher 5/8 source operations. Signature trust, complete
inventories, project locks, final configuration, import, benchmark, and
selection remain unresolved or absent; all six blockers remain open.
[Decision 0043](docs/decisions/0043-libsodium-managed-import-evidence.md)
and [OT-099 evidence](tests/hardware/OT-099-2026-08-20.md) record complete
managed-import evidence and an isolated generic ESP32-S3 build pass. The
candidate archive entered the link graph but probe symbols were not retained.
Source-lock admission remains pending and all six blockers remain open.
[Decision 0044](docs/decisions/0044-libsodium-source-lock-admission-delta.md)
and [OT-100 evidence](tests/hardware/OT-100-2026-08-20.md) now accept only that
exact libsodium source lock. OT-094 and OT-097 remain historical six-blocker
records; OT-100 records the prior five-blocker state.
[Decision 0045](docs/decisions/0045-monocypher-source-lock-admission-delta.md)
and [OT-102 evidence](tests/hardware/OT-102-2026-08-20.md) accept the exact
Monocypher 4.0.3 source lock under the owner-selected `BSD-2-Clause` branch.
[Decision 0046](docs/decisions/0046-exact-received-target-profile-admission-delta.md)
and [OT-103 evidence](tests/hardware/OT-103-2026-08-20.md) then accept the exact
received `OT-DEV-001` profile without granting support, compatibility,
regulatory acceptance, or radio-profile authority. Historical six-blocker and
prior current four-blocker states remain recorded; three current requirements
remain, readiness is still blocked, and every API/configuration and
candidate-import anchor remains empty. Next close those three
readiness requirements, accept a new immutable executable plan,
run the exact candidate benchmark under separate authority, and make a later
explicit suite/wire decision. Implementation and physical acceptance remain
separately authorized.

V1.5 is the separate four-supported-node interoperability milestone. Any
compatible mix of supported hardware is allowed and heterogeneous evidence is
preferred; four identical supported nodes remain eligible. Four phones are not
required. Any mesh claim requires a physical three-radio relay path. See the
[canonical V1/V1.5 scope](docs/testing/V1_V1_5_ACCEPTANCE_SCOPE_V0.md).

The future V2 Integrated goal still moves the interface onto a dedicated
touchscreen client. Historical four-person standalone pilot artifacts remain
preserved for their original scope; they no longer define V1 Companion.

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

The available bench inventory contains two assembled Heltec V4 OLED units: `OT-DEV-001` now runs the experimental OT-085 OpenTrail target and has physically accepted startup, advertising, one fixed public BLE read, `BLE CONNECTED`, both phone-driven and target-timed disconnect paths, return to `BLE ADVERTISING`, and return of one compatible service advertiser; untouched `OT-DEV-002` remains a MeshCore USB Companion. It also contains one packaged Seeed SenseCAP Solar P1-Pro MeshCore repeater and one owner-reported Wio Tracker L1 Pro candidate. A privacy-safe USB check shows both Heltecs detect and activate their connected GNSS hardware and emit GPS telemetry; the SenseCAP reached a live fix, with subsequent checks increasing through four, seven, and eight satellites. The Wio's first read-only pass identified public USB model `Seeed Wio Tracker L1`, USB Companion `v1.17.0-727fc05`, the configured 910.525 MHz/BW 62.5 kHz/SF7/CR5/22 dBm profile, zero error/traffic counters across four cycles, and GNSS detected but inactive. No coordinates, identities, channel values, or transient ports were published, and the Wio did not transmit. Exact Wio label/revision and pre-write state, over-air interoperability, GNSS fix/loss, RF/regulatory fit, power/endurance, BLE, and recovery remain open. The accepted OLED evidence is bounded startup/link-status only—not interactive UI or supported-hardware evidence. The Heltec kits and Wio are bench candidates, not the frozen board-level parts for the first complete touchscreen client. The integrated solar SenseCAP may be evaluated for V1.5 or a future relay claim; exact two-pair V1 firmware, security, and physical acceptance remain open.

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
