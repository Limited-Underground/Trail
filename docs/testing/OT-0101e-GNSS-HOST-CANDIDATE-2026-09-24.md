# OT-0101e GNSS host candidate; physical acceptance open

Observed 2026-09-24 in isolated OpenTrail worktree `codex/ot0101e-gnss`,
starting at `cd1ce52801bb3e8fa566f724c1df6ed4dd8c587a`. This is a
host-tested target candidate, not an installed or accepted Heltec result.

## Change and result

The Heltec V4.2 GNSS adapter now distinguishes unavailable, live no-fix,
live usable-fix, stale and invalid observations. A checksum-valid GGA/GNS
receiver quality flag alone cannot claim a usable fix: bounded latitude and
longitude shape/hemisphere and nonzero satellite count must agree. Invalid,
estimated, manual and simulated modes do not claim a satellite fix. The
existing five-second freshness boundary remains unchanged. The compact OLED
footer shows a satellite count only for a live usable fix, `GPS:NF` for a
live no-fix, `GPS:ST` for a stale observation, and `GPS:--` for unavailable
or invalid data. It does not display or share coordinates or GNSS time.

Coordinate digits are transient parser probes. They are cleared on sentence
acceptance, rejection, reset, a new sentence, and after one second without
further bytes while the target service loop continues. No position, time,
altitude, raw NMEA sentence or talker is exposed through the adapter. The
target-admission source guard was adjusted to permit this transient shape
check while requiring clear and timeout hooks. An independent source review
found no remaining concrete parser/privacy defect; a stalled service loop
cannot provide a hard wall-clock erasure guarantee.

## Validation

- Focused `heltec_v4_gnss_tests.cpp`: 10 groups passed, including exact
  stale boundary, fix loss/recovery, contradictory quality/coordinates,
  zero-satellite fix claim, malformed coordinates, checksum and partial
  sentence timeout/recovery.
- Focused `compact_status_footer_tests.cpp`: 12 groups passed.
- Focused `heltec_startup_display_tests.cpp`: 15 groups passed.
- `tests/host/heltec_v4_bench_target_tests.py`: 17 target-admission groups
  passed after the privacy guard correction.
- Affected `heltec_v4_bench` control target built under installed ESP-IDF
  6.0.2 for ESP32-S3 with evaluation disabled. Application image:
  `build/ot216-ble-ot0101e-final/opentrail_heltec_v4_bench.bin`, 590608 bytes,
  SHA-256 `69e669e53f6ffcb4775edebd9ebc139266056ab83d16b73e87ef7234e07f6d8e`.
  The image is a build artifact only; it has not been flashed.
- The complete `tools/Test-Host.ps1 -SkipSecurityOperators` matrix passed from
  final behavioral inputs with normal local compiler/DPAPI access. The
  sanitized run log is `.private/ot0101e-host-matrix-final.log`. The separately
  excluded security operator scripts are not claimed as run by this command.
- `tools/check_repository_docs.py` passed and
  `tests/host/repository_docs_tests.py` passed 18/18 groups.

An initial sandboxed target build could not launch the installed Xtensa
compiler; the same target build passed with normal local tool access. An
initial full host run stopped at the pre-existing lexical no-coordinate
source guard, which the new bounded parser required changing; its 17 focused
admission groups now pass. These are not physical firmware failures.

## Open acceptance gates

No Heltec was opened, reset, flashed, read through a serial session or moved
for this increment. Therefore GNSS acquisition/loss/recovery on the received
unit, actual OLED rendering, sustained clock behavior, region/clock survival
across approved restarts, and the pending original-control clock correction
are untested. OT-0101e remains In Progress and no V1 completion credit or
public website status changes follow from this candidate.

The next device step requires a separately authorized, exact one-device plan:
fresh device/port identification, candidate and original-image hashes,
protected-region and NVS custody, restoration and readback, a location/time
observation plan, then owner-visible GNSS/clock results. Preserve the other
Heltec as a control until its separate role is released. Cases stay closed
and batteries connected.
