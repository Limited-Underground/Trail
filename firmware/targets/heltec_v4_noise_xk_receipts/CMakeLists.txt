cmake_minimum_required(VERSION 3.16)
set(IDF_TARGET "esp32s3" CACHE STRING "Fixed benchmark target")
set(PROJECT_VER "nxk-receipts-v1")
get_filename_component(OT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(OT_BENCH "${OT_ROOT}/tests/benchmarks/crypto/esp_idf")
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
# Reuse the accepted offline, pinned dependency acquisition. Never patch it.
set(OT_RADIOLIB "${OT_BENCH}/noise_xk_ready_radio/managed_components/jgromes__radiolib")
set(EXTRA_COMPONENT_DIRS "${OT_RADIOLIB}"
    "${OT_BENCH}/espressif_libsodium_1_0_22/managed_components/espressif__libsodium")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
idf_build_get_property(OT_PYTHON PYTHON)
set(OT_APP_GENERATOR "${OT_ROOT}/tools/noise_xk_receipt_firmware_source.py")
set(OT_DRIVER_GENERATOR "${OT_ROOT}/tools/radiolib_busy_source.py")
set(OT_FROZEN_MAIN "${OT_BENCH}/ot153_noise_xk_radio_cost/main/app_main.cpp")
set(OT_DRIVER_SOURCE "${OT_RADIOLIB}/src/modules/SX126x/SX126x.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${OT_APP_GENERATOR}" "${OT_ROOT}/tools/noise_xk_ready_firmware_source.py"
    "${OT_ROOT}/tools/noise_xk_contained_firmware_source.py"
    "${OT_ROOT}/tools/noise_xk_console_firmware_source.py"
    "${OT_DRIVER_GENERATOR}" "${OT_FROZEN_MAIN}" "${OT_DRIVER_SOURCE}")
execute_process(COMMAND "${OT_PYTHON}" "${OT_APP_GENERATOR}"
    --source "${OT_FROZEN_MAIN}" --output "${CMAKE_CURRENT_BINARY_DIR}/contained_app_main.cpp"
    RESULT_VARIABLE OT_APP_RESULT)
execute_process(COMMAND "${OT_PYTHON}" "${OT_DRIVER_GENERATOR}"
    --source "${OT_DRIVER_SOURCE}" --output "${CMAKE_CURRENT_BINARY_DIR}/contained_SX126x.cpp"
    RESULT_VARIABLE OT_DRIVER_RESULT)
if(NOT OT_APP_RESULT EQUAL 0 OR NOT OT_DRIVER_RESULT EQUAL 0)
    message(FATAL_ERROR "Pinned containment source generation failed")
endif()
set(CMAKE_C_COMPILER_LAUNCHER "${CMAKE_COMMAND};-E;env;SOURCE_DATE_EPOCH=0")
set(CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_COMMAND};-E;env;SOURCE_DATE_EPOCH=0")
project(heltec_v4_noise_xk_receipts)

# Replace exactly one driver translation unit after component registration.
idf_component_get_property(OT_RADIO_TARGET jgromes__radiolib COMPONENT_LIB)
get_target_property(OT_RADIO_SOURCES ${OT_RADIO_TARGET} SOURCES)
set(OT_REPLACED 0)
set(OT_NEW_RADIO_SOURCES "")
foreach(OT_SOURCE IN LISTS OT_RADIO_SOURCES)
    if(OT_SOURCE STREQUAL OT_DRIVER_SOURCE)
        list(APPEND OT_NEW_RADIO_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/contained_SX126x.cpp")
        math(EXPR OT_REPLACED "${OT_REPLACED} + 1")
    else()
        list(APPEND OT_NEW_RADIO_SOURCES "${OT_SOURCE}")
    endif()
endforeach()
if(NOT OT_REPLACED EQUAL 1)
    message(FATAL_ERROR "Expected exactly one pinned SX126x translation unit")
endif()
set_property(TARGET ${OT_RADIO_TARGET} PROPERTY SOURCES "${OT_NEW_RADIO_SOURCES}")
target_include_directories(${OT_RADIO_TARGET} PRIVATE "${OT_RADIOLIB}/src/modules/SX126x")
target_compile_options(${OT_RADIO_TARGET} PRIVATE
    "-ffile-prefix-map=${CMAKE_CURRENT_BINARY_DIR}=/CONTAINED_BUILD_DIR")

# Catch ELF-linked direct ROM output attempts after console admission. ROM-internal
# startup/panic/reset paths remain outside the healthy benchmark lifecycle.
target_link_options(heltec_v4_noise_xk_receipts.elf PRIVATE "-Wl,--wrap=esp_rom_output_tx_one_char")
target_link_options(heltec_v4_noise_xk_receipts.elf PRIVATE "-Wl,--wrap=ets_write_char_uart")
target_link_options(heltec_v4_noise_xk_receipts.elf PRIVATE "-Wl,--wrap=uart_tx_one_char")
