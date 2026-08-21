# Decision 0045: Accept the exact Monocypher source lock host-only

- Status: Accepted
- Date: 2026-08-20
- Work item: OT-102

## Decision

Accept strict append-only `OTMSLA0/v0`, raw SHA-256
`6dbeeac0266f9e6dd90265cdd71a721acfd36b4308dcb87180bd9d7c24c77e52`,
as the independent host-only admission of exact `OTCSLE0/v1` Monocypher 4.0.3
source evidence, SHA-256
`fe037820304103f7ca2253665076e4dc41740598ca9742ba8d45f6ec64ebc06f`.
The owner selects the upstream `BSD-2-Clause` project-license branch. This is
a project choice for the locked evidence, not legal clearance or a
compatibility determination.

The admitted evidence retains all 161 exact upstream files from commit
`ab2b16dd619ad5f6979a4fbe69cfa324a6fcc35f` and Git tree
`eccc366491fc98c4149401d580ce41081a7854b1`. Canonical acquisition,
full-tree, license, SPDX, transitive-dependency, zero-patch, and project-lock
evidence binds that immutable vendored-source boundary.

## Readiness accounting

OT-094 and OT-097 remain immutable historical six-blocker records. OT-100
records the prior five-blocker state after accepting libsodium. OT-102 closes
only `monocypher_source_lock_absent`, accepts exactly one Monocypher source
lock, and records four current unresolved requirements:

1. exact received target profile;
2. final candidate build configuration;
3. ESP-IDF mbedTLS/PSA dependency lock plus API/configuration eligibility; and
4. direct-radio MTU, PHY, and region.

Two source anchors are now accepted in total: the prior libsodium anchor and
this Monocypher anchor. Every API/configuration and candidate-import registry
remains empty. `OTCBR0` readiness remains blocked.

## Boundaries

No firmware import or build, crypto execution, device/flash/radio/key action,
benchmark, candidate or suite selection, packet-v1 authority, legal clearance,
compatibility determination, physical acceptance, continuing authority, or
score credit is granted.
