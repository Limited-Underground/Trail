# OT-0101e partial lifecycle result and restored device

## Observed result

One owner-authorized v6 trial used Trail Bench OT-DEV-001, Heltec V4.2,
and the Note20 SM-N986U. Cases remained closed and batteries connected;
Bench 2 and the S24 were untouched. The baseline fresh region read was US915.
The exact candidate and preparation are recorded in the
[v6 preparation report](OT-0101e-LIFECYCLE-V6-PREPARATION-2026-09-29.md).
The candidate was 593200 bytes, SHA-256
`56a1737e5fcdcca0514aba7f615b9934efc0f025d656b393b29633c10ec808b1`.
The installed V1-Test APK was 11974618 bytes, SHA-256
`79603e1a49b299f06908d9792658af61c85cf77bc498240693b67bb5684adec0`.

The controller recorded three passed stages with durable acknowledgements:

| Stage | Elapsed time from observation start | Evidence |
| --- | ---: | --- |
| Baseline | 99757 ms | Saved protected phone connection, clock-sync report, fresh US915; owner reported GPS:8 / GPS FIX and separately confirmed visible time. |
| Phone stopped | 178923 ms | Normal app disconnect; ADB confirmed the service stopped; owner remembered the still-visible clock. |
| Phone disconnected | 371656 ms | Owner confirmed the visible clock advanced after more than two minutes; service remained stopped. Receipt-based hold was 177.630790 seconds. |

GPS loss ended inconclusive at 671673 ms because no complete observation was
submitted within its five-minute window. After restoration began, the owner
reported seeing GPS NO FIX followed by a blank screen. Retain that as an owner
observation: its exact occurrence time is unknown and clock advancement during
NO FIX was not confirmed. It is neither a passed loss case nor evidence that
GPS could not lose its fix. The blank screen coincided with restoration;
it does not establish a candidate runtime fault.

GPS recovery, guarded restart, clock clearing after restart, and fresh candidate
reconnect/region verification were not reached. Full OT-0101e remains unfinished.

## Restoration and retained evidence

Attempt `5959ac69ff9b8e6cbbc8b8c6649d07d5` restored the original application and
saved NVS, verified the original spans, and reset the device. Controller session
93400 exited zero with `restored:true`. The journal is closed and ends in
`restored_and_reset`; the active custody lock is absent. Independent post-exit
checks matched all five saved-original sizes and hashes to the journal descriptors.
The owner confirmed the usual Trail screen. The Note20's normal saved-device
route then reconnected, reported display-clock synchronization and freshly read
US915. App data and pairing were retained. This checks the restored original;
it does not complete the unrun candidate restart/reconnect case.
No second attempt was started.

Private evidence in the selected GNSS worktree:

- `.private/gnss-observation-5959ac69ff9b8e6cbbc8b8c6649d07d5.json`
- `.private/gnss-lifecycle-plan-v6/` binding, one-use authority, stage receipts,
  raw terminal output, decoded output and closeout manifest.

Eight ConPTY cursor-overwrite sequences in the retention output duplicated the
same character at the last terminal column. A narrowly checked presentation
decode removed only those matching overwrites; independent review verified all
eight pairs and exact decoded output. The CLI then confirmed the durable journal
history and actual next checkpoint. Raw evidence is preserved. This is not a
general terminal parser correction or permission to infer an acknowledgement.

## Required change before another trial

The five-minute environmental/manual GPS gate was an unsuitable test design.
Faster owner responses are not the correction. Show the owner a revised plan
before another attempt, separating independent clock/restart checks from GPS
loss/recovery. Human setup and finding a suitable reception condition must not
consume a short measurement countdown. Start timed measurements only when the
required setup and observations are ready; agree realistic finite waiting and
restoration limits. Preserve product/security deadlines and recovery safeguards.

Keep this trial's passed clock-retention result. Do not repeat it merely to
replace an inconclusive overall result. Any new scenario must align its plan,
validators, binding and one-use authority; do not skip required rows in the old
plan, extend a consumed grant, or mark unrun steps passed. No successor trial is
authorized by this report. Host preparation and review precede a new physical
authorization.

No firmware/app code changed or rebuilt during this trial. Preparation validation
remains separately documented; independent review checked the three accepted
stages and limits above. The documentation checker, all 18 repository
documentation tests and `git diff --check` pass. This partial evidence does not close a weighted V1
milestone. V1 progress and public website status are unchanged. All changes are
local and uncommitted; no Git publication occurred.
