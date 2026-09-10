get_filename_component(OT129_MAIN_DIR "${CMAKE_CURRENT_LIST_DIR}/../../monocypher_ot129/main" ABSOLUTE)

idf_component_register(
    SRCS
        "${CMAKE_CURRENT_LIST_DIR}/../../monocypher_ot142_corrected/main/app_main.c"
        "no_candidate_bindings.c"
        "${OT129_MAIN_DIR}/ot129_control_protocol.c"
    INCLUDE_DIRS
        "${OT129_MAIN_DIR}"
        "../../include"
        "${OT121_CRYPTO_ROOT}/../adapters/monocypher_api_v0"
        "${OT121_CRYPTO_ROOT}/../monocypher/4.0.3/source/src"
        "${OT121_CRYPTO_ROOT}/../monocypher/4.0.3/source/src/optional"
    PRIV_REQUIRES esp_driver_usb_serial_jtag esp_timer freertos heap log
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)

# Match accepted logical source paths after workspace migration.
get_filename_component(OT163_REPO_ROOT "${OT121_CRYPTO_ROOT}/../../../.." ABSOLUTE)
get_filename_component(OT163_CANDIDATE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../monocypher_ot142_corrected" ABSOLUTE)
target_compile_options(${COMPONENT_LIB} PRIVATE "-fmacro-prefix-map=${OT163_REPO_ROOT}=D:/ESP32/OpenTrail" "-fmacro-prefix-map=${OT163_CANDIDATE_ROOT}=.")
