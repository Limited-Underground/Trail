set(OT236_HISTORICAL_SOURCE "${OT121_CRYPTO_ROOT}/ot121_candidate_benchmarks/libsodium/main/app_main.c")
set(OT236_GENERATED_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/app_main.generated.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${OT236_HISTORICAL_SOURCE}" "${CMAKE_CURRENT_LIST_DIR}/compose.py")
execute_process(
    COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/compose.py" --output "${OT236_GENERATED_SOURCE}"
    RESULT_VARIABLE OT236_COMPOSE_RESULT
    OUTPUT_VARIABLE OT236_COMPOSE_OUTPUT
    ERROR_VARIABLE OT236_COMPOSE_ERROR
)
if(NOT OT236_COMPOSE_RESULT EQUAL 0)
    message(FATAL_ERROR "OT236 source composition failed: ${OT236_COMPOSE_ERROR}")
endif()
idf_component_register(
    SRCS "${OT236_GENERATED_SOURCE}"
        "${OT121_CRYPTO_ROOT}/../adapters/libsodium_noise_xk_v0/noise_xk_libsodium.c"
    INCLUDE_DIRS "${OT121_CRYPTO_ROOT}/ot121_candidate_benchmarks/include"
        "${OT121_CRYPTO_ROOT}/../adapters/libsodium_noise_xk_v0"
    PRIV_REQUIRES espressif__libsodium esp_driver_usb_serial_jtag esp_timer freertos heap log
)
# IDF's early requirements pass returns from idf_component_register. Source
# properties are valid only in the full configure pass below that boundary.
set_source_files_properties("${OT236_GENERATED_SOURCE}" PROPERTIES GENERATED TRUE)
set_source_files_properties(
    "${OT121_CRYPTO_ROOT}/../adapters/libsodium_noise_xk_v0/noise_xk_libsodium.c"
    PROPERTIES COMPILE_OPTIONS "-Wno-type-limits;-Wno-nonnull"
)
# Build-directory-dependent generated paths must not enter __FILE__ or DWARF.
target_compile_options(${COMPONENT_LIB} PRIVATE
    "-fmacro-prefix-map=${OT236_REPO_ROOT}=D:/ESP32/OpenTrail"
    "-fdebug-prefix-map=${CMAKE_BINARY_DIR}=D:/ESP32/OpenTrail/build/ot236_libsodium_yield_bench"
    "-fmacro-prefix-map=${CMAKE_CURRENT_BINARY_DIR}=D:/ESP32/OpenTrail/firmware/targets/libsodium_capture_yield_eval/main"
)
