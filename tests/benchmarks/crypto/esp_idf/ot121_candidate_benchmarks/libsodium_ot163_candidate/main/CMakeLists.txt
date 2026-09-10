idf_component_register(
    SRCS
        "${CMAKE_CURRENT_LIST_DIR}/../../libsodium/main/app_main.c"
        "${OT121_CRYPTO_ROOT}/../adapters/libsodium_noise_xk_v0/noise_xk_libsodium.c"
    INCLUDE_DIRS
        "."
        "../../include"
        "${OT121_CRYPTO_ROOT}/../adapters/libsodium_noise_xk_v0"
    PRIV_REQUIRES espressif__libsodium esp_driver_usb_serial_jtag esp_timer freertos heap log
)
set_source_files_properties(
    "${OT121_CRYPTO_ROOT}/../adapters/libsodium_noise_xk_v0/noise_xk_libsodium.c"
    PROPERTIES COMPILE_OPTIONS "-Wno-type-limits;-Wno-nonnull"
)

get_filename_component(OT163_REPO_ROOT "${OT121_CRYPTO_ROOT}/../../../.." ABSOLUTE)
get_filename_component(OT163_CANDIDATE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../libsodium" ABSOLUTE)
target_compile_options(${COMPONENT_LIB} PRIVATE "-fmacro-prefix-map=${OT163_REPO_ROOT}=D:/ESP32/OpenTrail" "-fmacro-prefix-map=${OT163_CANDIDATE_ROOT}=.")
