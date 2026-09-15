get_filename_component(OT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(OT_COMPONENTS "${OT_ROOT}/firmware/components")
set(OT_SECURITY_TARGET "${OT_ROOT}/firmware/targets/heltec_v4_security_eval/main")
set(OT_ADAPTER "${OT_SECURITY_TARGET}/noise_adapter")
idf_component_register(
    SRCS "${OT_ROOT}/firmware/targets/heltec_v4_pair_eval/main/app_main.cpp" "${OT_ROOT}/firmware/targets/heltec_v4_pair_eval/main/pair_display.cpp"
      "pair_radio_driver.cpp"
      "${OT_ROOT}/firmware/targets/heltec_v4_radio_diag/main/esp32_radiolib_hal.cpp"
      "${OT_COMPONENTS}/protocol/src/packet_codec.cpp"
      "${OT_SECURITY_TARGET}/entropy_runtime.cpp"
      "${OT_ADAPTER}/noise_xk_libsodium.c"
      "${OT_ROOT}/firmware/targets/heltec_v4_bench/main/heltec_v4_secure_random.cpp"
      "${OT_COMPONENTS}/security/src/serialized_secure_random.cpp"
      "${OT_COMPONENTS}/security/src/aead_nonce.cpp"
      "${OT_COMPONENTS}/persistence/src/persistent_storage_kv.cpp"
      "${OT_COMPONENTS}/persistence/src/outbound_counter_lease_store.cpp"
    INCLUDE_DIRS "." "${OT_ROOT}/firmware/targets/heltec_v4_pair_eval/main"
      "${OT_ROOT}/firmware/targets/heltec_v4_radio_diag/main"
      "${OT_COMPONENTS}/radio/include" "${OT_COMPONENTS}/protocol/include" "${OT_SECURITY_TARGET}" "${OT_ADAPTER}"
      "${OT_COMPONENTS}/security/include" "${OT_COMPONENTS}/security_evaluation/include"
      "${OT_COMPONENTS}/persistence/include"
      "${OT_ROOT}/firmware/targets/heltec_v4_bench/main"
    PRIV_REQUIRES jgromes__radiolib esp_driver_spi espressif__libsodium bt esp_timer esp_hw_support esp_rom nvs_flash
      freertos esp_driver_gpio esp_driver_i2c esp_driver_usb_serial_jtag esp_lcd)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)

target_compile_definitions(${COMPONENT_LIB} PRIVATE OT_PAIR_RADIO_EVAL=1)
