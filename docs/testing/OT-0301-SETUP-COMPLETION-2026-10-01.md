# OT-0301 Complete ordered resumable onboarding: host evidence

Recorded 2026-10-01, approved revision 1. **Host validated; owner review
pending.** Final source freeze, independent review and combined validation
passed. Physical UI acceptance remains separately open.

The prior `docs/testing/OT-0301-ONBOARDING-2026-09-23.md` report is preserved as
the name/region checkpoint. OT-0302 supplies the protected public-name/visibility
boundary. This addition finishes the ordered host flow without treating saved
phone drafts or a cached screen position as device authority.

## Completed host behavior

- Follow exact-device match, secure pair/authorize, private device name, durable
  valid region readback, public name/visibility, then Messages in that order.
- Derive progress from the current protected Ready session: verified private
  name, one of the twelve explicit region selections and strict-valid public
  profile with authoritative name/visibility/revision. Public visibility OFF is
  a valid chosen preference, not an incomplete profile.
- An absent public record is unconfigured. Unknown write, bad alias, missing
  readback, stale session, disconnect or reset cannot mark setup complete.
  Interrupted setup resumes the earliest incomplete step using fresh readbacks.
- Consume the completion-to-Messages transition once for the exact current
  endpoint/nonce/configuration-owner scope. Recomposition, late callbacks or
  rotation cannot repeatedly redirect; subsequent explicit Messages/Group/Device
  navigation and drafts remain the user's choice. Existing reset controls stay
  on their established Device screen.
- Local drafts, phone locale and region selection confer neither configuration
  proof nor radio transmission permission. Public preferences do not create
  discovery traffic, group membership, message delivery or an RF producer.

## Final acceptance evidence to fill

| Gate | Final combined status |
| --- | --- |
| Source freeze / bounded independent diff review | Passed; 23 frozen files, zero unresolved independent-review blockers |
| Focused full-order/interruption tests | Passed; actual projection/binding and late live-state/navigation regression cases included in the final matrix |
| Complete affected Android unit groups | **1,303 passed**; protocol/test: 58; app/testDebugUnitTest: 397; app/testReleaseUnitTest: 396; app/testV1TestUnitTest: 452; all failures/errors/skips must be zero |
| Debug/Release/V1Test lint | All three variants passed |
| Debug/V1Test/unsigned Release/Debug instrumentation APKs | All four built; exact pins below |
| Unsigned-release audit | PASS; Release remains unsigned and test-only diagnostics are excluded |
| Shared OT-0302 firmware host/build gate | 15 native + 6 Python jobs; all six fresh builds and nine raw artifact pairs per profile passed |
| Documentation roles/links and publication-content checks | Passed; final check receipts retained with the publication audit |

Required discriminating cases: interrupt every step; replace endpoint, nonce and
editor owner; wrong/invalid/uncertain public readback; hidden valid profile;
timeout/lost result and explicit reconciliation; completed current scope versus
stale scope; single Messages transition under repeated snapshots/recomposition;
explicit navigation and saved rotation state retained; disconnected/reset state
resumes actual incomplete step. Tests must exercise actual projection/binding
and navigation ownership rather than marking fixture setup complete manually.

The saved OT-0302-only run passed 1,285 unit executions, three lint variants and
four APK builds with an unsigned-release audit. It is a **prior checkpoint**,
not final proof for the changed onboarding source. Preserve all prior receipts
and the OT-0302 spec/corpus freeze; final combined evidence needs fresh source
pins, counts, reviews and artifact hashes.

Debug/V1Test artifacts use local debug signing. Release remains unsigned; no
release signing/distribution or installation is claimed. An instrumentation
APK build is not instrumentation execution. No device or physical display,
rotation, large-font, pairing, persistence or live recovery acceptance is claimed.

## Final combined APK pins

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| app-debug-androidTest.apk | 16042 | `a7eb1a3dd576373d084c62ad00d99256853e12554a66d8c4f4913cbbe322dcf8` |
| app-debug.apk | 13283217 | `8857578df0849ea8d929f30447cd73244b3167c701a323daf5de15be14cf95ca` |
| app-release-unsigned.apk | 8771268 | `82bae7212ef543629fb26b1a1d851d997f3283cf81ae22887734b2ab0c4fcbb5` |
| app-v1Test.apk | 12655202 | `e71cf81590f10d55b426b95dcd0bb3af5b5dee1774b03ba57b67d93e71df0995` |

## Evidence custody and remaining gates

Repository-relative private receipt identifiers, not public download links:

- `.private/overnight-20261001/OT-0301-pre-start-check.json` and `OT-0301-start.json` record approved host execution after supported correction of obsolete attention.
- `.private/overnight-20261001/android-final-result.json` / `android-final-run-1.json` and `android-owned-source-freeze.json` preserve the OT-0302-only checkpoint.
- Final combined source, focused/full Android, lint/artifact/audit and independent-review receipt identifiers: `.private\overnight-20261001\android-combined-source-freeze.json`, `.private/overnight-20261001/android-combined-final-result.json`, `android-final-run-2.json`, `android-release-audit-2.log` and `final-review-admissions.json`.

Owner review follows completed evidence; no additional accepted task dependency
is invented. Physical first-use, visible portrait/landscape/large-font behavior,
two-phone isolation, installation and production-launch acceptance remain
separately open under their actual task/artifact boundaries. No firmware, wire
contract, V1 score or public website capability change is made by OT-0301.
