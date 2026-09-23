# Optional Tracker, Console and Repeater task plan

Planning register, 2026-09-21. All children are Pending proposals. These products remain optional and do not change base V1 or completion.

## Completeness boundary

These are executable definition-stage tasks, not a claim that eight tasks deliver each product. Tracker target/receiver/provisioning, Console exact assembly and approved features, and Repeater topology/security/target remain undecided. OT-0258a, OT-0262a and OT-0266b must freeze and register the resulting implementation-through-release successor chain after those decisions. They cannot close with downstream work merely implied. Existing milestones are outcomes, not authorization.

Historical Decision 0007 standalone-first V1 and Decision 0004 first-release repeater wording are superseded for current V1 by Decision 0033 and direct-only Decision 0035. Reuse historical host evidence only within its exact boundary. A vendor product name does not establish an accepted Tracker target. The owner-defined Console is a LoRa radio running Trail firmware with ESP32 touchscreen LCD, microSD and battery; additional historical options require a decision.

## Owner requirements

All tasks below prepare documents/contracts/test procedures only: no device connection, attendance or time window is needed. Review each deliverable after submission. Only concrete unresolved choices should become owner questions. Future physical work must specify exact devices, connection, starting state, actions, expected result, timing and recovery before approval. Product planning may run independently; dependencies within each product prevent designing against undecided choices.

## OT-0256a: Recover Tracker evidence and ownership

- Product: tracker
- Parent milestone: OT-0256
- Scope: Inventory tracking-related decisions, components and tests; distinguish Tracker product from tracker state-machine names and Wio vendor naming. Planning deliverable only. Sources: tasks/BACKLOG.md (OT-0256 through OT-0259); owner-approved tracking-only boundary; docs/decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md
- Exclusions: No implementation, device access, purchasing, target support claim, release or publication. No base V1 expansion. Approval of this task does not approve successor implementation.
- Acceptance: Every claimed reusable component has a path and evidence layer; unknown hardware remains unknown; no new repository is proposed by implication.
- Checkpoint: Submit the named planning deliverable and exact unresolved decisions for owner review; register bounded implementation/physical children only after scope and target decisions.
- Depends on: none.

## OT-0256b: Prepare the tracking-only product decision

- Product: tracker
- Parent milestone: OT-0256
- Scope: Turn the owner's tracking-only, not two-way communication boundary into proposed use cases, permitted receivers and non-goals. Planning deliverable only. Sources: tasks/BACKLOG.md (OT-0256 through OT-0259); owner-approved tracking-only boundary; docs/decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md
- Exclusions: No implementation, device access, purchasing, target support claim, release or publication. No base V1 expansion. Approval of this task does not approve successor implementation.
- Acceptance: Decision lists intended operator, tracked subject, provisioning path and receiving endpoint choices; unresolved choices have specific owner questions.
- Checkpoint: Submit the named planning deliverable and exact unresolved decisions for owner review; register bounded implementation/physical children only after scope and target decisions.
- Depends on: OT-0256a.

## OT-0257a: Specify location privacy and reporting policy

- Product: tracker
- Parent milestone: OT-0257
- Scope: Propose consent, authorized recipients, identity binding, report cadence, retention and stop/reset semantics without choosing unmeasured numeric defaults. Planning deliverable only. Sources: tasks/BACKLOG.md (OT-0256 through OT-0259); owner-approved tracking-only boundary; docs/decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md
- Exclusions: No implementation, device access, purchasing, target support claim, release or publication. No base V1 expansion. Approval of this task does not approve successor implementation.
- Acceptance: Policy distinguishes fresh, stale, unavailable and disabled location; loss/restart never silently re-enables sharing; each policy choice is explicit.
- Checkpoint: Submit the named planning deliverable and exact unresolved decisions for owner review; register bounded implementation/physical children only after scope and target decisions.
- Depends on: OT-0256b.

## OT-0257b: Prepare Tracker target and power selection criteria

