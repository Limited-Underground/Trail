get_filename_component(OT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
idf_build_get_property(OT_BUILD_DIR BUILD_DIR)
set(OT_ADAPTER "${OT_ROOT}/tests/benchmarks/crypto/adapters/libsodium_noise_xk_v0/noise_xk_libsodium.c")
idf_component_register(
    SRCS "opentrail_console.cpp" "${OT_BUILD_DIR}/contained_app_main.cpp" "${OT_ADAPTER}"
         "${OT_ROOT}/firmware/targets/heltec_v4_radio_diag/main/esp32_radiolib_hal.cpp"
    INCLUDE_DIRS "." "${OT_ROOT}/firmware/components/diagnostics/include" "${OT_ROOT}/tests/benchmarks/crypto/adapters/libsodium_noise_xk_v0"
         "${OT_ROOT}/firmware/targets/heltec_v4_radio_diag/main"
    PRIV_REQUIRES espressif__libsodium jgromes__radiolib esp_driver_gpio esp_driver_spi esp_timer freertos log)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
set_source_files_properties("${OT_ADAPTER}" PROPERTIES COMPILE_OPTIONS "-Wno-type-limits;-Wno-nonnull")
target_compile_options(${COMPONENT_LIB} PRIVATE "-ffile-prefix-map=${OT_BUILD_DIR}=/CONTAINED_BUILD_DIR")
