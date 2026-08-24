cmake_minimum_required(VERSION 3.16)

get_filename_component(OT121_CRYPTO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ot123_monocypher_candidate_bench)