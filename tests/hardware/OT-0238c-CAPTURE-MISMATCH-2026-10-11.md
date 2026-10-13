# OT-0238c retained settings mismatch, 2026-10-11

OBSERVED physical attempt; enrollment acceptance was not reached. The owner
authorized one two-device trial and used the familiar launcher. Candidate
installation never began. Freshness verification stopped at
`freshness_A_nvs / capture_changed`; automatic capture release then failed with
`release_A / rom_adapter_refused`. The exact inner cleanup predicate was not
retained. Both screens were blank.

## Recovery observations

The owner separately authorized one brief Button 2 restart per device. Trail
Bench was restarted first and its usual screen confirmed; then Trail Bench 2
was restarted and its usual screen confirmed. These are OBSERVED normal-screen
checks, not byte-restoration or automated-custody verification. The failed
journals and active marker remain preserved, including uncertain reset intent.
No retry, grant replay, journal flag replacement or candidate write followed.

## Known boundary and unknown cause

The retained originals differ: A's application identifies `ot178-phone-v1`,
B's `ot171-label-v1`; both identify `opentrail_heltec_v4_bench` and ESP-IDF
`v6.0.2`. Do not assume the same original firmware on both devices.

The adapter discarded the mismatching read before saving its bytes. Existing
NVS binaries are retained-original links, not the rejected current sample.
Consequently the settings entries that changed cannot be reconstructed from
this attempt. Current bench source and the reset-marker helper describe normal
persistent writes, including empty namespace creation, but their occurrence in
this attempt is UNKNOWN. A mismatch alone does not prove corruption.

The host cleanup compares current NVS against differing retained NVS before its
safe-marker check. This can prevent release of untouched original firmware.
Computer-only correction and independent review are required before any new
device execution. Candidate admission must remain blocked on mismatch; retained
originals and historical uncertainty must remain intact.

## Private owning artifacts

The exact session is
`C:\lu\OpenTrail\.private\ot177-publication\.private\ot0238c-restart-package-20261010\sessions\37f2d4c791c484b2e90da97745f284d5`.
It owns the original/candidate runner results, capture journal and pins,
unchanged retained binaries, and
`owner-manual-recovery-observation-20261011.json`. No device identifiers,
settings contents or secrets are reproduced here. Full OT-0238c remains In
Progress; no V1 credit or public website capability changed.
