# OT-177 C01 automatic-report privacy input map

Date: 2026-10-19. Evidence layer: source inspection and documentation validation.
Status: twelve automatic fields and eight excluded-data categories mapped;
execution gaps remain open. This document is not a full C01 privacy pass.

## Scope and source snapshot

This October 19 source investigation maps the automatic report fields and
excluded-data categories against the linked repository sources. It did not
execute Android tests, inspect a package, or operate a device. The report test
and [schema evidence](OT-177-C01-HOST-SCHEMA-EVIDENCE.md) were separate retained
work when the map was prepared. The historical source snapshot was a dirty
working checkout; the map is not evidence for a clean or published build.

[OT-0177a in Trail Project #1](https://github.com/orgs/Limited-Underground/projects/1?pane=issue&itemId=270958440)
tracks parent scope, while the
[acceptance checklist](OT-177-SUPPORT-EXPORT-ACCEPTANCE-CHECKLIST.md) records
future cases. Writing this map changed no board disposition, checklist verdict,
V1 credit, or owner acceptance. Current task scope and status must be read from
the Project before new work.

The earlier stable-window schema run had nine report tests pass in each of
three variants, as dated in the schema evidence. Tests mentioned below were
inspected as source, not executed for this map.
## Actual report construction boundary

[Home screen][home] calls the private `@Composable v1SupportSnapshot(state)`
for the support screen. It constructs `V1SafeDiagnosticSnapshot` from five
labels, two pure state projectors, a literal null reconnect count and a projected
support code. [Support screen][screen] passes that snapshot and a separately
owned `problemNote` to [V1SupportReportGenerator][report]. The generator prints
only its explicit fields; it does not serialize the entire UI state or session.

`V1PublicDiagnosticLabel.create` permits 1-80 ASCII characters from its allowlist
and rejects outer whitespace, UUID/hardware-address/long-hex shapes and the
substrings PIN and KEY. Rejected labels become `Not available` in the home
adapter. These checks enforce selected formats, not the semantic origin of
text. Ordinary short private text, short digit strings, message-like text and
coordinate-like text using allowed characters can satisfy the constructor.
The source therefore does not justify treating it as a general sanitizer or
feeding arbitrary private strings into the five public label fields.

User-note text belongs to `[User-supplied problem description]` and intentionally
may contain user-entered details. The generator bounds and normalizes it and
includes review wording; that is the separate C02 contract. This map excludes
the note and the explanatory exclusion prose when discussing automatic values.

## Twelve automatic fields: producer, transformation and coverage

Each row is an explicit field printed by [the generator][report]. Coverage
references are source assertions, except the separately recorded prior report
suite run. Gaps are not silently promoted to PASS.

| Automatic field | Exact production producer and transformation | Output or fallback | Existing coverage and remaining gap |
|---|---|---|---|
| App | `v1SupportSnapshot`: package manager `getPackageInfo(context.packageName, 0).versionName`, remembered by Compose; nullable value becomes `Not available`, then label validation | `App: <validated version>`; rejected label becomes `Not available` | [Report tests][report-tests] serialize a supplied public label; actual package-manager/Compose producer is not exercised there. |
| Firmware | `v1SupportSnapshot`: literal `Not available`, passed through label validation; no firmware string read from session | `Firmware: Not available` | [Region sentinel test][region-tests] asserts unavailable output; current Compose constant is source-inspected only. |
| Phone | `v1SupportSnapshot`: `Build.MODEL`, then label validation | `Phone: <validated model>` or `Not available` | Report tests use fixed synthetic labels; actual Android model binding and fallback remain unexecuted. This is phone model, not a discovered companion name. |
| Android | `v1SupportSnapshot`: `Build.VERSION.RELEASE`, then label validation | `Android: <validated release>` or `Not available` | Report tests use a fixed synthetic release; Android/Compose metadata binding remains unexecuted. |
| Device | `v1SupportSnapshot`: literal `Not available`, passed through label validation; no companion identifier, name or model producer | `Device: Not available` | Region sentinel test asserts unavailable output; production constant binding remains source-only. |
| Radio region | [Region projector][region] reads only `BluetoothDevice.runtimeState` as `Ready`, then `session.configuration`: available, regionAvailable, positive regionRevision and catalog selection ID 1 with matching US915 code | `Radio region: US915`; otherwise `Not available` | Region tests cover availability/revisions/catalog and non-Ready invalidation, plus several ignored-field sentinels. A revision gates authority but is never printed. This is catalog projection, not arbitrary region text. |
| Phone connection | [Health projector][health] maps UI/runtime variants to `V1SupportPhoneConnection`; `Ready` and `FactoryResetRequesting` with session snapshot both produce READY | `Phone connection: <enum>`; chooser DISCONNECTED, local test UNKNOWN | [Health tests][health-tests] cover representative states; region tests explicitly assert FactoryResetRequesting health READY while its region is unavailable. No endpoint or diagnostic text is appended. |
| Radio | Health projector maps `CompanionStatusSnapshot.radio` in Ready/FactoryResetRequesting to a bounded support enum | `Radio: <enum>`; other states UNKNOWN | Health tests enumerate all authoritative radio values; report tests verify typed serialization. No radio payload or raw session is serialized. |
| GPS | Health projector maps `CompanionStatusSnapshot.gnss`, not `session.groupLocation` or a coordinate | `GPS: <enum>`; other states UNKNOWN | Health tests enumerate GNSS health; populated-coordinate invariance through projector-to-report composition is missing. Health status is distinct from position data. |
| Power | Health projector maps `CompanionStatusSnapshot.power` to its bounded support enum, without deriving charge or numeric battery values | `Power: <enum>`; other states UNKNOWN | Health tests enumerate authoritative power values; no raw telemetry is accepted by this field. |
| Reconnect count | `v1SupportSnapshot` passes literal null; generator accepts only null or nonnegative Int if another caller supplies one | `Reconnect count: Not available` in current home path | Report tests cover supplied 3, null and rejection of a negative counter. No production counter is read; `Reconnecting.attempt/maximumAttempts` are not copied. |
| Last support code | Health projector maps only `BleRuntimeState.Failed.reason` to a bounded `V1SupportCode`; session states return null and diagnostic detail is ignored | `Last support code: <enum>` or `Not available` | Health tests cover authorization/internal/reconnect code classes; report tests cover null. Full runtime reason strings, platform diagnostics and codes not selected by this mapping are not printed. |

Health state comes from [TrailAppUiState][controller] and
[BleActiveSession / BleRuntimeState][runtime]. The protocol
[CompanionStatusSnapshot][semantics] exposes typed health plus revision/count
fields; only the three health enums above are used by the health projector.
FactoryResetRequesting health and Ready-only region deliberately have different
source conditions in the current code; this map preserves that distinction.

## Eight excluded-data categories and candidate input paths

These rows describe the reviewed route into automatic fields, not a global
security or storage audit. A missing raw-data slot is not an executed sentinel
result. Names that resemble identities are not assumed to be actual pairing IDs
or keys. No real sensitive values were collected.

| Excluded category | Candidate input and route inspected | Current source boundary and present coverage | Remaining evidence gap |
|---|---|---|---|
| PIN | Group UI `V1GroupUiAction.Join(pin)` in [group screens][group-screen]; system pairing owner documented in [bond coordinator][bond] | Home support construction does not read group UI actions or pairing input; health/region projectors do not read authorization/group-confirmation contents. Label tests reject PIN/KEY-shaped text, not arbitrary numeric PIN semantics. | No complete PIN-origin injection path into the private Compose producer is exercised. System pairing documentation says the coordinator never receives a passkey; that is source contract evidence only. |
| Key | Platform bond/security material and opaque group authority; reviewed [session][runtime] and [authorization state][authorization] have no raw bond-key field selected by support construction | No key-bearing input is selected by the twelve-field adapter. Report label tests reject selected KEY/long-hex formats. | Raw platform/group-key production and storage are outside this bounded map; no end-to-end key sentinel proof or claim that every key shape is rejected. |
| Pairing ID | Candidate identity/correlation carriers: discovered companion endpoint token, session nonce, authorization claim token; actual pairing ID is not established by those names | Neither projector forwards these carriers. Existing region test excludes endpoint/session and configuration correlation sentinels. | Opaque endpoint/session tokens are not proof that actual pairing identifiers were injected. Complete pairing-ID origin coverage remains unknown. |
| Hardware address | [Discovered companion][runtime] carries opaque endpointToken/advertisedLabel, not an automatic report field; Android address authority is outside this adapter | Existing label test rejects selected separated/compact address shapes; region test carries endpoint/name sentinels that are not copied. | An endpoint sentinel is not an actual platform-address producer test. Android address-to-endpoint path is not executed here. |
| Invitation secret | Group invitation UI/actions and `V1GroupConfirmationState.Review(offer)` in [group confirmation][group-confirmation]; offer includes confirmation and identity metadata, not a declared raw invitation-secret field | Home support adapter ignores groupConfirmation and group screen state. Current region sentinel fixture does not populate a confirmation offer. | Offer metadata is not interchangeable with a real invitation secret. Raw invitation-secret producer is not established in this map; a populated ignored-offer fixture can only test its own seam. |
| Group secret | Group authority, membership token and confirmation metadata in [group profile][group-profile] / group confirmation; no raw group-secret field in the selected support snapshot | Group profile/confirmation values are not read by the home support constructor or its projectors. Redacted `toString` methods are additional guards, not the report's selection mechanism. | No secret-bearing group storage/crypto path was reviewed end to end. A token/fingerprint fixture must not be described as injecting the raw group secret. |
| Message body | Local quick-message strings/templates in [home messages UI][home] and [quick messages][messages]; no message body property in the selected safe snapshot | Support construction does not read local template preferences or message drafts. Report test excludes forbidden field headings but does not inject a real message-origin fixture. | Local Compose/preferences input path is unexecuted. User-note text may legitimately contain message-like text and belongs to C02, not automatic-field absence claims. |
| Coordinates | `BleActiveSession.groupLocation` contains [GroupLocationCoordinate][location] through peer position data, alongside the typed health snapshot | Health reads GNSS enum only; region reads configuration only; home adapter does not copy groupLocation. Existing region test uses authoritativeUnavailable location data. | No populated-coordinate sentinel currently reaches the complete pure projection/report fixture; that is the smallest useful next test seam. |

## Existing tests versus evidence still needed

- [V1SupportReportTest][report-tests]: nine methods cover deterministic text and
  metadata, exact twelve-field schema, missing values, selected label rejection,
  negative count, note bounds and redacted string forms. The method named
  `automaticSchemaHasNoPrivateIdentifiersMessagesInvitationsOrCoordinates`
  checks absent headings; it is not an injection test for all eight categories.
- [V1SupportHealthProjectionTest][health-tests]: pure state/enum projection,
  selected failure code classes and nonphysical modes; no Compose metadata
  producer, populated coordinate export or raw secret-origin coverage.
- [V1SupportRegionProjectionTest][region-tests]: verified region rules and
  `regionBindingCannotAddSessionSecretsToTheAutomaticReport`. The latter
  injects device/public/suggested names, notice, endpoint/advertised label,
  session/configuration IDs and revisions into a manually assembled snapshot
  path. Its `groupLocation` fixture is unavailable. Its broad method name does
  not establish all eight semantic excluded-data categories.

## One next executable sentinel seam (proposal only)

Propose a test-only increment at the existing pure
`V1SupportHealthProjector.from(state)` + `V1SupportRegionProjector.from(state)`
+ `V1SupportReportGenerator.create(snapshot, emptyNote)` composition. Use two
Ready-state fixtures with identical authoritative health/verified region values
and fixed public labels. Populate and vary ignored companion identity/name,
configuration notice/correlation fields, a valid synthetic group-location
coordinate/peer fixture, and a synthetic group-confirmation offer. Compare the
projected public values and complete automatic sections. Require positive
US915 and expected health controls, then assert absence of each actually injected
sentinel (including distinct E7 and degree-rendered coordinate forms) and record
output digest/size. Choose unique synthetic markers that cannot accidentally
match fixed public labels or counters.

The coordinate-containing fixture adds coverage that the current unavailable
location fixture cannot provide. Record only the categories truly represented
by injected values; do not relabel an endpoint, fingerprint or confirmation as
a raw key, pairing ID, invitation secret or group secret. Reuse existing test
helpers where possible; source fixes require a separate plan if a test fails.
No test is written or run by this document.

This seam can exercise pure selection under populated ignored state without
changing product code. It still manually assembles the safe snapshot and cannot
call the private `@Composable v1SupportSnapshot` as an ordinary pure host helper.
Package-manager, Build metadata, Compose integration, local note/template state,
platform pairing/keys and actual Save/Share remain outside its proof. Whether
to expose a testable adapter or use a supported Compose harness is a separate
owner-scoped decision. This map grants neither implementation nor test authority.

## Dated documentation validation and limits

On October 19, the repository documentation checker and all 18 documentation
tests passed for this source map. Review counted twelve automatic-field rows
and eight excluded-category rows; local links resolved. The prior 170-file
preservation comparison passed for that historical source snapshot. These
results do not validate today's source or execute an Android privacy test.
Exact local scratch paths, branch state, hashes, and run receipts are retained
in private recovery records.

The map changes no C01-C12 checklist verdict, OT-0172a/OT-0319 dependency,
public capability, or V1 completion. No product code, Android test, device,
export, package, traffic, PR, or board operation was part of this map. Full C01
privacy proof, owner disposition, physical, release, and parent acceptance
remain separate open gates. Recheck the current Project item before new work.
[home]: ../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/V1HomeScreen.kt
[screen]: ../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/V1SupportScreen.kt
[report]: ../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/V1SupportReport.kt
[health]: ../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/V1SupportHealthProjection.kt
[region]: ../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/V1SupportRegionProjection.kt
[controller]: ../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/TrailAppController.kt
[runtime]: ../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/BleCompanionRuntime.kt
[authorization]: ../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/DeviceAuthorizationClaim.kt
[group-screen]: ../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/V1GroupScreens.kt
[group-confirmation]: ../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/V1GroupConfirmation.kt
[group-profile]: ../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/V1GroupProfile.kt
[location]: ../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/GroupLocationPresentation.kt
[messages]: ../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/V1QuickMessages.kt
[bond]: ../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/AndroidSystemBondCoordinator.kt
[semantics]: ../../android/protocol/src/main/kotlin/io/github/nbjelanovic/otprotocol/CompanionSemantics.kt
[report-tests]: ../../android/app/src/test/kotlin/io/github/nbjelanovic/otclient/V1SupportReportTest.kt
[health-tests]: ../../android/app/src/test/kotlin/io/github/nbjelanovic/otclient/V1SupportHealthProjectionTest.kt
[region-tests]: ../../android/app/src/test/kotlin/io/github/nbjelanovic/otclient/V1SupportRegionProjectionTest.kt
