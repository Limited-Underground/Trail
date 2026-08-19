# Decision 0031: Android unsigned release-build foundation

- Status: Accepted for build-only implementation
- Date: 2026-08-19
- Work item: OT-087
- Scope: V1 Companion Android operational-release plan prerequisites

## Context

Decision 0030 accepts the `OTAR0/v0` operational-release admission contract
and canonical plan while execution remains blocked. Two of its eight named
prerequisites were purely source/build foundations: an explicit production-
shaped release variant and one stable frozen candidate version. Neither
requires a signer, phone, device, account, installation, or distribution
operation.

The owner authorizes OT-087 to change source and run one local unsigned release
build with version code `1` and version name `1.0.0`. This authority does not
extend to signing or execution of the private-sideload acceptance plan.

## Decision

OpenTrail freezes the Android base version as code `1` / name `1.0.0`. Debug
builds append `-dev`. The explicit release build type is non-debuggable, not
JNI-debuggable, unminified for this first inspection boundary, and has no
signing configuration.

The standard Android foundation gate now runs protocol tests, debug and release
application unit tests, debug and release warning-as-error lint, debug
assembly, debug instrumentation assembly, and unsigned release assembly. A
separate bounded inspector must check the packaged APK rather than infer from
source:

- exact application ID, version, minimum SDK, and target SDK;
- exact authored plus toolchain-generated permission union;
- non-debuggable and non-test-only manifest state, backup exclusion, and both
  complete data-extraction exclusion branches;
- absence of instrumentation and the OT-085 debug-only helper classes;
- every DEX checksum and absence of v1 signature entries; and
- an exact Android build-tool result that the APK does not verify as signed.

The local APK is disposable engineering output. One build is not reproducible-
build evidence, and its size or digest does not make it the coherent immutable
candidate required by the later release gate.

The canonical plan retains schema `OTAR0/v0`, its existing plan ID, and outcome
`PLAN-ACCEPTED-EXECUTION-BLOCKED`. OT-087 satisfies exactly
`production_variant_not_configured` and `release_version_not_frozen`. The
remaining blockers are physical acceptance matrix, privacy/data-safety,
release identity, rollback policy, signer/custody, and support policy.

## Authority boundary

OT-087 grants no authority to create, import, unlock, inspect, export, rotate,
or use a signing key or certificate; install or remove an app; access a phone,
Bluetooth device, Heltec target, account, store, or upload surface; or
distribute a package. It does not approve the working release identity,
supported devices, privacy/data-safety promise, rollback promise, support
promise, protected authorization, `Ready`, or field operation.

## Acceptance and progress

OT-087 is accepted when the configured release source, updated canonical plan,
fail-closed tests, full Android gate, packaged-artifact inspection, cleanup, and
public evidence agree on the bounded unsigned result.

The fifth Android gate has no partial credit. Android remains 60%, and V1
Companion remains exact 43.75% and displayed 44%. A future release run must
still satisfy all six blockers and execute every package, lifecycle, privacy,
usability, support, recovery, and cleanup case as one separately authorized
coherent evidence set.
