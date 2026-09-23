set(OPENTRAIL_COMPONENT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../components")
set(OPENTRAIL_CONFIRMATION_SOURCES)
set(OPENTRAIL_CONFIRMATION_INCLUDES)
set(OPENTRAIL_CONFIRMATION_REQUIRES)
if(OPENTRAIL_CONFIRMATION_EVALUATION)
    set(OPENTRAIL_CONFIRMATION_ADAPTER "${CMAKE_CURRENT_LIST_DIR}/../../heltec_v4_security_eval/main/noise_adapter")
    list(APPEND OPENTRAIL_CONFIRMATION_SOURCES
        "confirmation_evaluation_backend.cpp"
        "confirmation_nonowning_entropy.cpp"
        "${OPENTRAIL_CONFIRMATION_ADAPTER}/noise_xk_libsodium.c"
        "${OPENTRAIL_COMPONENT_ROOT}/security/src/serialized_secure_random.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/security/src/aead_nonce.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/persistence/src/persistent_storage_kv.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/persistence/src/outbound_counter_lease_store.cpp")
    list(APPEND OPENTRAIL_CONFIRMATION_INCLUDES
        "${OPENTRAIL_CONFIRMATION_ADAPTER}"
        "${OPENTRAIL_COMPONENT_ROOT}/security_evaluation/include"
        "${OPENTRAIL_COMPONENT_ROOT}/persistence/include")
    list(APPEND OPENTRAIL_CONFIRMATION_REQUIRES espressif__libsodium)
endif()

idf_component_register(
    SRCS
        ${OPENTRAIL_CONFIRMATION_SOURCES}
        "app_main.cpp"
        "companion_boot_self_check.cpp"
        "companion_nimble_gatt.cpp"
        "companion_nimble_runtime.cpp"
        "companion_v1_heltec_adapters.cpp"
        "companion_authorization_storage.cpp"
        "companion_name_storage.cpp"
        "companion_region_storage.cpp"
        "companion_authorization_nvs_backend.cpp"
        "companion_authorization_nvs_context.cpp"
        "companion_protected_root_key_roster_adapter.cpp"
        "companion_protected_root_configuration_security_adapter.cpp"
        "heltec_startup_display.cpp"
        "heltec_v4_battery.cpp"
        "heltec_v4_factory_reset_input.cpp"
        "heltec_v4_factory_reset_storage.cpp"
        "enrollment_identity_nvs_storage.cpp"
        "heltec_v4_gnss.cpp"
        "heltec_v4_oled.cpp"
        "heltec_oled_presentation.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/ui/src/oled_presentation.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/time/src/oled_clock.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/time/src/oled_time_admission.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_configuration_codec.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_confirmation_codec.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_configuration_dispatcher.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_device_name_codec.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_device_name_owner.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_region_owner.cpp"
        "heltec_v4_secure_random.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_protocol.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_semantics.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_request_coordinator.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_gatt_session.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_authorization_wire.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_gatt_authorization.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/ui/src/compact_status_footer.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_gatt_authorization_adapter.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_authorization.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_authorization_persistence.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_authorization_protected_kv_media.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_ble_runtime_owner.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_v1_bond_owner.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_factory_reset_gesture.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_factory_reset_authority.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_pairing_window.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/device_factory_reset_executor.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_public_link_info.cpp"
    INCLUDE_DIRS
        "${OPENTRAIL_COMPONENT_ROOT}/persistence/include"
        "${OPENTRAIL_COMPONENT_ROOT}/security_evaluation/include"
        ${OPENTRAIL_CONFIRMATION_INCLUDES}
        "${OPENTRAIL_COMPONENT_ROOT}/ui/include"
        "${OPENTRAIL_COMPONENT_ROOT}/time/include"
        "."
        "${OPENTRAIL_COMPONENT_ROOT}/companion/include"
        "${OPENTRAIL_COMPONENT_ROOT}/security/include"
        "${OPENTRAIL_COMPONENT_ROOT}/protocol/include"
        "${OPENTRAIL_COMPONENT_ROOT}/radio/include"
    REQUIRES
        bt
        bootloader_support
        efuse
        esp_adc
        esp_partition
        esp_security
        esp_system
        esp_timer
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_uart
        esp_lcd
        mbedtls
        nvs_flash
    PRIV_REQUIRES ${OPENTRAIL_CONFIRMATION_REQUIRES}
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE OPENTRAIL_CONFIRMATION_EVALUATION=$<BOOL:${OPENTRAIL_CONFIRMATION_EVALUATION}>)
if(OPENTRAIL_CONFIRMATION_EVALUATION)
    # IDF's early requirements pass does not inherit this custom cache option.
    # Resolve the admitted target here so its exported headers/link dependencies
    # are applied in the actual opt-in build and an absent component is fatal.
    target_link_libraries(${COMPONENT_LIB} PRIVATE idf::espressif__libsodium)
    target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
    target_compile_options(${COMPONENT_LIB} PRIVATE -fstack-usage)
endif()
