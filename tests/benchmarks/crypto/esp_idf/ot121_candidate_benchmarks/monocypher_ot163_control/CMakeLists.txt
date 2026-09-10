cmake_minimum_required(VERSION 3.16)

get_filename_component(OT121_CRYPTO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ot142_monocypher_corrected_bench)
