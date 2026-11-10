#pragma once
#include "Arduino.h"
// Only the API actually called by the target adapter is observed.
struct SPIClass {
    void begin() { p1pro_controlled_io::record(p1pro_controlled_io::Call::spi_begin); }
};
inline SPIClass SPI;
