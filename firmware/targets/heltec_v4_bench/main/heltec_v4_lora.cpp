#include "heltec_v4_lora.hpp"

#include <algorithm>
#include <cmath>

#include "driver/gpio.h"

namespace opentrail::target::heltec_v4_bench {
namespace {

constexpr std::uint32_t kFemPins[Module::RFSWITCH_MAX_PINS] = {
    7, 2, 46, RADIOLIB_NC, RADIOLIB_NC};
const Module::RfSwitchMode_t kFemModes[] = {
    {Module::MODE_IDLE, {0, 0, 0, 0, 0}},
    {Module::MODE_RX, {1, 1, 0, 0, 0}},
    {Module::MODE_TX, {1, 1, 1, 0, 0}},
    END_OF_MODE_TABLE,
};
constexpr int kDio1Pin = 14;
// A 255-byte frame at SF7/125 kHz is about 0.4 s on air; anything past 2 s
// means the chip did not report TX done.
constexpr std::uint64_t kMaximumTransmitMs = 2000;

}  // namespace

void HeltecV4Lora::fem_off() {
    for (const auto pin : {7, 2, 46}) {
        (void)gpio_set_level(static_cast<gpio_num_t>(pin), 0);
    }
}

void HeltecV4Lora::fault() {
    // After a failed SPI command no further command is issued; the front end
    // is powered down directly and the transport stays off until restart.
    running_ = transmitting_ = receiving_ = false;
    faulted_ = true;
    last_error_ = radio::RadioError::io_failure;
    fem_off();
    tx_bytes_ = rx_bytes_ = 0;
}

bool HeltecV4Lora::ok(std::int16_t result) {
    if (result == RADIOLIB_ERR_NONE) return true;
    fault();
    return false;
}

bool HeltecV4Lora::arm_receive() {
    if (!ok(radio_.startReceive())) return false;
    receiving_ = true;
    return true;
}

bool HeltecV4Lora::start(const LoraProfile& profile) {
    if (running_ || faulted_) return false;
    gpio_config_t config{};
    config.pin_bit_mask = (1ULL << 7) | (1ULL << 2) | (1ULL << 46);
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&config) != ESP_OK) {
        faulted_ = true;
        last_error_ = radio::RadioError::io_failure;
        return false;
    }
    fem_off();
    module_.setRfSwitchTable(kFemPins, kFemModes);
    // TCXO 1.8 V, regulator LDO off (DC-DC), as in the OT-234 driver.
    if (!ok(radio_.begin(profile.frequency_mhz, profile.bandwidth_khz,
                         profile.spreading_factor,
                         profile.coding_rate_denominator, profile.sync_word,
                         profile.power_dbm, profile.preamble_symbols, 1.8F,
                         false)) ||
        !ok(radio_.explicitHeader()) || !ok(radio_.setCRC(2)) ||
        !ok(radio_.forceLDRO(false))) {
        return false;
    }
    frequency_hz_ =
        static_cast<std::uint32_t>(std::lround(profile.frequency_mhz * 1e6F));
    running_ = true;
    last_error_ = radio::RadioError::none;
    return arm_receive();
}

void HeltecV4Lora::stop() {
    if (running_) (void)radio_.standby();
    running_ = transmitting_ = receiving_ = false;
    fem_off();
    tx_bytes_ = rx_bytes_ = 0;
}

radio::TransportStatus HeltecV4Lora::status() const {
    radio::RadioState state = radio::RadioState::offline;
    if (faulted_) {
        state = radio::RadioState::fault;
    } else if (running_) {
        state = transmitting_ ? radio::RadioState::transmitting
                              : radio::RadioState::receiving;
    }
    return {state,
            last_error_,
            kMtu,
            tx_bytes_ != 0 ? 1U : 0U,
            rx_bytes_ != 0 ? 1U : 0U,
            counters_.tx_completed,
            counters_.rx_frames,
            counters_.rx_errors + counters_.rx_dropped};
}

radio::SendResult HeltecV4Lora::send(radio::ByteView frame, std::uint64_t) {
    if (!running_) return {radio::RadioError::not_ready, 0};
    if (frame.data == nullptr || frame.size == 0) {
        return {radio::RadioError::invalid_argument, 0};
    }
    if (frame.size > tx_.size()) return {radio::RadioError::payload_too_large, 0};
    if (tx_bytes_ != 0 || transmitting_) return {radio::RadioError::queue_full, 0};
    std::copy_n(frame.data, frame.size, tx_.begin());
    tx_bytes_ = frame.size;
    return {radio::RadioError::none, frame.size};
}

