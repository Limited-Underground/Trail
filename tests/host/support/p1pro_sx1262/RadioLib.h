#pragma once
// Controlled API observations matching the used RadioLib 7.1.2 signatures.
// This is not a radio driver and does not model physical timing or RF behavior.
#include "Arduino.h"
#define RADIOLIB_NC (0xFFFFFFFFUL)
#define RADIOLIB_ERR_NONE (0)
#define RADIOLIB_ERR_CHIP_NOT_FOUND (-2)
#define RADIOLIB_ERR_INVALID_FREQUENCY (-12)
#define RADIOLIB_ERR_SPI_CMD_TIMEOUT (-705)
#define RADIOLIB_ERR_SPI_CMD_FAILED (-707)
#define RADIOLIB_SX126X_CMD_GET_IRQ_STATUS (0x12)

class Module {
public:
    Module(std::uint32_t cs, std::uint32_t irq, std::uint32_t reset, std::uint32_t gpio) {
        p1pro_controlled_io::module_pins = {cs, irq, reset, gpio};
    }
    std::int16_t SPIreadStream(std::uint16_t cmd, std::uint8_t* data, std::size_t length,
        bool waitForGpio = true, bool verify = true) {
        using namespace p1pro_controlled_io;
        record(Call::irq_read); irq_command = cmd; irq_length = length;
        irq_wait = waitForGpio; irq_verify = verify;
        // Deliberately populate bytes even on failure: caller must distrust them.
        if (length >= 2) {
            data[0] = static_cast<std::uint8_t>(irq_value >> 8);
            data[1] = static_cast<std::uint8_t>(irq_value);
        }
        return irq_result;
    }
};
class SX1262 {
public:
    explicit SX1262(Module* mod) : module_(mod) {}
    void setRfSwitchPins(std::uint32_t rx, std::uint32_t tx) {
        p1pro_controlled_io::record(p1pro_controlled_io::Call::rf_switch);
        p1pro_controlled_io::rx_pin = rx; p1pro_controlled_io::tx_pin = tx;
    }
    std::int16_t begin(float freq, float bw, std::uint8_t sf, std::uint8_t cr,
        std::uint8_t syncWord, std::int8_t outputPower, std::uint16_t preambleLength,
        float tcxoVoltage, bool useRegulatorLDO) {
        using namespace p1pro_controlled_io;
        record(Call::begin); frequency = freq; bandwidth = bw; spreading = sf;
        coding = cr; sync = syncWord; power = outputPower;
        preamble = preambleLength; tcxo = tcxoVoltage; regulator_ldo = useRegulatorLDO;
        return begin_result;
    }
    std::int16_t setDio2AsRfSwitch(bool enable) {
        p1pro_controlled_io::record(p1pro_controlled_io::Call::dio2);
        p1pro_controlled_io::dio2_enabled = enable; return p1pro_controlled_io::dio2_result;
    }
    std::int16_t startReceive() {
        p1pro_controlled_io::record(p1pro_controlled_io::Call::receive);
        return p1pro_controlled_io::receive_result;
    }
    // Included to reproduce the old value-only API's lost-error behavior.
    std::uint32_t getIrqFlags() {
        std::uint8_t data[2]{};
        (void)module_->SPIreadStream(RADIOLIB_SX126X_CMD_GET_IRQ_STATUS, data, 2);
        return (static_cast<std::uint32_t>(data[0]) << 8) | data[1];
    }
private:
    Module* module_;
};