- Product: tracker
- Parent milestone: OT-0257
- Scope: Define required location/radio/power interfaces and candidate evaluation evidence from the tracking-only use cases. Planning deliverable only. Sources: tasks/BACKLOG.md (OT-0256 through OT-0259); owner-approved tracking-only boundary; docs/decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md
- Exclusions: No implementation, device access, purchasing, target support claim, release or publication. No base V1 expansion. Approval of this task does not approve successor implementation.
- Acceptance: Selection checklist covers received board/revision, region, power measurement and recovery; no candidate is labeled supported and no hardware purchase is assumed.
- Checkpoint: Submit the named planning deliverable and exact unresolved decisions for owner review; register bounded implementation/physical children only after scope and target decisions.
- Depends on: OT-0257a.

## OT-0258a: Map Tracker implementation slices and contract tests

- Product: tracker
- Parent milestone: OT-0258
- Scope: Map approved tracking behavior to existing OpenTrail modules and identify bounded adapter, provisioning and reporting changes. Planning deliverable only. Sources: tasks/BACKLOG.md (OT-0256 through OT-0259); owner-approved tracking-only boundary; docs/decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md
- Exclusions: No implementation, device access, purchasing, target support claim, release or publication. No base V1 expansion. Approval of this task does not approve successor implementation.
- Acceptance: Each proposed implementation slice names owning files/interfaces, prerequisites and positive/negative host tests; wire or receiver changes require explicit compatibility review. After prerequisite decisions, register and freeze bounded implementation, build/host-test, physical-validation and release successor tasks with exact acceptance and dependencies. This task cannot close with downstream implementation merely implied.
- Checkpoint: Submit the named planning deliverable and exact unresolved decisions for owner review; register bounded implementation/physical children only after scope and target decisions.
- Depends on: OT-0257b.

## OT-0258b: Write Tracker failure and recovery test procedures

- Product: tracker
- Parent milestone: OT-0258
- Scope: Prepare deterministic scenarios for lost/stale fix, denied recipient, radio outage, depleted battery, restart and interrupted configuration. Planning deliverable only. Sources: tasks/BACKLOG.md (OT-0256 through OT-0259); owner-approved tracking-only boundary; docs/decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md
- Exclusions: No implementation, device access, purchasing, target support claim, release or publication. No base V1 expansion. Approval of this task does not approve successor implementation.
- Acceptance: Each scenario states starting state, trigger, expected observable result, forbidden behavior and retained-state check; physical cases await exact device approval.
- Checkpoint: Submit the named planning deliverable and exact unresolved decisions for owner review; register bounded implementation/physical children only after scope and target decisions.
- Depends on: OT-0258a.

## OT-0259a: Prepare Tracker physical acceptance and measurement plan

- Product: tracker
- Parent milestone: OT-0259
- Scope: Define candidate-specific field comparison, delivery/latency/loss and measured power procedures once target and policy decisions are accepted. Planning deliverable only. Sources: tasks/BACKLOG.md (OT-0256 through OT-0259); owner-approved tracking-only boundary; docs/decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md
- Exclusions: No implementation, device access, purchasing, target support claim, release or publication. No base V1 expansion. Approval of this task does not approve successor implementation.
- Acceptance: Plan identifies exact device/receiver roles and antenna/region prerequisites; separates coordinate accuracy, radio receipt and UI freshness; requires bounded recovery and privacy-safe evidence.
- Checkpoint: Submit the named planning deliverable and exact unresolved decisions for owner review; register bounded implementation/physical children only after scope and target decisions.
- Depends on: OT-0258b.

## OT-0259b: Prepare Tracker operator and release checklist

