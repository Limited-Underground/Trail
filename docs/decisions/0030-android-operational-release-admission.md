# Decision 0030: Android operational-release admission

- Status: Accepted for plan-only implementation
- Date: 2026-08-19
- Work item: OT-086
- Scope: V1 Companion Android operational-release evidence gate

## Context

Decision 0017 defines complete operational/release acceptance as one of five
equal Android evidence gates. OT-085A and OT-085B close only the separate
physical public-GATT gate. The Android application remains a development build
with no accepted production variant, signer, private-sideload execution result,
supported-device matrix, release package, lifecycle result, or support
boundary.

Decision 0028 independently defers rollback-protected one-phone authorization
and `Ready` beyond the current Heltec V1 target. Operational-release planning
must neither erase that open gate nor represent the non-privileged prototype as
a production-secure companion.

Running ad hoc signing, installation, store, account, or physical acceptance
steps before their evidence and stop conditions are fixed would create an
ambiguous result. OpenTrail therefore requires a machine-checkable admission
plan before any release candidate can be accepted.

## Decision

OT-086 accepts `OTAR0/v0`, the OpenTrail Android operational-release admission
contract, and one checked-in plan at
`tests/release-plans/OT-086-ANDROID-OPERATIONAL-RELEASE-PLAN-V0.json`.

The plan freezes the required evidence classes and records the current missing
prerequisites. A structurally valid incomplete plan has exactly one successful
planning outcome:

`PLAN-ACCEPTED-EXECUTION-BLOCKED`

That outcome means only that the later release run is bounded. It is not an
application, artifact, signer, distribution, installation, operational,
support, or V1 release pass.

The contract requires exact evidence for:

- application identity, version, build variant, artifact format, byte length,
  SHA-256 digest, manifest, permissions, and absence of debug/test surfaces;
- signer identity, custody, rotation/revocation, and reproducible public
  verification without placing a key, password, token, or credential in the
  plan or repository;
- the single selected `private-sideload-v1-pilot` distribution scope and its
  rollback/removal procedure;
- a supported Android/API/device matrix, with production BLE acceptance limited
  to Android 12/API 31 or later even though host/UI foundations may build for an
  earlier API;
- clean install, the first-release upgrade boundary, cold start, foreground/
  background, process recreation, notification grant/denial, permission loss/
  recovery, Bluetooth off/on, reboot, uninstall, and app-data removal;
- accessibility/usability, privacy and retained-data inspection, backup
  exclusion, battery/thermal/endurance, recovery, support, and public evidence;
  and
- explicit pass, deny, blocked, cleanup, and stale/mixed-evidence rules.

Unknown fields, duplicate JSON keys, unsupported schema/version, missing
requirements, contradictions, overclaiming, private material, or a request for
device/store/signing authority fail closed.

`private-sideload-v1-pilot` is the only distribution scope admitted by this
plan. It means controlled private installation of one accepted candidate on
four V1 pilot phones that must each be approved and frozen before execution;
it is not public distribution, a store release, or supported-device evidence
by itself. A future Google Play scope requires a separate decision and plan, a
fresh check of the then-current Play target-API policy, and any required
Android toolchain and target-SDK update before admission. OT-086 makes no Play-
policy-compliance claim.

`OTAR0/v0` governs the first supported release, so its in-place-upgrade mode
is exactly `first-release-not-applicable`. That status earns no acceptance
credit. The first release still must pass uninstall/reinstall, promised data
removal, and explicit downgrade rejection/no-downgrade handling. A real in-
place upgrade from the immediately prior supported release becomes mandatory
for the next supported release.

## Authority boundary

OT-086 grants no authority to:

- create, import, unlock, export, rotate, or use a signing key;
- access a store or developer account, upload or distribute a package, or
  change a public listing;
- build or claim a production artifact, install or remove an app, change phone
  settings, or access Bluetooth or another device;
- pair, claim, authorize, provision, enable `Ready`, write a protected
  characteristic, operate LoRa/GNSS, or access the current Heltec target; or
- declare a supported phone, target, release, or field configuration.

Each later physical, signing, distribution, or account operation requires its
own exact prerequisites and owner authorization. Secret-bearing evidence must
remain outside public records and must never be accepted merely because it was
redacted after collection.

## Acceptance and progress

OT-086 is complete when the decision, operational contract, canonical plan,
validator, deterministic denial tests, and repository gates pass. The evidence
record must say plan accepted and execution blocked.

The Android milestone remains 60%. V1 Companion remains exact 43.75% and
displayed 44%. A later release candidate may close the fifth Android gate only
after every required package, lifecycle, distribution, privacy, usability,
support, and cleanup result is accepted as one coherent evidence set.

Protected one-phone authorization/`Ready` and the four-pair Companion field
proof remain separate mandatory V1 gates. This decision does not reweight,
remove, or weaken either one.
