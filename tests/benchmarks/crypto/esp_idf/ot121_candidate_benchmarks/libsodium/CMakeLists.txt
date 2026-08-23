cmake_minimum_required(VERSION 3.16)

get_filename_component(OT121_CRYPTO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(EXTRA_COMPONENT_DIRS
    "${OT121_CRYPTO_ROOT}/espressif_libsodium_1_0_22/managed_components/espressif__libsodium")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ot121_libsodium_primitive_bench)
