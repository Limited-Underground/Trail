#pragma once
#include "esp_stub.hpp"
#include <cstddef>

struct usb_serial_jtag_driver_config_t { unsigned tx_buffer_size{}, rx_buffer_size{}; };
esp_err_t usb_serial_jtag_driver_install(const usb_serial_jtag_driver_config_t*);
int usb_serial_jtag_read_bytes(void*, std::size_t, std::uint32_t);
int usb_serial_jtag_write_bytes(const void*, std::size_t, std::uint32_t);
