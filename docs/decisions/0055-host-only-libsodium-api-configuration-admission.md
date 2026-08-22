# Decision 0055: admit complete host-only libsodium API/configuration evidence

- Status: Accepted
- Date: 2026-08-22
- Scope: OT-005 benchmark phase 0 only

## Decision

Accept the exact OT-117 host-only libsodium operation bundle, candidate evidence,
and append-only `OTLAPIA0/v0` admission under the frozen `OTCAC0/v1` and
`OTCBX1/v1` contracts. The evidence binds Espressif libsodium 1.0.22 to its
accepted source lock and candidate-specific OT-107 sdkconfig and covers all eight
fixed operations. Noise XK is supplied by the separately hash-bound,
benchmark-only `OTNXK0/v0` composition for
`Noise_XK_25519_ChaChaPoly_SHA256`.

The accepted source/API-configuration/candidate-import counts advance from
`3/1/0` to `3/2/0`. Complete coverage makes libsodium structurally selection
eligible; it does not select the candidate or authorize execution.

## Evidence

- Operation bundle raw/canonical SHA-256: `b1daf950473ea9e86ddff16d8f28efbc0972582902d5b262686cf73d0907bf58` / `6419ac77392aa7b7f295cbda7719a16581a617e7564c2afec6c03aac7b2fea90`
- Candidate evidence raw/canonical SHA-256: `34888d71da2c9042856ea48c7b1225f21c1345582c144239cab0096ff03e69b5` / `6e5e969c3b3f7bf29372e15e3cc75c693fb00f57f7f297c14cc77123dec4610d`
- Admission raw/canonical SHA-256: `527c5e713a6d96d38b7dfbfb9d3e0ceb891f4652f8428e2ca65e0b8ea8f316d2` / `6eb12a9cf44bdc37e6ac87081b1d256b94f5ee013459c3549606c56b5faf0527`
- Noise XK header/source raw SHA-256: `b7c649434cdffe648e467bb117849ae0296a73fa041d614d3d4ba32578e40c45` / `8534fe1a6a4b68cd37e491ebd0f564dd38fd3935fb21d8f2d45aa8333ae442b8`

Two fresh configuration-only runs reproduce the exact accepted libsodium
sdkconfig. The bounded adapter host fixture passes four strict C11/C++20 groups
covering the 48/48/64-byte, 160-byte-total handshake and fail-closed rejection
and wiping behavior. The focused admission suite passes 10/10 adversarial groups.

## Boundaries

Phase 0 remains incomplete: Monocypher API/configuration evidence and the second
measurement node's exact-profile admission are still missing. Phase 1 retained
import/build admissions remain absent for every candidate, and fresh benchmark
execution authority is absent. No retained candidate import/build, benchmark,
device, flash, radio, production key or entropy operation, selection, secure-LoRa
implementation, Packet V1, support, compatibility, regulatory/range, physical,
or score claim is accepted.
