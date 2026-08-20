# Decision 0044: Accept the exact libsodium source lock host-only

- Status: Accepted
- Date: 2026-08-20
- Work item: OT-100

## Decision

Accept strict append-only `OTCSLA0/v0`, raw SHA-256
`df595f2d07ba1b5d0a9bdf70237b1f0ea5a01fe8cb5a63ffb3575fe484faede0`,
as the independent host-only admission of the exact Espressif libsodium 1.0.22
source evidence recorded by OT-099. It binds unchanged OT-097 policy and OT-099
evidence, accepts exactly one `espressif_libsodium` source anchor, and leaves
the mbedTLS/PSA and Monocypher source registries plus all API/configuration and
candidate-import registries empty.

## Readiness accounting

OT-094 and OT-097 remain immutable historical six-blocker records. This closes
only `espressif_libsodium_source_lock_absent`; the current unresolved set is
exact received target profile, final candidate build configuration, mbedTLS/PSA
dependency lock and API/configuration eligibility, Monocypher source lock, and
direct-radio MTU/PHY/region. `OTCBR0` readiness remains blocked.

## Boundaries

No API/configuration or import acceptance, exact-target or final-configuration
proof, device/flash/radio/key/crypto execution, benchmark, selection, packet-v1
authority, legal clearance, compatibility determination, physical acceptance,
continuing authority, or score credit is granted.
