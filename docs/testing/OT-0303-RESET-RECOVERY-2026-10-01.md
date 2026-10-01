# OT-0303 Reset-aware saved-device recovery: host evidence

Recorded 2026-10-01, approved revision 2. **Owner-accepted host scope.**
The owner corrected dependencies to accepted OT-0302 and OT-0168b before start.

The verified-reset screen now distinguishes an unconfirmed Android system pairing
from a cleared local outcome, gives bounded manual Settings guidance, and offers
an explicit fresh-setup scan. The button samples current controller state and
requires the exact captured completion instance before dispatching. A delayed
callback cannot reuse an equal-looking replacement state as authority.

## Actual change and reused owners

Five owned paths beneath `android/app/src/`, all in the existing
`io/github/nbjelanovic/otclient` package:

| Path | Result |
| --- | --- |
| `main/kotlin/.../FactoryResetRecoveryPresentation.kt` (new) | Outcome-specific guidance and live-instance fresh-setup dispatch fence. |
| `main/kotlin/.../MainActivity.kt` | Bind that presentation to actual completion state, Bluetooth Settings and the explicit scan action. |
| `test/kotlin/.../FactoryResetRecoveryPresentationTest.kt` (new) | Cleared versus unconfirmed pairing guidance; stale/replaced/null completion refuses dispatch. |
| `test/kotlin/.../AndroidFactoryResetReceiptStoreTest.kt` | Distinguish commit failure after the fixture memory map changes; reconstructed store stays conservative. |
| `test/kotlin/.../BleCompanionRuntimeTest.kt` | Real receipt-store/runtime composition with wrong receipt, foreign store, stale scan/GATT and failed local completion. |

Production facade, runtime and receipt persistence remain unchanged. Their
existing receipt bounds, current-generation ownership and protected returning
authorization are reused. No peer-identity database, address linkage, new wire
profile, firmware change or automatic system-bond deletion is introduced.

## Complete flow and refusals

| Owner / transition | Required behavior and evidence boundary |
| --- | --- |
| Current protected owner requests reset | Existing exact request/nonce/receipt checks apply. `ADMITTED` starts erasure; it does not prove completion. Unknown outcomes retry observation, not the reset command. |
| Verification receives an advertisement | Require the exact bounded receipt and current scan ownership. Wrong/expired/corrupt receipt cannot complete local cleanup or affect another receipt store. |
| Firmware completion is correlated | Receipt is correlation only, never peer identity, pairing or Ready authority. Existing target cleanup/readback evidence remains [OT-0168b](OT-0168b-RESET-COMPOSITION-2026-09-30.md), with simulated SDK I/O. |
| Local completion write fails | Actual store returns failure; runtime stays NotVerified. Memory-first failed-commit models retain conservative fresh-setup state; no cleanup success or automatic returning connection is inferred. |
| System pairing is unconfirmed | Offer Bluetooth Settings. Forget only the entry the user already knows belongs to this device; leave other entries alone and remove nothing if uncertain. Trail cannot prove Android removed it. |
| User chooses fresh setup | Sample live controller state; require referential identity with the captured verified completion. Dispatch a scan only, without connection, pairing, reset resend or Ready receipt. |
| Fresh scan and pairing | Existing exact-device/PIN/protected authorization remain necessary. The pairing window keeps running during Settings; an expired PIN requires a later manual boot of the reset device. No window extension is claimed. |
| Ordinary returning owner | Existing unambiguous bonded candidate and fresh protected application authorization are required. The returning path never creates a bond or derives Ready from a bond alone. |
| Restart / late callback | Existing pending-receipt and fresh-setup markers govern recovery; old callbacks cannot revive the completed owner. Object reconstruction is host model evidence only. |

Normative boundary: [Device factory reset V1](../platform/DEVICE_FACTORY_RESET_V1.md).
The public-profile prerequisite remains the separately accepted
[OT-0302 host support](OT-0302-PUBLIC-PROFILE-SETTINGS-2026-10-01.md).

## Validation and artifact gate

