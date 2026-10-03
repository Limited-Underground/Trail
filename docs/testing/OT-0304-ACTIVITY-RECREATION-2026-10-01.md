# OT-0304 saved-pair physical checkpoint

Recorded 2026-10-01, approved revision 1. **Partial physical evidence;
full task blocked by Activity recreation and unprepared fresh first-use.**

The owner separately authorized replacing V1-Test on both phones, reconnecting,
ordinary reopening, rotation and larger text, while preserving app data/bonds
and restoring display settings. No firmware installation, factory reset,
re-pairing, board restart, app-data clearing or RF transmission was performed.

## Exact app and selected pairs

| Input | Observed identity / boundary |
| --- | --- |
| Note20 pair | Samsung SM-N986U, Android 13, saved Trail Bench returning connection |
| S24 pair | Samsung SM-S928U, Android 16, saved Trail Bench 2 returning connection |
| Boards | Existing Heltec V4.2 bench inventory labels; USB electronic identity, resident firmware hash/profile and physical port mapping were not newly established in this app-only checkpoint |
| Package | `io.github.nbjelanovic.otclient.v1test`, `1.0.0-v1test`, local debug signing |
| New APK | 12,656,607 bytes; SHA-256 `a1c04187aeea4a762ec0c9e752ac5c9378a95997a1f447beaa1ae09257d8d722` |
| Both installed originals | 11,974,618 bytes; SHA-256 `79603e1a49b299f06908d9792658af61c85cf77bc498240693b67bb5684adec0` |
| Upgrade admission | Original/candidate signing certificates matched; replacement used no uninstall or erase; each installed APK hash independently matched the accepted [OT-0303 artifact](OT-0303-RESET-RECOVERY-2026-10-01.md) |

## Observed sequence and outcomes

Both ordinary launcher opens reached device setup. Find device, then Start
Bluetooth device service, recovered the respective saved pair without new
pairing. Current setup reached Step 5 after name/region readback. Public-profile
readback was unavailable in both observed app setups, which remained paused
rather than treating phone drafts as completed settings. The exact resident
firmware/profile was not measured.
This cannot establish the full ordered [OT-0301 first-use flow](OT-0301-SETUP-COMPLETION-2026-10-01.md).

Both app clock-sync requests reported success; the owner independently confirmed
that each Heltec showed a time. No actual time or coordinates are recorded here.
This is a clock-display observation, not endurance or board-restart acceptance.

| Case | Note20 | S24 |
| --- | --- | --- |
| Saved-pair reconnect after APK replacement | Connected to correct named bench | Connected to correct named bench |
| Unsaved `Rotation check` quick-message draft | Created in previously empty editor; never saved/sent | Same; field needed ordinary scrolling into view |
| Portrait to landscape | Messages/Customize retained; indicator changed to disconnected | Same |
| Return to original portrait | Exact unsaved draft retained; indicator remained disconnected | Same |
| Temporary font scale 1.3, Home, ordinary reopen | Customize retained; exact draft visible | Customize retained; draft off-screen at enlarged size, confirmed intact after original size restored |
| Service after recreation | Target present, start requested, foreground true | Same |
| Explicit existing-service observation recovery | Find device / Start service immediately restored connected display | Same |
| Final cleanup | Exact test draft cleared; original empty editor verified; display settings restored; app connected | Same |

Original font scale 1.0, automatic rotation 1 and user rotation 0 were freshly
captured and independently read back after restoration. Stay-awake settings were
unchanged. Both upgraded V1-Test apps remain installed. Existing saved bonds
were usable afterward; no comprehensive private-history or disk-durability audit
is implied by replacement and these observations.

Landscape and enlarged-text screenshots were inspected. The tested text reflows
and content below the viewport needs scrolling; an off-screen draft was not
misreported as lost. These few screens do not constitute complete accessibility,
populated production UI, process-death or signed-release acceptance.

## First failing transition and bounded correction

The replacement Activity loses its observation of the existing service.
`MainActivity` creates a fresh `TrailActivityController` whose mode is unset and
whose service-request flag is false. Its lifecycle start therefore does not bind;
the old controller releases observation without stopping the user-started owner.
Actual service-liveness measurements and immediate explicit reattachment on both
phones match this source path. Service presence alone does not independently
prove uninterrupted protected BLE Ready or radio delivery.

Proposed [OT-0304a](https://limitedunderground.com/lab/tasks/7d1bf0b5-40b5-40c3-93af-c0c19d11a28a) corrects only reattachment to a live existing binder, with
fresh permission/state/generation checks and no auto-creation, new scan, cached
Ready or owner shutdown after failed local attachment. A composed replacement-
controller regression must fail before the fix and pass afterward; actual Android
rotation remains a later physical continuation. This report does not implement
or authorize that correction.

## Remaining gates and evidence custody

Full fresh setup requires the public-profile-capable standard firmware and a
concrete initial-state/preservation plan. Existing owned pairs are not fresh
devices. Before any reset preservation promise, include the 1 MiB `ot_state`
partition at `0xf00000`; historical limited BLE/enrollment custody lists omit it.
No reset or firmware attempt was added to this checkpoint.

Warm board restart, fresh pairing/reset, complete public-profile onboarding and
production usability remain unreached. No V1 credit or public website capability
change. Git publication is separate. Existing host tests/builds are unchanged
evidence, not new device results; no redundant Android rebuild was performed.

Private receipt identifiers under `.private/ot0304-preflight-20261001/`:
approved-check/start/operation-check; both install receipts; exact recovery APK;
plan/review; per-stage UI XML and screenshots; original display journals;
both post-rotation service observations; final result/input manifest. These are
custody references, not public download links or grants.
