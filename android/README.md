# Limited Underground Trail Android App

The Android app is part of this [Trail repository](../README.md), in `android/`;
there is no separate public Android source repository. It presents device state
and submits intent over BLE. The Heltec owns radio authority and delivery state.

**Physically demonstrated — November 2, 2026:** a fixed status from Note20 →
Heltec A → LoRa → Heltec B → S24, then the reply back to Note20, appeared on
the destination phones with matched diagnostic ACKs. See the
[recorded OT-0005j result](../docs/PROGRESS_LOG.md#ot-0005j-two-phone-diagnostic-lora-exchange-delivered-both-ways)
and [exact artifacts and source boundaries](docs/SOURCE_BUILD.md#exact-reference-and-qualified-reconstruction).
**This was unauthenticated diagnostic RF, not protected production delivery or
release acceptance.**

**Product development is frozen indefinitely.** Current work is documentation,
preservation and reproducibility. Read the [working contract](../CONTRIBUTING.md#working-contract)
and [current handoff](../docs/PROJECT_STATUS.md#current-recovery-handoff).
[Android App Project #8](https://github.com/orgs/Limited-Underground/projects/8)
owns Android task scope/status; [Firmware Project #1](https://github.com/orgs/Limited-Underground/projects/1)
owns firmware work. Neither a board status nor a documented command grants execution authority.

## Read by concern

| Area | Reading path |
| --- | --- |
| Product | [V1 scope and evidence boundaries](docs/PRODUCT_SCOPE.md) |
| Architecture | [Android arc42 sections and C4 views](docs/ARCHITECTURE.md) |
| Protocol & interfaces | [Source-backed boundary map](docs/PROTOCOL_INTERFACES.md) |
| Security | [Ownership, privacy and profile selection](docs/SECURITY.md) |
| Source & build | [Public source, qualified overlays and exact inputs](docs/SOURCE_BUILD.md) |
| Quality | [Host, physical and release evidence](docs/QUALITY.md) |
| Operations & recovery | [Lifecycle and five separate recovery claims](docs/OPERATIONS_RECOVERY.md) |
| IP | [License and third-party records](docs/IP_THIRD_PARTY.md) |
| Engineering controls | [Portable rules and reusable procedures](docs/ENGINEERING_CONTROLS.md) |

Then read the [existing remaining V1 criteria](docs/V1_ACCEPTANCE.md). This map
does not create new requirements or change the dependency graph.

## What is demonstrated

The November 2 two-phone/two-Heltec run displayed a fixed status and reply across
BLE → direct LoRa → BLE, with matched diagnostic ACKs and verified closure.
Its RF was **unauthenticated diagnostic traffic**. The exact APK used private
qualified source that differs from the checked-in Android foundation.
[Source and artifact identities](docs/SOURCE_BUILD.md) explain that distinction.

The independently qualified reconstruction passed 43 focused Android tests;
every non-signing APK entry matched the accepted APK. It produced an unsigned
artifact, not matching-signer, installed-device, protected-messaging or release
acceptance. A public clone alone does not reproduce the private qualified source.

## Build

Use the [source/build guide](docs/SOURCE_BUILD.md#checked-in-foundation-recipe)
for the checked-in Gradle project and its working directory, tools and matrix.
The qualified no-signing reconstruction is a distinct preserved recipe.
No build, signing or installation is requested by this page.

## Continue

Use the [current handoff](../docs/PROJECT_STATUS.md#current-recovery-handoff) and
[Android App Project #8](https://github.com/orgs/Limited-Underground/projects/8)
for live task state, owners and existing dependency gates. The
[V1 criteria](docs/V1_ACCEPTANCE.md) identify remaining protected-messaging and
release acceptance; diagnostic success does not close those gates. Product
resumption, physical operations and publication require their applicable owner
authority. Documentation does not authorize them.

The [historical foundation guide](docs/HISTORICAL_FOUNDATION_GUIDE.md) preserves
earlier detailed checkpoints and recipes. Its dated “current” statements do not
override this reading path, the maintained handoff or later accepted decisions.
