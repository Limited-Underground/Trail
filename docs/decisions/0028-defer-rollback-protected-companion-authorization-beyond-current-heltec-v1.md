# Decision 0028: Defer rollback-protected companion authorization beyond current Heltec V1

- Status: Accepted
- Date: 2026-08-18
- Scope: Current Heltec V1 product boundary

## Context

OpenTrail's companion-authorization design requires an independent monotonic rollback floor. The current Heltec V1 target has no accepted provider for that floor. ESP32-S3 `SECURE_VERSION` is reserved for firmware anti-rollback and is not suitable for companion-authorization generations. The custom on-chip eFuse candidate is not viable on the current target, and no external monotonic component is selected or present.

The repository contains build-tested authorization, protected-storage, normalized-metadata, recovery, and provider-evaluation foundations. Those artifacts establish design and build evidence only. They do not establish secure ownership, physical provisioning, a protected rollback floor, or an authorized runtime path on current hardware.

## Decision

Rollback-protected companion authorization is deferred beyond the current Heltec V1 target.

The feature is deferred, not complete, removed, failed, or silently weakened. Existing foundations remain as dormant historical and engineering evidence. They are not injected into the current runtime and do not authorize a physical operation.

Current Heltec V1 may continue as a development and field-prototype platform for bounded non-privileged functions. It must not be represented as a production-secure companion controller while the independent rollback floor remains unavailable.

## Controls that remain closed

The current Heltec V1 runtime must not expose authorization-dependent controls, including:

- secure ownership, trusted-phone, claim, replacement, transfer, revocation, or deauthorization workflows;
- provisioning or use of companion-authorization keys, group secrets, private-channel material, or a rollback-floor provider;
- BLE writes that alter radio region, groups, messaging identity, location sharing, persistent configuration, firmware, recovery state, or protected storage;
- privileged messaging, emergency/public-message transmission, administrative control, OTA/DFU, reset, recovery, or provisioning commands; and
- any runtime path that presents mobile operating-system BLE pairing as OpenTrail authorization.

Provider, descriptor, allocation, provisioning, device-read, write, burn, protection-change, runtime-injection, and execution authority remain closed. No external monotonic part is selected or present.

## Product and public claims

Current Heltec V1 must not claim:

- secure or rollback-resistant ownership;
- a trusted or authorized companion phone;
- provider-backed revocation, replacement, transfer, or recovery;
- production key custody or protected authorization state; or
- completion of the deferred authorization milestone.

The truthful public statement is:

> Rollback-protected companion authorization is deferred beyond the current Heltec V1 because the target has no accepted monotonic provider. The build-tested authorization foundations remain dormant. Current V1 work may continue on non-privileged device functions, but secure ownership, protected control, private provisioning, and provider-backed recovery are not current hardware capabilities.

## Progress accounting

This decision does not complete a V1 milestone and does not support a score increase or denominator reweighting.

- V1 exact weighted progress remains 39.75.
- V1 displayed progress remains 40.
- The historical baseline remains 31.75 exact and 32 displayed.
- V2 remains unmeasured.

Any later V1 scope or weighting change requires a separate owner-approved decision that preserves the historical measurements.

## Consequences

- Authorization-dependent runtime work remains blocked on current Heltec V1.
- Future protected authorization requires a separately accepted hardware architecture, provider, transaction model, provisioning route, and physical evidence.
- Functional firmware work may proceed only where it does not require or imply secure ownership or privileged control.
- A bounded read-only BLE link/status surface may be designed separately under Decision 0029.
- This decision creates no implementation, build, device, flash, live, or publication evidence by itself.
