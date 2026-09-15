#pragma once
#include <cstddef>
#include <cstdint>
struct usb_serial_jtag_driver_config_t {std::size_t tx_buffer_size,rx_buffer_size;};
int usb_serial_jtag_driver_install(const usb_serial_jtag_driver_config_t*);
int usb_serial_jtag_write_bytes(const void*,std::size_t,std::uint32_t);
int usb_serial_jtag_read_bytes(void*,std::uint32_t,std::uint32_t);
