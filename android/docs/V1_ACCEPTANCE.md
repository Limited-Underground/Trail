# Existing Android V1 criteria and next step

[Android reading path](../README.md) · [Existing remaining-work plan](../../tasks/V1_REMAINING_PLAN.md)

This is a map of already-agreed criteria, not a new acceptance plan. Dedicated
Projects own live status/dependencies; the [backlog](../../tasks/BACKLOG.md),
[remaining plan](../../tasks/V1_REMAINING_PLAN.md), [V1/V1.5 scope](../../docs/testing/V1_V1_5_ACCEPTANCE_SCOPE_V0.md)
and original accepted decisions retain their wording and graph. Table rows are
unfinished boundaries/evidence limits, not newly polled task statuses.

| Existing tasks | Retained required result | Current limit |
| --- | --- | --- |
| OT-0005i / OT-0005j; OT-0237e and enrollment dependencies | Selected production secure transport/enrollment reaches protected BLE commands/events and both phone UIs. | Fixed-status unauthenticated diagnostic success does not complete it. |
| OT-0300 | Retention/sharing/cleanup promises agree with later accepted ownership/group/block/consent policy. | Reconciliation accepted; this does not accept every durable adapter or physical privacy case. |
| OT-0301–OT-0304 | Ordered resumable setup with protected name/region/public-profile readbacks; exact-peer reset recovery; complete first-use/launch on supported phones. | Bounded host and historical name/time/reconnect evidence are not full production physical acceptance. |
| OT-0305–OT-0308 | Authenticated durable group profile (one group/person, one admin, six members), invitation/join, administration/leave and interruption/revocation consistency. | Models/local review are not the completed transport/persistence composition; stale/revoked invitations and a seventh member must be rejected. |
| OT-0309–OT-0311 | Authenticated bounded visibility/discovery, accepted direct-contact consent and persistent block/unblock. | No user content before consent; a UI model cannot prove enforcement after restart. |
| OT-0312–OT-0314 | Bounded authorized typed/quick messages and templates; queue/TX/ACK/retry/deduplication/restart events rendered truthfully. | Queued requires local device acceptance; Sent radio acceptance; Delivered authenticated recipient ACK. No read receipt or diagnostic-ACK substitution. |
| OT-0315–OT-0317 | Informed group sharing, direct-chat OFF default, immediate stop and authenticated device location with truthful time/age/current/stale/unavailable details. | No silent reactivation, phone-location substitution or built-in-map prerequisite. |
| OT-0318–OT-0319 | Populated responsive portrait/landscape UI, retained non-destructive work and physical usability/accessibility. | Host rendering/rotation models do not accept complete supported-phone workflows. |
| OT-0320–OT-0321; OT-0177a | User-reviewed support Save/Share, format-3 V1-Test diagnostics (512 records/64 KiB), Clear and production test-surface exclusion. | Schema/validator proof is not complete actual privacy/export/Clear evidence. |
| OT-0322–OT-0323; OT-0086a | Frozen API/device matrix, identity/signer/custody/removal route and audited immutable signed pilot artifact. | Source minSdk 26 is not operational admission; existing API31 minimum BLE boundary remains. Unsigned payload equality is not this gate. |
| OT-0324–OT-0327 | Exact candidate installation/removal/reinstall and no-downgrade route, lifecycle/permissions/Bluetooth recovery, endurance/thermal/privacy and complete OTAR evaluation. | First-release upgrade is not applicable and earns no credit. Missing/skipped/mixed/stale cases deny; historical plan-only validator cannot issue release PASS. |
| OT-0328–OT-0331; OT-0089a/e/f/g | Freeze coherent signed two-pair set; authenticated encrypted bidirectional delivery/replies; rejection/interruption/recovery; independent release reconciliation and separately authorized publication. | Requires the existing firmware/security/reset/target and Android prerequisites. Diagnostic success does not close protected V1. |

The original dependency graph controls ordering; ranges above only group related
reading. Optional Server, Display, Tracker, Console, Repeater, Gateway, maps,
V1.5, phone-location source and iPhone are not added as V1 prerequisites. Existing
owner-deferred numeric-keypad and cold-power-disassembly work stays deferred.

## Continue under the current freeze

Use the [current handoff](../../docs/PROJECT_STATUS.md#current-recovery-handoff)
and [Android App Project #8](https://github.com/orgs/Limited-Underground/projects/8)
for live task state, ownership and the existing dependency gates. Under the
[working contract](../../CONTRIBUTING.md#working-contract), the current Android
host task is OT-0305-1. Its [dormant versioned projection](OT-0305-1-DURABLE-GROUP-SNAPSHOT.md)
passed 37 focused host tests; real authenticated reader/actor admission,
production transport and protected persistence remain unimplemented/unverified.
This reading map does not mark the task Done or admit a physical candidate.

Preserve the live task's recorded dependencies and source/criteria. Shared production admission remains
under OT-0237e before final selected transport binding; coherent physical and
release execution remain separately authorized. Preserve the qualified source
and original artifacts, and report private-source access gaps rather than
substituting current public main. No new question, algorithm choice, gate or
percentage is introduced by this map.
