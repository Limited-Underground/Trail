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

void NmeaSatelliteObserver::CoordinateProbe::consume(
    char value, std::uint8_t degree_digits) {
    if (value == '.') {
        if (decimal || whole_digits != degree_digits + 2U) invalid = true;
        decimal = true;
        return;
    }
    if (value < '0' || value > '9') {
        invalid = true;
        return;
    }
    const auto digit = static_cast<std::uint8_t>(value - '0');
    if (!decimal) {
        if (whole_digits >= degree_digits + 2U) {
            invalid = true;
            return;
        }
        if (whole_digits < degree_digits)
            degrees = static_cast<std::uint16_t>(degrees * 10U + digit);
        else
            minutes = static_cast<std::uint8_t>(minutes * 10U + digit);
        ++whole_digits;
    } else {
        if (fractional_digits >= 8U) invalid = true;
        else {
            fractional_nonzero |= digit != 0;
            ++fractional_digits;
        }
    }
}

bool NmeaSatelliteObserver::CoordinateProbe::valid(
    std::uint8_t degree_digits, std::uint16_t max_degrees) const {
    return !invalid && whole_digits == degree_digits + 2U && decimal &&
           fractional_digits != 0 && minutes < 60U &&
           degrees <= max_degrees &&
           (degrees != max_degrees || (minutes == 0 && !fractional_nonzero));
}

void NmeaSatelliteObserver::clear_candidate_position() {
    latitude_ = {};
    longitude_ = {};
    north_south_bytes_ = 0;
    east_west_bytes_ = 0;
    north_south_valid_ = false;
    east_west_valid_ = false;
}

