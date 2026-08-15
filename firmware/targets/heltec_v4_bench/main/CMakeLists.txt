set(OPENTRAIL_COMPONENT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../components")

idf_component_register(
    SRCS
        "app_main.cpp"
        "companion_boot_self_check.cpp"
        "companion_nimble_gatt.cpp"
        "companion_authorization_storage.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_protocol.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_semantics.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_request_coordinator.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_gatt_session.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_authorization_wire.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_gatt_authorization.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_gatt_authorization_adapter.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_authorization.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_authorization_persistence.cpp"
    INCLUDE_DIRS
        "."
        "${OPENTRAIL_COMPONENT_ROOT}/companion/include"
        "${OPENTRAIL_COMPONENT_ROOT}/protocol/include"
        "${OPENTRAIL_COMPONENT_ROOT}/radio/include"
    REQUIRES
        bt
        esp_timer
)
