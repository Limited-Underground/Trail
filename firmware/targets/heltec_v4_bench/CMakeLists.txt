cmake_minimum_required(VERSION 3.16)

option(OPENTRAIL_CONNECTION_DIAGNOSTICS "Enable bounded OT0101e connection observations" OFF)
option(OPENTRAIL_DIAGNOSTIC_SENSOR_DISPLAY "Keep live sensor fields in diagnostic display" ON)
option(OPENTRAIL_CONFIRMATION_EVALUATION "Enable OT216 protected BLE confirmation evaluation" OFF)
# F5 step R1: bench LoRa link test (unauthenticated beacons between boards). Off in
# every normal build; never combine with a release generation.
option(OPENTRAIL_LORA_LINK_TEST "Enable the bench LoRa link test beacons" OFF)
option(OPENTRAIL_LORA_F7_TEXT_TEST "Exercise typed board-to-board text in the LoRa link test" OFF)
option(OPENTRAIL_LORA_F6_PHONE_TEST "Exercise bounded phone-to-phone quick statuses in the LoRa Link Test" OFF)
# Board-pinned qualification builds keep their existing release-generation path.
# A V1 tester build must use a qualified product radio and security composition.
option(OPENTRAIL_V1_PRODUCTION "Request a MeshTrail V1 tester build" OFF)
if(OPENTRAIL_V1_PRODUCTION)
    set(enabled_bench_switches)
    foreach(bench_switch IN ITEMS
            OPENTRAIL_CONNECTION_DIAGNOSTICS
            OPENTRAIL_CONFIRMATION_EVALUATION
            OPENTRAIL_LORA_LINK_TEST
            OPENTRAIL_LORA_F7_TEXT_TEST
            OPENTRAIL_LORA_F6_PHONE_TEST)
        if(${bench_switch})
            list(APPEND enabled_bench_switches "${bench_switch}")
        endif()
    endforeach()
    if(enabled_bench_switches)
        list(JOIN enabled_bench_switches ", " bench_switch_list)
        message(FATAL_ERROR "MeshTrail V1 production rejects enabled bench switches: ${bench_switch_list}")
    endif()
    message(FATAL_ERROR
        "MeshTrail V1 production is unavailable on the Heltec V4 bench target. "
        "With Link Test disabled this target has no product radio or messaging runtime.")
endif()

if(NOT OPENTRAIL_CONNECTION_DIAGNOSTICS AND NOT OPENTRAIL_DIAGNOSTIC_SENSOR_DISPLAY)
    message(FATAL_ERROR "Sensor display isolation requires connection diagnostics")
endif()
if(OPENTRAIL_LORA_F7_TEXT_TEST AND NOT OPENTRAIL_LORA_LINK_TEST)
    message(FATAL_ERROR "The F7 text test requires the LoRa link test")
endif()
option(OPENTRAIL_LORA_F7_PHONE_TEST "Exercise bounded phone-to-phone text in the LoRa Link Test" OFF)
if(OPENTRAIL_LORA_F7_PHONE_TEST AND (NOT OPENTRAIL_LORA_LINK_TEST OR OPENTRAIL_LORA_F7_TEXT_TEST))
    message(FATAL_ERROR "The F7 phone test requires Link Test and excludes the fixed F7 sample")
endif()
if(OPENTRAIL_LORA_F6_PHONE_TEST AND
   (NOT OPENTRAIL_LORA_LINK_TEST OR OPENTRAIL_LORA_F7_TEXT_TEST OR OPENTRAIL_LORA_F7_PHONE_TEST))
    message(FATAL_ERROR "The F6 phone test requires Link Test and excludes the F7 text modes")
endif()
if(OPENTRAIL_LORA_LINK_TEST AND OPENTRAIL_CONFIRMATION_EVALUATION)
    message(FATAL_ERROR "The LoRa link test and the confirmation evaluation are separate builds")
