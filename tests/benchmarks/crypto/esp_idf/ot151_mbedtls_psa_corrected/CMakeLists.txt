cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET "esp32s3" CACHE STRING "OpenTrail OT-151 target" FORCE)
get_filename_component(OT149_ROOT "${CMAKE_CURRENT_LIST_DIR}/../ot149_mbedtls_psa" ABSOLUTE)
get_filename_component(OT149_ESP_IDF_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(OT149_REPO_ROOT "${OT149_ESP_IDF_ROOT}/../../../.." ABSOLUTE)
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig" CACHE FILEPATH "OT-149 generated sdkconfig" FORCE)
set(
    SDKCONFIG_DEFAULTS
    "${OT149_REPO_ROOT}/firmware/targets/heltec_v4_bench/sdkconfig.defaults;${OT149_ESP_IDF_ROOT}/ot120_candidate_builds/reproducible.defaults;${OT149_ESP_IDF_ROOT}/ot120_candidate_builds/esp_idf_mbedtls_psa/sdkconfig.overlay;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults"
    CACHE STRING "OT-149 admitted configuration plus quiet transport successor" FORCE
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ot151_mbedtls_psa_corrected_bench)

set(OT149_EXPECTED_SDKCONFIG_BYTES 106890)
set(OT149_EXPECTED_SDKCONFIG_SHA256 "00fd8a75e1df36e7cb4d4aa2275492297e1300383e15cbbf2d4a6284dd99d85e")
file(SIZE "${SDKCONFIG}" OT149_GENERATED_SDKCONFIG_BYTES)
file(SHA256 "${SDKCONFIG}" OT149_GENERATED_SDKCONFIG_SHA256)
string(TOLOWER "${OT149_GENERATED_SDKCONFIG_SHA256}" OT149_GENERATED_SDKCONFIG_SHA256)
if(NOT OT149_GENERATED_SDKCONFIG_BYTES EQUAL OT149_EXPECTED_SDKCONFIG_BYTES OR
   NOT OT149_GENERATED_SDKCONFIG_SHA256 STREQUAL OT149_EXPECTED_SDKCONFIG_SHA256)
    message(FATAL_ERROR "OT-151 generated sdkconfig successor binding mismatch")
endif()
