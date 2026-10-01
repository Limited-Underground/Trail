get_filename_component(OT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(OT_COMPONENTS "${OT_ROOT}/firmware/components")
set(OT_BENCH "${OT_ROOT}/firmware/targets/heltec_v4_bench/main")
set(OT_ADAPTER "${OT_ROOT}/firmware/targets/heltec_v4_security_eval/main/noise_adapter")
file(TO_CMAKE_PATH "$ENV{IDF_PATH}" OT_IDF)
idf_component_register(
    SRCS "app_main.cpp" "candidate_runtime.cpp" "candidate_store_runtime.cpp"
      "${OT_BENCH}/heltec_enrollment_input_arbiter.cpp"
      "${OT_BENCH}/heltec_v4_factory_reset_input.cpp"
      "${OT_BENCH}/heltec_v4_factory_reset_storage.cpp"
      "${OT_BENCH}/enrollment_identity_nvs_storage.cpp"
      "${OT_BENCH}/confirmation_nonowning_entropy.cpp"
      "${OT_BENCH}/heltec_v4_secure_random.cpp"
      "${OT_BENCH}/heltec_startup_display.cpp"
      "${OT_BENCH}/heltec_v4_oled.cpp"
      "${OT_BENCH}/heltec_oled_presentation.cpp"
      "${OT_COMPONENTS}/ui/src/oled_presentation.cpp"
      "${OT_COMPONENTS}/ui/src/compact_status_footer.cpp"
      "${OT_COMPONENTS}/time/src/oled_clock.cpp"
      "${OT_COMPONENTS}/time/src/oled_time_admission.cpp"
      "${OT_COMPONENTS}/companion/src/companion_factory_reset_gesture.cpp"
      "${OT_COMPONENTS}/companion/src/device_factory_reset_executor.cpp"
      "${OT_COMPONENTS}/security/src/serialized_secure_random.cpp"
      "${OT_COMPONENTS}/security/src/aead_nonce.cpp"
      "${OT_COMPONENTS}/persistence/src/persistent_storage_kv.cpp"
      "${OT_COMPONENTS}/persistence/src/outbound_counter_lease_store.cpp"
      "${OT_ADAPTER}/noise_xk_libsodium.c"
    INCLUDE_DIRS "." "${OT_BENCH}" "${OT_ADAPTER}"
      "${OT_COMPONENTS}/companion/include" "${OT_COMPONENTS}/security/include"
      "${OT_COMPONENTS}/security_evaluation/include" "${OT_COMPONENTS}/persistence/include"
      "${OT_COMPONENTS}/ui/include" "${OT_COMPONENTS}/time/include"
      "${OT_COMPONENTS}/protocol/include" "${OT_COMPONENTS}/radio/include"
      "${OT_IDF}/components/bt/host/nimble/nimble/nimble/host/src"
    PRIV_REQUIRES espressif__libsodium bt esp_timer esp_hw_support esp_system esp_partition app_update
      nvs_flash freertos esp_driver_gpio esp_driver_i2c esp_driver_usb_serial_jtag esp_lcd)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -fstack-usage)
target_link_libraries(${COMPONENT_LIB} PRIVATE idf::espressif__libsodium)
