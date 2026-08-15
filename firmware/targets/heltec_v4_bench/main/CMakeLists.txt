set(OPENTRAIL_COMPONENT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../components")

idf_component_register(
    SRCS
        "app_main.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_protocol.cpp"
        "${OPENTRAIL_COMPONENT_ROOT}/companion/src/companion_semantics.cpp"
    INCLUDE_DIRS
        "."
        "${OPENTRAIL_COMPONENT_ROOT}/companion/include"
        "${OPENTRAIL_COMPONENT_ROOT}/protocol/include"
        "${OPENTRAIL_COMPONENT_ROOT}/radio/include"
)
