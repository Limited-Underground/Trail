# OT-0302 Protected public-profile settings: host evidence

Recorded 2026-10-01 for approved revision 2. **Host validated; owner review
pending.** Final combined Android validation, six fresh affected firmware builds
and all reproducibility audits passed. Physical UI acceptance remains open.

The Android configuration screen can read and save a chosen public name and
visibility together through the current protected device session. The firmware
owner returns success only after exact durable readback. A possibly applied
write with a lost or uncertain result requires an explicit read; it is never
automatically repeated. Existing private-name and twelve-region behavior is
preserved. Saved visibility is a preference, not a public-presence publisher.

## Contract and authority

- Strict negotiated configuration profile **5**, capabilities `0xff`, request
  **8**, response **0x8a**, atomic `OTPC/v1` name/visibility/revision record.
- Public names: at most **40 UTF-16 units / 96 UTF-8 bytes**, well-formed UTF-8,
  bounded controls/spacing and no normalization. Private names retain 32 units.
  Android additionally requires `V1PublicName` / `StablePeerDisplayAlias` for
  writes and named readbacks. A generic wire-valid but invalid display alias
  never becomes authoritative; future publishing must validate aliases again.
- Absent storage is explicitly revision 0, empty name, visibility ON. Damaged,
  unknown-schema or unreadable storage is not an absent/default profile. A write
  requires a nonempty name even when hidden, and the expected revision.
- Current protected Ready, exact device/runtime/owner/transport/controller
  generation, session nonce and exchange bind admission and response. Firmware
  rechecks authority/lifetime before load, before commit and after readback.
  APPLIED must match the exact name, visibility and expected revision + 1.
- Disconnect/revoke/reset/expiry retires queued work and stale callbacks/results.
  Ambiguous commits or mismatched readback enter reconciliation; Android clears
  all public values/revision and requires READ before another write. No hidden
  retry, stale UI scope or restored draft can confer current-session authority.
- Factory reset includes `ot_public_v1` in the existing durable all-domain cleanup
  inventory and verifies absence. Erase/recovery uncertainty stays contained;
  the candidate evaluation target's prior commit-and-stop reset contract remains.
- Profiles **2, 3 and evaluation 127** retain their strict operation boundaries
  and refuse the new public-settings operation. Protocol compatibility, saved
  region and visibility grant no radio transmission or public-discovery authority.

Normative repository sources: `docs/platform/COMPANION_PUBLIC_PROFILE_V1.md`,
`docs/product/V1_USER_EXPERIENCE.md` and
`tests/fixtures/companion_public_profile_v1_vectors.csv`.

## Prior OT-0302 checkpoint and combined gate

The 1,285-test run and APK pins below are the saved **OT-0302-only checkpoint**.
They are not final combined-source evidence after OT-0301 changes. Retain those
receipts unchanged; replace final placeholders only from the combined gate.
Counts describe test executions or suite jobs, not physical results.

| Gate | Observed result |
| --- | --- |
| Prior Android full affected unit matrix | **1,285 passed**: protocol 58; Debug 391; Release 390; V1Test 446. Zero failures, errors or skips. Final combined: **1,303 passed**, zero failures/errors/skips; protocol/test: 58; app/testDebugUnitTest: 397; app/testReleaseUnitTest: 396; app/testV1TestUnitTest: 452. |
| Prior Android lint | Debug, Release and V1Test passed. Final combined lint: all three variants passed. |
| Prior Android artifacts | Debug, V1Test, unsigned Release and Debug instrumentation APKs built; instrumentation was not executed. Final four APK pins: [combined onboarding evidence](OT-0301-SETUP-COMPLETION-2026-10-01.md). |
| Prior unsigned release audit | PASS; unsigned, 8,771,268 bytes; test-only diagnostics excluded. Final combined unsigned-release audit: PASS. |
| Native/Python affected gate | **15 native + 6 Python suite jobs passed** across final-native-2 and supplemental receipts; native source compiles and real composition tests are included. |
| Public profile / storage / reset | Shared 21 vectors plus codec/actual NVS/owner faults; stale context/deadline, uncertain commit/readback, explicit reconciliation and reset namespace residue/failure/reconstruction covered. |
| Target source admission | 19 groups passed; exact new sources/linkage, standard profile 5 and evaluation-127 restrictions checked. |
| Raw input and content checks | Raw-byte policy 291 passed; tracked/untracked publication content scan passed. These checks do not authorize publication. |
| Independent reviews | Prior OT-0302 Android review: zero blockers at 18 frozen files; final combined Android review: zero unresolved blockers. Firmware final signoff received by root at 05:34: zero blockers, target admission and raw-byte preservation reviewed. |

The BLE stack Python suite ran four checks; three optional retained-build
checks were skipped because that legacy audit was not requested. The fresh
three-profile build audit verifies current generated configurations; skipped
cases are not counted as executed tests.

The shared **21 synthetic vectors** exercise decoding and exact re-encoding in
both languages. They cover absent/default ON, initial/write/snapshot/APPLIED
visibility, name limits, stale/exhausted revision and malformed fields/UTF-8.
Android tests distinguish generic wire validity from strict alias validity,
exercise actual protected-runtime profile-5/0x8a routing, uncertain writes,
timeout without retry, old indications and stale endpoint/nonce/editor scopes.

