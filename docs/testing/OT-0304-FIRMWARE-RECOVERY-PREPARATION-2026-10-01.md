# OT-0304 Standard firmware and setup recovery preparation

2026-10-01. **Computer preparation complete; device execution not started.**

The next useful check is the existing Note20/Trail Bench pair with the standard
firmware's device-backed settings. Preserve the passed phone startup/rotation
checkpoint. First establish the current board image and recovery custody; then
authorize one bounded existing-pair test if the preflight matches. Do not erase
the current pairing just to call this a fresh first-use test.

## Confirmed inputs and limits

The [accepted public-settings implementation](OT-0302-PUBLIC-PROFILE-SETTINGS-2026-10-01.md)
already provides configuration profile **5**, capabilities **0xff**, protected
kind **8** requests / **0x8a** responses and durable atomic public-name/visibility
readback. The standard `firmware/targets/heltec_v4_bench` application composes
the actual public-profile storage owner. Evaluation profile 127 deliberately
does not expose these operations. No product-code correction was needed here.

| Reused artifact | Exact identity |
| --- | --- |
| Standard Heltec application | 602096 bytes; SHA-256 `33c27be5dea55e38276eafe96f278504353ca5ab1749bec0e5e95db1668e5193` |
| Tested Note20 V1-Test APK | 12658887 bytes; SHA-256 `fcd3e97353cf67fc345e7f81cd541237c60dd5d9d94148e960d9ff042a66b274` |

The standard image is retained at `build/ot0302-settings-standard-a-r2/opentrail_heltec_v4_bench.bin`;
the equal B build is `build/ot0302-settings-standard-b-r2/`. Prior ESP-IDF 6.0.2
build/reproducibility and installed-SDK audit evidence are reused, not rerun.
Current verification matched all **390 actual consumed firmware repository
inputs**, all **18 raw A/B artifacts**, **181 accepted Android authored inputs**,
**12 Android tool pins** and the APK. There were no mismatches. Installed firmware
SDK files were not freshly audited; no new compilation occurred.

The [last phone checkpoint](OT-0304b-NOTE20-CONFIRMATION-2026-10-01.md) observed
unavailable public-profile readback. Its exact resident firmware/profile and
active boot slot were not measured. An older/incompatible resident image is an
inference, not a proven exclusive cause. Do not replace that unknown with a
claim that flashing will necessarily solve the whole task.

## Exact layout and recovery boundary

Source `firmware/targets/heltec_v4_bench/partitions.csv` and the accepted decoded
build partition audit agree on OTHP0/v1, ESP32-S3/16 MB. The generated application
offset is **0x10000**. The following are preparation requirements, not current
device measurements or permission to write:

| Region | Offset / bytes | Required custody |
| --- | --- | --- |
| Bootloader | 0 / 32768 | Compare only; do not rewrite |
| Partition table | 0x8000 / 4096 | Decode/compare exact layout; do not rewrite |
| OTA metadata | 0x9000 / 8192 | Verify factory boot selection; do not rewrite |
| Ordinary NVS | 0xd000 / 12288 | Preserve complete bytes for settings/owner/bond recovery |
| Factory application prefix | 0x10000 / verified sector-complete extent | Preserve both original and candidate write/erase coverage |
| `ot_state`, custom type 0x40/subtype 0 | 0xf00000 / 1048576 | Preserve once before claiming all saved-state/reset recovery |

The historical application capture is **733184 bytes**, which fits the standard
602096-byte image; the candidate's own rounded sector extent is **602112 bytes**.
Historical size is not proof of the current original image or boot slot. Before
reuse, independently establish that the original and intended padded operator
write both fit the captured extent. Otherwise revise the exact plan before a
write. Reuse a previous original only after fresh byte equality; do not create
another identical copy solely for preservation.

`tools/ble_confirmation_trial_transport.py` currently allowlists five spans and
does **not** admit `ot_state`. Do not relabel that executor as a complete fresh-
reset recovery path, or bypass its allowlist. Any extra capture/restore operation
needs a reviewed, host-checked extension before use. The earlier enrollment
candidate's six spans include a repurposed OTA prefix, not this missing state
partition; its case/authority is not interchangeable with this standard target.

The generated `flasher_args.json` is a build recipe: it includes bootloader,
partition table and initial OTA metadata writes. Do not run that whole recipe
for this application-only test. Its default stub/reset choices also do not
replace the maintained, verified ROM-loader recovery procedure. The historical
`physical-flash-plan.json` is a consumed different-image/erase plan, not authority
for this candidate.

## Proposed staged sequence