- Product: tracker
- Parent milestone: OT-0259
- Scope: Draft setup, sharing stop, loss/stale indications, recovery and known-limit documentation plus artifact/support acceptance gates. Planning deliverable only. Sources: tasks/BACKLOG.md (OT-0256 through OT-0259); owner-approved tracking-only boundary; docs/decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md
- Exclusions: No implementation, device access, purchasing, target support claim, release or publication. No base V1 expansion. Approval of this task does not approve successor implementation.
- Acceptance: Checklist maps each supported claim to required evidence and release artifact; unresolved tests prevent support claims; signing/publication remain separately authorized.
- Checkpoint: Submit the named planning deliverable and exact unresolved decisions for owner review; register bounded implementation/physical children only after scope and target decisions.
- Depends on: OT-0259a.

## OT-0260a: Reconcile Console scope with historical client work

- Product: console
- Parent milestone: OT-0260
- Scope: Map the owner's LoRa plus ESP32 touchscreen LCD, microSD and battery Console direction to historical standalone/shared UI evidence. Planning deliverable only. Sources: tasks/BACKLOG.md (OT-0260 through OT-0263); owner-approved Console description; docs/decisions/0007-shared-client-presentation-tracks.md; docs/decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md
- Exclusions: No implementation, device access, purchasing, target support claim, release or publication. No base V1 expansion. Approval of this task does not approve successor implementation.
- Acceptance: Inventory distinguishes simulator/host evidence from physical evidence and identifies obsolete V1/Gold/Platinum assumptions; current base V1 remains unchanged.
- Checkpoint: Submit the named planning deliverable and exact unresolved decisions for owner review; register bounded implementation/physical children only after scope and target decisions.
- Depends on: none.

## OT-0260b: Prepare Console hardware and interface decision

- Product: console
- Parent milestone: OT-0260
- Scope: List required renderer/input, radio, storage and power interfaces and evidence needed to select a compatible assembly. Planning deliverable only. Sources: tasks/BACKLOG.md (OT-0260 through OT-0263); owner-approved Console description; docs/decisions/0007-shared-client-presentation-tracks.md; docs/decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md
- Exclusions: No implementation, device access, purchasing, target support claim, release or publication. No base V1 expansion. Approval of this task does not approve successor implementation.
- Acceptance: Decision names unresolved board/display/SD/power choices and recovery constraints; GNSS and other historical options require scope confirmation rather than automatic inclusion.
- Checkpoint: Submit the named planning deliverable and exact unresolved decisions for owner review; register bounded implementation/physical children only after scope and target decisions.
- Depends on: OT-0260a.

## OT-0261a: Define the approved Console interaction flow

- Product: console
- Parent milestone: OT-0261
- Scope: Specify the minimum approved touch workflows using existing semantic state and delivery/failure meanings. Planning deliverable only. Sources: tasks/BACKLOG.md (OT-0260 through OT-0263); owner-approved Console description; docs/decisions/0007-shared-client-presentation-tracks.md; docs/decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md
- Exclusions: No implementation, device access, purchasing, target support claim, release or publication. No base V1 expansion. Approval of this task does not approve successor implementation.
- Acceptance: Flow covers startup, authorized operation, pending/failed results and recovery; empty/offline/error states are explicit; no simulator success is credited as device acceptance.
- Checkpoint: Submit the named planning deliverable and exact unresolved decisions for owner review; register bounded implementation/physical children only after scope and target decisions.
- Depends on: OT-0260b.

## OT-0261b: Specify microSD and power failure behavior

- Product: console
- Parent milestone: OT-0261
- Scope: Define approved storage purposes, retention, capacity/error behavior, safe removal and interrupted writes together with power-state expectations. Planning deliverable only. Sources: tasks/BACKLOG.md (OT-0260 through OT-0263); owner-approved Console description; docs/decisions/0007-shared-client-presentation-tracks.md; docs/decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md
- Exclusions: No implementation, device access, purchasing, target support claim, release or publication. No base V1 expansion. Approval of this task does not approve successor implementation.
- Acceptance: Policy covers absent/full/corrupt/removable media and interrupted power; storage or optional-service failure cannot silently grant access or fabricate delivery; numeric power limits await measurement.
- Checkpoint: Submit the named planning deliverable and exact unresolved decisions for owner review; register bounded implementation/physical children only after scope and target decisions.
- Depends on: OT-0261a.

