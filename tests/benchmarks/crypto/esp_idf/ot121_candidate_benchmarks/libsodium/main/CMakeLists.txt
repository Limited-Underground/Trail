idf_component_register(
    SRCS
        "app_main.c"
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
