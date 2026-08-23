idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "." "../../include"
    PRIV_REQUIRES espressif__libsodium esp_driver_usb_serial_jtag esp_timer freertos log
)
