# Decision 0032: Android private-pilot operational policy freeze

- Status: Accepted for policy-only implementation
- Date: 2026-08-19
- Work item: OT-088
- Scope: V1 Companion Android private-sideload release-plan prerequisites

## Context

Decision 0030 accepted the fail-closed `OTAR0/v0` release-plan boundary, and
Decision 0031 satisfied only its production-variant and stable-version
prerequisites. Privacy/data-safety, rollback, and support remained unspecified,
so a later release run could not make one coherent promise about collection,
retention, cleanup, downgrade handling, supported users, or failure reporting.

Those policies can be frozen without selecting a phone, signer, certificate,
release identity, or distributable artifact and without exercising any device,
installation, signing, account, upload, or distribution authority.

## Decision

OpenTrail accepts
[`OT-088-ANDROID-PRIVATE-PILOT-OPERATIONAL-POLICY-V0`](../platform/ANDROID_PRIVATE_PILOT_OPERATIONAL_POLICY_V0.md)
for the existing `private-sideload-v1-pilot` scope.

The privacy/data-safety promise is offline, account-free, and transient. The
policy permits no collection, sharing, advertising, analytics, remote
telemetry, crash upload, phone-location use, cloud sync, or backup/transfer.
Private runtime
values must not be persisted or published; fixed notification text remains
public and non-identifying. Logs, notifications, recent tasks, screenshots,
crash output, storage, backup/transfer, and cleanup still require physical
verification on the accepted candidate and matrix.

The first-release rollback policy is removal, not proof that removal has run. A failed candidate must
disconnect, stop its service, uninstall, prove package/app-data absence, restore
only run-created Android bond state, and either stop or reinstall the same
accepted version, digest, and signer from the frozen private source. No older,
debug, unsigned, differently signed, or rebuilt APK is a rollback target.

Support begins only after a complete OTAR pass and applies only to version
`1.0.0`, four approved pilot phones, the exact later-accepted matrix, and an
owner-controlled hash-and-signer-verified APK. It is best-effort with no service
level or safety/rescue guarantee, uses an owner-provided private-pilot channel,
and ends at owner revocation or a superseding accepted release. Sensitive
reports follow `SECURITY.md`. Public/store distribution, iPhone, unlisted devices,
secure ownership, protected control, `Ready`, and field/production claims remain
outside the policy.

This satisfies exactly three more plan prerequisites. Five of eight are now
satisfied; physical matrix, release identity, and signer/custody remain blocked.
The plan therefore remains `PLAN-ACCEPTED-EXECUTION-BLOCKED`, the release gate
remains `NOT-EVALUATED`, and execution authority remains false.

## Authority boundary

OT-088 grants no authority to create, inspect, import, unlock, export, rotate,
or use a signing key or certificate; select or approve a phone/device matrix or
release identity; build an accepted release candidate; install/remove an app;
change phone or Bluetooth settings; access hardware, accounts, stores, or
upload surfaces; or distribute a package.

It does not approve protected authorization, `Ready`, LoRa/GNSS operation,
supported hardware, field use, public distribution, or production readiness.
Each later operation retains the separate authorization requirements from
Decision 0030.

## Acceptance and progress

OT-088 is accepted when the operational policy, canonical plan, validator and
denial tests, repository gates, evidence, and public project records agree on
the same policy-only result.

No partial percentage is awarded within the fifth Android evidence gate.
Android remains 60%, and V1 Companion remains exact 43.75% and displayed 44%.
The complete candidate-bound lifecycle, privacy, accessibility, usability,
endurance, rollback, recovery, support, and cleanup evidence set remains open.
