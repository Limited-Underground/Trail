cmake_minimum_required(VERSION 3.16)

get_filename_component(OT236_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(OT121_CRYPTO_ROOT "${OT236_REPO_ROOT}/tests/benchmarks/crypto/esp_idf")
set(EXTRA_COMPONENT_DIRS
    "${OT121_CRYPTO_ROOT}/espressif_libsodium_1_0_22/managed_components/espressif__libsodium")

set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig" CACHE FILEPATH "" FORCE)
set(SDKCONFIG_DEFAULTS "${OT121_CRYPTO_ROOT}/ot120_candidate_builds/historical_common_sdkconfig_ot120.defaults;${OT121_CRYPTO_ROOT}/ot120_candidate_builds/reproducible.defaults;${OT121_CRYPTO_ROOT}/ot121_candidate_benchmarks/libsodium/sdkconfig.overlay")
if(NOT DEFINED PROJECT_VER OR NOT PROJECT_VER STREQUAL "ot236-libsodium-yield-v1")
    message(FATAL_ERROR "Explicit PROJECT_VER=ot236-libsodium-yield-v1 required")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ot236_libsodium_yield_bench)
