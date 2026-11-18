#pragma once
#include "RadioLib.h"
class Esp32RadioLibHal {
public:
    Esp32RadioLibHal(int, int, int) {}
    void yield() { radio_mock::now_us += 1000; }
};
