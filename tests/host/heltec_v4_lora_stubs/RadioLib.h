#pragma once
// Host stand-in for the RadioLib subset used by heltec_v4_lora.cpp (F5 R1).
// Derived from tests/host/pair_radio_driver_stubs, with the chip behavior the
// F5 R1 review relied on (REVIEW-F5-R1.md):
// - SetStandby keeps latched IRQ flags (only clearIrq/finish* clear them);
// - TX staging (startTransmit) clears all IRQ flags and reuses the shared FIFO;
// - DIO1 follows the default routing: RX_DONE while receiving, TX_DONE or
//   TIMEOUT while transmitting; nothing in standby;
// - GetPacketStatus (0x14) returns three bytes.
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
constexpr std::uint16_t RADIOLIB_SX126X_CMD_GET_PACKET_STATUS = 0x14;
constexpr std::uint16_t RADIOLIB_SX126X_CMD_READ_BUFFER = 0x1e;
constexpr std::uint16_t RADIOLIB_SX126X_IRQ_TX_DONE = 0x01;
constexpr std::uint16_t RADIOLIB_SX126X_IRQ_RX_DONE = 0x02;
constexpr std::uint16_t RADIOLIB_SX126X_IRQ_HEADER_VALID = 0x10;
constexpr std::uint16_t RADIOLIB_SX126X_IRQ_HEADER_ERR = 0x20;
constexpr std::uint16_t RADIOLIB_SX126X_IRQ_CRC_ERR = 0x40;
constexpr std::uint16_t RADIOLIB_SX126X_IRQ_TIMEOUT = 0x200;

namespace radio_mock {
enum class Mode { standby, rx, tx };
inline Mode chip_mode = Mode::standby;
inline std::vector<std::string> calls;
inline std::string fault;
inline std::uint16_t irq = 0;
inline std::size_t length = 3;
inline std::array<std::uint8_t, 255> packet{};
inline std::array<std::uint8_t, 3> packet_status{0xA0, 0xFC, 0xB4};
inline std::array<std::uint8_t, 255> transmitted{};
inline std::size_t transmitted_bytes = 0;
inline std::array<int, 47> gpio{};
inline std::int64_t now_us = 100000;
inline std::function<void(const std::string&)> callback;
inline std::int16_t op(const std::string& name) {
    calls.push_back(name);
    if (callback) callback(name);
    return name == fault ? RADIOLIB_ERR_SPI_CMD_TIMEOUT : RADIOLIB_ERR_NONE;
}
inline void reset() {
    chip_mode = Mode::standby; calls.clear(); fault.clear(); irq = 0; length = 3;
    packet.fill(0); packet[0] = 7; packet[1] = 8; packet[2] = 9;
    packet_status = {0xA0, 0xFC, 0xB4};
    transmitted.fill(0); transmitted_bytes = 0; gpio.fill(0); now_us = 100000; callback = {};
}
inline unsigned count(const std::string& name) {
    unsigned n = 0; for (const auto& call : calls) n += call == name; return n;
}
inline bool dio1() {
    if (chip_mode == Mode::rx) return (irq & RADIOLIB_SX126X_IRQ_RX_DONE) != 0;
    if (chip_mode == Mode::tx) return (irq & (RADIOLIB_SX126X_IRQ_TX_DONE | RADIOLIB_SX126X_IRQ_TIMEOUT)) != 0;
    return false;
}
}  // namespace radio_mock

class Module {
public:
    static constexpr std::size_t RFSWITCH_MAX_PINS = 5;
    static constexpr unsigned MODE_IDLE = 0, MODE_RX = 1, MODE_TX = 2;
    struct RfSwitchMode_t { unsigned mode; std::array<int, 5> values; };
    template<class Hal> Module(Hal*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) {}
    void setRfSwitchTable(const std::uint32_t*, const RfSwitchMode_t*) { (void)radio_mock::op("table"); }
    std::int16_t SPIreadStream(std::uint16_t command, std::uint8_t* data, std::size_t size,
                               bool = true, bool = true) {
        if (command == RADIOLIB_SX126X_CMD_GET_IRQ_STATUS) {
            if (const auto r = radio_mock::op("irq")) return r;
            if (size != 2) return -1;
            data[0] = static_cast<std::uint8_t>(radio_mock::irq >> 8);
            data[1] = static_cast<std::uint8_t>(radio_mock::irq);
            return 0;
        }
        if (command == RADIOLIB_SX126X_CMD_GET_RX_BUFFER_STATUS) {
            if (const auto r = radio_mock::op("rx_length")) return r;
            if (size != 2) return -1;
            data[0] = static_cast<std::uint8_t>(radio_mock::length); data[1] = 64;
            return 0;
        }
        if (command == RADIOLIB_SX126X_CMD_GET_PACKET_STATUS) {
            if (const auto r = radio_mock::op("packet_status")) return r;
            if (size != 3) return -1;
            for (std::size_t i = 0; i < 3; ++i) data[i] = radio_mock::packet_status[i];
            return 0;
        }
        return -1;
    }
    std::int16_t SPIreadStream(const std::uint8_t* command, std::uint8_t command_size,
                               std::uint8_t* data, std::size_t size, bool = true, bool = true) {
        if (const auto r = radio_mock::op("rx_data")) return r;
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
    std::int16_t begin(float, float, unsigned, unsigned, unsigned, int, unsigned, float, bool) {
        return radio_mock::op("begin");
    }
    std::int16_t explicitHeader() { return radio_mock::op("header"); }
    std::int16_t setCRC(unsigned) { return radio_mock::op("crc"); }
    std::int16_t forceLDRO(bool) { return radio_mock::op("ldro"); }
    std::int16_t startReceive() {
        const auto r = radio_mock::op("start_rx");
        if (!r) { radio_mock::chip_mode = radio_mock::Mode::rx; radio_mock::irq = 0; }
        return r;
    }
    std::int16_t startTransmit(const std::uint8_t* data, std::size_t size) {
        const auto r = radio_mock::op("start_tx");
        if (!r) {
            radio_mock::irq = 0;  // staging clears every IRQ flag
            radio_mock::chip_mode = radio_mock::Mode::tx;
            radio_mock::transmitted_bytes = size;
            for (std::size_t i = 0; i < size; ++i) radio_mock::transmitted[i] = data[i];
        }
        return r;
    }
    std::int16_t finishTransmit() {
        const auto r = radio_mock::op("finish_tx");
        if (!r) { radio_mock::chip_mode = radio_mock::Mode::standby; radio_mock::irq = 0; }
        return r;
    }
    std::int16_t finishReceive() {
        const auto r = radio_mock::op("finish_rx");
        if (!r) { radio_mock::chip_mode = radio_mock::Mode::standby; radio_mock::irq = 0; }
        return r;
    }
    std::int16_t standby() {
        const auto r = radio_mock::op("standby");
        if (!r) radio_mock::chip_mode = radio_mock::Mode::standby;  // IRQ flags stay latched
        return r;
    }
};
