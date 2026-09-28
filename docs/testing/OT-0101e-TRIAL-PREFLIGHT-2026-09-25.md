# OT-0101e one-device GNSS trial preflight

Status: read-only preflight and one bounded candidate trial complete; original
firmware and saved state independently verified and restored. The
owner identified the connected unit as Trail Bench 2 and authorized one
read-only service-mode preparation. The original Note20-paired Trail Bench
remained unchanged. Cases stayed closed and batteries connected.

## Bound candidate and current evidence

The corrected candidate is `build/ot0101e-gnss-display-v2/opentrail_heltec_v4_bench.bin`,
591456 bytes, SHA-256
`2808d63b610619a69e14bd1809a56d921bded6efc2720fb08fd204aaaff9feb2`.
The ESP-IDF 6.0.2 target build completed on 2026-09-25 with evaluation mode
off, ESP32-S3, 16 MB, 80 MHz, DIO. Its padded partition span and OTA sector
match the saved original protected-span pins. This supersedes the image in the
[earlier host candidate report](OT-0101e-GNSS-HOST-CANDIDATE-2026-09-24.md),
which lacks the normal-screen GNSS/battery wiring correction. This establishes
candidate file identity, not physical GNSS acceptance.
The candidate also emits only aggregate GNSS UART byte, accepted-sentence,
rejected-sentence and read-error counters at 30-second intervals. These contain
no coordinates or raw receiver sentences and can distinguish a silent receiver
path from parser rejection if the corrected screen remains unknown.

## Bench 2 read-only custody

After a fresh one-device USB inventory, esptool 5.3.1 entered ESP32-S3 ROM
service mode on the owner-selected Bench 2. The maintained electronic-identity
helper enrolled and reverified the distinct `OT-DEV-002` factory binding in the
ignored private registry. Raw identity and transient port were not copied into
this report. No second unit was opened.

No-stub reads captured the complete original application (733184 bytes), NVS
(12288 bytes), bootloader (32768 bytes), partition sector (4096 bytes), and OTA
sector (8192 bytes) under the ignored private custody directory. Application
and NVS repeat reads matched SHA-256 before service-mode exit; the NVS parser
accepted 6 namespaces and 28 live records without publishing their contents.
The application prefix matched the retained OT-163 control hash
`f3642e5753adf1aa724e9a79dd2eec546bfd292d129fa871f46792e771d3a04b`.
The partition and OTA hashes matched the candidate's protected-span pins,
respectively `b7bbaf702afd377973aa2371f288bcea50548865d10e2cdada4d5e7f98a91601`
and `7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f`.
Host-only `esptool image-info` reported the same ESP32-S3 chip, 16 MB flash,
80 MHz flash frequency and DIO mode for the original and candidate app headers.
Original full-application and NVS hashes are retained with the private binary
captures; no original or saved state was written to the device. A USB hard reset
exited service mode, and the owner confirmed the normal Trail screen returned.
This is a current original-image and recovery *input*, not proof that the
candidate boots or that restoration after a candidate write succeeds.
On the unchanged original firmware immediately afterward, the owner reported
`GPS:--`, `GPS UNKNOWN` on the separate GPS-status line, and `--:--` for
displayed time. Source tracing then found that the normal OLED presenter does
not receive the typed GNSS/satellite snapshot or render the computed compact
footer. Both GPS strings can therefore remain defaults even with receiver
data; neither is evidence of a missing GNSS signal. This is a display-wiring
defect to correct before a physical GNSS acceptance trial.
The owner then connected the S24 Ultra companion, reported that the displayed
time appeared, and moved Bench 2 under open sky. The GPS strings remained
unchanged, which cannot establish receiver behavior while the display-wiring
defect exists.
The owner further confirmed Bench 2 was connected outdoors with open sky and
still showed no satellite count. This rules out the initial indoor placement
as the sole explanation for the *visible* result, but the unchanged installed
image cannot distinguish live receiver data from its display defaults. No
fresh receiver diagnosis or physical GNSS acceptance is claimed.
This later action may have changed saved settings, so the point-in-time NVS
capture must be refreshed during any separately approved candidate trial.
The phone and improved location do not by themselves prove a GPS sample or
sustained clock behavior; those observations are pending.

## Trial sequence and stop conditions

