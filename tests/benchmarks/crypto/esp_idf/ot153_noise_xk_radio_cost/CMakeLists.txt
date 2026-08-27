cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET "esp32s3" CACHE STRING "OT-153 fixed hardware target")

get_filename_component(OT153_ESP_IDF_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(OT153_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
set(EXTRA_COMPONENT_DIRS
    "${OT153_ESP_IDF_ROOT}/espressif_libsodium_1_0_22/managed_components/espressif__libsodium"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(ot153_noise_xk_radio_cost)
