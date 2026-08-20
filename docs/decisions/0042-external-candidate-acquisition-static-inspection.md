# Decision 0042: Record external candidate acquisition and static inspection

- Status: Accepted host-only acquisition evidence; source locks and readiness remain blocked
- Date: 2026-08-20
- Work item: OT-098

## Decision

OpenTrail accepts strict host-only `OTCAI0/v0`, audit ID
`OT-098-OT005-EXTERNAL-CANDIDATE-ACQUISITION-V0`, raw artifact SHA-256
`b7be03e305c6253e10f69f624132a736cce5aea3f559760cde4f948ae79abad6`,
and result:

`EXTERNAL-CANDIDATE-SOURCES-ACQUIRED-AND-STATICALLY-INSPECTED; ZERO-SOURCES-IMPORTED; ZERO-SOURCE-LOCKS-ACCEPTED; OTCBR0-READINESS-BLOCKED`

The exact official libsodium 1.0.22 and Monocypher 4.0.3 source trees were
acquired into the excluded private inspection area and inspected without
modification. The evidence binds their origins, refs, commits, trees, manifests,
license files, unresolved signature state, and fixed OT-005 operation matrix.
Libsodium exposes source paths for seven of eight operations; Monocypher exposes
five of eight. Neither source contains Noise XK composition.

## Boundary

The libsodium tag contains an unverified embedded signature because the public
key was unavailable. Monocypher uses a lightweight tag resolving to an unsigned
commit. Signature trust therefore remains unresolved. Libsodium records project
license choice ISC. Monocypher preserves upstream `CC0-1.0 OR BSD-2-Clause`
with project choice null. This is neither legal clearance nor a compatibility
determination.

Neither candidate has a complete SBOM or transitive-dependency inventory, an
OpenTrail project dependency lock, final candidate configuration, import,
build, benchmark execution, or selection. Static source presence is not
API/final-configuration eligibility and primitives are not Noise XK.

All six `OTCBR0/v0` blockers remain open. No continuing acquisition, import,
build, execution, device, radio, key/entropy, suite, packet-v1, physical, or
score authority is granted. Android remains 60%; V1 remains exact 43.75% and
displayed 44%; the historical baseline remains exact 31.75% and displayed 32%;
V1.5 and V2 remain unmeasured.

## Next gate

Create candidate-specific project dependency locks, deliberately resolve the
Monocypher project license choice, complete the required inventories, and
produce independently admitted `OTCSLE0/v1` evidence before closing either
external-candidate source-lock blocker. Import, build, benchmark execution, and
selection remain separately authorized.
