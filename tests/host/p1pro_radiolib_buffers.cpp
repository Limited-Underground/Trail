// Runs the unmodified pinned RadioLib Module against an in-memory SPI observer.
#include <Module.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

class MemoryHal final : public RadioLibHal {
public:
    MemoryHal() : RadioLibHal(0, 1, 0, 1, 1, 0) {}
    std::array<std::uint8_t, 32> sent{};
    std::size_t count = 0;
    void pinMode(std::uint32_t, std::uint32_t) override {}
    void digitalWrite(std::uint32_t, std::uint32_t) override {}
    std::uint32_t digitalRead(std::uint32_t) override { return 0; }
    void attachInterrupt(std::uint32_t, void (*)(void), std::uint32_t) override {}
    void detachInterrupt(std::uint32_t) override {}
    void delay(RadioLibTime_t) override {}
    void delayMicroseconds(RadioLibTime_t) override {}
    RadioLibTime_t millis() override { return 0; }
    RadioLibTime_t micros() override { return 0; }
    long pulseIn(std::uint32_t, std::uint32_t, RadioLibTime_t) override { return 0; }
    void spiBegin() override {}
    void spiBeginTransaction() override {}
    void spiEndTransaction() override {}
    void spiEnd() override {}
    void spiTransfer(std::uint8_t* out, std::size_t len, std::uint8_t* in) override {
        assert(len <= sent.size());
        count = len;
        std::copy_n(out, len, sent.begin());
        std::fill_n(in, len, 0xa5);
    }
};

int main() {
    static_assert(RADIOLIB_STATIC_ONLY == 0, "I-011 proof binds dynamic transfer buffers");
    static_assert(RADIOLIB_SPI_PARANOID == 1, "Status-path coverage requires paranoid SPI");
    MemoryHal hal;
    Module module(&hal, 4, 1, 2, 3);
    // These are the pinned SX126x::modSetup values independently checked by the runner.
    module.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_CMD] = Module::BITS_8;
    module.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_ADDR] = Module::BITS_16;
    module.spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_READ] = 0x1d;
    module.spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_WRITE] = 0x0d;
    module.spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_STATUS] = 0xc0;
    module.spiConfig.stream = true;
    assert(module.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_STATUS] == Module::BITS_8);
    for (std::size_t n = 0; n <= 16; ++n) {
        std::array<std::uint8_t, 18> data{};
        data.fill(0x5a);
        module.SPIreadRegisterBurst(0x1234, n, data.data() + 1);
        assert(hal.count == 4 + n);
        assert(hal.sent[0] == 0x1d && hal.sent[1] == 0x12 && hal.sent[2] == 0x34);
        assert(data.front() == 0x5a && data[n + 1] == 0x5a);
        for (std::size_t i = 1; i <= n; ++i) assert(data[i] == 0xa5);
    }
    assert(module.SPIreadRegister(0x1234) == 0xa5 && hal.count == 5);
    for (std::size_t n = 0; n <= 2; ++n) {
        std::array<std::uint8_t, 2> data{0x53, 0x69};
        module.SPIwriteRegisterBurst(0x1234, data.data(), n);
        assert(hal.count == 3 + n && hal.sent[0] == 0x0d);
        for (std::size_t i = 0; i < n; ++i) assert(hal.sent[3 + i] == data[i]);
    }
    module.SPIwriteRegister(0x1234, 0x77);
    assert(hal.count == 4 && hal.sent[3] == 0x77);
    for (std::size_t n = 0; n <= 2; ++n) {
        std::array<std::uint8_t, 4> data{};
        data.fill(0x5a);
        assert(module.SPIreadStream(0x12, data.data() + 1, n, true, false) == 0);
        assert(hal.count == 2 + n && hal.sent[0] == 0x12);
        assert(data[0] == 0x5a && data[n + 1] == 0x5a);
    }
    for (std::size_t n = 0; n <= 9; ++n) {
        std::array<std::uint8_t, 9> data{};
        data.fill(0x53);
        assert(module.SPIwriteStream(0x8c, data.data(), n, true, false) == 0);
        assert(hal.count == 1 + n && hal.sent[0] == 0x8c);
        for (std::size_t i = 0; i < n; ++i) assert(hal.sent[1 + i] == 0x53);
    }
    assert(module.SPIcheckStream() == 0);
    assert(hal.count == 3 && hal.sent[0] == 0xc0);
    std::cout << "PASS: 7 pinned RadioLib command-header paths\n";
}
