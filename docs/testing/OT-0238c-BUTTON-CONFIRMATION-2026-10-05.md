# OT-0238c button confirmation repair — started 2026-10-05

## Result and evidence boundary

The software repair passes independent source review, the affected host matrix,
and two matching current-source firmware builds. The v14 package passes synthetic admission and independent static binding
review, and the exact final launcher passes independent review.
No new device trial or physical enrollment acceptance is established.
The historical `finish_A / target_refused` cause remains unknown.

## Simplified owner flow

One owner-visible launcher runs the fixed sequence. Device comparisons use
physical Button 1 (the top button), not a computer Continue action:

1. Compare MY ID and confirm on Trail Bench, then Trail Bench 2.
2. Compare PEER ID and confirm on Trail Bench, then Trail Bench 2.
3. Compare the complete code and confirm on Trail Bench, then Trail Bench 2.

Each device instruction appears only when its checkpoint is ready. A fresh
approximately two-second hold and release confirms a comparison. The host
advances only after a matching device-generated confirmation. Cancel remains
available. Original-screen restoration uses one fresh, separately bound chat
reply. The owner does not launch separate prepare/publish command files.

## Validation

- Final affected matrix: 14 Python suites / 483 unique tests and 16 compiled
  C++ suites / 865 groups. Two outdated fixture expectations were corrected;
  the original failure receipt and passing capture-20/runner-1 retests remain.
  Production sources stayed unchanged through the full matrix and corrections.
- Critical independent source review verifies the final 26 source/test/template
  pins, physical confirmation scopes, callback serialization, final CODE expiry,
  stale tokens, wrong pages and the fresh final restoration reply.
- Two fresh ESP-IDF 6.0.2 / Xtensa 15.2.0 ESP32-S3 builds pass without warnings.
  Nine raw artifact pairs and 23 source-bound stack reports match. Both embed
  `ot0238c-buttonconfirmation-v1`; raw application is 646,304 bytes, privately
  padded with verified `0xff` bytes to the unchanged 733,184-byte capture span.
  A private default-OFF object also proves startup diagnostics are excluded.
- The first sandboxed configure failure is preserved. The path-only successor
  uses the retained host-access compiler launch; no SDK installation/change.
- Thirty focused private preparation tests pass with unchanged input pins.
  Latest v13 failed-trial restoration/owner confirmation is distinct from the
  older v11 missing-acknowledgement record; retained original backups are reused.

## Evidence and commands

Private evidence lives under `.private/ot0238c-button-confirmation-20261005/`:
`final-host-matrix.json`, `matrix-fixture-delta.json`,
`matrix-fixture-retest.json`, `focused-prestage-receipt.json`,
`firmware-inputs.json`, and `firmware/pair-result-hostaccess.json`.
Independent review and exact source/build GO records are under `firmware/`.
The final binding/plan reviews are under `prestage/`;
`owner-launcher-review.json` pins the single `Start Trail trial.cmd` launcher.

The matrix command is `C:\Python314\python.exe -X utf8 -B
.private/ot0238c-button-confirmation-20261005/final_host_matrix.py`; the receipt
owns exact suite commands, compiler invocations and the corrective phase.
Firmware uses `Run-ButtonConfirmationPairHostAccess.ps1` through the installed
PowerShell 7 runtime, followed by the retained audits. Focused preparation uses
`C:\Python314\python.exe -X utf8 -B
.private/ot0238c-button-confirmation-20261005/prestage/prestage_tests.py`.

## Next gate

The reviewed package is ready for fresh desktop readiness and exact
authorization for one device trial. No device,
Git publication, website capability or V1 progress credit changed in this repair.
