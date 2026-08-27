get_filename_component(OT149_CONTROL_DIR "${OT149_ESP_IDF_ROOT}/ot121_candidate_benchmarks/monocypher_ot129/main" ABSOLUTE)

idf_component_register(
    SRCS
        "${OT149_ROOT}/common/app_main.c"
        "mbedtls_psa_benchmark_api.c"
        "${OT149_CONTROL_DIR}/ot129_control_protocol.c"
    INCLUDE_DIRS
        "${OT149_ROOT}/common"
        "${OT149_ESP_IDF_ROOT}/ot121_candidate_benchmarks/include"
        "${OT149_CONTROL_DIR}"
    PRIV_REQUIRES mbedtls esp_driver_usb_serial_jtag esp_timer freertos heap log
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