## OT-0262a: Map Console target integration and host gates

- Product: console
- Parent milestone: OT-0262
- Scope: Prepare bounded implementation slices for the selected renderer/input/storage/power/radio adapters and shared state. Planning deliverable only. Sources: tasks/BACKLOG.md (OT-0260 through OT-0263); owner-approved Console description; docs/decisions/0007-shared-client-presentation-tracks.md; docs/decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md
- Exclusions: No implementation, device access, purchasing, target support claim, release or publication. No base V1 expansion. Approval of this task does not approve successor implementation.
- Acceptance: Every slice names module ownership, build targets, contract tests and prerequisites; proposed target changes require firmware-porting preflight and retain independent base-field behavior. After prerequisite decisions, register and freeze bounded implementation, build/host-test, physical-validation and release successor tasks with exact acceptance and dependencies. This task cannot close with downstream implementation merely implied.
- Checkpoint: Submit the named planning deliverable and exact unresolved decisions for owner review; register bounded implementation/physical children only after scope and target decisions.
- Depends on: OT-0261b.

## OT-0262b: Prepare Console recovery and optional-service tests

- Product: console
- Parent milestone: OT-0262
- Scope: Write procedures for boot failure, restart, interrupted storage, radio loss, and any approved optional service disappearing. Planning deliverable only. Sources: tasks/BACKLOG.md (OT-0260 through OT-0263); owner-approved Console description; docs/decisions/0007-shared-client-presentation-tracks.md; docs/decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md
- Exclusions: No implementation, device access, purchasing, target support claim, release or publication. No base V1 expansion. Approval of this task does not approve successor implementation.
- Acceptance: Procedures identify observable pass/fail outcomes, preserved data, restoration and stop conditions; maps/server/GNSS cases are conditional on accepted scope.
- Checkpoint: Submit the named planning deliverable and exact unresolved decisions for owner review; register bounded implementation/physical children only after scope and target decisions.
- Depends on: OT-0262a.

## OT-0263a: Prepare Console physical UI and endurance acceptance

- Product: console
- Parent milestone: OT-0263
- Scope: Define the selected assembly's touch/readability, real-radio, storage and measured power test matrix. Planning deliverable only. Sources: tasks/BACKLOG.md (OT-0260 through OT-0263); owner-approved Console description; docs/decisions/0007-shared-client-presentation-tracks.md; docs/decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md
- Exclusions: No implementation, device access, purchasing, target support claim, release or publication. No base V1 expansion. Approval of this task does not approve successor implementation.
- Acceptance: Matrix separates actual display/touch evidence from host rendering, names exact future device roles and owner actions, and has bounded stop/recovery criteria before hardware authorization.
- Checkpoint: Submit the named planning deliverable and exact unresolved decisions for owner review; register bounded implementation/physical children only after scope and target decisions.
- Depends on: OT-0262b.

## OT-0263b: Prepare Console operator and release acceptance

- Product: console
- Parent milestone: OT-0263
- Scope: Draft operator guidance and support/release gates for the approved Console assembly. Planning deliverable only. Sources: tasks/BACKLOG.md (OT-0260 through OT-0263); owner-approved Console description; docs/decisions/0007-shared-client-presentation-tracks.md; docs/decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md
- Exclusions: No implementation, device access, purchasing, target support claim, release or publication. No base V1 expansion. Approval of this task does not approve successor implementation.
- Acceptance: Guidance covers setup, unavailable services, storage handling, battery/recovery and limitations; supported configuration and artifact matrix require accepted hardware evidence before release.
- Checkpoint: Submit the named planning deliverable and exact unresolved decisions for owner review; register bounded implementation/physical children only after scope and target decisions.
- Depends on: OT-0263a.

## OT-0264a: Reconcile repeater prototype evidence and scope