radio::ReceiveResult HeltecV4Lora::receive(radio::MutableByteView destination) {
    if (rx_bytes_ == 0) {
        return {running_ ? radio::RadioError::no_data : radio::RadioError::not_ready,
                0, {}};
    }
    if (destination.data == nullptr || destination.size < rx_bytes_) {
        return {radio::RadioError::buffer_too_small, rx_bytes_, rx_metadata_};
    }
    const auto count = rx_bytes_;
    std::copy_n(rx_.begin(), count, destination.data);
    rx_bytes_ = 0;
    return {radio::RadioError::none, count, rx_metadata_};
}

void HeltecV4Lora::service(std::uint64_t now_ms) {
    if (!running_) return;

    if (transmitting_) {
        if (now_ms - tx_started_ms_ >= kMaximumTransmitMs) {
            fault();
            return;
        }
    } else if (tx_bytes_ != 0) {
        ++counters_.tx_attempts;
        receiving_ = false;
        tx_started_ms_ = now_ms;
        if (!ok(radio_.startTransmit(tx_.data(), tx_bytes_))) return;
        tx_bytes_ = 0;
        transmitting_ = true;
        return;
    } else if (!receiving_) {
        (void)arm_receive();
        return;
    }

    if (gpio_get_level(static_cast<gpio_num_t>(kDio1Pin)) == 0) return;
    std::uint8_t irq_bytes[2]{};
    if (!ok(module_.SPIreadStream(RADIOLIB_SX126X_CMD_GET_IRQ_STATUS, irq_bytes, 2))) {
        return;
    }
    const auto irq = static_cast<std::uint16_t>((irq_bytes[0] << 8) | irq_bytes[1]);

    if (transmitting_) {
        if ((irq & RADIOLIB_SX126X_IRQ_TIMEOUT) != 0 ||
            (irq & RADIOLIB_SX126X_IRQ_TX_DONE) == 0) {
            fault();
            return;
        }
        if (!ok(radio_.finishTransmit())) return;
        transmitting_ = false;
        ++counters_.tx_completed;
        (void)arm_receive();
        return;
    }

    const bool bad =
        (irq & (RADIOLIB_SX126X_IRQ_CRC_ERR | RADIOLIB_SX126X_IRQ_HEADER_ERR |
                RADIOLIB_SX126X_IRQ_TIMEOUT)) != 0 ||
        (irq & RADIOLIB_SX126X_IRQ_RX_DONE) == 0;
    std::uint8_t buffer_status[2]{};
    if (!bad &&
        !ok(module_.SPIreadStream(RADIOLIB_SX126X_CMD_GET_RX_BUFFER_STATUS,
                                  buffer_status, 2))) {
        return;
    }
    const std::size_t count = buffer_status[0];
    if (bad || count == 0 || count > rx_.size()) {
        ++counters_.rx_errors;
    } else if (rx_bytes_ != 0) {
        // The previous frame has not been taken yet; drop the new one.
        ++counters_.rx_dropped;
    } else {
        const std::uint8_t command[2] = {RADIOLIB_SX126X_CMD_READ_BUFFER,
                                         buffer_status[1]};
        if (!ok(module_.SPIreadStream(command, 2, rx_.data(), count))) return;
        rx_metadata_ = {};
        rx_metadata_.received_at_ms = now_ms;
        rx_metadata_.frequency_hz = frequency_hz_;
        rx_metadata_.frequency_valid = true;
        rx_metadata_.rssi_dbm = static_cast<std::int16_t>(std::lround(radio_.getRSSI()));
        rx_metadata_.rssi_valid = true;
        rx_metadata_.snr_db_quarter = static_cast<std::int16_t>(std::lround(radio_.getSNR() * 4.0F));
        rx_metadata_.snr_valid = true;
        rx_bytes_ = count;
        ++counters_.rx_frames;
    }
    if (!ok(radio_.finishReceive())) return;
    receiving_ = false;
    (void)arm_receive();
}

}  // namespace opentrail::target::heltec_v4_bench