endif()
if(OPENTRAIL_CONFIRMATION_EVALUATION)
    get_filename_component(OPENTRAIL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
    set(PROJECT_VER "ot216-ble-confirmation-v1")

    if(NOT SDKCONFIG_DEFAULTS)
        set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
    endif()
    list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.confirmation-eval.defaults")
endif()
# L7 step 4: the Firmware Loader's release generation. 0 marks a development build, never a
# release; a release build passes -DOPENTRAIL_RELEASE_GENERATION=<n> (n >= 1, strictly increasing).
set(OPENTRAIL_RELEASE_GENERATION "0" CACHE STRING "Firmware Loader release generation (0 = development build)")
if(NOT OPENTRAIL_RELEASE_GENERATION MATCHES "^(0|[1-9][0-9]?[0-9]?[0-9]?[0-9]?[0-9]?[0-9]?[0-9]?[0-9]?)$")
    message(FATAL_ERROR "OPENTRAIL_RELEASE_GENERATION must be 0 or a positive integer below 1000000000")
endif()
if(OPENTRAIL_LORA_LINK_TEST)
    if(NOT SDKCONFIG_DEFAULTS)
        set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
    endif()
    list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.lora-link-test.defaults")
endif()
if(OPENTRAIL_LORA_LINK_TEST AND NOT OPENTRAIL_RELEASE_GENERATION STREQUAL "0")
    message(FATAL_ERROR "The LoRa link test is a development build only (release generation 0)")
endif()
get_filename_component(OPENTRAIL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
list(APPEND EXTRA_COMPONENT_DIRS "${OPENTRAIL_ROOT}/tests/benchmarks/crypto/esp_idf/espressif_libsodium_1_0_22/managed_components/espressif__libsodium")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# I-034: map the Trail checkout path to a fixed name in compiler output, so the same commit
# builds to the same image from any folder. ESP-IDF's reproducible-build mapping covers
# only the IDF and project folders; the shared components sit outside the project folder.
idf_build_set_property(COMPILE_OPTIONS "-ffile-prefix-map=${OPENTRAIL_ROOT}=/opentrail" APPEND)
    idf_build_get_property(OPENTRAIL_PYTHON PYTHON)
    execute_process(COMMAND "${OPENTRAIL_PYTHON}" "${OPENTRAIL_ROOT}/tools/security_evaluation_inputs.py" RESULT_VARIABLE OPENTRAIL_INPUTS)
    if(NOT OPENTRAIL_INPUTS EQUAL 0)
        message(FATAL_ERROR "Evaluation source admission failed")
    endif()
# F5 R1 review F1: RadioLib's SX126x TX launch and calibration wait on BUSY with no
# timeout. With the radio enabled, build the two affected translation units from the
# same pinned generator as the evaluation targets (bounded by RadioLib's own SPI
# BUSY deadline). Inputs are SHA-256 checked; the managed sources are never edited.
if(OPENTRAIL_LORA_LINK_TEST)
    idf_build_get_property(OPENTRAIL_RADIO_PYTHON PYTHON)
    set(OPENTRAIL_RADIOLIB "${CMAKE_CURRENT_LIST_DIR}/managed_components/jgromes__radiolib")
    set(OPENTRAIL_RADIO_CORE "${OPENTRAIL_RADIOLIB}/src/modules/SX126x/SX126x.cpp")
    set(OPENTRAIL_RADIO_CONFIG "${OPENTRAIL_RADIOLIB}/src/modules/SX126x/SX126x_config.cpp")
    set(OPENTRAIL_RADIO_GENERATOR "${OPENTRAIL_ROOT}/tools/pair_radio_driver_source.py")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${OPENTRAIL_RADIO_GENERATOR}" "${OPENTRAIL_ROOT}/tools/radiolib_busy_source.py")
endif()
project(opentrail_heltec_v4_bench)
if(OPENTRAIL_LORA_LINK_TEST)
    execute_process(COMMAND "${OPENTRAIL_RADIO_PYTHON}" "${OPENTRAIL_RADIO_GENERATOR}"
        --core-source "${OPENTRAIL_RADIO_CORE}" --config-source "${OPENTRAIL_RADIO_CONFIG}"
        --core-output "${CMAKE_CURRENT_BINARY_DIR}/bounded_SX126x.cpp"
        --config-output "${CMAKE_CURRENT_BINARY_DIR}/bounded_SX126x_config.cpp"
        RESULT_VARIABLE OPENTRAIL_RADIO_GENERATED)
    if(NOT OPENTRAIL_RADIO_GENERATED EQUAL 0)
        message(FATAL_ERROR "Bounded RadioLib generation failed (pinned source mismatch?)")
    endif()
    idf_component_get_property(OPENTRAIL_RADIO_TARGET jgromes__radiolib COMPONENT_LIB)
    get_target_property(OPENTRAIL_RADIO_SOURCES ${OPENTRAIL_RADIO_TARGET} SOURCES)
    set(OPENTRAIL_RADIO_REPLACED 0)
    set(OPENTRAIL_RADIO_NEW_SOURCES "")
    foreach(OPENTRAIL_SOURCE IN LISTS OPENTRAIL_RADIO_SOURCES)
        get_filename_component(OPENTRAIL_SOURCE_REAL "${OPENTRAIL_SOURCE}" REALPATH)
        get_filename_component(OPENTRAIL_CORE_REAL "${OPENTRAIL_RADIO_CORE}" REALPATH)
        get_filename_component(OPENTRAIL_CONFIG_REAL "${OPENTRAIL_RADIO_CONFIG}" REALPATH)
        if(OPENTRAIL_SOURCE_REAL STREQUAL OPENTRAIL_CORE_REAL)
            list(APPEND OPENTRAIL_RADIO_NEW_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/bounded_SX126x.cpp")
            math(EXPR OPENTRAIL_RADIO_REPLACED "${OPENTRAIL_RADIO_REPLACED} + 1")
        elseif(OPENTRAIL_SOURCE_REAL STREQUAL OPENTRAIL_CONFIG_REAL)
            list(APPEND OPENTRAIL_RADIO_NEW_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/bounded_SX126x_config.cpp")
            math(EXPR OPENTRAIL_RADIO_REPLACED "${OPENTRAIL_RADIO_REPLACED} + 1")
        else()
            list(APPEND OPENTRAIL_RADIO_NEW_SOURCES "${OPENTRAIL_SOURCE}")
        endif()
    endforeach()
    if(NOT OPENTRAIL_RADIO_REPLACED EQUAL 2)
        message(FATAL_ERROR "Expected to replace exactly the two pinned RadioLib units, replaced ${OPENTRAIL_RADIO_REPLACED}")
    endif()
    set_property(TARGET ${OPENTRAIL_RADIO_TARGET} PROPERTY SOURCES "${OPENTRAIL_RADIO_NEW_SOURCES}")
    target_include_directories(${OPENTRAIL_RADIO_TARGET} PRIVATE "${OPENTRAIL_RADIOLIB}/src/modules/SX126x")
    target_compile_options(${OPENTRAIL_RADIO_TARGET} PRIVATE "-ffile-prefix-map=${CMAKE_CURRENT_BINARY_DIR}=/opentrail_build")
endif()

# OT0101e host-stack admission: existing sdkconfig overrides defaults on resume.
if(NOT CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE MATCHES "^[0-9]+$" OR
   CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE LESS 8192)
    message(FATAL_ERROR "OpenTrail BLE host task requires at least 8192 stack bytes")
endif()
# End OT0101e host-stack admission.
# I-051: board pairing (link-test builds) needs the 24576-byte main task stack.
if(OPENTRAIL_LORA_LINK_TEST AND
   (NOT CONFIG_ESP_MAIN_TASK_STACK_SIZE MATCHES "^[0-9]+$" OR
    CONFIG_ESP_MAIN_TASK_STACK_SIZE LESS 24576))
    message(FATAL_ERROR "LoRa link-test pairing requires at least 24576 main task stack bytes; "
        "build with a fresh sdkconfig (-DSDKCONFIG=<build dir>/sdkconfig)")
endif()
