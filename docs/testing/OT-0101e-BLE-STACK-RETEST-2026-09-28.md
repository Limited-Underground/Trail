# OT-0101e corrected BLE-stack physical result

## Result

OBSERVED on 2026-09-28: one separately approved Bench 2/S24 trial reached
protected Ready, synchronized the physical display clock, and retained the same
connection while the owner confirmed clock advancement across 184.7712608 seconds.
This validates the bounded corrected-stack connection path, not all OT-0101e gates.

The exact 593200-byte `A_STACK_8192` image has SHA-256
`56a1737e5fcdcca0514aba7f615b9934efc0f025d656b393b29633c10ec808b1`.
It retains the diagnostic sensors/display/security configuration and increases
the BLE task allocation from 4096 to 8192 bytes. Toolchain, two-build equality,
ordinary build and configuration gates are in the
[correction](OT-0101e-BLE-STACK-CORRECTION-2026-09-28.md); exact frozen execution
and restoration boundaries are in the
[retest plan](OT-0101e-BLE-STACK-RETEST-PLAN-2026-09-28.md).

## Setup and observed flow

Canonical project C:/lu/OpenTrail; selected checkout
C:/lu/OpenTrail/.private/ot0101e-gnss. The live OT-0101e revision1 was Approved/In
Progress at version271. The owner approved this exact single trial and confirmed
the original physical clock and presence before issuance. Bench 2 is the registered
Heltec V4.2, US915; S24 model SM-S928U. Both remained on USB, cases closed,
batteries connected. Bench1 was unchanged; no radio transmission occurred.

The existing diagnostic V1-Test APK was retained with its app data and bond:
11974618 bytes, SHA-256
`79603e1a49b299f06908d9792658af61c85cf77bc498240693b67bb5684adec0`.
Original control reached protected Ready and the owner confirmed its physical
clock. The app was stopped for backup/write/readback, and started only after the
passive recorder explicitly armed. UI navigation located the saved-device service;
no replacement connection, new pairing, data clear or manual retry occurred.

Candidate phone session23 returned successful protected reads: status0/20 bytes/
143ms internal request age, then status0/16 bytes/57ms. Authorization and snapshot
were accepted; Ready followed 909ms after connection initiation. The app showed
Connected to Trail Bench2 and Display clock synchronized. The owner first reported
GPS FIX with10 satellites, battery84% and a visible clock, then9 satellites and
an advancing clock 184.7712608 seconds later. Second fix status was not separately
reported; no coordinates or actual clock values are retained in this report.

## Firmware evidence and independent review

The identity-bound sanitized capture observed419.721 seconds,410 stack samples
and113 events. Both protected-read paths entered, acquired the lock, passed
security and exited successfully with20 and16 bytes. BLE minimum free stack fell
from6060 to3996 bytes and remained3996 through the last sample; main-task minimum
was3168 bytes. Sample uptime remained monotonic. No captured overflow, panic,
restart, disconnect or host reset occurred. Display counters reached4114 calls/
442 renders with zero failures, maximum25336 microseconds; sampled drops were0.
An independent read-only reviewer checked the phone, firmware and owner evidence.

Capture SHA-256 `16872b586e34c74f8495fac3ac8d06b310fb466e90800eedb25009f0893204ae`,
93635 bytes. `operator_stop` and truncation reflect deliberate terminal completion.
Startup/boot output was missed; draining can delay or lose events. Periodic samples
do not establish all-path stack headroom. The prior actual overflow plus this
successful allocation correction supports the bounded stack fix; the exact prior
overflowing function and all earlier failures' causes remain unknown.

## Restoration and remaining gates

The maintained controller stopped/reaped the capture before ROM restoration,
restored/read back the original733184-byte application and12288-byte NVS, checked
all five original/protected spans, reset the original, and exited0 with custody
closed. Independent audit rehashed all five saved spans and all23 frozen input
files and confirmed the active lock absent. Restored-original session24 reached
protected Ready and reported display-clock sync using the preserved app and bond.
Physical normal-screen owner confirmation is recorded separately in the hardware
record when received.

Private owning artifacts: `.private/connection-stack-trial-prepared.json`,
`.private/gnss-observation-3b11b68bbacc6a52417d8ae077d9e2de.json`,
`.private/connection-capture-3b11b68bbacc6a52417d8ae077d9e2de.json`,
`.private/connection-stack-trial-phone-log.txt`,
`.private/connection-stack-trial-owner-observations.json`,
`.private/connection-stack-trial-restoration-audit.json`, and
`.private/connection-stack-restored-phone-log.txt`.

The owner separately observed battery84% despite prolonged USB connection.
No battery voltage or charge completion was measured; this is an OT-0101d
observation, not proof of full charge or a connection regression.

GNSS freshness/stale/loss/recovery and approved restart/retention acceptance remain
open. No production installation, V1 completion increase, Git publication or public
website status change is claimed. Next: review existing GNSS state/deadline logic
and saved evidence to define the smallest remaining loss/recovery validation.