| Stage | Required evidence before the next stage | Stop or recovery condition |
| --- | --- | --- |
| Fresh identity | Bound one physical unit, current port, ESP32-S3/16 MB target and exact partition layout; preserve the other unit. | Any identity, port, target or layout ambiguity stops before write. |
| Original custody | Independently capture and hash the complete original application and saved NVS plus protected bootloader, partition and OTA spans. Prove a one-device restore/readback path before candidate mutation. | Missing, conflicting or incomplete originals stop before write. A serial/ROM operation may reset the board, so verify its original running state on exit. |
| Candidate install | Recheck one-use authority and hashes after backup; write only the approved application range and read back the exact padded image. | Any uncertain write moves directly to restore-only recovery, never a blind retry. |
| Observation | Confirm candidate boot separately from OLED status. The operator inspects the S24 over ADB, uses the saved-owner Bench 2 route, waits for protected Ready, syncs the display clock, and asks the owner to confirm a visible time before starting timed readings. Observe `GPS:--`, `GPS:NF`, a live satellite count, or `GPS:ST` only when naturally produced; compare the displayed clock with a trusted time source over multiple minute changes. Record only status and elapsed bounds, not coordinates or raw NMEA. | An initial app failure does not end the trial: inspect and recover the saved-owner route while the candidate runs. If protected Ready and visible clock sync cannot be established within ten minutes, or an explicit cancellation occurs, restore the original without claiming clock evidence. A missing satellite fix is an observed no-fix, not an invented pass. Do not open the case or interrupt battery power to force a state. |
| Restart and release | Perform only separately approved restart cases, check clock/region retention, then restore the exact original application and NVS, independently read back every captured span, reset, and confirm the normal original screen. | If restoration is uncertain, hold custody for the maintained restore-only path. Do not run another candidate attempt. |

The GNSS host candidate renders a GPS status/satellite count but **does not
display coordinates or GNSS time**. Its clock is supplied by the companion
configuration path, not by GNSS. The hosted task's instruction to report the
"location and time shown" therefore cannot be met literally by this firmware.
The revised operator first requires the S24 saved-owner connection to reach
protected Ready, synchronize the display clock, and make a time visible on the
Heltec. A first not-ready report leaves the candidate running for operator-led
app diagnosis; timeout or explicit cancellation restores the original without
claiming clock evidence. It then requests two privacy-safe OLED status readings at
least two minutes apart, plus whether the phone-synchronized displayed clock
advanced. The phone-ready checkpoint allows ten minutes, followed by a
separate nine-minute observation deadline. The operator restores the originals
on a timeout or cancellation. The first GPS reading must arrive within three
minutes after the phone-ready checkpoint. This can reveal a no-fix to fix transition and
clock advancement without storing coordinates, GNSS time or NMEA. It does not
by itself prove loss/recovery or clock/region retention across restart. Those
remain explicitly open; a physical candidate trial must not be misreported as
complete OT-0101e acceptance if those conditions are not observed.
Adequate satellite reception may require an outdoor/near-window setup while
keeping USB custody; absence of a fix indoors must be reported as such.
The task's current/stale/unavailable and sustained-clock criteria remain
separate: naturally observed states can be recorded, while an unobserved stale
transition or sustained clock/region result must remain open. Resolve the
instruction/acceptance wording before declaring this physical task accepted.

A GNSS-specific host-only custody engine and operator now exist in
`tools/gnss_observation_trial.py` and `tools/run_gnss_observation_trial.py`.
Ten fake-transport tests cover restoration, uncertain writes and fresh
restore-only recovery. A fresh isolated 3563-file OT-212 runtime was built
from the previously verified dependency closure and the current worktree's
policy sources. Its manifest and source/candidate binding passed exact-hash
checks, and its child probe passed without opening or enumerating ports.
Both one-use grants were consumed in separately approved Bench 2 trials. Each
operator run closed its private journal and removed the custody lock after full
original restoration. [The hardware result](../../tests/hardware/OT-0101e-2026-09-25.md)
records two current fixes from the first trial and the phone-ready refusal in
the second; clock and loss/recovery gates remain open. Neither consumed grant
authorizes another candidate attempt.
The complete host matrix (excluding separate security operators) passed with
normal Windows DPAPI access after a sandboxed first run stopped at that
environment-dependent key test. The affected GNSS trial tests passed within
the matrix. The later-discovered normal-screen wiring defect was corrected and
the revised candidate was built and exercised in the single physical trial
described above. The phone-ready operator checkpoint passed its focused host
regression on 2026-09-27 (11 tests); it has not yet run on hardware.
The historical BLE one-device and two-node radio operators remain tied to
other candidates and grants; do not run either unchanged for this trial.