| Before / owner | Trigger and required evidence | Premature effect to prevent | Failure / recovery |
| --- | --- | --- | --- |
| Passed saved pair; no new grant | Freshly identify Note20/Trail Bench and authorize read-only service-mode preparation | No app clear, bond removal, factory reset, firmware write or second-pair operation | Stop ambiguity; return board to normal application and confirm screen |
| Phone connection stopped; board in authorized ROM service mode | Read identity/layout/boot selection and complete required originals; independently hash/read back | No inferred port, stale original, truncated application coverage or missing saved-state promise | Record exact completed reads; no trial on incomplete custody |
| Exact matches and reviewed operator | Separately authorize one standard application-only candidate test; write/readback exact bound image | No whole-flash recipe, table/OTA rewrite, profile127 substitution or automatic repeat | Resume restoration from durable journal; unknown result is not retry authority |
| Candidate boot; retained phone owner | Saved-device connection reaches fresh protected Ready; actual profile5/0xff and private-name/region readback | No Add Device/createBond, cached Ready, silent default-name or region write | Inspect bounded existing diagnostics; maintain defined recovery reserve |
| Current valid name and region | Read public profile through kind8/0x8a | An absent revision0 record proves support, not completed setup | Preserve absent/failed/unknown states accurately; do not synthesize completion |
| Exact separately scoped chosen values | Atomic public-name/visibility write, explicit fresh read and one warm board restart/reconnect | No RF, public presence, repeated uncertain write or phone-only draft as authority | Uncertain write requires READ; lost authority invalidates completion |
| Trial terminal or interrupted | Restore exact original application/NVS and any scoped changed state; compare protected/state regions; release/reset to original app | No normal boot before restoration verified or deletion of recovery evidence | Continue only recovery journal; independently verify normal screen and saved connection |

Private name, current region and public-profile reads are ordered by the actual
Android configuration session. Missing name/region revisions are a real earlier
gate: a public READ must not be forced by silently saving defaults. Visibility
OFF can be a valid completed preference. Profile/version compatibility alone
does not authorize a settings write, radio transmission or public discovery.

No new first-use controller or six-span executor was implemented or declared
ready by this planning result. Fresh identity/custody determines the necessary
narrow operator scope before execution. Existing maintained custody/journal/
readback/restoration patterns are retained; another hardware attempt is not a
substitute for that gate.

## Fresh first-use and timing

Android explicitly disables backup and device transfer. An APK copy restores
code, not app-private receipts, Keystore state or Android Bluetooth bonds.
Verified reset can record a fresh-setup marker while the system bond remains;
the app cannot safely identify the previous bond from a reset private address.
Do not promise exact phone/pairing rollback from the current APK-only recovery.

Full fresh-first-use therefore needs a separately approved disposable-state or
accepted-loss plan, or proven exact app/bond custody. It is not silently included
in the owned-pair firmware test. Prepare the phone, permissions, operator and
owner instructions before opening the genuine **60-second unowned pairing
window**. Ask for readiness immediately beforehand; do not extend the protocol
deadline or spend its budget assembling host preflight. Human steps should have
clear labels and practical availability allowances outside protocol deadlines.
No GPS-loss move, battery disconnection or case opening is part of this plan.

## Preflight disposition and validation

Applied `docs/firmware-porting-lessons.md` and the end-to-end flow review.
Target/linkage, public-profile storage/serialized owner, unchanged stack/sensor
configuration and source-bound build inputs are reviewed. The standard target
retains the existing GPS/battery/clock display paths; no LoRa init/TX producer is
enabled by public settings. Source behavior is not a new physical sensor result.
No target architecture, protocol, offsets, timeout or security rule changed.

Actual unit identity, resident/boot selection, storage occupancy, live serial
re-enumeration, original capture, installed firmware readback, pairing/settings,
warm restart and interruption recovery are **unrun** because this increment is
computer-only preparation. Current phone connection/display and availability
were not newly measured. The existing physical grants were consumed.

One final repository-documentation checker, documentation regressions and diff
check cover the new report and owning-record updates. No product tests, builds,
hardware access, Git mutation/network or website publication occurred. Prior
reports/closed evidence, unrelated Repeater/owner backlog work, detailed V1
record and concurrent HomeAssistant focus are preserved.

OT-0304 retains partial physical evidence and remains open. No V1 progress or
public website capability change. New preparation is local/uncommitted; GitHub
publication remains separately pending. Private
`.private/ot0304-firstuse-preparation-20261001/` owns exact live-task receipts,
input revalidation, reviews, command results and final preservation/closeout.
