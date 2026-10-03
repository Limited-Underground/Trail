# OT-0304b Note20 app-only confirmation

2026-10-01. **Passed the bounded Note20 returning-device checkpoint;
full OT-0304 first-use and production acceptance remain open.**

The owner separately authorized one Note20/Trail Bench V1-Test update,
starting the saved connection, rotation, ordinary reopening and larger text.
App data and pairing were not cleared; original display settings were restored.
No Heltec serial access, reset, firmware installation, case opening, battery
disconnection, other pair operation or radio-send action occurred.

## Exact setup and artifact

Samsung SM-N986U, Android 13, saved Trail Bench returning-owner connection.
The board retains its existing bench firmware and radio configuration; its
electronic identity, exact resident image/profile and region were not newly
measured in this app-only checkpoint. This is not a new hardware-compatibility
or radio-delivery claim.

Package: `io.github.nbjelanovic.otclient.v1test`, locally debug-signed.
Candidate APK: **12,658,887 bytes**, SHA-256
`fcd3e97353cf67fc345e7f81cd541237c60dd5d9d94148e960d9ff042a66b274`.
The matching installed original was 12,656,607 bytes, SHA-256
`a1c04187aeea4a762ec0c9e752ac5c9378a95997a1f447beaa1ae09257d8d722`.
Its existing verified private APK copy was reused, without another duplicate.
Both signatures verified and matched. Replacement used `adb install -r`, with
independent installed-byte SHA-256 verification before observation and at closure.
No uninstall, data clear, bond deletion or permission-bypass flag was used.

The [OT-0304b host correction](OT-0304b-PENDING-START-2026-10-01.md) was
owner-accepted before this separate physical grant. Its final affected matrix
and source freeze are reused evidence, not rerun tests or new device results.

## Observed sequence

Before replacement, the original app showed Connected to Trail Bench. After
replacement, ordinary launcher and saved-device navigation showed an enabled
Start control. Before Start, current diagnostic session 36 contained only
FOREGROUND and the service was not started/foreground. One explicit observed
Start tap made the service started/foreground and began returning-owner discovery.
That current session then observed protected ProtocolInfo acceptance, negotiated
MTU/subscription, authorization acceptance, snapshot acceptance and Ready.
The app displayed Connected to Trail Bench. Historical session records were
not treated as a current connection.

The app acknowledged Display clock synchronized. The owner independently
confirmed that Trail Bench's physical screen showed a time; its actual time
and location are not recorded in this report.

| Case | Observed result |
| --- | --- |
| Initially empty Prepare device setup fields | Unsaved `Rotation check` added only to Device name; Your display name remained empty |
| Normal portrait to landscape | Setup destination, exact draft and current connected indicator retained |
| Android Home and ordinary launcher reopening | Setup destination, exact draft and connected indicator retained |
| Text scale 1.3, portrait and landscape | Exact draft retained; observed text reflows; lower portrait controls reachable by scrolling |
| Service checkpoints after rotation, reopening and large text | Started/foreground service and connected presentation observed in current session 36 |
| Cleanup | Only the exact test draft cleared; both original empty fields independently verified |
| Final state | Candidate installed hash verified; app connected to saved Trail Bench; exact original display settings restored |

Five saved portrait/landscape screenshots were independently inspected. Normal
portrait and enlarged portrait show readable fields; the enlarged portrait
lower screenshot reaches the Save setup draft control. Landscape upper content
and normal landscape lower content were observed. Enlarged landscape shows the
draft and reflowing text, but this checkpoint does not establish every lower
control in every combined layout. A viewport requiring scrolling is not
evidence of a lost draft. No setup draft was saved or applied to the device.

The maintained phone operator was reused unchanged. This page has two fields,
so its historical single-field cleanup assertion was unsuitable. A narrow
guarded helper instead required the exact setup page, two initially empty
package-owned fields and the unique Device name label. Cleanup required the
exact test text, the other field empty, an enabled/clickable/focusable target,
and independent two-field readback. No unrelated owner content was edited.

## Display-restoration exception and custody

The first restoration invocation reported an immediate readback mismatch after
the font restoration step and before the remaining rotation settings completed.
A saved follow-up showed font scale 1.0 restored, while automatic rotation and
user rotation remained at the temporary values 0 and 1. The mismatched initial
readback value was not captured; its unique cause is unknown.

One idempotent continuation of the same restoration helper completed successfully.
Final independent readback verified font scale **1.0**, automatic rotation **1**,
user rotation **0**, and unchanged stay-awake **2**. This operator restoration
exception is retained rather than described as a firmware or product failure.

The candidate passed this bounded checkpoint and remains installed as authorized;
matching-original APK restoration was not needed. An APK copy is not a snapshot
or rollback of private app data. The usable saved-device connection and absence
of clearing operations do not imply a comprehensive private-history audit.
The operator stopped after final verification; no further device action is
needed to review these receipts.

## Remaining gates and next action

This result confirms the new artifact's tested Note20 startup/rotation/reopening
behavior. It does not uniquely attribute the earlier physical startup failure
or prove uninterrupted protected BLE throughout the checkpoint: current UI,
service snapshots and bounded typed logs are observations at specific stages.
The S24 has not been tested with this new artifact.

Public name/visibility readback remains unavailable in this resident setup;
the app continues to state that its phone-only choices do not complete device
setup. Full first-use was not exercised on a fresh device. Exact resident
firmware/profile, fresh-state preservation custody, warm board restart and
production Release/install/distribution gates remain open. OT-0304 remains
blocked with partial physical evidence; OT-0304b retains its accepted host scope.

Next: prepare OT-0304's exact public-profile-capable standard firmware and
complete preservation/recovery plan before a separately authorized full
first-use and warm-restart checkpoint. Do not treat the existing owned pair
or phone-only draft as fresh setup acceptance.

No V1 progress credit or public website capability change. Changes remain
local/uncommitted; no Git publication or deployment occurred. The prior host
and failed physical reports remain unchanged as historical evidence.

Private `.private/ot0304b-note20-confirmation-20261001/` owns the current approval,
install journal/result, exact recovery reference, signatures and installed hashes,
per-stage UI XML/screenshots, current-session service/log observations, draft
guards/cleanup, restoration exception and successful completion, observations,
independent review and documentation gate. `observations.json` is 21,182 bytes,
SHA-256 `c49664d5b1dd0e50d2b75c10ce188061c20cbb47e50fb59f5c68e86950a53a32`.