Prior OT-0302 checkpoint APK pins (retain; do not relabel as combined artifacts):

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Debug APK | 12679646 | 6c7028b24656603c193ff84911e03b5d41ca9fea1798ce5996032ab7a8d903a9 |
| V1Test APK | 11991002 | 7b51568ecf56c5ea1315c5bcac848ce1d7b75b153a449d2b28da3f30088f68cf |
| Unsigned Release APK | 8771268 | 713b13a385c558f3d0c9d2190b61708cfb0a005c477da0a0382b6c79503d062f |
| Debug instrumentation APK | 16042 | a7eb1a3dd576373d084c62ad00d99256853e12554a66d8c4f4913cbbe322dcf8 |

Debug/V1Test are local debug-signed build artifacts. Release remains unsigned;
no release signing, distribution or installation is authorized by these builds.
Final combined artifact hashes and audit are recorded in [OT-0301 evidence](OT-0301-SETUP-COMPLETION-2026-10-01.md).

## Mandatory target-porting preflight

Applied `docs/firmware-porting-lessons.md`; the source-based preflight does not
establish a received-unit identity, live boot or physical power-cut result.

| Requirement | Applicable disposition / reason |
| --- | --- |
| Exact target and linkage | Existing Heltec V4 ESP32-S3, QIO 80 MHz/16 MB + PSRAM profile, partitions/offsets/pins/peripherals unchanged. Bench links public codec/owner/storage; candidate shares reset inventory only. Actual final build source/config admission: passed. |
| Byte/build reproducibility | Existing unchanged raw-line terminators preserved; changed/new lines LF, no blanket normalization. SDK v6.0.2/tool/dependency/source pins retained in the r2 freeze; newly consumed compiler dependencies must match the saved wide input snapshot. Six fresh builds and nine raw artifact pairs per profile: passed. |
| Boot/USB/reset transport | No transport or physical-reset change. Host NVS reconstruction tested; live boot/serial/re-enumeration/power interruption skipped because no device execution is authorized. |
| Concurrency/ordering | Storage runs on the serialized application owner, not GATT callbacks. Current lane/session/context and admission lifetime are rechecked; no new task/stack ownership is claimed. |
| Persistence/cleanup | Atomic profile, exact key/schema/size inventory and fresh-handle readback; uncertainty contains writes. Same reset executor retires/verifies the new namespace before unowned publication. |
| Composed target validation | Actual codec/dispatcher/owner/storage/reset host paths and static source/security admission passed. Fresh target ELF/map/config/consumed-input audit passed. |
| Physical gate | Flash/readback, display, pairing/Ready, live interruption/recovery and actual power cuts skipped: outside host-only authorization. No hardware or RF claim. |

## Firmware build gate — passed

Use only the fresh **r2** destinations; initial failed compiler-probe attempts
remain retained evidence and are not successful builds. No prior output is reused.

| Configuration | Fresh builds A/B | Nine raw artifact pairs | Final app BIN bytes / SHA-256 |
| --- | --- | --- | --- |
| Ordinary bench, standard profile 5 | Passed | All 9 equal | 602096 / `33c27be5dea55e38276eafe96f278504353ca5ab1749bec0e5e95db1668e5193` |
| Bench confirmation evaluation, profile 127 | Passed | All 9 equal | 737840 / `25d967c4afff6973a3551dea7577c1a2413d4e04302b4bf518168c6b8ed4dd56` |
| Enrollment candidate, shared reset source | Passed | All 9 equal | 637808 / `ee58e250b63b4ded87a688bc88223dc9e450ec827df5b47980219c56acd42f42` |

Each pair covers application BIN/ELF/map, bootloader, partition table, initial
OTA data and sdkconfig/generated JSON/header. Actual compiler dependency
closures match the frozen repository and installed SDK inputs. Required affected
units compiled, unchanged configurations/partitions were verified, reproducible
metadata is exact and there are no compiler warnings.

## Evidence custody and remaining acceptance

Private receipt identifiers below are repository-relative references, **not
public download links**. Exact SDK/source/input pins and commands stay in those
receipts; this report contains no executable grant or private device values.

- `.private/overnight-20261001/android-final-result.json` and `android-final-run-1.json`.
- `.private/overnight-20261001/android-owned-source-freeze.json`, `android-focused-final.json`, `android-independent-review.md` are the prior OT-0302 checkpoint; final combined source/review/validation receipt identifiers: `.private\overnight-20261001\android-combined-source-freeze.json`, `.private/overnight-20261001/android-combined-final-result.json`, `android-final-run-2.json`, `android-release-audit-2.log` and `final-review-admissions.json`.
- `.private/overnight-20261001/final-native-2-result.json`, `supplemental-native-result.json`, `public-profile-target-admission-result.json`.
- `.private/overnight-20261001/firmware-OT0302-result.json` and `firmware-OT0302-preflight.md`; initial `OT-0302-flow-review.md` retained separately from root's final firmware signoff.
- `.private/overnight-20261001/firmware-builds-r2/build-input-freeze.json`, `build-plan.json`, `preparation.json`; final `build-result.json` and all six per-build audits passed.

This records computer-only settings support; OT-0301 separately owns the final
ordered setup-to-Messages host flow. Both tasks are **host validated; owner review pending**.
Their registered host scopes have no mutual owner-acceptance prerequisite. Physical UI acceptance
remains separately open. Neither task implements public presence/discovery or
enables RF. No installation, release signing/distribution, deployment, V1 completion credit or website capability change is claimed.
Source publication is separately authorized; it does not establish product readiness.
