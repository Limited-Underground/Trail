#include "heltec_v4_gnss.hpp"

#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#endif

namespace opentrail::target::heltec_v4_bench {
namespace {

int hex_value(std::uint8_t value) {
    if (value >= static_cast<std::uint8_t>('0') &&
        value <= static_cast<std::uint8_t>('9')) {
        return value - static_cast<std::uint8_t>('0');
    }
    if (value >= static_cast<std::uint8_t>('A') &&
        value <= static_cast<std::uint8_t>('F')) {
        return value - static_cast<std::uint8_t>('A') + 10;
    }
    if (value >= static_cast<std::uint8_t>('a') &&
        value <= static_cast<std::uint8_t>('f')) {
        return value - static_cast<std::uint8_t>('a') + 10;
    }
    return -1;
}

bool supported_sentence_kind(const char (&value)[5]) {
    for (std::size_t index = 0; index < 2; ++index) {
        if (value[index] < 'A' || value[index] > 'Z') {
            return false;
        }
    }
    return (value[2] == 'G' && value[3] == 'G' && value[4] == 'A') ||
           (value[2] == 'G' && value[3] == 'N' && value[4] == 'S');
}

}  // namespace

void NmeaSatelliteObserver::begin_sentence() {
    parse_state_ = ParseState::body;
    sentence_bytes_ = 1;
    checksum_ = 0;
    expected_checksum_ = 0;
    field_index_ = 0;
    sentence_kind_bytes_ = 0;
    candidate_satellites_ = 0;
    satellite_digits_ = 0;
    candidate_supported_ = false;
    candidate_invalid_ = false;
    saw_carriage_return_ = false;
    for (char& value : sentence_kind_) {
        value = '\0';
    }
}

void NmeaSatelliteObserver::finish_field() {
    if (field_index_ == 0) {
        candidate_supported_ =
            sentence_kind_bytes_ == 5 && supported_sentence_kind(sentence_kind_);
        if (!candidate_supported_) {
            candidate_invalid_ = true;
        }
    } else if (field_index_ == 7 && satellite_digits_ == 0) {
        candidate_invalid_ = true;
    }
}

void NmeaSatelliteObserver::consume_body_character(char value) {
    if (value == ',') {
        finish_field();
        if (field_index_ != 0xFFU) {
            ++field_index_;
        } else {
            candidate_invalid_ = true;
        }
        return;
    }

    if (field_index_ == 0) {
        if (sentence_kind_bytes_ < 5) {
            sentence_kind_[sentence_kind_bytes_++] = value;
        } else {
            candidate_invalid_ = true;
        }
        return;
    }

    if (field_index_ != 7) {
        return;
    }
    if (value < '0' || value > '9' || satellite_digits_ >= 2) {
        candidate_invalid_ = true;
        return;
    }
    candidate_satellites_ = static_cast<std::uint8_t>(
        candidate_satellites_ * 10U + static_cast<std::uint8_t>(value - '0'));
    ++satellite_digits_;
}

bool NmeaSatelliteObserver::candidate_complete() const {
    return candidate_supported_ && !candidate_invalid_ && field_index_ >= 7 &&
           satellite_digits_ != 0 && checksum_ == expected_checksum_;
}

NmeaIngestResult NmeaSatelliteObserver::reject_sentence() {
    parse_state_ = ParseState::discard;
    return NmeaIngestResult::sentence_rejected;
}

NmeaIngestResult NmeaSatelliteObserver::ingest(
    std::uint8_t byte,
    std::uint64_t received_at_ms) {
    if (byte == static_cast<std::uint8_t>('$')) {
        begin_sentence();
        return NmeaIngestResult::none;
    }

    if (parse_state_ == ParseState::waiting_for_start ||
        parse_state_ == ParseState::discard) {
        return NmeaIngestResult::none;
    }

    ++sentence_bytes_;
    if (sentence_bytes_ > kHeltecV4MaxNmeaSentenceBytes) {
        return reject_sentence();
    }

    switch (parse_state_) {
        case ParseState::body:
            if (byte == static_cast<std::uint8_t>('*')) {
                finish_field();
                parse_state_ = ParseState::checksum_high;
                return NmeaIngestResult::none;
            }
            if (byte == static_cast<std::uint8_t>('\r') ||
                byte == static_cast<std::uint8_t>('\n') || byte < 0x20U ||
                byte > 0x7EU) {
                return reject_sentence();
            }
            checksum_ ^= byte;
            consume_body_character(static_cast<char>(byte));
            return NmeaIngestResult::none;

        case ParseState::checksum_high: {
            const int value = hex_value(byte);
            if (value < 0) {
                return reject_sentence();
            }
            expected_checksum_ = static_cast<std::uint8_t>(value << 4U);
            parse_state_ = ParseState::checksum_low;
            return NmeaIngestResult::none;
        }

        case ParseState::checksum_low: {
            const int value = hex_value(byte);
            if (value < 0) {
                return reject_sentence();
            }
            expected_checksum_ = static_cast<std::uint8_t>(
                expected_checksum_ | static_cast<std::uint8_t>(value));
            parse_state_ = ParseState::line_end;
            return NmeaIngestResult::none;
        }

        case ParseState::line_end:
            if (byte == static_cast<std::uint8_t>('\r') &&
                !saw_carriage_return_) {
                saw_carriage_return_ = true;
                return NmeaIngestResult::none;
            }
            if (byte != static_cast<std::uint8_t>('\n')) {
                return reject_sentence();
            }
            parse_state_ = ParseState::waiting_for_start;
            if (!candidate_complete()) {
                return NmeaIngestResult::sentence_rejected;
            }
            has_observation_ = true;
            satellites_ = candidate_satellites_;
            sampled_at_ms_ = received_at_ms;
            return NmeaIngestResult::observation_accepted;

        case ParseState::waiting_for_start:
        case ParseState::discard:
        default:
            return NmeaIngestResult::none;
    }
}

GnssSatelliteObservation NmeaSatelliteObserver::snapshot(
    std::uint64_t now_ms,
    std::uint64_t fresh_for_ms) const {
    if (!has_observation_) {
        return {};
    }
    if (now_ms < sampled_at_ms_) {
        return {GnssSatelliteState::invalid, 0, 0};
    }
    if (fresh_for_ms == 0 || now_ms - sampled_at_ms_ >= fresh_for_ms) {
        return {GnssSatelliteState::stale, 0, sampled_at_ms_};
    }
    return {GnssSatelliteState::valid, satellites_, sampled_at_ms_};
}

void NmeaSatelliteObserver::reset() {
    parse_state_ = ParseState::waiting_for_start;
    sentence_bytes_ = 0;
    checksum_ = 0;
    expected_checksum_ = 0;
    field_index_ = 0;
    sentence_kind_bytes_ = 0;
    candidate_satellites_ = 0;
    satellite_digits_ = 0;
    candidate_supported_ = false;
    candidate_invalid_ = false;
    saw_carriage_return_ = false;
    has_observation_ = false;
    satellites_ = 0;
    sampled_at_ms_ = 0;
}

#ifdef ESP_PLATFORM
bool HeltecV4Gnss::initialize() {
    if (attempted_) {
        return initialized_;
    }
    attempted_ = true;

    constexpr auto uart = static_cast<uart_port_t>(kHeltecV4GnssUartNumber);
    bool uart_driver_installed = false;
    const auto contain_failure = [&]() {
        if (uart_driver_installed) {
            static_cast<void>(uart_driver_delete(uart));
        }
        static_cast<void>(gpio_set_level(
            static_cast<gpio_num_t>(kHeltecV4GnssResetGpio),
            kHeltecV4GnssResetAssertedLevel));
        static_cast<void>(gpio_set_level(
            static_cast<gpio_num_t>(kHeltecV4GnssEnableGpio),
            kHeltecV4GnssInactiveLevel));
        initialized_ = false;
        return false;
    };

    gpio_config_t output_config{};
    output_config.pin_bit_mask =
        (UINT64_C(1) << kHeltecV4GnssEnableGpio) |
        (UINT64_C(1) << kHeltecV4GnssResetGpio);
    output_config.mode = GPIO_MODE_OUTPUT;
    output_config.pull_up_en = GPIO_PULLUP_DISABLE;
    output_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    output_config.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&output_config) != ESP_OK ||
        gpio_set_level(
            static_cast<gpio_num_t>(kHeltecV4GnssEnableGpio),
            kHeltecV4GnssInactiveLevel) != ESP_OK ||
        gpio_set_level(
            static_cast<gpio_num_t>(kHeltecV4GnssResetGpio),
            kHeltecV4GnssResetAssertedLevel) != ESP_OK) {
        return contain_failure();
    }

    uart_config_t uart_config{};
    uart_config.baud_rate = static_cast<int>(kHeltecV4GnssBaud);
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_DEFAULT;
    if (uart_param_config(uart, &uart_config) != ESP_OK ||
        uart_set_pin(
            uart,
            kHeltecV4GnssTxGpio,
            kHeltecV4GnssRxGpio,
            UART_PIN_NO_CHANGE,
            UART_PIN_NO_CHANGE) != ESP_OK ||
        uart_driver_install(uart, 512, 0, 0, nullptr, 0) != ESP_OK) {
        return contain_failure();
    }
    uart_driver_installed = true;

    if (gpio_set_level(
            static_cast<gpio_num_t>(kHeltecV4GnssEnableGpio),
            kHeltecV4GnssEnableLevel) != ESP_OK ||
        gpio_set_level(
            static_cast<gpio_num_t>(kHeltecV4GnssResetGpio),
            kHeltecV4GnssResetReleasedLevel) != ESP_OK) {
        return contain_failure();
    }

    initialized_ = true;
    return true;
}

void HeltecV4Gnss::service(std::uint64_t now_ms) {
    if (!initialized_) {
        return;
    }
    std::uint8_t buffer[128]{};
    constexpr auto uart = static_cast<uart_port_t>(kHeltecV4GnssUartNumber);
    const int received = uart_read_bytes(uart, buffer, sizeof(buffer), 0);
    if (received <= 0) {
        return;
    }
    for (int index = 0; index < received; ++index) {
        static_cast<void>(observer_.ingest(buffer[index], now_ms));
    }
}
#else
bool HeltecV4Gnss::initialize() {
    attempted_ = true;
    initialized_ = false;
    return false;
}

void HeltecV4Gnss::service(std::uint64_t) {}
#endif

}  // namespace opentrail::target::heltec_v4_bench