| Gate | Observed / pending result |
| --- | --- |
| Source freeze | 29 files; five owned paths independently match the released freeze. `android-source-freeze-final.json`, SHA-256 `37865941d9d05b04975802fe9885634e90220236c58bfaa023808842f70e7db0`. |
| Focused checkpoint | 101 executions across six XML suites; zero failures/errors/skips; Debug/Release Kotlin compilation passed. `android-focused-final.json`. |
| Independent frozen flow review | Passed, zero blockers; 29 source pins matched. `flow-review-final.md`, SHA-256 `ff09f3e1e78070266be23aed57ea4c5c4edf705b847260924737a27b406cb061`. |
| Complete affected unit matrix | 1,318 passing results (1,260 app tests executed in the first full matrix; 58 unchanged protocol results reused); protocol/test: 58; app/testDebugUnitTest: 402; app/testReleaseUnitTest: 401; app/testV1TestUnitTest: 457; zero failures/errors/skips. |
| Three lint variants | All three passed, zero issues. |
| Four APK builds and exact pins | Four artifacts pass assembly/audit; unchanged task outputs may be reused; pins below. |
| Unsigned-release audit / source policy | Passed; release unsigned and maintained hidden/reflective bond-removal checks pass. |
| Documentation / publication-content checks | Owning checker, 18 regression tests and final diff check are recorded in docs-validation.json at closeout. |

| Final artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Debug APK | 13284821 | `49a53bd0b44bbe8bda4df1ef2b5101084ee8614007e13ee81089af6965a8cf9d` |
| V1Test APK | 12656607 | `a1c04187aeea4a762ec0c9e752ac5c9378a95997a1f447beaa1ae09257d8d722` |
| Unsigned Release APK | 8771268 | `eaf2b3382a26018688efb8740df7d5fe102a5adb59aaa758b9d530ddd4acd78d` |
| Debug instrumentation APK | 16042 | `a7eb1a3dd576373d084c62ad00d99256853e12554a66d8c4f4913cbbe322dcf8` |

Debug/V1Test use local debug signing. Release remains unsigned; no release
signing/distribution or installation is claimed. Building an instrumentation APK
does not execute instrumentation tests. Firmware is unchanged; this increment
does not require a new firmware build or change the accepted reset contract.

## Model limits and remaining acceptance

Fault tests execute actual receipt-store/runtime logic over injected fixture maps.
They model a write that updates memory and then reports failed commit. Recreating
store/runtime objects over those maps proves the tested control-flow response;
it does not demonstrate Android process death, real SharedPreferences disk I/O,
disk durability, an OS bond inventory change or a physical reset. No production
persistence correction is claimed by these tests.

Real-phone process/service recovery and disk interruption, Settings behavior,
known-peer isolation with two actual pairs, visible layout/rotation/large fonts,
old-owner rejection, actual prior-user-data absence and the live fresh pairing
window retain their separate physical/Android gates. This host work does not
complete OT-0168a/c or production-launch acceptance, grant RF, or add V1 credit.

Private receipt identifiers, not public download links, live under
`.private/ot0303-recovery-20261001/`: approved check/start, `flow-review-before.md`,
released source freeze, focused final receipt and frozen final review. Final Android result, command/environment, XML/lint/APK pins and audits are
recorded in android-combined-final-result-2.json, android-final-run-2.json and
android-release-audit-run-2.json. Original full-matrix receipts are preserved. UP-TO-DATE/cache reuse and XML provenance are
explicit; unchanged reused tests are not claimed as newly executed. The first
focused attempt failed on a missing test import; the corrected focused rerun
and both complete matrix receipts are retained. A final diff check caught three
CRLF terminators on changed test lines. Only those three CR bytes were removed;
independent byte reconstruction proves identical normalized source. The second
cached matrix binds the final raw freeze; all four APK pins equal the first run.
The byte proof and review addendum are line-ending-repair.json and
flow-review-final-addendum.md. Changes remain local/uncommitted;
publication requires the next scoped PR operation. No public website capability
change or V1 credit. The owner accepted this host result at checklist version 480,
revision 2, on the [OT-0303 task](https://limitedunderground.com/lab/tasks/98893ef6-147e-440d-9079-93921404be76).
Saved proof: `.private/after-ot0303-acceptance-20261001/OT-0303-check.json`.
Physical acceptance and Git publication remain separate.
Earlier attempts and the prior OT-0301/OT-0302 accepted evidence remain preserved.
