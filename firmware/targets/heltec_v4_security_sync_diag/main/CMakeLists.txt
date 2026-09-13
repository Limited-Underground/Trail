get_filename_component(OT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(OT_COMPONENTS "${OT_ROOT}/firmware/components")
set(OT_PREVIOUS "${OT_ROOT}/firmware/targets/heltec_v4_security_eval/main")
set(OT_ADAPTER "${OT_PREVIOUS}/noise_adapter")
idf_component_register(
    SRCS "app_main.cpp" "${OT_ROOT}/firmware/targets/heltec_v4_security_policy_eval/main/policy_console.cpp" "${OT_PREVIOUS}/entropy_runtime.cpp"
      "${OT_ADAPTER}/noise_xk_libsodium.c"
      "${OT_ROOT}/firmware/targets/heltec_v4_bench/main/heltec_v4_secure_random.cpp"
      "${OT_COMPONENTS}/security/src/serialized_secure_random.cpp"
      "${OT_COMPONENTS}/security/src/aead_nonce.cpp"
      "${OT_COMPONENTS}/persistence/src/persistent_storage_kv.cpp"
      "${OT_COMPONENTS}/persistence/src/outbound_counter_lease_store.cpp"
    INCLUDE_DIRS "." "${OT_ROOT}/firmware/targets/heltec_v4_security_policy_eval/main" "${OT_COMPONENTS}/security_diagnostics/include" "${OT_PREVIOUS}" "${OT_COMPONENTS}/security_evaluation/include" "${OT_COMPONENTS}/diagnostics/include" "${OT_ADAPTER}" "${OT_COMPONENTS}/security/include"
      "${OT_COMPONENTS}/persistence/include" "${OT_ROOT}/firmware/targets/heltec_v4_bench/main"
    PRIV_REQUIRES espressif__libsodium bt esp_timer esp_hw_support esp_rom nvs_flash freertos)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
