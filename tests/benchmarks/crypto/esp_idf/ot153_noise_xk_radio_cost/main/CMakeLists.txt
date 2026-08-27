set(OT153_NOISE_ADAPTER
    "${OT153_REPO_ROOT}/tests/benchmarks/crypto/adapters/libsodium_noise_xk_v0/noise_xk_libsodium.c")
set(OT153_RADIO_HAL
    "${OT153_REPO_ROOT}/firmware/targets/heltec_v4_radio_diag/main/esp32_radiolib_hal.cpp")

idf_component_register(
    SRCS
        "app_main.cpp"
        "${OT153_NOISE_ADAPTER}"
        "${OT153_RADIO_HAL}"
    INCLUDE_DIRS
        "."
        "${OT153_REPO_ROOT}/tests/benchmarks/crypto/adapters/libsodium_noise_xk_v0"
        "${OT153_REPO_ROOT}/firmware/targets/heltec_v4_radio_diag/main"
    PRIV_REQUIRES
        espressif__libsodium
        jgromes__radiolib
        esp_driver_gpio
        esp_driver_spi
        esp_timer
        freertos
        log
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
set_source_files_properties(
    "${OT153_NOISE_ADAPTER}"
    PROPERTIES COMPILE_OPTIONS "-Wno-type-limits;-Wno-nonnull"
)
