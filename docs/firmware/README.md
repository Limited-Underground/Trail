# Limited Underground Trail Firmware V1

Start here for the firmware's current scope, implementation, evidence and remaining V1 criteria. Development is active under the [working contract](../../CONTRIBUTING.md#working-contract); device sessions and releases each need the owner's approval first. OT-0005i still waits for accepted security admission and exact production-source selection; those production prerequisites remain unmet. [Current handoff](../PROJECT_STATUS.md#current-recovery-handoff) identifies the documentation task, artifact identities and evidence-access limits.

## Purpose and present result

The intended V1 connects exactly two Android phones, each paired with one supported Heltec device, through protected BLE and separately protected direct LoRa. The device owns radio security, membership, queues, delivery identity and durable state; the phone submits intent and presents authoritative results. Internet, maps and other portfolio products are optional to this direct path. See [Decision 0033](../decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md), [Decision 0009](../decisions/0009-one-phone-companion-authority.md) and the [architecture](../architecture/README.md).

The latest demonstrated two-pair exchange is a fixed-status diagnostic experiment with unauthenticated RF. It reached both phone-to-phone directions with matching diagnostic ACKs and verified closure. It establishes neither authenticated product delivery nor a human read receipt. The [November 2 diagnostic record](../PROGRESS_LOG.md#ot-0005j-two-phone-diagnostic-lora-exchange-delivered-both-ways) preserves the exact success and preparation history from [PR #70](https://github.com/Limited-Underground/Trail/pull/70); publication is separate from product acceptance.

Independent frozen-evidence review subsequently qualified a declared-host firmware reconstruction and focused native tests. The rebuilt firmware **does not equal the accepted diagnostic image**. Preserve and identify the accepted binary separately. [Build and recovery](BUILD_AND_RECOVERY.md) explains the source boundary, exact comparison and limits; [V1 acceptance](V1_ACCEPTANCE.md) maps the existing unfinished criteria without granting execution.

## Nine assessment areas

| Area | Current reading route and finding |
| --- | --- |
| Product | [Product boundaries](../PRODUCT_BOUNDARIES_V0.md), [V1 UX](../product/V1_USER_EXPERIENCE.md), [acceptance map](V1_ACCEPTANCE.md). Direct two-pair protected V1 remains intended; diagnostic success is narrower. |
| Architecture | [Twelve arc42 sections](../architecture/README.md), [versioned C4 source](../architecture/workspace.dsl), [detailed architecture](../ARCHITECTURE.md). Device authority and phone projection remain separate. |
| Protocol & Interfaces | [Protocol selection](../protocol/README.md) separates standard BLE/LoRa mechanisms, custom records, experimental packet v0, diagnostic traffic and isolated Packet-v1 host candidates. No production selection is inferred. |
| Security | [Threat model](../security/THREAT_MODEL_V0.md), [OTSL0](../security/SECURE_LORA_KEY_TRANSPORT_V0.md), [security policy](../../SECURITY.md). BLE ownership does not secure radio; final composition, target persistence and admission remain open. |
| Source & Build | [Build/recovery guide](BUILD_AND_RECOVERY.md), [components](../../firmware/components/), [bench target](../../firmware/targets/heltec_v4_bench/README.md), [target lock](../../firmware/targets/heltec_v4_bench/dependencies.lock). Exact successful diagnostic overlays differ from canonical source and isolated protected candidates. |
| Quality | [Acceptance map](V1_ACCEPTANCE.md), [host tests](../../tests/host/), [testing records](../testing/). Four native suites/110 groups plus eight SDK fact tests and eight composed scenarios passed within the frozen host review; physical/product acceptance is separate. |
| Ops & Recovery | [Five recovery claims](BUILD_AND_RECOVERY.md#recovery-claims), [current handoff](../PROJECT_STATUS.md), [porting lessons](../firmware-porting-lessons.md). Exact artifact retrieval, source reconstruction, equality and runtime/data restoration are distinct. |
| IP | The repository [Apache-2.0 license](../../LICENSE) does not replace third-party obligations. Retain exact SDK/library versions and their own licenses/notices with source-lock evidence. [License-aware admission](../decisions/0041-license-aware-source-lock-admission-v1.md) preserves the existing dependency-admission boundary; no whole-distribution legal clearance is claimed. |
| Engineering Controls | [Working contract](../../CONTRIBUTING.md#working-contract), [agent adapter](../../AGENTS.md), [backlog](../../tasks/BACKLOG.md) and [Firmware Project #1](https://github.com/orgs/Limited-Underground/projects/1). Current owner authority controls execution; source records retain evidence; historical physical grants cannot be reused. |

## Read by role

- A new maintainer follows this guide → architecture → protocol selection → build/recovery → V1 acceptance → current task. Private evidence is identified through its custodian; it is not secretly required to navigate a public clone.
- An Android maintainer starts with the [Android guide](../../android/README.md) and the same shared device/phone contracts. Android source and acceptance remain separately owned in [Project #8](https://github.com/orgs/Limited-Underground/projects/8).
- An installer maintainer uses the separate [Limited Underground Firmware Loader repository](https://github.com/Limited-Underground/Firmware-Loader). The in-tree Windows loader is a frozen predecessor and does not accept the current Loader or authorize a writer.

Use full Limited Underground product names in current prose. Preserve `OT-` task IDs, `OpenTrail` historical references, package names, UUIDs, protocol bytes and other technical compatibility keys. arc42/C4 organize these reading paths; ISO/IEC/IEEE 42010:2022 is a review reference, with no conformance claim.
