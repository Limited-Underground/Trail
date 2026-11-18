# Limited Underground Trail architecture

Foundation documentation, updated November 3, 2026. The [working contract](../../CONTRIBUTING.md#working-contract) has the current development rules. This page is the shared architecture reading path, mapping existing decisions, source and evidence with arc42/C4. It grants no behavior change, device operation, deployment, release or merge authority. The [current handoff](../PROJECT_STATUS.md#current-recovery-handoff) identifies documentation publication/adoption state separately from product acceptance.

Read [current status](../PROJECT_STATUS.md#current-recovery-handoff) for exact accepted artifacts and restrictions, [firmware V1](../firmware/README.md) for the nine assessment areas and remaining criteria, [protocol selection](../protocol/README.md) for contract status, and [the working contract](../../CONTRIBUTING.md) before using any procedure. Intended requirements, implemented source and demonstrated results are distinguished below.

## 1. Introduction and Goals

Trail is intended to exchange compact messages, status and consented location without requiring internet. First V1 is exactly two supported Heltec devices and two approved Android phones, one phone per device, using BLE → direct LoRa → BLE. Messaging must survive unavailable GNSS without inventing a current location. Queued, sent, delivered and read are different facts. Trail is a supplemental communication aid, not guaranteed rescue.

Participants need truthful connection/delivery/privacy state. Maintainers need exact build/artifact identity, recoverable source and clear evidence limits. [Product boundaries](../PRODUCT_BOUNDARIES_V0.md), [Decision 0033](../decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md) and [V1 user experience](../product/V1_USER_EXPERIENCE.md) retain the detailed requirements. Many product flows remain intended rather than accepted implementations.

## 2. Architecture Constraints

Use bounded ESP32-class resources and compact radio payloads under the selected region's operating constraints. BLE phone ownership and LoRa peer membership are independent. A phone's bond, UI flag, display name or region selection cannot create radio authentication or transmission authority.

The current V1 threat boundary discloses that physical firmware-writing access or old-flash restoration can reset/roll back ownership; it makes no rollback-proof physical-attacker claim. [Decision 0103](../decisions/0103-adopt-ot168-v1-factory-reset-and-boot-pairing.md) supersedes older pairing/replacement clauses without discarding other Decision 0033 constraints. Product names remain presentation; existing OT technical identifiers are preserved.

The owner froze product development indefinitely on November 2. Historical host-development permission does not resume it. Hardware operations, private evidence publication, production/release and merge remain separately controlled.

## 3. System Scope and Context

The intended field path is:

```text
Phone A ⇄ protected BLE ⇄ Heltec A ⇄ protected direct LoRa ⇄ Heltec B ⇄ protected BLE ⇄ Phone B
```

The Heltec owns security, radio, queues, message/ACK identity, truthful delivery, GNSS freshness and durable state. Android renders authoritative state and submits intent. A repeater, Server, Gateway, Display, Tracker, Console, map, cloud account, internet or maintenance laptop is not required by this direct V1 path. Those products retain their own source/status and acceptance.

The November 2 diagnostic run used laptop control/observation. That is an experiment dependency and does not redefine the intended field topology. [Decision 0009](../decisions/0009-one-phone-companion-authority.md) explains the device-authority boundary. C4 view keys `TrailContext` and `TrailContainers` are defined in [workspace.dsl](workspace.dsl); generated views require successful official parsing/export.

The current model passed official validation and JSON/static export with pinned Structurizr 2026.09.19. Generated presentation is a derivative of this model, not a separate manually maintained diagram. Parser/export success does not establish architecture truth, visual readability or product acceptance; those remain review concerns.

## 4. Solution Strategy

Existing architecture separates portable C++ codecs/state owners from board/stack adapters. Android separates pure Kotlin wire codecs, connection/runtime ownership and Compose presentation. Failures must remain typed and bounded; unknown/uncertain security or persistence state fails closed. A reconnect obtains authoritative state rather than trusting a phone cache.

These are existing architecture choices, not a completed protected product composition. The maintainer-held OT-0237e security recommendation is proposed. A separate isolated Packet-v1 profile records owner approval of its conditional architecture/host slice; it is not final production algorithm/wire admission or activation. Neither candidate document is integrated into current main. [Existing architecture detail](../ARCHITECTURE.md) retains dated implementation/evidence narratives and links.

## 5. Building Block View

| Boundary | Existing source responsibility | Evidence limit |
|---|---|---|
| [Android app](../../android/app/) | UI/controller graph, connected-device foreground service, BLE facade/runtime and generation-bound session owner. | Canonical source and retained diagnostic APK overlays differ. Source presence does not accept lifecycle on every phone or production UI. |
| [Android protocol module](../../android/protocol/) | Pure Kotlin custom companion codecs and semantic records. | Host codec parity is distinct from BLE stack/device acceptance. |
| [Firmware companion](../../firmware/components/companion/) | Custom BLE records, sessions, authorization and configuration dispatch. | Target/stack and exact profile composition must be identified. |
| [Firmware protocol/delivery/security](../../firmware/components/) | Packet codecs, budgets, queues, identity, durable-counter and security boundaries. | Experimental v0, host requirements and isolated candidates do not automatically form a selected protected wire. |
| [Heltec target composition](../../firmware/targets/heltec_v4_bench/) | ESP-IDF/NimBLE/radio, NVS, display/input/GNSS/power adapters and application wiring. | Experimental bench target; exact successful diagnostic firmware includes retained source overlays. |

C4 containers represent the Android application, embedded firmware runtime and device-owned durable-state boundary. Individual classes and two copies of the same runtime are not separate products. The model's view descriptions record intended versus implemented boundaries and evidence limits.

The [Android architecture](../../android/docs/ARCHITECTURE.md) and versioned [AndroidComponents model](../../android/docs/android-components.dsl) refine the Android application container. They share these system/interface boundaries; a component view does not imply protected-radio or device acceptance.

## 6. Runtime View

**Intended pairing/reconnect.** Verified unowned boot opens one 60-second pairing window with a fresh locally displayed six-digit PIN and D1 marker. Owned boot exposes neither PIN nor D1. Returning-owner discovery uses the existing bond and D0 route, then protected ProtocolInfo/current owner authorization before Ready. No lost-phone transfer/replacement is implied; destructive reset has its own contract and physical gates.

**Implemented lifecycle boundary.** Android's service/session owner retains one controller/runtime/facade graph and rejects stale-generation callbacks. UI binding is separate from graph lifetime. Existing host and exact-phone evidence remains scoped in [Android guidance](../../android/README.md) and [current status](../PROJECT_STATUS.md).

**Demonstrated diagnostic exchange.** The November 2 attempt `981e43cff4914a948386933fb3edb7fb` delivered a fixed status Note20 → A → direct LoRa → B → S24 and a fixed reply back. Diagnostic ACKs matched; each board recorded two completed transmissions, one DATA receive, one ACK receive and zero rejected frames. Four RF transmissions, no retry, 57.2 seconds opt-in to close and verified stopped/phone closure were recorded. RF was unauthenticated diagnostic traffic. It does not accept authenticated production delivery or prove a person read a message.

**Unresolved production runtime.** Selected authenticated framing/ACKs, peer/key provisioning, persistence/replay/lifecycle and coherent two-pair acceptance remain open. The [V1/V1.5 scope](../testing/V1_V1_5_ACCEPTANCE_SCOPE_V0.md) and existing OT-0089a task retain the full gate; the diagnostic result does not close it. The more detailed local two-pair checklist is not yet published into main.

## 7. Deployment View

The intended product deploys the same Android application and embedded firmware types in two phone/device pairs. The mapped canonical target is ESP32-S3 Heltec V4 bench firmware using ESP-IDF 6.0.2; its [dependency lock](../../firmware/targets/heltec_v4_bench/dependencies.lock) and [target guide](../../firmware/targets/heltec_v4_bench/README.md) retain build constraints. This is not universal supported-hardware qualification.

Canonical Android configuration pins compile/target SDK 35, build tools 35.0.0, minimum SDK 26, JVM 17, AGP 8.7.3, Kotlin 2.0.21 and Gradle 8.11.1 with a distribution checksum. `v1Test` has a separate application suffix and diagnostic surface. Exact accepted APK identity and signer/source overlays come from maintained evidence, not `versionName` alone.

No product build was run for this documentation. The separate independent frozen-evidence review now qualifies declared-host firmware reconstruction and unsigned Android source-build/focused-test evidence. Firmware raw equality failed; Android non-signing entries match but signed-container equality/signer execution is unproved. [Build/recovery](../firmware/BUILD_AND_RECOVERY.md) records the exact result. This is not new-OS/hermetic reconstruction or device/runtime restoration; emulators do not establish Samsung BLE/radio acceptance.

## 8. Crosscutting Concepts

- **Authority:** device-issued state and exact current generations govern operations; stale callbacks, replay, unsupported profiles and uncertain state fail closed. BLE Ready is not LoRa membership.
- **Security:** [OTSL0](../security/SECURE_LORA_KEY_TRANSPORT_V0.md) requires device-local secrets, independent purpose/direction contexts, durable activation and replay/counter safety. Recognized primitives and library benchmarks do not establish Trail's complete composition.
- **Delivery:** local acceptance, transport acceptance, authenticated recipient acknowledgement and human reading remain distinct. Host retry/expiry examples are not measured deployed timing.
- **Privacy:** phones must not receive radio private/conversation/nonce secrets. Credentials, signing keys, raw captures, private identifiers and recovery payloads stay in protected custody and outside ordinary logs/handbook inputs.
- **Time/data:** monotonic deadlines, GNSS freshness, durable readback and uncertain-write containment must be explicit. Optional location/maps cannot silently become prerequisites for messaging.

## 9. Architectural Decisions

Preserve original IDs and explicit supersession: [0033](../decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md) defines direct two-pair V1 and separate V1.5; [0103](../decisions/0103-adopt-ot168-v1-factory-reset-and-boot-pairing.md) replaces pairing/replacement clauses; [0104](../decisions/0104-freeze-ot169-v1-user-experience-profile.md) defines UX; [0009](../decisions/0009-one-phone-companion-authority.md) defines device-authoritative state. Configuration/time decisions [0106](../decisions/0106-oled-configuration-time-authority.md) through [0109](../decisions/0109-bind-companion-name-and-time-configuration.md) and [0110](../decisions/0110-protected-region-selection-without-transmit-authority.md) retain their boundaries.

The [decision directory](../decisions/) retains rationale/history. Newer timestamps alone do not supersede accepted decisions; the proposed OT-0237e recommendation is not a completed selection.

## 10. Quality Requirements

Existing acceptance requires a candidate-bound coherent protected path, rejected unauthenticated/replayed/corrupt/wrong-recipient input, bounded failure/recovery, truthful stale position, correct one-phone ownership and exact signed-release identity. [V1/V1.5 scope](../testing/V1_V1_5_ACCEPTANCE_SCOPE_V0.md), [backlog](../../tasks/BACKLOG.md), [V1 progress](../V1_PROGRESS.json) and the dedicated Firmware/Android Projects own criteria and evidence. Local OT-0089a two-pair and OT-0086a pilot-signing checklists add preparation detail but are not yet part of main; their existence grants no acceptance.

| Existing scenario | Required observable outcome | Evidence limit |
|---|---|---|
| Wrong peer, invalid authentication or replay | No false application delivery or authenticated-success claim. | OTSL0/Packet-v1 host obligations; complete production composition remains open. |
| BLE owner/session changes or runtime closes | Stale generation cannot publish current results or gain control. | Host lifecycle tests and exact-phone observations; no universal phone acceptance. |
| GNSS is absent or position becomes stale | Position is unavailable/stale truthfully; messaging must remain independent. | Intended product boundary; a parser/host snapshot is not physical GNSS or end-to-end acceptance. |
| Persistence or operation outcome is uncertain | Preserve uncertainty; do not release authority or automatically invent/retry success. | Existing durable lifecycle requirements; interrupted target persistence remains gated. |
| Original source environment is unavailable | Retrieve declared source and exact binary separately; identify rebuild/equality/runtime limits. | Declared-host reconstruction/focused tests qualified; firmware raw equality failed. Exact artifact retrieval and runtime/data restore remain distinct. |

Use the [development guide](../DEVELOPMENT.md), [Android guide](../../android/README.md#build) and target guide for declared recipes. Tests and builds demonstrate their recorded scope; this documentation does not rerun or broaden them. The in-tree Windows loader is historical; current Firmware Loader lives in its separate repository.

## 11. Risks and Technical Debt

Protected production wire/crypto admission, exact peer/key provisioning, interrupted persistence, secret retirement, replay/lifecycle, coherent signed two-pair acceptance and field/endurance remain open. Source/history preservation, exact-binary retrieval, source rebuild, byte equality/differences and runtime restore are separate recovery claims; see [OT-0340](../testing/OT-0340-TRAIL-SOURCE-RECOVERY-2026-10-23.md) and current handoff.

Canonical main plus dirty files is not the exact successful diagnostic source. Older contract/guide openings retain historical states that must not override later exact evidence. Retained private evidence may not be available to a public clone; record that access limit instead of fabricating remote retrievability. Parser/build success and independent editorial review cannot by themselves prove architectural truth or product acceptance.

## 12. Glossary

| Term | Meaning here |
|---|---|
| Ready | The current BLE lane's admitted state; no radio membership/delivery implication. |
| Diagnostic ACK | Fixed-test peer correlation from unauthenticated diagnostic RF. |
| Delivered | Authenticated recipient acknowledgement under the selected product contract; no read receipt. |
| Intended | Accepted design/requirement whose complete implementation or acceptance may remain open. |
| Implemented | Identifiable source exists at the stated boundary; acceptance is separate. |
| Demonstrated | Exact recorded result for its artifact/setup and stated limits. |
| OpenTrail / OT identifiers | Preserved technical/historical names, not new branding. |

This document follows arc42 and uses C4/Structurizr views. ISO/IEC/IEEE 42010:2022 is a review reference; no conformance or certification is claimed. Original contracts and evidence retain authority.
