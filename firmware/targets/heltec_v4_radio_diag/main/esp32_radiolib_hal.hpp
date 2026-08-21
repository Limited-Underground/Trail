#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <RadioLib.h>
#include "driver/spi_master.h"

class Esp32RadioLibHal final : public RadioLibHal {
public:
    Esp32RadioLibHal(int sck, int miso, int mosi);

    void pinMode(uint32_t pin, uint32_t mode) override;
    void digitalWrite(uint32_t pin, uint32_t value) override;
    uint32_t digitalRead(uint32_t pin) override;
    void attachInterrupt(uint32_t interrupt_num, void (*callback)(void), uint32_t mode) override;
    void detachInterrupt(uint32_t interrupt_num) override;
    void delay(RadioLibTime_t milliseconds) override;
    void delayMicroseconds(RadioLibTime_t microseconds) override;
    RadioLibTime_t millis() override;
    RadioLibTime_t micros() override;
    long pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout) override;
    void spiBegin() override;
    void spiBeginTransaction() override;
    void spiTransfer(uint8_t* out, size_t length, uint8_t* in) override;
    void spiEndTransaction() override;
    void spiEnd() override;
    void yield() override;
    void pullUpDown(uint32_t pin, bool enable, bool up) override;

private:
    struct InterruptSlot {
        void (*callback)(void){nullptr};
        uint32_t pin{RADIOLIB_NC};
    };

    static void gpio_isr(void* argument);
    InterruptSlot* slot_for(uint32_t pin);

    int sck_;
    int miso_;
    int mosi_;
    spi_device_handle_t spi_{nullptr};
    bool bus_initialized_{false};
    std::array<InterruptSlot, 4> interrupts_{};
};
