set(OT_CRYPTO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")

idf_component_register(
    SRCS
        "candidate_anchor.c"
        "${OT_CRYPTO_ROOT}/adapters/libsodium_noise_xk_v0/noise_xk_libsodium.c"
    INCLUDE_DIRS
        "${OT_CRYPTO_ROOT}/adapters/libsodium_noise_xk_v0"
    REQUIRES
        espressif__libsodium
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
