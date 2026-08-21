# Decision 0047: Accept the exact pinned ESP-IDF mbedTLS/PSA source and dependency lock host-only

- Status: Accepted
- Date: 2026-08-21
- Work item: OT-105

## Decision

Accept append-only `OTMPSLA0/v0`, raw SHA-256 `26b6acdc9928eb9510a0baed53c609a4f9a23288155636c6462747745f28ac85`, as the independent host-only admission of exact `OTCSLE0/v1` evidence, SHA-256 `ae12ad7da6702ac85092e9cb8ad793b749871153fadee8b1a276e5a46b036e49`, for the installed pinned ESP-IDF mbedTLS/PSA comparison candidate.

The owner-authorized project choice is `Apache-2.0`; the upstream expression is `Apache-2.0 OR GPL-2.0-or-later`. This is neither legal clearance nor a compatibility determination.

## Exact dependency boundary

The lock binds clean installed ESP-IDF v6.0.2 commit `7101770dc6db2667b3c477cc31365dd1acd6db4e`, tree `402f8035c2915b97713251ec036bd6afb457f9fd`, and component tree `22c8f32fe2cf02a128c8f7a39363ecf3f70fb9ff` to mbedTLS 4.1.0 gitlink commit `6cc42afad309e861f4c07e6f106e2ab14a9cb8e5`, tree `c8766facace97c13f9996d08638dc4ba52f66e4d`, including TF-PSA-Crypto tree `3f133cd7475b00c0f7e7e2f2548d5f64813c17b5`.

Metadata covers all 3,551 source and 198 component-glue files exactly once across full-tree, glue, license, SPDX, seven bundled-source partitions, patch, and project-lock records. Bundled partitions are not runtime/link-dependency evidence. The zero patch count means only zero OpenTrail-applied patches after the exact pinned Espressif gitlink. Espressif/upstream divergence is unassessed. No source was newly acquired or copied.

## Readiness accounting

History remains six requirements at OT-094/OT-097, five after OT-100, four after OT-102, three after OT-103, and three after OT-105. OT-105 closes none. Current blockers remain final candidate configuration; the composite mbedTLS/PSA dependency-lock plus API/configuration requirement; and direct-radio MTU/PHY/region.

Only the dependency-lock portion of the composite requirement is supplied. OT-096 remains the controlling 5/8 static result; Ed25519 sign/verify and Noise XK remain absent, and API/configuration eligibility is not proved. Accepted source/API-configuration/import counts are `3/0/0`. Readiness and the historical draft-blocked plan remain blocked.

## Boundaries

No source acquisition/copy, import, build/configuration, API eligibility, crypto execution, device/flash/radio/key action, benchmark, selection, packet-v1 authority, legal/compatibility/support claim, physical evidence, continuing authority, or score credit is granted.