- Product: repeater
- Parent milestone: OT-0264
- Scope: Inventory historical exact-byte forwarding, replay persistence, hardware candidates and optional client/repeater direction. Planning deliverable only. Sources: tasks/BACKLOG.md (OT-0264 through OT-0267); docs/FUTURE_CONCEPTS.md; docs/decisions/0004-immutable-first-release-forwarding.md; docs/decisions/0035-host-tested-secure-lora-key-transport-contract.md
- Exclusions: No implementation, device access, purchasing, target support claim, release or publication. No base V1 expansion. Approval of this task does not approve successor implementation.
- Acceptance: Evidence map separates simulation, host policy and physical support; current direct-only V1 and unscheduled client/repeater direction are explicit.
- Checkpoint: Submit the named planning deliverable and exact unresolved decisions for owner review; register bounded implementation/physical children only after scope and target decisions.
- Depends on: none.

## OT-0264b: Prepare dedicated versus client/repeater role decision

- Product: repeater
- Parent milestone: OT-0264
- Scope: Present scope choices for dedicated relay and optional combined client/relay behavior without silently committing both. Planning deliverable only. Sources: tasks/BACKLOG.md (OT-0264 through OT-0267); docs/FUTURE_CONCEPTS.md; docs/decisions/0004-immutable-first-release-forwarding.md; docs/decisions/0035-host-tested-secure-lora-key-transport-contract.md
- Exclusions: No implementation, device access, purchasing, target support claim, release or publication. No base V1 expansion. Approval of this task does not approve successor implementation.
- Acceptance: Decision fixes intended first topology, role authority, non-goals and compatibility boundary; base clients remain usable when relay disabled or absent.
- Checkpoint: Submit the named planning deliverable and exact unresolved decisions for owner review; register bounded implementation/physical children only after scope and target decisions.
- Depends on: OT-0264a.

## OT-0265a: Define a secure forwarding contract review

- Product: repeater
- Parent milestone: OT-0265
- Scope: Prepare protocol review of authenticated eligible forwarding, immutable protected bytes, membership/epoch and replay handling. Planning deliverable only. Sources: tasks/BACKLOG.md (OT-0264 through OT-0267); docs/FUTURE_CONCEPTS.md; docs/decisions/0004-immutable-first-release-forwarding.md; docs/decisions/0035-host-tested-secure-lora-key-transport-contract.md
- Exclusions: No implementation, device access, purchasing, target support claim, release or publication. No base V1 expansion. Approval of this task does not approve successor implementation.
- Acceptance: Review explicitly resolves supersession of historical Decision0004 versus direct-only OTSL0/v0; no unauthenticated Boolean, mutable protected TTL or reserved-bit reinterpretation grants forwarding.
- Checkpoint: Submit the named planning deliverable and exact unresolved decisions for owner review; register bounded implementation/physical children only after scope and target decisions.
- Depends on: OT-0264b.

## OT-0265b: Specify bounded relay scheduling and congestion tests

- Product: repeater
- Parent milestone: OT-0265
- Scope: Propose queue, duplicate, expiry, airtime/rate and client/relay scheduling rules for the selected topology. Planning deliverable only. Sources: tasks/BACKLOG.md (OT-0264 through OT-0267); docs/FUTURE_CONCEPTS.md; docs/decisions/0004-immutable-first-release-forwarding.md; docs/decisions/0035-host-tested-secure-lora-key-transport-contract.md
- Exclusions: No implementation, device access, purchasing, target support claim, release or publication. No base V1 expansion. Approval of this task does not approve successor implementation.
- Acceptance: Test cases cover loops/duplicates/replays, overload, stale epochs and relay loss; limits require reviewed budget evidence and no unbounded flooding is allowed.
- Checkpoint: Submit the named planning deliverable and exact unresolved decisions for owner review; register bounded implementation/physical children only after scope and target decisions.
- Depends on: OT-0265a.

## OT-0266a: Prepare repeater hardware and deployment profile

