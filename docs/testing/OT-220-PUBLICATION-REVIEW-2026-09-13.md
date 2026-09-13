# OT-220 publication review

The user requested publication of accumulated OpenTrail work after OT-219.
The coherent boundary is the final OT-200 through OT-219 snapshot, together with
the preceding local OT-199 restoration closeout. Reconstructing individual
historical commits would introduce intermediate states without improving review.

## Included scope

- Input synchronization, receipt and invitation evaluation targets/operators,
  deterministic tests and accepted build/physical evidence.
- Android Group confirmation codec/coordinator/UI and device confirmation owner,
  explicitly experimental protected-BLE integration, and host/build evidence.
- Exact-span BLE custody/restoration, phone admission and the OT-219 failed
  candidate observation with independently verified original recovery.
- Status, backlog, progress records, source-byte attributes and host test wiring.

This does not claim physical BLE confirmation, two-node membership, radio or
release acceptance. OT-219 never reached candidate protected Ready. The same
updated phone/bond reached Ready before and after original restoration. See the
[physical result](OT-219-BLE-PHYSICAL-TRIAL-2026-09-13.md).

## Publication safeguards

Review covered 253 initially pending public files. Private grants, captures,
runtime capsules, raw device identifiers, APK copies and phone traces remain
excluded under `.private/` and `build/`; neither tree has tracked content.
Recorded absolute workspace paths are provenance metadata, not credentials.
Hash-bound reports were preserved rather than cosmetically rewritten.

Git attributes preserve the exact bytes of affected hash-authoritative Android
sources, runner and evidence reports. Existing 148 source pins, 21 firmware
artifacts and four APKs were reverified without rebuilding.

The publication scanner's upstream-attribution exception admits only the exact
OT-209 notices path and immutable full-content SHA-256
`b3297ea5a6fada91dda74c1470d37c48a065695385c67fa5dfab8b51198fb6c3`.
The 794591-byte notices bundle matches its accepted inventory. The exception
applies only to copyright email attribution; changed content, copied paths,
appended personal emails and private keys are rejected. Other detectors remain
active. The notice bytes and source inventory were not edited. A synthetic test
address is generated explicitly from fixture bytes.

## Validation and publication boundary

Publication scanner scenarios and the full content scan pass. Focused operator
and transport suites pass (11 and 20 tests); repository documentation checks and
17 documentation tests pass. Existing firmware/Android acceptance is retained,
including its physical limitations. The configured PR gates remain **Repository
documentation** and **Windows host matrix**; neither may be bypassed.

This document records review scope, not a claim that a push or merge succeeded.
The workspace project handoff records the observed publication outcome and
remote commit after the configured checks. V1 completion and public website
status are unchanged; no website or Firmware-Loader work is included.
