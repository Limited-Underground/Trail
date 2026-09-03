# Limited Underground Trail

[![Host validation](https://github.com/Limited-Underground/Trail/actions/workflows/host-validation.yml/badge.svg)](https://github.com/Limited-Underground/Trail/actions/workflows/host-validation.yml)

Limited Underground Trail is a free and open-source ESP32/LoRa platform for
off-grid group communication, location awareness, and safety alerts. It is
intended to keep a small group useful when cellular service and internet access
are unavailable.

The base design is a self-contained portable client with its own power, display,
input, radio, and GNSS-aware group status. Repeaters, remote archives, vehicle
alerts, larger displays, and offline maps are optional additions, not
requirements for basic operation.

> **Working names:** Limited Underground is the parent identity and Limited
> Underground Trail is the product family. Names remain provisional pending
> professional clearance. Stable `OpenTrail`, `OT-*`, protocol, schema,
> package, cryptographic, and device identifiers are not renamed. See
> [Decision 0008](docs/decisions/0008-limited-underground-trail-working-product-family.md).

## Project status

| Area | Current state |
| --- | --- |
| Phase | Architecture, host-tested components, bounded bench proofs, and two experimentally flashed Heltec targets |
| Latest increment | OT-168 now has the owner-approved factory-reset/boot-pairing contract, final focused firmware and Android foundation evidence, and one final pinned ESP-IDF target artifact; complete Host, physical, interruption, and coherent two-phone acceptance remain pending |
| Proven so far | Focused reset storage/target, companion semantics, reset executor/gesture/authority, 60-second D1 enrollment, D0 returning-owner, and privacy-safe receipt tests pass. The final 549,184-byte Heltec application has SHA-256 `90FE9479653F16000D71BC7AA76EA3ECB41F6FF0B4CF7A263E6056E054AC0454` and 89% app-slot free. The final Android foundation gate passes 137 actionable tasks and 347 tests with zero failures/errors/skips; the inspected 8,508,592-byte unsigned release APK has SHA-256 `56897297b7512ef4a3fdc35a256d3a2e59fddd7b78b9efa2fc8ee69f1e15e1d3` |
| Planned V1 | Two supported Heltec-and-Android pairs exchanging authenticated and encrypted messages bidirectionally through BLE, direct LoRa, and BLE |
| Not yet proven | Physical durable bond/reconnect behavior, destructive app/physical reset execution, power-interruption recovery, verified on-device user-data erasure, automatic unowned-boot pairing on both units, physical protected GATT, authenticated on-device LoRa, full two-phone operation, supported hardware, production firmware, power endurance, field range, or regulatory acceptance |

Android remains 60% complete. V1 remains exact 43.75% and is displayed as 44%.
Trail is not production-ready, and no hardware is currently listed as
supported.

## Start here

- [Documentation guide](docs/README.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Product boundaries](docs/PRODUCT_BOUNDARIES_V0.md)
- [Project status](docs/PROJECT_STATUS.md)
- [Future concepts](docs/FUTURE_CONCEPTS.md)
- [V1 progress](docs/V1_PROGRESS.json)
- [Progress log](docs/PROGRESS_LOG.md)
- [Engineering backlog](tasks/BACKLOG.md)
- [Hardware inventory](hardware/INVENTORY.md)
- [Contributing](CONTRIBUTING.md) and [security reporting](SECURITY.md)

## V1 release boundary

V1 Companion requires this physical path in both directions:

```text
Phone A <-> BLE <-> Heltec A <-> direct LoRa <-> Heltec B <-> BLE <-> Phone B
```

Each Heltec has one authorized phone. A verified unowned boot automatically
opens exactly one 60-second window with a fresh locally displayed six-digit PIN;
the app discovers that enrollment window through the pairable `D1` marker. An
owned boot is PIN-free and accepts only the saved phone. Returning-owner
discovery considers currently bonded devices advertising the normal protected
`D0` service, requires exactly one candidate, never creates a new bond, and
requires protected `ProtocolInfo` plus device-owner authorization before
`Ready`. V1 has no phone-
replacement or lost-phone transfer flow. Recovery is a destructive factory
reset initiated either by the authorized app without Heltec confirmation or by
the local 10-second hold, warning, release, and short-press confirmation
sequence. Both paths erase all user data, including maps, before returning to
the unowned pairing state. App reset uses a random nonzero 64-bit little-endian
receipt: the device echoes it only after durable intent admission, then exposes
the exact receipt in the next D1 scan response after verified cleanup. The
receipt correlates that reset only; it is neither identity nor authorization.
Unknown outcomes are verified without resubmitting the destructive command.
Acceptance also requires authenticated and encrypted
bidirectional messaging, explicit rejection and bounded recovery, and one exact
signed Android artifact installed on both approved phones. V1 has no server,
internet, or relay dependency.

Factory reset, reflashing, invasive access, or restoring old flash may reset or
roll back ownership; V1 does not claim resistance to physical firmware-writing
access. V1.5 separately requires four supported interoperable nodes. A future V2
may move the primary interface to a dedicated touchscreen client.

The exact scope and current evidence live in
[Decision 0033](docs/decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md),
[the V1/V1.5 acceptance scope](docs/testing/V1_V1_5_ACCEPTANCE_SCOPE_V0.md),
[project status](docs/PROJECT_STATUS.md), [progress](docs/V1_PROGRESS.json),
[the dated log](docs/PROGRESS_LOG.md), and [the backlog](tasks/BACKLOG.md).
Host-tested contracts and bench evidence do not establish field readiness.

## How it fits together

```text
Phone / local display
        |
       BLE
        |
 self-contained Trail client
        |
   direct LoRa traffic
        |
 another Trail client

Optional: repeater | archive service | Limited Underground Display alerts |
          offline maps and larger screens
```

- Clients originate and receive compact messages, positions, status, and alerts.
- An optional repeater may forward eligible immutable traffic once.
- An optional archive retains selected breadcrumbs only while explicitly enabled.
- Limited Underground Display may provide normalized critical events, never raw
  CAN/J1939 traffic.
- Offline maps and larger displays add local context and never travel over LoRa.

## Intended capabilities

- Compact LoRa messaging, position/status sharing, priority alerts, and
  controlled relaying
- Explicit privacy controls and graceful behavior when GPS or peers disappear
- Portable, vehicle-mounted, fixed-repeater, and touchscreen forms over shared
  protocols
- Locally transferred offline maps from a licensed, replaceable package source
- Group-defined quick alerts and versioned normalized critical-event input

These are product goals unless linked evidence explicitly proves them.

## Hardware status

Two assembled Heltec V4 OLED bench candidates run the identical 507,296-byte
OT-164 experimental application and expose the bounded local six-digit pairing
window. Their battery percentage is an approximate voltage-derived estimate,
and the displayed GPS satellite count does not prove a fix, position accuracy,
or fix-loss behavior. The Wio Tracker L1 Pro and SenseCAP hardware remain
candidates. No Trail hardware is supported yet; authenticated end-to-end
operation, RF/regulatory fit, range, endurance, recovery, and field use remain
unproven.

See the [hardware inventory](hardware/INVENTORY.md), [regulatory
reconciliation](hardware/HARDWARE_REGULATORY_INVENTORY_2026-08-10.md), and
[Wio Tracker procedure](hardware/WIO_TRACKER_L1_PRO_BRINGUP.md). An in-band
frequency or radio preset alone is not proof of legal operation.

## Build and validate

On Windows, run the complete host matrix from the repository root:

```powershell
.\tools\Test-Host.ps1
```

Run the Android foundation matrix from `android`:

```powershell
.\Test-AndroidFoundation.ps1
```

See [development setup](docs/DEVELOPMENT.md) for toolchain details.

## Repository layout

| Path | Purpose |
| --- | --- |
| `docs/` | Architecture, decisions, specifications, and dated records |
| `android/` | Native Android client and validation |
| `firmware/components/` | Hardware-independent, host-testable components |
| `firmware/targets/` | Applications composed for defined boards and roles |
| `hardware/` | Inventory, procedures, power/RF details, and compatibility evidence |
| `tests/` | Host, integration, and physical evidence |
| `tools/` | Validation, diagnostics, and evidence utilities |
| `tasks/` | Prioritized backlog and acceptance criteria |

## Safety and privacy boundary

Trail is a supplemental communication and awareness aid, not a guaranteed rescue
system. Missing GPS, maps, UI, peers, repeaters, archives, or Limited Underground
Display data must degrade independently. Real location sharing and archive
capture require explicit user control, and public evidence must remain
privacy-safe.

## License and contributions

Trail is licensed under the [Apache License 2.0](LICENSE). Contributions are
welcome through GitHub issues and pull requests; read
[CONTRIBUTING.md](CONTRIBUTING.md) first and use [SECURITY.md](SECURITY.md) for
sensitive reports.