- Product: repeater
- Parent milestone: OT-0266
- Scope: Define evidence required for selected board/radio region, antenna, enclosure, power source and safe recovery. Planning deliverable only. Sources: tasks/BACKLOG.md (OT-0264 through OT-0267); docs/FUTURE_CONCEPTS.md; docs/decisions/0004-immutable-first-release-forwarding.md; docs/decisions/0035-host-tested-secure-lora-key-transport-contract.md
- Exclusions: No implementation, device access, purchasing, target support claim, release or publication. No base V1 expansion. Approval of this task does not approve successor implementation.
- Acceptance: Profile distinguishes candidate specifications from received-unit measurements; no range, runtime or environmental rating is claimed without tests.
- Checkpoint: Submit the named planning deliverable and exact unresolved decisions for owner review; register bounded implementation/physical children only after scope and target decisions.
- Depends on: OT-0265b.

## OT-0266b: Specify provisioning, diagnostics and durable recovery

- Product: repeater
- Parent milestone: OT-0266
- Scope: Prepare opt-in role control, membership removal, private diagnostics and restart/persistence requirements. Planning deliverable only. Sources: tasks/BACKLOG.md (OT-0264 through OT-0267); docs/FUTURE_CONCEPTS.md; docs/decisions/0004-immutable-first-release-forwarding.md; docs/decisions/0035-host-tested-secure-lora-key-transport-contract.md
- Exclusions: No implementation, device access, purchasing, target support claim, release or publication. No base V1 expansion. Approval of this task does not approve successor implementation.
- Acceptance: Specification covers stale or damaged state, interrupted saves, disabled relay and power loss; retained replay protection and endpoint authentication are not weakened. After prerequisite decisions, register and freeze bounded implementation, build/host-test, physical-validation and release successor tasks with exact acceptance and dependencies. This task cannot close with downstream implementation merely implied.
- Checkpoint: Submit the named planning deliverable and exact unresolved decisions for owner review; register bounded implementation/physical children only after scope and target decisions.
- Depends on: OT-0266a.

## OT-0267a: Prepare relay interoperability and fault acceptance

- Product: repeater
- Parent milestone: OT-0267
- Scope: Write sender-relay-receiver and relay-disabled negative-control procedures for approved topology. Planning deliverable only. Sources: tasks/BACKLOG.md (OT-0264 through OT-0267); docs/FUTURE_CONCEPTS.md; docs/decisions/0004-immutable-first-release-forwarding.md; docs/decisions/0035-host-tested-secure-lora-key-transport-contract.md
- Exclusions: No implementation, device access, purchasing, target support claim, release or publication. No base V1 expansion. Approval of this task does not approve successor implementation.
- Acceptance: Matrix requires three radios for relay-path proof; accounts packets/loss/duplicates/latency/airtime, power/restart and concurrent client traffic if combined mode is selected; exact hardware approval remains separate.
- Checkpoint: Submit the named planning deliverable and exact unresolved decisions for owner review; register bounded implementation/physical children only after scope and target decisions.
- Depends on: OT-0266b.

## OT-0267b: Prepare repeater deployment and release guidance

- Product: repeater
- Parent milestone: OT-0267
- Scope: Draft operator provisioning, antenna/power placement, removal/recovery and support checklist. Planning deliverable only. Sources: tasks/BACKLOG.md (OT-0264 through OT-0267); docs/FUTURE_CONCEPTS.md; docs/decisions/0004-immutable-first-release-forwarding.md; docs/decisions/0035-host-tested-secure-lora-key-transport-contract.md
- Exclusions: No implementation, device access, purchasing, target support claim, release or publication. No base V1 expansion. Approval of this task does not approve successor implementation.
- Acceptance: Every support/range/power claim links to required measured evidence; direct path remains optional-relay independent and release/signing/publication need explicit approval.
- Checkpoint: Submit the named planning deliverable and exact unresolved decisions for owner review; register bounded implementation/physical children only after scope and target decisions.
- Depends on: OT-0267a.
