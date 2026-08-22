# Decision 0056: admit partial host-only Monocypher API/configuration evidence

- Status: Accepted
- Date: 2026-08-22
- Scope: OT-005 benchmark phase 0 only

## Decision

Accept the exact OT-118 host-only Monocypher operation bundle, candidate
evidence, and append-only `OTMAPIA0/v0` admission under the frozen `OTCAC0/v1`
and `OTCBX1/v1` contracts. The evidence binds Monocypher 4.0.3 under the
owner-selected `BSD-2-Clause` branch to its accepted source lock, both exact
OT-107 source requirements, and the candidate-specific generated sdkconfig.

Five of eight fixed operations are admitted: Ed25519 sign and verify, X25519,
and ChaCha20-Poly1305 encrypt and decrypt. SHA-256, HKDF-SHA256, and Noise XK
remain unavailable and have no operation-evidence digest. `OTMAPI0/v0` is a
separately hash-bound benchmark-only adapter over the accepted Monocypher API;
it does not add the three absent operations.

Monocypher is a comparison candidate, so the strict five-of-eight subset is
measurable only for those five operations and remains structurally
nonselectable. Accepted source/API-configuration/candidate-import counts advance
from `3/2/0` to `3/3/0`. This admission does not select a candidate or authorize
execution.

## Evidence

- Operation bundle raw/canonical SHA-256: `3a5f21eecf3be83b4259282c42f68d3c24780a9269bb0993ccfaeec648a2eb8d` / `e8be5eade8699b51f6d5dee42a8ca85b05d15ac8119e9c2ddccd700a542aa852`
- Candidate evidence raw/canonical SHA-256: `f158c2b33dd9a6bbcdf6b13f396e831703184eeeb623914e400bf10d9929c009` / `c1ce6c0de2a72852359fa15efd9e27d9ffd15171362ab6adc4d04f66825949e9`
- Admission raw/canonical SHA-256: `9fbecf19b206b31fae948b6bc7e7aa4e206ba26aa59b94fb7f07d4e1d300810a` / `df412285515fe29525b0bfd7cba45fd7ccd9a3d601be284242886e8adb19fec9`
- Benchmark adapter header/source raw SHA-256: `110f5b54cf37538e30450d73a3402807eb59143f2ceb528b12280b7725e52072` / `e12b800841c6c8347cdf08d05768f2cfbc83ee271fdae7616f8a3b16e4263e59`

Two fresh configuration-only runs reproduce the exact 106,913-byte accepted
Monocypher sdkconfig. The benchmark-only adapter passes 4/4 strict host groups,
and the focused append-only admission suite passes 8/8 adversarial groups.

## Boundaries

All three candidate-specific API/configuration registries are now populated.
Phase 0 remains incomplete only because the second measurement node lacks exact-
profile admission. Phase 1 retained import/build admissions remain absent for
every candidate, and fresh benchmark execution authority remains absent.

No retained candidate import/build, benchmark, device, flash, radio, production
key or entropy operation, selection, secure-LoRa implementation, Packet V1,
support, compatibility, regulatory/range, physical, or score claim is accepted.
