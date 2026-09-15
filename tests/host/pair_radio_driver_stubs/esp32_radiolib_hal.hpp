#pragma once
#include "RadioLib.h"
class Esp32RadioLibHal {
public:
    Esp32RadioLibHal(int sck, int miso, int mosi) { radio_mock::spi_pins = {sck, miso, mosi}; }
};