void NmeaSatelliteObserver::begin_sentence() {
    parse_state_ = ParseState::body;
    sentence_bytes_ = 1;
    checksum_ = 0;
    expected_checksum_ = 0;
    field_index_ = 0;
    sentence_kind_bytes_ = 0;
    candidate_satellites_ = 0;
    satellite_digits_ = 0;
    fix_field_bytes_ = 0;
    candidate_fix_ = false;
    candidate_non_gnss_mode_ = false;
    clear_candidate_position();
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
    } else if (field_index_ == 6) {
        if (fix_field_bytes_ == 0) {
            candidate_invalid_ = true;
        }
        if (sentence_kind_[3] == 'N' && candidate_non_gnss_mode_) {
            candidate_fix_ = false;
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

    if (field_index_ == 2) {
        latitude_.consume(value, 2);
        return;
    }
    if (field_index_ == 3) {
        north_south_valid_ = north_south_bytes_ == 0 &&
                             (value == 'N' || value == 'S');
        ++north_south_bytes_;
        return;
    }
    if (field_index_ == 4) {
        longitude_.consume(value, 3);
        return;
    }
    if (field_index_ == 5) {
        east_west_valid_ = east_west_bytes_ == 0 &&
                           (value == 'E' || value == 'W');
        ++east_west_bytes_;
        return;
    }
    if (field_index_ == 6) {
        if (sentence_kind_[3] == 'G') {
            // GGA 0 is no fix; 1..5 are satellite-derived fixes. Estimated,
            // manual and simulated quality must not claim a GNSS fix.
            if (fix_field_bytes_ != 0 || value < '0' || value > '8') {
                candidate_invalid_ = true;
            } else {
                candidate_fix_ = value >= '1' && value <= '5';
            }
        } else {
            // GNS permits one mode per constellation. A/D/P/R/F identify
            // satellite-derived modes; E/M/S are not GNSS fixes. Any such
            // mode makes the combined sentence unsuitable for a fix claim.
            if (fix_field_bytes_ >= 4) {
                candidate_invalid_ = true;
            } else if (value == 'A' || value == 'D' || value == 'P' ||
                       value == 'R' || value == 'F') {
                candidate_fix_ = true;
            } else if (value == 'E' || value == 'M' || value == 'S') {
                candidate_non_gnss_mode_ = true;
            } else if (value != 'N') {
                candidate_invalid_ = true;
            }
        }
        ++fix_field_bytes_;
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
           fix_field_bytes_ != 0 && satellite_digits_ != 0 &&
           checksum_ == expected_checksum_;
}

NmeaIngestResult NmeaSatelliteObserver::reject_sentence() {
    parse_state_ = ParseState::discard;
    clear_candidate_position();
    return NmeaIngestResult::sentence_rejected;
}

NmeaIngestResult NmeaSatelliteObserver::ingest(
    std::uint8_t byte,
    std::uint64_t received_at_ms) {
    if (byte == static_cast<std::uint8_t>('$')) {
        begin_sentence();
        last_byte_at_ms_ = received_at_ms;
        return NmeaIngestResult::none;
    }

    if (parse_state_ == ParseState::waiting_for_start ||
        parse_state_ == ParseState::discard) {
        return NmeaIngestResult::none;
    }
    last_byte_at_ms_ = received_at_ms;

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
                clear_candidate_position();
                return NmeaIngestResult::sentence_rejected;
            }
            has_observation_ = true;
            // A receiver flag without bounded coordinates is not a usable
            // location fix. The coordinates are inspected but never retained.
            fix_valid_ = candidate_fix_ && candidate_satellites_ != 0 &&
                         latitude_.valid(2, 90) &&
                         longitude_.valid(3, 180) &&
                         north_south_bytes_ == 1 && north_south_valid_ &&
                         east_west_bytes_ == 1 && east_west_valid_;
            satellites_ = candidate_satellites_;
            sampled_at_ms_ = received_at_ms;
            clear_candidate_position();
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

GnssFixObservation NmeaSatelliteObserver::fix(
    std::uint64_t now_ms,
    std::uint64_t fresh_for_ms) const {
    if (!has_observation_) return {};
    if (now_ms < sampled_at_ms_) return {GnssFixState::invalid, 0};
    if (fresh_for_ms == 0 || now_ms - sampled_at_ms_ >= fresh_for_ms)
        return {GnssFixState::stale, sampled_at_ms_};
    return {fix_valid_ ? GnssFixState::valid : GnssFixState::no_fix,
            sampled_at_ms_};
}

void NmeaSatelliteObserver::expire_partial(std::uint64_t now_ms) {
    constexpr std::uint64_t kPartialSentenceTimeoutMs = 1'000;
    if (parse_state_ == ParseState::waiting_for_start ||
        parse_state_ == ParseState::discard) return;
    if (now_ms < last_byte_at_ms_ ||
        now_ms - last_byte_at_ms_ >= kPartialSentenceTimeoutMs) {
        clear_candidate_position();
        parse_state_ = ParseState::discard;
    }
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
    fix_field_bytes_ = 0;
    candidate_fix_ = false;
    candidate_non_gnss_mode_ = false;
    clear_candidate_position();
    candidate_supported_ = false;
    candidate_invalid_ = false;
    saw_carriage_return_ = false;
    has_observation_ = false;
    fix_valid_ = false;
    satellites_ = 0;
    sampled_at_ms_ = 0;
    last_byte_at_ms_ = 0;
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
    observer_.expire_partial(now_ms);
    std::uint8_t buffer[128]{};
    constexpr auto uart = static_cast<uart_port_t>(kHeltecV4GnssUartNumber);
    const int received = uart_read_bytes(uart, buffer, sizeof(buffer), 0);
    if (received < 0) {
        ++diagnostics_.uart_read_errors;
        return;
    }
    if (received <= 0) {
        return;
    }
    diagnostics_.bytes_received += static_cast<std::uint64_t>(received);
    for (int index = 0; index < received; ++index) {
        const auto result = observer_.ingest(buffer[index], now_ms);
        if (result == NmeaIngestResult::observation_accepted) {
            ++diagnostics_.accepted_sentences;
        } else if (result == NmeaIngestResult::sentence_rejected) {
            ++diagnostics_.rejected_sentences;
        }
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
