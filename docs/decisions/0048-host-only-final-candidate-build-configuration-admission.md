# Decision 0048: Accept the owner-approved final per-candidate build configuration host-only

- Status: Accepted
- Date: 2026-08-21
- Work item: OT-107

## Decision

Accept append-only `OTCBCGA0/v0`, raw SHA-256 `3d71dfb02b6fd25e0881ac63cc085174c2cdd7e5ac2cd1e12c320ec34928f5a2`, as the host-only admission of exact `OTCBCGE0/v0` configuration-generation evidence, raw SHA-256 `0c1b8cb574a210c6123b82b565e6ea8e12cee59bacd6ab4b94b293ddf9d2dfbc`.

The admission binds the owner's approval to the unchanged proposal raw SHA-256 `f9072a602a9c139b1e7728735db04cc270720bc37e0429c22bcdb0cd56202a15`. The proposal remains immutable with its historical pre-approval fields; the append-only admission records approval.

## Accepted configuration boundary

The common profile remains ESP32-S3, reproducible build enabled, compile-time date disabled, size optimization, 160 MHz CPU, and a 100 Hz FreeRTOS tick under pinned ESP-IDF v6.0.2 commit `7101770dc6db2667b3c477cc31365dd1acd6db4e` and `xtensa-esp32s3-elf-gcc` 15.2.0.

The accepted candidate-specific generated configurations are:

- primary Espressif libsodium: native libsodium SHA with `CONFIG_LIBSODIUM_USE_MBEDTLS_SHA=n`, 107,001 bytes, SHA-256 `b4fb46a1d2fa27953a9e9f02cd87da8be60c09d7f5e3ef00905839f7f38f2f9f`;
- comparison ESP-IDF mbedTLS/PSA: `CONFIG_MBEDTLS_CHACHA20_C=y` and `CONFIG_MBEDTLS_CHACHAPOLY_C=y`, 106,921 bytes, SHA-256 `9fc68f61f2fd5ce5f277c3050bdb33e520038349100d60ac142df9fe37d91686`;
- comparison Monocypher: empty Kconfig overlay with exact core and optional Ed25519 source requirements, 106,913 bytes, SHA-256 `4260688e6323cfda7a50912b4cc9c77a7b6f5133b6970b543bf0ce822ffd023f`.

Each candidate was generated twice in a fresh, initially absent, component-manager-disabled, configuration-only root. Each pair exited zero and reproduced the same generated sdkconfig byte count and SHA-256. No candidate source was copied or compiled.

Partial candidates may be measured later as comparisons, but complete operation coverage remains required before final selection. Candidate-specific Kconfig surfaces are represented by per-candidate generated sdkconfig digests rather than the historical single whole-file digest assumption.

## Readiness accounting

OT-107 closes only `final_candidate_build_configuration_unresolved`. Historical six-, prior five-, prior four-, prior three-, and current two-blocker states remain recorded. The two current blockers are:

1. `esp_idf_mbedtls_psa_dependency_lock_and_api_config_unresolved`
2. `direct_radio_mtu_phy_region_unresolved`

Accepted source/API-configuration/candidate-import counts remain `3/0/0`. OT-096 remains the controlling 5/8 static mbedTLS/PSA assessment, the historical `OTCB0/v0` plan remains `draft_blocked`, and `OTCBR0/v0` readiness remains blocked.

## Boundaries

This decision authorizes and proves only the final per-candidate build configuration. It grants no source acquisition or copy, candidate import or compile, benchmark build or execution, device/flash/radio/key/entropy operation, suite or candidate selection, packet-v1 authority, target support, compatibility or regulatory conclusion, physical evidence, continuing execution authority, or score credit.
