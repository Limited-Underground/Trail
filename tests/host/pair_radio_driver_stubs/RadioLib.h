#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
constexpr std::uint32_t RADIOLIB_NC = 0xffffffffU;
constexpr std::int16_t RADIOLIB_ERR_NONE = 0;
constexpr std::int16_t RADIOLIB_ERR_SPI_CMD_TIMEOUT = -705;
constexpr std::uint16_t RADIOLIB_SX126X_CMD_GET_IRQ_STATUS = 0x12;
constexpr std::uint16_t RADIOLIB_SX126X_CMD_GET_RX_BUFFER_STATUS = 0x13;
constexpr std::uint16_t RADIOLIB_SX126X_CMD_READ_BUFFER = 0x1e;
constexpr std::uint16_t RADIOLIB_SX126X_IRQ_TX_DONE = 1;
constexpr std::uint16_t RADIOLIB_SX126X_IRQ_RX_DONE = 2;
constexpr std::uint16_t RADIOLIB_SX126X_IRQ_HEADER_ERR = 0x20;
constexpr std::uint16_t RADIOLIB_SX126X_IRQ_CRC_ERR = 0x40;
constexpr std::uint16_t RADIOLIB_SX126X_IRQ_TIMEOUT = 0x200;

namespace radio_mock {
inline std::int64_t now_us = 100000;
inline std::vector<std::string> calls;
inline std::string fault, delay_at;
inline unsigned fail_on = 1, matching = 0;
inline std::uint64_t delay_ms = 0;
inline std::uint16_t irq = 0;
inline std::size_t length = 3;
inline std::array<std::uint8_t, 255> packet{}, transmitted{};
inline std::size_t transmitted_bytes = 0;
inline std::array<int, 47> gpio{};
inline std::array<std::uint32_t, 4> module_pins{};
inline std::array<int, 3> spi_pins{};
inline std::array<std::uint32_t, 5> switch_pins{};
inline std::array<std::array<int, 5>, 3> switch_modes{};
inline bool exact_profile = false;
inline std::function<void(const std::string&)> callback;
inline std::int16_t op(const std::string& name) {
    calls.push_back(name);
    if (callback) callback(name);
    if (name == delay_at) now_us += static_cast<std::int64_t>(delay_ms * 1000);
    return name == fault && ++matching == fail_on ? RADIOLIB_ERR_SPI_CMD_TIMEOUT : 0;
}
inline void mode(unsigned value) {
    gpio[7] = value != 0; gpio[2] = value != 0; gpio[46] = value == 2;
}
inline void reset() {
    now_us = 100000; calls.clear(); fault.clear(); delay_at.clear(); fail_on = 1; matching = 0;
    delay_ms = 0; irq = 0; length = 3; packet.fill(0); packet[0] = 7; packet[1] = 8; packet[2] = 9;
    transmitted.fill(0); transmitted_bytes = 0; gpio.fill(0); exact_profile = false; callback = {};
}
inline unsigned count(const std::string& name) {
    unsigned count = 0; for (const auto& call : calls) count += call == name; return count;
}
}

class Module {
public:
    static constexpr std::size_t RFSWITCH_MAX_PINS = 5;
    static constexpr unsigned MODE_IDLE = 0, MODE_RX = 1, MODE_TX = 2;
    struct RfSwitchMode_t { unsigned mode; std::array<int, 5> values; };
    template<class Hal> Module(Hal*, std::uint32_t nss, std::uint32_t dio,
                              std::uint32_t reset, std::uint32_t busy) {
        radio_mock::module_pins = {nss, dio, reset, busy};
    }
    void setRfSwitchTable(const std::uint32_t* pins, const RfSwitchMode_t* table) {
        (void)radio_mock::op("table");
        for (unsigned i = 0; i < 5; ++i) radio_mock::switch_pins[i] = pins[i];
        for (unsigned i = 0; i < 3; ++i) radio_mock::switch_modes[i] = table[i].values;
    }
    std::int16_t SPIreadStream(std::uint16_t command, std::uint8_t* data, std::size_t size,
                               bool = true, bool = true) {
        const auto result = radio_mock::op(command == RADIOLIB_SX126X_CMD_GET_IRQ_STATUS ? "irq" : "rx_length");
        if (result || size != 2) return result ? result : -1;
        if (command == RADIOLIB_SX126X_CMD_GET_IRQ_STATUS) {
            data[0] = static_cast<std::uint8_t>(radio_mock::irq >> 8);
            data[1] = static_cast<std::uint8_t>(radio_mock::irq);
        } else if (command == RADIOLIB_SX126X_CMD_GET_RX_BUFFER_STATUS) {
            data[0] = static_cast<std::uint8_t>(radio_mock::length); data[1] = 64;
        } else return -1;
        return 0;
    }
    std::int16_t SPIreadStream(const std::uint8_t* command, std::uint8_t command_size,
                               std::uint8_t* data, std::size_t size, bool = true, bool = true) {
        const auto result = radio_mock::op("rx_data");
        if (result) return result;
        if (command_size != 2 || command[0] != RADIOLIB_SX126X_CMD_READ_BUFFER || command[1] != 64 ||
            size > radio_mock::packet.size()) return -1;
        for (std::size_t i = 0; i < size; ++i) data[i] = radio_mock::packet[i];
        return 0;
    }
};
#define END_OF_MODE_TABLE {99, {0, 0, 0, 0, 0}}
class SX1262 {
public:
    explicit SX1262(Module*) {}
    std::int16_t begin(float frequency, float bandwidth, unsigned sf, unsigned cr,
                       unsigned sync, int power, unsigned preamble, float tcxo, bool ldo) {
        radio_mock::exact_profile = frequency == 915.0F && bandwidth == 125.0F && sf == 7 &&
            cr == 5 && sync == 0x12 && power == 2 && preamble == 8 && tcxo == 1.8F && !ldo;
        return radio_mock::op("begin");
    }
    std::int16_t explicitHeader() { return radio_mock::op("header"); }
    std::int16_t setCRC(unsigned length) { return length == 2 ? radio_mock::op("crc") : -1; }
    std::int16_t forceLDRO(bool enabled) { return !enabled ? radio_mock::op("ldro") : -1; }
    std::int16_t startReceive() {
        const auto result = radio_mock::op("start_rx"); radio_mock::mode(1); radio_mock::irq = 0; return result;
    }
    std::int16_t startTransmit(const std::uint8_t* data, std::size_t size) {
        const auto result = radio_mock::op("start_tx"); radio_mock::mode(2);
        radio_mock::transmitted_bytes = size;
        for (std::size_t i = 0; i < size; ++i) radio_mock::transmitted[i] = data[i];
        return result;
    }
    std::int16_t finishTransmit() {
        const auto result = radio_mock::op("finish_tx");
        if (!result) { radio_mock::mode(0); radio_mock::irq = 0; } return result;
    }
    std::int16_t finishReceive() {
        const auto result = radio_mock::op("finish_rx");
        if (!result) { radio_mock::mode(0); radio_mock::irq = 0; } return result;
    }
    std::int16_t standby() {
        const auto result = radio_mock::op("standby");
        if (!result) { radio_mock::mode(0); radio_mock::irq = 0; } return result;
    }
};
