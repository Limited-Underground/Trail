# Quality and evidence map

[Android reading path](../README.md) · [Existing V1 criteria](V1_ACCEPTANCE.md)

## Evidence levels

| Level | Existing evidence | What remains separate |
| --- | --- | --- |
| Checked-in source/host foundation | [Android matrix record](../../docs/testing/OT-0170A-HOST-MATRIX-2026-10-24.md), pure Kotlin codecs and lifecycle tests. | Not every exact phone, complete product composition or current signed artifact. |
| Qualified private source reconstruction | 43 tests passed: GATT queue (6), runtime (11), cache refresh (9), radio diagnostic state (7), execution rehearsal (7), opt-in Compose UI (3). | Synthetic boundaries, not actual Samsung stack/RF behavior; not today's public-source matrix. |
| Exact historical hardware | [Configuration evidence](../../docs/testing/OT-170-178-LIVE-INTEGRATION-2026-09-06.md) and maintained handoff; November 2 diagnostic status/reply, destination displays, matched ACKs and closure. | Artifact/setup-specific. Diagnostic RF unauthenticated; no human-read, protected-product or release claim. |
| Operational/release acceptance | [OTAR0 contract](../../docs/platform/ANDROID_OPERATIONAL_RELEASE_ACCEPTANCE_V0.md), later retention reconciliation and existing candidate criteria. | Planning/schema/validator PASS cannot produce candidate release PASS. |

The independent reconstruction review verified the compact seals, source rows,
145 non-signing APK entries and test XML. See [exact artifact limits](SOURCE_BUILD.md).
Firmware's optional unfixed-source negative control was skipped; no skipped case
is promoted to a new pass. Builds/tests were reused, not rerun for these docs.

## Source-to-test routing

| Concern | Existing source/test route | Acceptance still required |
| --- | --- | --- |
| Exact native operation/callback ownership | [AndroidGattCommandQueue](../app/src/main/kotlin/io/github/nbjelanovic/otclient/AndroidGattCommandQueue.kt), [app tests](../app/src/test/) | Supported-phone lifecycle/permissions/recovery on the exact candidate. |
| Ready/authorization and stale generation | [BleCompanionRuntime](../app/src/main/kotlin/io/github/nbjelanovic/otclient/BleCompanionRuntime.kt), runtime tests and retained qualified overlay tests | Historical Ready never substitutes for a fresh physical session. |
| Record parsing/semantic parity | [protocol tests](../protocol/src/test/), [shared fixtures](../../tests/fixtures/) | Actual admitted profile, peer and transport composition. |
| Opt-in/CCCD/native lane | Private qualified source, 43-test receipt, G1/G2 exact-phone evidence | No current operation or general hidden-API guarantee. |
| Support privacy/export | [schema evidence](../../docs/testing/OT-177-C01-HOST-SCHEMA-EVIDENCE.md), support source and reconciliation | Full physical preview/Save/Share/grants/retention/Clear and release-package checks. |

## Required quality is already defined

Existing criteria require rejection of stale/unauthorized/misrouted input, truthful
Queued/Sent/Delivered/failure state, restart/interruption safety, independent
GNSS/radio degradation, populated responsive UI and accessibility, explicit
support export, production exclusion of test surfaces and candidate-bound
lifecycle/endurance/privacy. The [task map](V1_ACCEPTANCE.md) preserves their IDs
and dependencies. This page adds no thresholds, new acceptance gate or percentage.

An emulator or host UI test cannot reproduce Samsung BLE behavior. A recorded
physical diagnostic result cannot accept missing protected messaging. Complete
physical matrices and independent release reconciliation remain frozen pending
their exact owner direction.
