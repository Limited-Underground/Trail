#pragma once
#include <cstdint>
int usb_serial_jtag_ll_txfifo_writable();
std::uint32_t usb_serial_jtag_ll_write_txfifo(const std::uint8_t*,std::uint32_t);
void usb_serial_jtag_ll_txfifo_flush();
