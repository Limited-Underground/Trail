# OpenTrail

[![Host validation](https://github.com/nbjelanovic/OpenTrail/actions/workflows/host-validation.yml/badge.svg)](https://github.com/nbjelanovic/OpenTrail/actions/workflows/host-validation.yml)

OpenTrail is a free and open-source ESP32/LoRa platform for off-grid group communication, location awareness, and safety alerts. It is intended to keep a small group useful when cellular service and internet access are unavailable.

The base design is a self-contained portable client with its own power, display, input, radio, and GNSS-aware group status. Repeaters, remote archives, vehicle alerts, larger displays, and offline maps are optional additions—not requirements for basic operation.

> **Working names:** `Limited Underground` is the parent identity and `Limited Underground Trail` is the Android application and product family. The provisional tiers are `Trail Essential` (screenless, phone-required LoRa companion), `Trail Gold` (one touchscreen), `Trail Platinum` (two displays), and `Trail Repeater`. The shared desktop utility is `Limited Underground Firmware Loader`, visibly marked `Preview` and `Inspection only` until real writing and recovery pass. All names await professional clearance; no `®` is used. `OpenTrail` remains the repository/engineering name, and existing folders, namespaces, `OT-*` records, protocols, GATT/schema/crypto identifiers, and device IDs remain stable. See [Decision 0008](docs/decisions/0008-limited-underground-trail-working-product-family.md).

## Project status

| Area | Current state |
| --- | --- |
| Phase | Architecture, host-tested components, bounded bench proofs, and two experimentally flashed Heltec targets |
| Latest increment | OT-133 binds the OT-132 runner into a fresh immutable successor and consumes exactly one non-reusable attempt. Node A passed benchmark readback, then capture failed closed as `capture_failed` / `preamble_invalid`: one 512-byte read contained nine complete pre-READY records, exceeding the frozen eight-record cap before any frame. Node A restored/readback/reset exactly to Trail, Node B was never benchmark-written and remained on Trail, and the owner confirmed both Trail logos. No result, radio, selection, Phase 2 completion, support, regulatory, production, end-to-end, or score claim is added. The next gate is host-only correction and adversarial testing of the nine-record boundary; any later device access requires a new immutable binding and fresh authority. V1 remains exact 43.75%/displayed 44%. |
| Proven so far | The exact 473,152-byte OT-115 image built under pinned ESP-IDF 6.0.2, flashed and hash-verified automatically on both test units, and produced one bounded OpenTrail heartbeat from each unit. Owner-provided photos show both displays lit and one clear close view confirms `BAT:--% GPS:-- BLE:A` with no traffic arrow. These are honest placeholders: live battery, satellite count, and LoRa RX/TX sources are not yet bound. OT-114 direct-radio evidence remains accepted separately. No score changes: Android remains 60%, V1 exact 43.75%/displayed 44%, the historical baseline exact 31.75%/displayed 32%, and V1.5 and V2 remain unmeasured |
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
candidate-import anchor remains empty. Decision 0047 and OT-105 evidence then accept the exact installed pinned ESP-IDF v6.0.2 / mbedTLS 4.1.0 source dependency lock. OT-105 closes no requirement: historical six-, prior five-, prior four-, prior three-, and current three-blocker states remain recorded. Accepted source/API-configuration/candidate-import counts are `3/0/0`, while OT-096 remains 5/8 and the composite API/configuration blocker stays open. See [Decision 0047](docs/decisions/0047-host-only-mbedtls-psa-source-lock-admission-delta.md) and [OT-105 evidence](tests/hardware/OT-105-2026-08-21.md).

[Decision 0049](docs/decisions/0049-versioned-per-candidate-api-configuration-acceptance-contract.md) and [OT-108 evidence](tests/hardware/OT-108-2026-08-21.md) freeze the append-only `OTCAC0/v1` successor boundary. Candidate-specific OT-107 sdkconfig digests replace the obsolete common-sdkconfig assumption; complete operation coverage is required for structural selection eligibility, while a strict partial comparison remains measurable only for evidenced operations and cannot be selected. All API/configuration evidence registries remain empty, counts stay `3/0/0`, the same two blockers remain, and readiness stays blocked.

[Decision 0050](docs/decisions/0050-host-only-mbedtls-psa-api-configuration-admission.md) and [OT-109 evidence](tests/hardware/OT-109-2026-08-21.md) admit the exact host-only mbedTLS/PSA five-of-eight comparison-only result. The [generated operation bundle](tests/benchmarks/crypto/OT-109-OT005-MBEDTLS-PSA-API-CONFIG-OPERATION-EVIDENCE-V0.json), [candidate evidence](tests/benchmarks/crypto/OT-109-OT005-MBEDTLS-PSA-API-CONFIG-EVIDENCE-V2.json), and [append-only admission](tests/benchmarks/crypto/OT-109-OT005-MBEDTLS-PSA-API-CONFIG-ADMISSION-DELTA-V0.json) bind the exact accepted source, candidate-specific sdkconfig, and five purpose-distinct eligible operations. The partial comparison remains structurally nonselectable; Ed25519 sign/verify and Noise XK remain unavailable. Counts become `3/1/0`; only the direct-radio region/MTU/full-PHY requirement remains, readiness stays blocked, and no score changes.

[Decision 0052](docs/decisions/0052-host-only-us915-direct-radio-profile-execution-contract-v1.md), [Decision 0053](docs/decisions/0053-admit-us915-direct-radio-profile-evidence-v1.md), and [OT-114 evidence](tests/hardware/OT-114-2026-08-21.md) supersede the prior current blocker state without rewriting its history. The exact two-node US915 close-bench result closes only `direct_radio_mtu_phy_region_unresolved`; counts remain `3/1/0`, readiness and scores do not advance, and a successor readiness decision plus new executable benchmark plan remain required before comparison execution or selection.

[Decision 0054](docs/decisions/0054-successor-crypto-benchmark-readiness-and-execution-plan.md) and [OT-116 evidence](tests/hardware/OT-116-2026-08-22.md) accept the append-only `OTCBR1/v0` successor review and freeze the `OTCBX1/v1` phased execution contract. All six historical OT-094 requirements now have accepted closure evidence, but at OT-116 acceptance benchmark measurement remained blocked: counts were `3/1/0`, libsodium and Monocypher lacked candidate-specific API/configuration admission, every candidate lacked an accepted retained import/build anchor, the second measurement node lacked exact-profile admission, and execution authority was false. The contract is executable as a fail-closed procedure only after those preflights and fresh authority; it makes no crypto selection and changes no score.

[Decision 0055](docs/decisions/0055-host-only-libsodium-api-configuration-admission.md) and [OT-117 evidence](tests/hardware/OT-117-2026-08-22.md) accept complete eight-of-eight host-only libsodium API/configuration evidence, including the hash-bound benchmark-only `OTNXK0/v0` Noise XK composition. Current source/API-configuration/import counts advance to `3/2/0`. Libsodium is structurally selection eligible but is neither selected nor authorized for execution. At OT-117 acceptance, Phase 0 and measurement remained blocked pending Monocypher API/configuration, second-node exact-profile admission, every retained import/build anchor, and fresh execution authority; no benchmark, device, selection, or score claim changes.

[Decision 0056](docs/decisions/0056-host-only-monocypher-api-configuration-admission.md) and [OT-118 evidence](tests/hardware/OT-118-2026-08-22.md) accept strict five-of-eight host-only Monocypher API/configuration evidence for comparison measurement only. SHA-256, HKDF-SHA256, and Noise XK remain unavailable, so Monocypher is structurally nonselectable. All three API/configuration registries are populated and current source/API-configuration/import counts advance to `3/3/0`. At OT-118 acceptance, Phase 0 remained incomplete only because the second measurement node lacked exact-profile admission; every retained import/build anchor and fresh execution authority also remained absent. No benchmark, device, selection, or score claim changes.

[Decision 0057](docs/decisions/0057-second-measurement-node-exact-profile-admission.md) and [OT-119 evidence](tests/hardware/OT-119-2026-08-22.md) independently admit OT-DEV-002 as the second exact-profile measurement node. One bounded read-only USB/ROM observation records only sanitized ESP32-S3, revision v0.2, 40 MHz crystal, 2 MiB embedded PSRAM, and 16 MiB flash facts; owner-supplied physical evidence associates `HTIT-WB32LAF` and `V4.2` with that same unit. No private identifier, raw photo, raw probe output, flash read, persistent change, radio/key operation, or benchmark is retained or claimed. At OT-119 acceptance, the exact-profile registry contained two units, Phase 0 was complete, and counts were `3/3/0`; measurement remained blocked only by absent retained candidate import/build admissions and fresh execution authority. No support, compatibility, regulatory, selection, or score claim changes.

[Decision 0058](docs/decisions/0058-retained-candidate-import-build-admission.md) and [OT-120 evidence](tests/hardware/OT-120-2026-08-22.md) accept the atomic retained import/build admission for all three candidates. Six fresh computer-only ESP-IDF 6.0.2 builds reproduce the accepted candidate sdkconfigs with zero warnings and equal two-run artifact tuples. Counts advance to `3/3/3`; Phase 1 is complete. Measurement remains false and blocked only by absent fresh execution authority. Libsodium remains structurally eligible but unselected; mbedTLS/PSA and Monocypher remain five-of-eight, comparison-only, and structurally nonselectable. No benchmark, hardware, radio, key/entropy, selection, implementation, support, compatibility, regulatory, physical, or score claim changes.

[Decision 0059](docs/decisions/0059-one-time-phase-two-benchmark-execution-authority.md), [Decision 0060](docs/decisions/0060-bounded-libsodium-local-primitives-checkpoint.md), and [OT-121 evidence](tests/hardware/OT-121-2026-08-23.md) record the first bounded Phase 2 execution checkpoint. The privacy-safe `OT121LPER1/v1` receipt proves that both admitted anonymous nodes passed all seven libsodium local primitives with 100 data-cache-conditioned and 100 warm samples per operation, and that benchmark readback, capture validation, exact Trail restoration, restore readback, and reset passed on both. This is not complete Phase 2 or Phase 3 admission: no Noise XK, radio, cross-candidate comparison, resource result, suite/wire selection, support, compatibility, regulatory, production, or score claim is added.

[Decision 0061](docs/decisions/0061-bounded-libsodium-noise-resource-checkpoint.md) and [OT-122 evidence](tests/hardware/OT-122-2026-08-23.md) record the continuation checkpoint. Both anonymous nodes pass 8/8 operations including complete benchmark-only Noise XK, with 100 conditioned and 100 warm samples per operation. Both independently report 0 bytes peak dynamic heap, 4,312 bytes maximum stack use, and 0 watchdog resets, and both restore exactly to Trail. The receipt remains `phase_two_complete=false` and `radio_used=false`; other candidates, radio, the matched linked-flash/static-RAM admission, Phase 3, and explicit selection remain open.

[Decision 0062](docs/decisions/0062-monocypher-comparison-benchmark-preparation.md) and [OT-123 evidence](tests/hardware/OT-123-2026-08-23.md) accept the computer-only Monocypher comparison preparation checkpoint. The exact 5-of-8, structurally nonselectable target has reproducible two-build evidence, fixed RFC known-answer gates, a strict 1,014-frame parser/schema, and a recovery-safe two-node runner. No hardware run occurred, and the matched control, linked-flash/static-RAM deltas, radio, Phase 3, and selection remain open.

[Decision 0068](docs/decisions/0068-host-only-monocypher-start-ready-protocol-correction.md) and [OT-129 evidence](tests/hardware/OT-129-2026-08-24.md) accept the computer-only successor protocol correction required by OT-128. Exact retrying START and drained READY replace the blind startup delay; partial bytes survive read timeouts; endpoint lifecycle is either observed re-enumeration or three stable-present polls; bounded startup chatter and privacy-safe failure categories fail closed; and the real unchanged 1,014-frame parser is exercised. No hardware or flash occurred. At that checkpoint, a new immutable firmware/runner/restoration binding and fresh non-reusable authority remained mandatory before another attempt.

[Decision 0069](docs/decisions/0069-freeze-monocypher-execution-bundle-and-one-attempt-authority.md) and [OT-130 evidence](tests/hardware/OT-130-2026-08-24.md) now accept that exact immutable binding and a fresh non-reusable one-attempt authority. The bundle pins the OT-129 firmware inputs and six-file output tuple, transport, parser/schema, restoration-safe coordinator, and exact Trail image; two fresh cache-disabled ESP-IDF 6.0.2 builds match, and coordinator plus authority tests each pass 11/11. At the OT-130 checkpoint, no hardware, flash, benchmark, or radio occurred; the authority was unexecuted, and the next permitted device step is only the authorized two-node attempt with exact Trail restoration of both nodes on success or abort.

[Decision 0070](docs/decisions/0070-record-ot131-monocypher-execution-abort.md) and [OT-131 evidence](tests/hardware/OT-131-2026-08-24.md) record the successor executable boundary and its single consumed attempt. Node A passed benchmark readback, capture failed closed as `capture_failed` / `preamble_invalid` after one complete non-frame line, and Node A restored/readback/reset exactly to Trail. Node B was never benchmark-flashed and remained on Trail; restoration completed and the owner visually confirmed both Trail logos. The closed diagnostics do not disclose the preamble or prove a physical root cause. No result, radio, selection, Phase 2 completion, support, regulatory, production, end-to-end, or score claim is added. No further Monocypher hardware attempt is authorized.

[Decision 0072](docs/decisions/0072-record-ot133-immutable-successor-execution-abort.md) and [OT-133 evidence](tests/hardware/OT-133-2026-08-24.md) accept the fresh immutable OT-132 successor bundle and record its single consumed attempt. Node A passed benchmark readback before `capture_failed` / `preamble_invalid`; one 512-byte read contained nine complete pre-READY records, exceeding the frozen eight-record cap before any frame was accepted. Node A restored/readback/reset exactly to Trail, Node B remained untouched on Trail, and the owner confirmed both Trail logos. The [sanitized abort receipt](tests/benchmarks/crypto/OT-133-OT005-MONOCYPHER-EXECUTION-ABORT-RECEIPT-V0.json) admits no result, radio, selection, Phase 2 completion, or score credit. No further Monocypher hardware attempt is authorized; next correct and adversarially test the nine-record boundary host-only, then require a new immutable successor and fresh one-attempt authority before device access.

[Decision 0071](docs/decisions/0071-host-only-monocypher-opaque-preamble-correction.md) and [OT-132 evidence](tests/hardware/OT-132-2026-08-24.md) accept a new host-only successor runner for the bounded OT-131 failure. Complete pre-READY records may be opaque only within the unchanged eight-record/512-byte limits; exact READY, frame-before-READY rejection, post-READY strictness, fixed deadlines, privacy-safe counters, and the unchanged real 1,014-frame parser remain enforced. Fourteen adversarial tests and all frozen OT-129 through OT-131 regression gates pass. No hardware, flash, benchmark, radio, execution authority, result, selection, Phase 2 completion, or score claim is added. OT-131 remains consumed; a new immutable executable binding and fresh explicit one-attempt authority are required before any device access.

[Decision 0063](docs/decisions/0063-monocypher-comparison-execution-abort.md) through [Decision 0067](docs/decisions/0067-record-monocypher-second-corrective-retry-abort.md) preserve three aborted Monocypher attempts without admitting a result. [OT-128 evidence](tests/hardware/OT-128-2026-08-24.md) records that both installed Trail applications passed preflight, only Node A was benchmark-written, every touched node was restored exactly, and a later non-writing reset returned both USB endpoints. After the immutable receipt was prepared, the owner visually confirmed both Trail displays on. The private receipt cannot distinguish endpoint re-enumeration, partial input, read failure, incomplete frames, or semantic rejection, so the physical root cause remains unconfirmed. Source inspection requires a host/device start-ready handshake, partial-byte accumulation, verified endpoint lifecycle, and privacy-safe failure classification before any fresh authority. Decision 0066 was consumed and, at that checkpoint, no Monocypher hardware attempt was authorized.

[Decision 0048](docs/decisions/0048-host-only-final-candidate-build-configuration-admission.md) and [OT-107 evidence](tests/hardware/OT-107-2026-08-21.md) accept the owner-approved final per-candidate configuration. Exact reproducible sdkconfig digests are now bound separately for libsodium, mbedTLS/PSA, and Monocypher. OT-107 closes only `final_candidate_build_configuration_unresolved`; the mbedTLS/PSA API/configuration and direct-radio region/MTU/full-PHY requirements remain open. Counts stay `3/0/0`, OT-096 stays 5/8, readiness stays blocked, and the historical plan stays `draft_blocked`. OT-109 later closes the API/configuration requirement and OT-114 later closes the direct-radio requirement. OT-117 and OT-118 later populate all three candidate API/configuration registries, OT-119 admits the second node's exact profile and completes Phase 0, and OT-120 accepts all retained candidate import/build evidence and completes Phase 1, and OT-121/OT-122 partially exercise Phase 2 with two-node libsodium local-primitives, Noise XK, and runtime-resource checkpoints. OT-131 records its successor executable attempt as aborted and exactly restored; OT-132 accepts the host-only opaque-preamble correction; OT-133 binds that successor and consumes its one attempt on the nine-record/eight-record fail-closed boundary with exact restoration and no result. Next correct and adversarially test that boundary host-only, then require a new immutable successor and fresh authority before any further Monocypher hardware attempt; complete the remaining candidate, radio, and matched linked-flash/static-RAM measurements only under separately explicit authority before a separate Phase 3 admission and explicit
suite/wire decision. Implementation and physical acceptance remain
separately authorized.

[OT-106 evidence](tests/hardware/OT-106-2026-08-21.md) records the separate
compact-footer target-linkage build. Both fresh computer-only builds used the
exact 309-file staged input, exited with zero warnings, and produced identical
ordered artifact tuples; no device or radio operation ran. All five accepted
OT-093 files remain byte-for-byte unchanged. Its frozen `3837dbce...` mixed-EOL
raw-working-tree digest remains an immutable one-time historical digest whose
per-line map was not recorded and is therefore non-reconstructible. The
successor record's separately reproducible `c84ba0e3...` Git-blob transform
aggregate describes a deterministic isolated checkout; it is distinct from
and does not replace or reconstruct `3837dbce...`.

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

The available bench inventory contains two assembled Heltec V4 OLED units. Both now run the identical experimental OT-115 `heltec_v4_bench` image; a two-unit view shows both displays lit with the footer row, while one clear close view confirms the exact placeholder text; `OT-DEV-001` also retains its earlier startup, advertising, fixed public BLE read, `BLE CONNECTED`, disconnect, and advertising-return evidence. It also contains one packaged Seeed SenseCAP Solar P1-Pro MeshCore repeater and one owner-reported Wio Tracker L1 Pro candidate. A privacy-safe USB check shows both Heltecs detect and activate their connected GNSS hardware and emit GPS telemetry; the SenseCAP reached a live fix, with subsequent checks increasing through four, seven, and eight satellites. The Wio's first read-only pass identified public USB model `Seeed Wio Tracker L1`, USB Companion `v1.17.0-727fc05`, the configured 910.525 MHz/BW 62.5 kHz/SF7/CR5/22 dBm profile, zero error/traffic counters across four cycles, and GNSS detected but inactive. No coordinates, identities, channel values, or transient ports were published, and the Wio did not transmit. Exact Wio label/revision and pre-write state, over-air interoperability, GNSS fix/loss, RF/regulatory fit, power/endurance, BLE, and recovery remain open. The accepted OLED evidence is bounded startup/link-status only—not interactive UI or supported-hardware evidence. The Heltec kits and Wio are bench candidates, not the frozen board-level parts for the first complete touchscreen client. The integrated solar SenseCAP may be evaluated for V1.5 or a future relay claim; exact two-pair V1 firmware, security, and physical acceptance remain open.

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
