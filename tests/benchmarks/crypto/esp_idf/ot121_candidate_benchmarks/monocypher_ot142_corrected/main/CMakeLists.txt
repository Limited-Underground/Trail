get_filename_component(OT129_MAIN_DIR "${CMAKE_CURRENT_LIST_DIR}/../../monocypher_ot129/main" ABSOLUTE)

idf_component_register(
    SRCS
        "app_main.c"
        "${OT129_MAIN_DIR}/ot129_control_protocol.c"
        "${OT121_CRYPTO_ROOT}/../adapters/monocypher_api_v0/monocypher_benchmark_api.c"
        "${OT121_CRYPTO_ROOT}/../monocypher/4.0.3/source/src/monocypher.c"
        "${OT121_CRYPTO_ROOT}/../monocypher/4.0.3/source/src/optional/monocypher-ed25519.c"
    INCLUDE_DIRS
        "${OT129_MAIN_DIR}"
        "../../include"
        "${OT121_CRYPTO_ROOT}/../adapters/monocypher_api_v0"
        "${OT121_CRYPTO_ROOT}/../monocypher/4.0.3/source/src"
        "${OT121_CRYPTO_ROOT}/../monocypher/4.0.3/source/src/optional"
    PRIV_REQUIRES esp_driver_usb_serial_jtag esp_timer freertos heap log
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
