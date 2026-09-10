cmake_minimum_required(VERSION 3.16)

get_filename_component(OT121_CRYPTO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(EXTRA_COMPONENT_DIRS
    "${OT121_CRYPTO_ROOT}/espressif_libsodium_1_0_22/managed_components/espressif__libsodium")

set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig" CACHE FILEPATH "" FORCE)
set(SDKCONFIG_DEFAULTS "${OT121_CRYPTO_ROOT}/ot120_candidate_builds/historical_common_sdkconfig_ot120.defaults;${OT121_CRYPTO_ROOT}/ot120_candidate_builds/reproducible.defaults;${CMAKE_CURRENT_LIST_DIR}/../libsodium/sdkconfig.overlay")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ot121_libsodium_primitive_bench)
