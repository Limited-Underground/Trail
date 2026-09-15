#include "pair_radio_driver.hpp"
#include <algorithm>
#include "driver/gpio.h"
#include "esp_timer.h"

namespace opentrail::target::heltec_v4_pair_radio_eval {
namespace {
constexpr std::uint32_t fem_pins[Module::RFSWITCH_MAX_PINS] = {7, 2, 46, RADIOLIB_NC, RADIOLIB_NC};
const Module::RfSwitchMode_t fem_modes[] = {
    {Module::MODE_IDLE, {0, 0, 0, 0, 0}},
    {Module::MODE_RX, {1, 1, 0, 0, 0}},
    {Module::MODE_TX, {1, 1, 1, 0, 0}}, END_OF_MODE_TABLE,
};
constexpr std::uint64_t maximum_tx_ms = 2000;
// Public handshake buffers are cleared with volatile stores without depending on
// crypto-library initialization during radio startup or failed cleanup.
template<std::size_t N> void clear(std::array<std::uint8_t, N>& bytes) {
    volatile std::uint8_t* destination = bytes.data();
    for (std::size_t i = 0; i < N; ++i) destination[i] = 0;
}
}

void HeltecPairRadioDriver::wipe_queues() {
    clear(tx_); clear(rx_); tx_bytes_ = rx_bytes_ = 0;
}
bool HeltecPairRadioDriver::sample(std::uint64_t& now) {
    const auto micros = esp_timer_get_time();
    if (micros < 0 || revoked_) return false;
    now = static_cast<std::uint64_t>(micros) / 1000;
    if (clock_seen_ && now < last_ms_) return false;
    last_ms_ = now; clock_seen_ = true;
    return true;
}
bool HeltecPairRadioDriver::fem_off() {
    if (!fem_touched_) return true;
    bool ok = true;
    for (const auto pin : {7, 2, 46}) {
        const auto result = gpio_set_level(static_cast<gpio_num_t>(pin), 0);
        ok = result == ESP_OK && ok;
    }
    return ok && !revoked_;
}
void HeltecPairRadioDriver::contain(radio::RadioError error) {
    // Never issue another SPI command after a failed/late call. Direct external
    // FEM shutdown is separate from claiming the chip has entered standby.
    available_ = transmitting_ = receiving_ = false;
    stop_done_ = true;
    const bool gpio_safe = fem_off();
    stop_ok_ = !chip_touched_ && gpio_safe && !revoked_;
    statistics_.stopped = stop_ok_;
    error_ = error;
    wipe_queues();
}
bool HeltecPairRadioDriver::live() {
    std::uint64_t now = 0;
    if (!available_ || !sample(now) || now >= deadline_ms_ ||
        (transmitting_ && now - tx_started_ms_ >= maximum_tx_ms)) {
        if (available_) contain(radio::RadioError::not_ready);
        return false;
    }
    return true;
}
bool HeltecPairRadioDriver::checked(std::int16_t result) {
    if (result != RADIOLIB_ERR_NONE) { contain(radio::RadioError::io_failure); return false; }
    return live();
}
bool HeltecPairRadioDriver::arm_receive() {
    if (!live()) return false;
    if (!checked(radio_.startReceive())) return false;
    receiving_ = true;
    return true;
}

bool HeltecPairRadioDriver::start(std::uint64_t deadline, unsigned maximum) {
    Lease lease(*this);
    if (!lease.entered) return false;
    if (attempted_start_ || stop_done_) return false;
    attempted_start_ = true;
    std::uint64_t now = 0;
    if (!sample(now) || deadline <= now || deadline - now > 60000 || (maximum != 1 && maximum != 2)) {
        contain(radio::RadioError::invalid_argument); return false;
    }
    deadline_ms_ = deadline; maximum_transmissions_ = maximum;
    available_ = true; statistics_.stopped = false;
    gpio_config_t config{};
    config.pin_bit_mask = (1ULL << 7) | (1ULL << 2) | (1ULL << 46);
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE; config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    fem_touched_ = true;
    if (gpio_config(&config) != ESP_OK || !fem_off() || !live()) {
        contain(radio::RadioError::io_failure); return false;
    }
    module_.setRfSwitchTable(fem_pins, fem_modes);
    if (!live()) return false;
    chip_touched_ = true;
    if (!checked(radio_.begin(915.0F, 125.0F, 7, 5, 0x12, 2, 8, 1.8F, false)) ||
        !checked(radio_.explicitHeader()) || !checked(radio_.setCRC(2)) ||
        !checked(radio_.forceLDRO(false))) return false;
    return arm_receive();
}

radio::TransportStatus HeltecPairRadioDriver::status() const {
    const auto state = !available_ ? (stop_ok_ ? radio::RadioState::offline : radio::RadioState::fault)
        : transmitting_ ? radio::RadioState::transmitting : radio::RadioState::receiving;
    return {state, error_, mtu(), tx_bytes_ ? 1U : 0U, rx_bytes_ ? 1U : 0U,
            statistics_.tx_completed, statistics_.rx_frames, statistics_.rx_errors};
}
radio::SendResult HeltecPairRadioDriver::send(radio::ByteView frame, std::uint64_t now_ms) {
    (void)now_ms; // The driver's trusted deadline uses esp_timer, never packet metadata.
    Lease lease(*this);
    if (!lease.entered || !live()) return {radio::RadioError::not_ready, 0};
    if (!frame.data || !frame.size) return {radio::RadioError::invalid_argument, 0};
    if (frame.size > tx_.size()) return {radio::RadioError::payload_too_large, 0};
    if (tx_bytes_ || transmitting_) return {radio::RadioError::queue_full, 0};
    if (statistics_.tx_attempts >= maximum_transmissions_) return {radio::RadioError::not_ready, 0};
    std::copy_n(frame.data, frame.size, tx_.begin()); tx_bytes_ = frame.size;
    return {radio::RadioError::none, frame.size};
}
radio::ReceiveResult HeltecPairRadioDriver::receive(radio::MutableByteView destination) {
    Lease lease(*this);
    if (!lease.entered || !live()) return {radio::RadioError::not_ready, 0, {}};
    if (!rx_bytes_) return {radio::RadioError::no_data, 0, {}};
    radio::LinkMetadata metadata{}; metadata.received_at_ms = rx_received_ms_;
    if (!destination.data || destination.size < rx_bytes_)
        return {radio::RadioError::buffer_too_small, rx_bytes_, metadata};
    const auto count = rx_bytes_;
    std::copy_n(rx_.begin(), count, destination.data); clear(rx_); rx_bytes_ = 0;
    return {radio::RadioError::none, count, metadata};
}

void HeltecPairRadioDriver::service(std::uint64_t now_ms) {
    (void)now_ms;
    Lease lease(*this);
    if (!lease.entered || !live()) return;
    if (transmitting_) {
        if (last_ms_ - tx_started_ms_ >= maximum_tx_ms) { contain(radio::RadioError::io_failure); return; }
    } else if (tx_bytes_) {
        if (statistics_.tx_attempts >= maximum_transmissions_) { contain(radio::RadioError::not_ready); return; }
        ++statistics_.tx_attempts; // Consume the physical attempt before any TX call.
        tx_started_ms_ = last_ms_; receiving_ = false;
        if (!checked(radio_.startTransmit(tx_.data(), tx_bytes_))) return;
        clear(tx_); tx_bytes_ = 0; transmitting_ = true;
        if (last_ms_ - tx_started_ms_ >= maximum_tx_ms) contain(radio::RadioError::io_failure);
        return;
    } else if (rx_bytes_) return; // Preserve one captured frame in standby.
    else if (!receiving_) { (void)arm_receive(); return; }

    const int irq_pin = gpio_get_level(static_cast<gpio_num_t>(14));
    if (!live()) return;
    if (!irq_pin) return;
    std::uint8_t irq_bytes[2]{};
    if (!checked(module_.SPIreadStream(RADIOLIB_SX126X_CMD_GET_IRQ_STATUS, irq_bytes, 2))) return;
    const auto irq = static_cast<std::uint16_t>((irq_bytes[0] << 8) | irq_bytes[1]);
    if (transmitting_) {
        if ((irq & RADIOLIB_SX126X_IRQ_TIMEOUT) || !(irq & RADIOLIB_SX126X_IRQ_TX_DONE)) {
            contain(radio::RadioError::io_failure); return;
        }
        if (!checked(radio_.finishTransmit())) return;
        transmitting_ = false; ++statistics_.tx_completed;
        if (!rx_bytes_) (void)arm_receive();
        return;
    }
    if ((irq & (RADIOLIB_SX126X_IRQ_CRC_ERR | RADIOLIB_SX126X_IRQ_HEADER_ERR |
                RADIOLIB_SX126X_IRQ_TIMEOUT)) || !(irq & RADIOLIB_SX126X_IRQ_RX_DONE)) {
        ++statistics_.rx_errors;
        if (!checked(radio_.finishReceive())) return;
        receiving_ = false; (void)arm_receive(); return;
    }
    std::uint8_t buffer_status[2]{};
    if (!checked(module_.SPIreadStream(RADIOLIB_SX126X_CMD_GET_RX_BUFFER_STATUS, buffer_status, 2))) return;
    const auto count = buffer_status[0];
    if (!count || count > rx_.size()) {
        ++statistics_.rx_errors;
        if (!checked(radio_.finishReceive())) return;
        receiving_ = false; (void)arm_receive(); return;
    }
    const std::uint8_t command[2] = {RADIOLIB_SX126X_CMD_READ_BUFFER, buffer_status[1]};
    if (!checked(module_.SPIreadStream(command, 2, rx_.data(), count))) return;
    if (!checked(radio_.finishReceive())) return;
    receiving_ = false; rx_bytes_ = count; rx_received_ms_ = last_ms_;
    ++statistics_.rx_frames;
}

bool HeltecPairRadioDriver::stop_locked() {
    if (stop_done_) return stop_ok_ && !revoked_;
    available_ = false; stop_done_ = true;
    // Lower external amplification first. A checked standby is permitted only
    // on a healthy chip; contain() already made uncertain stop terminal.
    const bool gpio_safe = fem_off();
    bool chip_safe = !chip_touched_;
    if (chip_touched_ && gpio_safe && !revoked_) chip_safe = radio_.standby() == RADIOLIB_ERR_NONE;
    const bool gpio_still_safe = fem_off();
    stop_ok_ = gpio_safe && gpio_still_safe && chip_safe && !revoked_;
    statistics_.stopped = stop_ok_;
    transmitting_ = receiving_ = false; wipe_queues();
    if (!stop_ok_) error_ = radio::RadioError::io_failure;
    return stop_ok_;
}
bool HeltecPairRadioDriver::stop() {
    Lease lease(*this);
    return lease.entered && stop_locked();
}
} // namespace opentrail::target::heltec_v4_pair_radio_eval
