# Decision 0058: admit retained candidate import/build evidence

- Status: Accepted
- Date: 2026-08-22
- Scope: OT-005 benchmark phase 1 only

## Decision

Accept the three exact retained candidate import/build evidence records and the
append-only atomic `OTCIBA1/v1` admission produced under the frozen OT-120
contract. Espressif libsodium 1.0.22, ESP-IDF mbedTLS/PSA 4.1.0, and Monocypher
4.0.3 each completed two independent, initially absent, cache-disabled,
component-manager-network-disabled ESP-IDF 6.0.2 builds with zero compiler
warnings and identical retained artifact tuples between runs.

Each build reproduces its accepted OT-107 sdkconfig and retains the exact
candidate source plus every operation anchor allowed by its earlier
API/configuration admission. Current source/API-configuration/candidate-import
counts advance atomically from `3/3/0` to `3/3/3`. Phase 0 and phase 1 are now
complete. Measurement readiness remains false because fresh benchmark execution
authority is absent.

## Evidence

- Frozen contract raw/canonical SHA-256 `ac0b3dd0e7f6fbd1fdb7edbf482ed301cfd2ce15a32c9b6f2e48bf1b8408df51` / `bbbc9c93028affce509bc145ce2f3de44c0cc2a5934cc3804bd3526dee94a8ea`.
- libsodium graph raw SHA-256 `c939dbc7afbc103a44c16d92474528acc782ca442873f06fa9a8a8b04aaec20c`; evidence raw/canonical SHA-256 `735b4755f25da280cde7ba79387f5eeeee5a38bf477b60042e64d31f20f2186f` / `28c98e83cf2149177353f47346e8c37d263e8a436a10bff3b3f4cefe7608bd49`.
- mbedTLS/PSA graph raw SHA-256 `7338e2383f152d554d6a64e3d46e7260b0c9dba79099311ec7e140b1ccde7a55`; evidence raw/canonical SHA-256 `e4cf79a47c6cd3a64f44412bf6b010815e7e66f3e7633242d2c2c89c61bf1307` / `5ed9d04e6d773be599e22bbccb3a8117850d99636dfc3a30adeefcc1f384866d`.
- Monocypher graph raw SHA-256 `9e59ab26cca582301027ee3544bfd69643dfde5b8a84a2ae494cb98969ba9645`; evidence raw/canonical SHA-256 `4ba0b05ab4ee043e506b8bc15d5140451b0e6b1b95fd5eb46bae26a595789633` / `390a94a0d256f4a8863c0d44363b788c7b8c9a91c4e94bebdf9356d8ca1a0c61`.
- Atomic admission raw/canonical SHA-256 `90af31966553bee58fcf71e4decfee8d2bcadfee58ef026e3f96cffcd6f45ccf` / `0c55f49803d833c075670b17fa8d033bd5a7cd4997e8714ff247161f7fa2057b`.

Strict validation accepts all seven retained artifacts together. Eleven
adversarial admission groups, three build-harness groups, and the complete host
gate pass.

## Boundaries

This is computer-only import/build evidence. No benchmark was executed; no
device was accessed or flashed; no radio, key, or entropy operation occurred;
and no candidate, suite, handshake/KDF, or packet-v1 wire format was selected.
Libsodium remains structurally selection eligible but unselected. mbedTLS/PSA
and Monocypher remain five-of-eight, comparison-only, and structurally
nonselectable.

No support, compatibility, regulatory, production, physical-acceptance, or
score claim is added. Android remains 60%; V1 Companion remains exact 43.75%
and displayed 44%; the historical standalone baseline remains exact 31.75% and
displayed 32%; V1.5 and V2 remain unmeasured.
