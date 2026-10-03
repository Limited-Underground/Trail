# OT-0304 Landscape status strip

Owner-requested Android layout correction, 2026-10-01. The approved OT-0304
rotation task was checked and started before editing. Canonical project is
`C:/lu/OpenTrail`; selected checkout is
`C:/lu/OpenTrail/.private/ot177-publication`.

## Result and boundary

The landscape status row now has a 28dp minimum height and 2dp vertical padding,
instead of 48dp and 8dp. Status text stays on one line. Fonts and icons retain
their sizes; minimum-only sizing allows enlarged text to grow naturally.
Horizontal display-cutout protection and truthful status semantics are preserved.
Portrait sizing and text wrapping remain unchanged.

The test-only Connection log action shares the existing Device heading row in
landscape, removing its separate row beneath the status strip. Portrait keeps
its existing top action. The log callback and recording behavior are unchanged.
Only `V1StatusStrip.kt`, `MainActivity.kt` and `V1HomeScreen.kt` changed.
No controller, service, firmware, protocol or stored-settings behavior changed.

Independent read-only source review found no blocking defect. OBSERVED on the
owner-authorized Note20 (Samsung SM-N986U, Android 13): both landscape directions
show a compact, readable status row at normal text and scale 1.3. Both Device
heading actions fit. Normal/enlarged portrait retain their existing layout.
Independent review of six candidate captures found no clipping or overlap.
Normal strip height is visually approximately 74 pixels versus the earlier
126 pixels, consistent with the 28dp/48dp source dimensions. Larger text grows
naturally. All observed clocks use 24-hour format; 12-hour formatting and the
S24 were not tested in this check. Log navigation was not exercised; its callback
is unchanged and the visible actions are enabled in the hierarchy.
This correction does not complete physical first-use or production acceptance.
Earlier accepted Android evidence remains bound to its earlier APK.

## Validation and continuation

VERIFIED: the final offline gate passed all three app builds and lint checks
with zero lint issues. Existing unit tests passed: Debug 432, Release 431 and
V1-Test 487, with no failures, errors or skipped tests. The changed common
Compose code compiled in all variants; no physical rendering is inferred.
The exact command, full Android input hashes and output identities are in
`android-checks.json` and `validation-summary.json` in the private folder below.

V1-Test candidate: `build/android-ot0304-landscape/app/outputs/apk/v1Test/app-v1Test.apk`,
12,007,386 bytes, SHA-256
`f992da1f187a533c9cae3c1e5c368fda9fb4b140861b3186449164288afb80f5`.
Existing dependencies and the pinned JDK17/Gradle8.11.1/AGP8.7.3/Kotlin2.0.21/
SDK35 toolchain are reused; outputs use a new build directory to preserve the
previous accepted APK. No new tests or dependencies are introduced for this
reversible layout correction.

The owner explicitly authorized this app-only Note20 update. Fresh installed
original bytes matched the retained earlier APK, and both signing certificates
verified and matched. Replacement used only `adb install -r`; independent
installed-byte readback matched the exact candidate. No uninstall, app clear,
new pairing or bond operation occurred. The new APK remains installed.
An APK recovery reference is not a snapshot of private app history.

The initial installed-app capture was portrait after rotation settings reverted;
only the subsequent settled landscape capture counts as landscape evidence.
The original comparison was plain MainActivity and the candidate was the ordinary
V1-Test launcher, so different connection/page states are not a lifecycle comparison.

Display cleanup first read automatic rotation 0 instead of its original 1 after
all restoration writes. The failed receipt is preserved; its unique cause is
unknown. Font scale 1.0 and user rotation 0 already matched. One exact restoration
continuation restored automatic rotation 1; settled readback and the independent
final check verified all three original fields and unchanged stay-awake 2.

The app returned through its saved-device service route without a PIN or new
pairing. Current diagnostic session 39 reached fresh protected ProtocolInfo,
authorization, snapshot acceptance and Ready; the final UI says Connected to
Trail Bench. This is a returning-owner observation, not fresh first-use acceptance.
No Heltec serial/reset/firmware, case/battery or radio-send operation occurred.

Next: prepare automated physical-settings observation before another separately
authorized firmware session. Full OT-0304, warm-board and production gates remain
incomplete. The requested landscape correction is complete for this Note20 check.

Changes are local and uncommitted. The separately approved Note20 installation
and layout check occurred; no Git network/publication or website deployment occurred. Accepted evidence did
not change public website status or V1 completion.

Private command/input/artifact evidence:
`C:/lu/OpenTrail/.private/ot177-publication/.private/ot0304-landscape-strip-20261001/`.
