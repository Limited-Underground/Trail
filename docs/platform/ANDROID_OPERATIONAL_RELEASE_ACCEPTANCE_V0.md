# Android Operational Release Acceptance v0

Status: plan contract accepted; execution blocked

Schema: `OTAR0/v0`

Work item: `OT-086`

## Purpose

This contract defines the evidence required before the Limited Underground
Trail Android application can satisfy the operational/release-acceptance gate
in V1 Companion. It is an admission boundary, not a release checklist whose
unchecked items may be waived during a run.

The canonical plan is
[`OT-086-ANDROID-OPERATIONAL-RELEASE-PLAN-V0.json`](../../tests/release-plans/OT-086-ANDROID-OPERATIONAL-RELEASE-PLAN-V0.json).
The deterministic validator is
[`android_release_admission.py`](../../tools/android_release_admission.py).

## Evidence-set rule

One release candidate must bind all accepted evidence to the same immutable:

- application ID, version name/code, build variant, artifact format, byte
  length, and SHA-256 digest;
- source revision and reproducible build/toolchain declaration;
- signer public identity and selected distribution scope;
- acceptance-plan revision and supported Android/API/device matrix; and
- nonzero evidence-set identity and bounded observation dates.

Evidence from different candidates, variants, signers, channels, plans, or
stale runs cannot be combined. A corrected candidate starts a new evidence set;
failed evidence is retained as failure history rather than silently discarded.

## Required admission domains

### Package and supply chain

- The production application ID and release version are exact and not marked
  debug, test, development, snapshot, or prerelease unless the whole run is
  explicitly ineligible for release acceptance.
- The candidate artifact is immutable and independently checked for exact size
  and SHA-256.
- The production manifest and packaged contents contain no instrumentation,
  exported test/debug component, debug-only dependency, test runner, private
  path, credential, or unexpected permission.
- The exact authored permission set is
  `android.permission.BLUETOOTH_CONNECT`,
  `android.permission.BLUETOOTH_SCAN`,
  `android.permission.FOREGROUND_SERVICE`,
  `android.permission.FOREGROUND_SERVICE_CONNECTED_DEVICE`, and
  `android.permission.POST_NOTIFICATIONS`.
- The exact current toolchain-generated merged permission is
  `io.github.nbjelanovic.otclient.DYNAMIC_RECEIVER_NOT_EXPORTED_PERMISSION`.
  It is tracked separately from the five authored permissions and does not
  expand application intent. A future candidate must independently verify the
  merged production artifact contains exactly the authored/generated union,
  with neither omissions nor additional permissions; source-manifest review
  alone is insufficient.
- Backup remains disabled and data-extraction/backup rules are inspected from
  the packaged manifest, not inferred from source alone.
- The signer is identified through a public digest or certificate identity.
  Private keys, passwords, recovery codes, tokens, account identifiers, and
  credential paths are structurally forbidden public-plan content.
- The only current distribution scope is `private-sideload-v1-pilot`, with one
  exact rollback/removal route frozen before execution. It permits controlled
  private installation only on four V1 pilot phones that must each be approved
  and frozen before execution. Local build output alone is not distribution
  evidence, and private sideload acceptance is neither public distribution nor
  a store-release claim.
- Google Play distribution is outside this contract. Admitting it later
  requires a separate current-policy review of Play's target-API requirement
  and any required Android toolchain and target-SDK update; OT-086 does not
  claim that the present toolchain satisfies a future Play policy.

### Supported Android boundary

- Operational BLE support begins at Android 12/API 31. Earlier min-SDK build or
  fake/UI evidence does not establish real BLE support.
- Each claimed Android/API/device class has an exact role in the test matrix.
  An emulator cannot replace physical BLE, lifecycle, notification, battery,
  thermal, or usability evidence.
- The plan distinguishes supported, tested-only, and explicitly unsupported
  combinations. No untested device class is inferred from a nearby version.

### Lifecycle and permission matrix

The same candidate must pass every applicable branch:

1. clean install and first cold start;
2. for this first supported release, exact upgrade mode
   `first-release-not-applicable`, which earns no acceptance credit, plus
   uninstall/reinstall, promised app-data removal, and explicit downgrade
   rejection/no-downgrade handling;
