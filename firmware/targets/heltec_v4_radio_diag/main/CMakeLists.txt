idf_component_register(
    SRCS
        "app_main.cpp"
        "esp32_radiolib_hal.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_gpio
        esp_driver_spi
        esp_timer
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
