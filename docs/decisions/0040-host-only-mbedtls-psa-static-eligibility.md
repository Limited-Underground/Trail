# Decision 0040: Record host-only mbedTLS/PSA static eligibility

- Status: Accepted bounded static assessment; source lock and readiness remain blocked
- Date: 2026-08-20
- Work item: OT-096
- Scope: Read-only provenance, source/API presence, and configuration eligibility for the already-installed pinned ESP-IDF mbedTLS/PSA comparison

## Decision

OpenTrail accepts the machine-readable
[`OTCMSE0/v0` assessment](../../tests/benchmarks/crypto/OT-096-OT005-MBEDTLS-STATIC-ELIGIBILITY-V0.json)
with audit ID `OT-096-OT005-MBEDTLS-STATIC-ELIGIBILITY-V0`, canonical SHA-256
`3034da5a9f21ed663f82dc45ba976f8b5d6ec4ff353c2f96a3d5de4b586c013e`,
status `fixed_operation_set_ineligible_host_only`, and public result:

`MBEDTLS-STATIC-ELIGIBILITY-FROZEN-HOST-ONLY; FIXED-OT005-OPERATION-SET-INELIGIBLE; OTCBR0-BLOCKER4-REMAINS-OPEN`

The audit binds unchanged OTCB0, OTCBL0, OTCBR0, and OTCSL0 digests. Its scope
is the clean, already-installed ESP-IDF v6.0.2 commit
`7101770dc6db2667b3c477cc31365dd1acd6db4e`, whose mbedTLS 4.1.0 gitlink is
`6cc42afad309e861f4c07e6f106e2ab14a9cb8e5`. OT-096 acquired/imported no
source. Upstream license is `Apache-2.0 OR GPL-2.0-or-later`; project choice
is `Apache-2.0`. No complete OTCSL0 license inventory or project lock is
claimed.

## Result and boundary

Concrete source/API paths are present for five of eight fixed operations:
X25519, SHA-256, HKDF-SHA-256, and ChaCha20-Poly1305 encrypt/decrypt. Concrete
Ed25519 sign/verify and Noise XK implementations are absent. Generic PSA APIs
and EdDSA/TLS identifiers are not implementation evidence.

Final configuration is unproven for every operation. Defaults and OT-093's
pre-selection sdkconfig are not final configuration evidence. The complete set
is ineligible in this pinned source, and blocker
`esp_idf_mbedtls_psa_dependency_lock_and_api_config_unresolved` remains open
atomically. This is not a benchmark failure, global suitability claim, or
permanent rejection.

Focused validation passes 17 scenario groups. No source lock, import, final
configuration, benchmark, selection, support, implementation, physical evidence,
authority, or score is accepted. All six blockers remain open; OTCB0 stays
`draft_blocked`. Android remains 60%; V1 exact 43.75%/displayed 44%;
historical baseline exact 31.75%/displayed 32%; V1.5/V2 remain unmeasured.

## Next gate

A later candidate needs an accepted dependency lock/license inventory, exact
final configuration, and the complete operation set. All six requirements must
close before a new executable plan and separately authorized benchmark.
