# OT-171 setup navigation, name suggestion and recovery correction - 2026-09-07

Status: final C577 S24 APK installed and verified; initial Device landing,
app-only/warm recovery and protected post-warm name/region readbacks pass.
First-use name suggestion and physical rotation remain software-validated only.

## Correction to the previous recovery account

Commit `3aab896` inaccurately reported that app-only recovery stalled about two
minutes until a board restart. Typed OTCL3 timestamps establish Ready before the
later disconnect, and a controlled unchanged S24-only repeat recovered without
touching either Heltec. That historical entry is preserved with this correction.
No firmware or reconnect-source correction was needed for these recoveries.

| Session / boundary | Elapsed timestamp (ms) | Derived interval |
| --- | ---: | --- |
| 9 discovery | 85894 | Discovery start |
| 9 connection attempt | 115912 | 30.018 s after discovery |
| 9 Ready | 116853 | 30.959 s after discovery |
| 9 later disconnect during warm-restart test | 141909 | 25.056 s after Ready |
| 9 subsequent Ready | 143944 | 2.035 s after disconnect |
| 10 unchanged app-only discovery | 40003 | S24 force-stop/relaunch and manual service start |
| 10 connection attempt | 70024 | 30.021 s after discovery |
| 10 Ready | 70879 | 30.876 s after discovery |
| 11 interim A888 discovery / Ready | 150340 / 181170 | 30.830 s |
| 12 final C577 discovery / Ready | 19833 / 51025 | 31.192 s |
| 12 final C577 disconnect / Ready after warm restart | 103282 / 105117 | 1.835 s |

Session 9 has no wall-clock restart-command marker: 25.056 seconds is Ready to
later disconnect, not exact command-time correlation. Session 10 used unchanged
CC5D/984E artifacts with no Heltec action, connected UI and automatic clock sync;
it is the strongest controlled evidence correcting the original stall inference.
These timings cover the existing explicit service-start flow, not automatic normal
launch or a universal latency guarantee.

## Final Android changes

Initial ChooseMode now opens Device before the splash finishes, including
saved-pair cold launches. ChooseMode means connection setup has not started; it
is not evidence that Android has no bond. Other initial states retain Messages.
Unkeyed saveable selected state preserves explicit tabs across runtime changes and
Activity recreation; no persistent pairing hint is introduced.

A validated first-use Trail label is retained only within the exact initial
GATT authorization generation. Fresh protected readback of an empty durable name
allows that label to appear as a name-entry suggestion. It never becomes the
connected header/current-name authority, and only explicit Apply followed by
protected write/readback saves it. User edits and cleared text win. Guarded
session/process editor state survives rotation but rejects stale, legacy or
malformed restored state. Returning owners without a current matched setup label
get ordinary name entry; existing names are unchanged.

The setup introduction now says Connect below to read your device name and
radio-region settings. The interim A888 installation exposed the obsolete
firmware-update warning; C577 includes the copy correction and the same features.

## Final validation and artifact

All 1,089 tests across 120 XML reports pass with zero failures, errors or skips.
All variant lint/build tasks, debug Android-test APK build and unsigned-release
audit pass; 143 Android input hashes remained unchanged. Navigation includes
three behavioral tests, with runtime/configuration/editor tests covering the
suggestion and restored-state boundaries. No firmware changed in this increment.

| Final artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| V1-Test APK | 12505023 | `C577D14953926B4BD138F0A0986AA6DABCE92053A4D29067CA6F5257CC366BAC` |
| Unsigned release APK | 8722116 | `F5A4ED54E48A48AEF5E7999021B2456B62C21E4C95CFA8821593B585B47D613A` |

V1-Test remains `io.github.nbjelanovic.otclient.v1test`, version code 1 /
`1.0.0-v1test`, minimum SDK 26 / target 35, with unchanged v2 debug signer
`bc60cc64be586444a0ce181e426e586ec74d55e764268089e3aa4e8f1dbfac06`.
The release APK is unsigned and does not establish signed-release acceptance.

## Physical acceptance

The owner selected Trail Bench 2. Before the APK update, protected Read device
confirmed an empty name, then explicit Apply saved and read back that exact name
on unchanged CC5D/984E artifacts. This was an owner-selected name, not automatic
application of the new suggestion.

Final C577 installed bytes were read back exactly on S24 only. Initial Device
landing and corrected setup wording were physically verified. On interim A888,
Messages selected during discovery remained selected after Ready; Device showed
Trail Bench 2 and automatic clock synchronization.

Final C577 session 12 reached Ready in 31.192 seconds. The subsequent second-only
warm restart at 16:09:51 UTC recovered without a new PIN; the trace interval from
disconnect to Ready was 1.835 seconds. Post-warm UI showed Connected to Trail Bench 2
and automatic Display clock synchronized. Explicit Read device then Read region
confirmed Last device readback: Trail Bench 2 and Saved choice US915 with TX
disabled. Durable name and region persistence across that warm restart pass.

Both firmware artifacts and the original phone remain unchanged. The original
pair still needs its clock correction. Physical first-use name suggestion and
rotation were not exercised; no additional factory reset was performed. Numeric
keypad feasibility for the Android-owned PIN dialog, exact-peer bond cleanup,
complete onboarding/reset-domain cleanup, secure two-pair messaging and release
gates remain open. V1 Companion stays exact 45.50%, displayed 46%. Website and
cold-power work remain deferred.
