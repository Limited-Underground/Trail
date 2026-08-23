set(OT_CRYPTO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
set(OT_MONOCYPHER_SOURCE "${OT_CRYPTO_ROOT}/monocypher/4.0.3/source/src")

idf_component_register(
    SRCS
        "candidate_anchor.c"
        "${OT_CRYPTO_ROOT}/adapters/monocypher_api_v0/monocypher_benchmark_api.c"
        "${OT_MONOCYPHER_SOURCE}/monocypher.c"
        "${OT_MONOCYPHER_SOURCE}/optional/monocypher-ed25519.c"
    INCLUDE_DIRS
        "${OT_CRYPTO_ROOT}/adapters/monocypher_api_v0"
        "${OT_MONOCYPHER_SOURCE}"
        "${OT_MONOCYPHER_SOURCE}/optional"
    # Suppress ESP-IDF's special main-requires-everything fallback without
    # trimming the full Kconfig surface used by the accepted OT-107 sdkconfig.
    PRIV_REQUIRES
        esp_system
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