3. foreground, background, process recreation, app reopen, and device reboot;
4. Nearby Devices grant, denial, later revocation, and recovery;
5. notification grant and denial while connected-device foreground-service
   behavior remains truthful and visible;
6. Bluetooth unavailable/off/on and permission-loss recovery without fake
   fallback or a stale `Ready` state;
7. explicit disconnect/cleanup, uninstall, and verification of the promised
   app-data removal boundary; and
8. failure injection with an observable, privacy-safe result and no hidden
   authority or retry loop.

Missing, skipped, crashed, timed-out, discarded, or contradictory cases deny
the complete gate.

A real in-place upgrade from the immediately prior supported release is
mandatory for the next supported release and every later release for which a
prior supported version exists. The first-release exception cannot be carried
forward or credited as upgrade evidence.

### Human and operational acceptance

- Accessibility review covers screen reader labels, keyboard/switch navigation
  where applicable, text scaling, contrast, touch targets, and visible error/
  recovery copy on the supported matrix.
- Usability evidence covers the complete intended non-privileged workflow with
  no operator interpretation of developer logs.
- Battery, thermal, background stability, and bounded endurance observations
  use a frozen duration and pass threshold.
- Privacy inspection covers logs, notifications, screenshots, backups, recent
  tasks, crash output, app storage, and public evidence. Device addresses,
  phone identifiers, owner bindings, coordinates, keys, group/channel material,
  pairing secrets, tokens, and private paths are excluded.
- Support and recovery documentation identifies installation source, supported
  versions, known limitations, upgrade/rollback/removal steps, and a user-
  visible failure path.

## Outcomes

`PLAN-ACCEPTED-EXECUTION-BLOCKED`

: The plan is canonical and structurally valid, but one or more named
  prerequisites are absent. This is the only successful OT-086 planning result.

`DENY`

: The plan or supplied evidence is malformed, contradictory, stale, mixed,
  secret-bearing, overclaiming, or requests authority that the contract does
  not grant.

`PASS`

: Reserved for a later complete release evidence evaluator. The OT-086 plan
  validator cannot emit this outcome.

No partial percentage is awarded within the fifth Android gate.

## Current blockers

OT-087 ([Decision 0031](../decisions/0031-android-unsigned-release-build-foundation.md),
[evidence](../../tests/hardware/OT-087-2026-08-19.md)) freezes version code/name
`1` / `1.0.0`, configures an explicit
non-debuggable release build type without signing, and accepts packaged
inspection of one disposable unsigned local APK. This satisfies exactly the
production-variant and stable-version prerequisites. It is not an accepted,
immutable, signed, reproducible, installable, or distributable release
candidate, and it earns no partial fifth-gate credit.

OT-088 ([Decision 0032](../decisions/0032-android-private-pilot-operational-policy-freeze.md),
[policy](ANDROID_PRIVATE_PILOT_OPERATIONAL_POLICY_V0.md),
[evidence](../../tests/hardware/OT-088-2026-08-19.md)) freezes the offline and
transient privacy/data-safety promise, first-release removal/no-downgrade route,
and bounded best-effort private-pilot support policy. This satisfies exactly
three additional policy prerequisites, not their later execution checks.

The checked-in plan remains blocked on three exact prerequisites: physical
acceptance matrix, release identity, and signer/custody. Full lifecycle,
privacy, operational endurance, usability, rollback, support, and coherent
cleanup evidence also remain unexecuted. The
distribution scope is frozen as
`private-sideload-v1-pilot`; freezing that scope supplies no package, signing,
installation, support, or release evidence.

The current unsigned release-build foundation, Android debug build, and
OT-085A/OT-085B instrumentation remain engineering or bounded physical-public-
BLE evidence. None is an eligible release artifact.

## Separate V1 gates

Decision 0028 keeps protected one-phone authorization and `Ready` unavailable
on the current Heltec target. Passing a future operational-release candidate
must not imply those capabilities. V1 completion still requires resolving that
separate gate and accepting a Companion-specific four-device/phone field pilot.
