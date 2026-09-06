# Limited Underground Trail

## 2026-09-06 - Configuration/time transport and readback contract

Accepted the bounded configuration/time transport and durable-readback implementation contract: matched148-byte buffers, normal MTU151, one operation slot, exact request/session correlation, revisioned name commits with uncertainty recovery, and reuse of the current time challenge owner. This is documentation and review only; no wire or storage capability is enabled.

Next implement a host-only fixed-memory name transaction owner with trusted authority and fake persistence. Prove duplicate fences, CAS/readback,5000-ms admission expiry, ambiguous commit recovery and revocation/reset ordering. Follow with typed Android receipts and matched wire codecs before live persistence/target integration. Website and cold-power remain deferred.

See [contract](docs/platform/CONFIGURATION_TIME_TRANSPORT_V1.md). No V1 completion change: exact43.75/display44.

## 2026-09-06 - OT-170 device-name payload parity

Imported the existing OT-170 C++ and Kotlin device-name candidate codecs without changing their production bytes or payload format. Both pass the same 151 synthetic vectors (25 accepted, 126 rejected), including decoded fields and exact re-encoding. The Android setup model now requires a matching preliminary name receipt; no live adapter issues it.

Freeze the negotiated configuration/time transport and device-name authority mapping next: matched frame capacities, exact request/revision/session correlation, durable compare-and-set/readback, uncertainty recovery and reset behavior. Resolve draft-name validation differences before enabling requests. No new opcode, target linkage, storage driver or hardware execution is accepted. Website and cold-power remain deferred.

See [validation evidence](docs/testing/OT-170-NAME-PAYLOAD-PARITY-2026-09-06.md). V1 remains exact 43.75% / displayed 44%; final matrix results are recorded in the evidence.

Current implementation (2026-09-06): [Heltec OLED target adapter](tests/hardware/OT-178-OLED-ADAPTER-2026-09-06.md) maps ordinary frames to region-required/TX-disabled status.
The complete host matrix passes and two fresh builds match; no new firmware is installed.
Authenticated configuration/time sources and physical OLED acceptance remain open.

Latest physical acceptance (2026-09-06): [corrected firmware reconnect](tests/hardware/OT-177-RECONNECT-ACCEPTANCE-2026-09-06.md)
passes initial Ready/Snapshot, full app restart and two service reconnect cycles
on the retained Heltec/Note20 pair. Cold-power and broader acceptance remain open.
Completion is unchanged; website updates are deferred to a bulk update.

[![Host validation](https://github.com/Limited-Underground/Trail/actions/workflows/host-validation.yml/badge.svg)](https://github.com/Limited-Underground/Trail/actions/workflows/host-validation.yml)

Limited Underground Trail is a free and open-source ESP32/LoRa platform for
off-grid group communication, location awareness, and safety alerts. It is
designed for flexible local group communication and shared awareness across
outdoor activities, field work, travel, and everyday coordination.

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
| Latest increment | Host-only OLED time-admission owner passes 21 focused groups and the complete host matrix. [Evidence](docs/testing/OT-178-TIME-ADMISSION-2026-09-06.md). No wire/target activation or hardware claim. Next reconcile the existing name payload draft; website/cold-power stay deferred. |
| Proven so far | Host-tested protocol/state components and bounded single-pair hardware evidence. Corrected 563,776-byte firmware SHA-256 `9ACDC90EEA9D0489ABABDD9B6F4E3C7AFF97162D03DDC09F8E4965D5A1D12784` passed exact application-only write/readback, then initial Ready/Snapshot, app restart and two service reconnects with the prior V1-Test APK. See [physical evidence](tests/hardware/OT-177-RECONNECT-ACCEPTANCE-2026-09-06.md) for limits. |
| Planned V1 | Two Heltec/Android pairs exchanging authenticated typed/quick messages and coordinates over direct LoRa. The approved UX uses resumable name/region onboarding, Messages/Group/Device portrait and landscape layouts, one private group per person with exactly one administrator and six total members, consent-gated public direct chat, group location ON at join under an admin Required/Optional rule, redacted support export, and the bounded Heltec OLED/clock surface. Built-in maps are not required. See Decision 0104. |
| Not yet proven | true cold-power recovery, production zero-tap launch, destructive app/physical reset and erasure recovery, automatic unowned-boot pairing on both units, authenticated on-device LoRa, coherent two-phone operation, calibrated battery percentage, supported hardware, production firmware, endurance, field range, or regulatory acceptance |

The installed corrected firmware uses version `ot177-reconnect-v1`; its exact
artifact identity and reproducible build evidence are recorded separately. Completion is calculated only
from the [canonical V1 record](docs/V1_PROGRESS.json).
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

[Decision 0104](docs/decisions/0104-freeze-ot169-v1-user-experience-profile.md)
adds the accepted user-facing completion boundary: fixed onboarding without a
group step; Messages, Group, and Device responsive navigation; one group, one
administrator, and six total members; consent-gated direct contact; typed and
quick messages; coordinates without a built-in-map dependency; support export;
and the final Heltec OLED/clock states. These requirements are not yet complete.

Factory reset, reflashing, invasive access, or restoring old flash may reset or
roll back ownership; V1 does not claim resistance to physical firmware-writing
access. V1.5 separately requires four supported interoperable nodes. A future V2
may move the primary interface to a dedicated touchscreen client.

The exact scope and current evidence live in
[Decision 0033](docs/decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md),
[Decision 0104](docs/decisions/0104-freeze-ot169-v1-user-experience-profile.md),
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
