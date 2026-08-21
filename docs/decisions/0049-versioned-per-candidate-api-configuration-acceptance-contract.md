# Decision 0049: Freeze the versioned per-candidate API/configuration acceptance contract host-only

- Status: Accepted
- Date: 2026-08-21
- Work item: OT-108

## Decision

Accept append-only `OTCAC0/v1`, canonical SHA-256 `ccdb11e19031a4a01c717e30c172332c5e71ca79ee2b36b721277e55ca9a6c22` and raw SHA-256 `575e8747cdba643f2abb6f3252a62adbf3f12a9faaa46ca1d4bf4ce8bd9d23f3`, as the host-only successor boundary for per-candidate API/configuration evidence.

The contract preserves historical `OTCBR0/v0`, `OTCSL0/v0-v1`, and OT-094 through OT-107 artifacts byte-for-byte. It binds each candidate separately to its accepted source evidence, source admission, and OT-107 generated sdkconfig digest. The obsolete one-common-sdkconfig assumption is forbidden in the successor boundary.

## Coverage and selection boundary

Future `OTCAPI0/v2` evidence must preserve the fixed eight-operation order and record every operation as either `eligible` with a purpose-distinct evidence digest or `unavailable` with no digest. Complete coverage may be structurally selection-eligible, but does not authorize selection. A strict nonempty partial set is permitted only for a comparison candidate, remains measurable only for its evidenced operations, and is structurally nonselectable.

All API/configuration evidence registries remain empty. Independently accepted evidence is still required before admission. No self-authored evidence or contract mutation can add acceptance.

## Readiness accounting

Accepted source/API-configuration/candidate-import counts remain `3/0/0`. The current blockers remain:

1. `esp_idf_mbedtls_psa_dependency_lock_and_api_config_unresolved`
2. `direct_radio_mtu_phy_region_unresolved`

OT-096 remains the controlling 5/8 static mbedTLS/PSA assessment, `OTCBR0/v0` readiness remains blocked, and the historical `OTCB0/v0` plan remains `draft_blocked`.

## Boundaries

This decision freezes only a corrected evidence and readiness-admission contract. It generates or accepts no candidate API/configuration evidence, imports or compiles no candidate, runs no benchmark, accesses no device, performs no radio/key/entropy operation, selects no candidate/suite/wire format, authorizes no packet v1 behavior, and adds no support, compatibility, regulatory, physical-evidence, continuing-authority, or score claim.
