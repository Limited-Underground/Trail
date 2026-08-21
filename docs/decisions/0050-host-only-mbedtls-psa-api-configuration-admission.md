# Decision 0050: Accept the host-only mbedTLS/PSA API/configuration comparison evidence

- Status: Accepted
- Date: 2026-08-21
- Work item: OT-109

## Decision

Accept append-only `OTCAPIA0/v0`, raw SHA-256 `0311b8b264d264d4a005ac9be8531c06175521362ed54ac9f40bdef1fd7a5df0` and canonical SHA-256 `fed7b009a97a60678b2dfbba23d933b974aa0fec46d0b460ac7eb221e91931dd`, as the host-only admission of the ESP-IDF mbedTLS/PSA comparison candidate's partial API/configuration evidence.

The admission binds exact generated `OTCAPIOE0/v0` operation evidence, raw SHA-256 `ea85f548deee36ca34241747cdf567036febfb9eecd88d9e134d3383edf2379e` and canonical SHA-256 `6a17a6f5a753a19d2d78d7cb6f0c757ef9791e0bf2e953e27afc3eccb04f27ed`, to strict `OTCAPI0/v2` evidence, raw SHA-256 `67532e10704d02489b72a72ef55607743c00a5bd8504276750931b5d986f6155` and canonical SHA-256 `22975ac7fbd3c9faab1ae0c9fa952a58dc4a7a893de3cc74604182b3492fe1f8`.

## Accepted evidence boundary

The accepted evidence records five eligible operations: X25519, SHA-256, HKDF-SHA-256, ChaCha20-Poly1305 encryption, and ChaCha20-Poly1305 decryption. Ed25519 signing, Ed25519 verification, and Noise XK handshake composition remain unavailable. This is therefore a strict 5/8 `comparison_partial` result. It is measurable only for the five evidenced operations and is structurally nonselectable.

The accepted final mbedTLS/PSA configuration enables `CONFIG_MBEDTLS_CHACHA20_C=y` and `CONFIG_MBEDTLS_CHACHAPOLY_C=y`. Two fresh, initially absent, component-manager-disabled, configuration-only roots each exited zero and reproduced the same 106,921-byte sdkconfig with SHA-256 `9fc68f61f2fd5ce5f277c3050bdb33e520038349100d60ac142df9fe37d91686`. No candidate source was copied or compiled.

## Readiness accounting

OT-109 closes `esp_idf_mbedtls_psa_dependency_lock_and_api_config_unresolved`. Accepted source/API-configuration/candidate-import counts become `3/1/0`. The one current blocker is:

1. `direct_radio_mtu_phy_region_unresolved`

The mbedTLS/PSA comparison remains partial and nonselectable. `OTCBR0/v0` readiness remains blocked, and the historical `OTCB0/v0` plan remains `draft_blocked`.

## Boundaries

This decision accepts only host-side partial API/configuration evidence for a comparison candidate. It imports or compiles no candidate, builds or executes no benchmark, accesses or flashes no device, performs no radio, key, or entropy operation, selects no candidate, suite, or wire format, authorizes no packet-v1 behavior, and adds no support, compatibility, regulatory, physical-evidence, continuing-authority, or score claim.
