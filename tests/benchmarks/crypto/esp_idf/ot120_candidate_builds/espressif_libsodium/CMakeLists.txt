cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../espressif_libsodium_1_0_22/managed_components/espressif__libsodium")
set(COMPONENTS main espressif__libsodium)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ot120_espressif_libsodium_import_build)
