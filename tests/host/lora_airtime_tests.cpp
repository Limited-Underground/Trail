#include <cstdlib>
#include <iostream>

#include "opentrail/lora_airtime.hpp"

namespace {

using opentrail::radio::LoRaAirtimeError;
using opentrail::radio::LoRaAirtimeSettings;
using opentrail::radio::calculate_lora_airtime;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

void test_current_bench_settings_and_position_frame() {
    const LoRaAirtimeSettings settings{62500, 8, 7, 5, true, true, false};
    const auto result = calculate_lora_airtime(settings, 38);
    EXPECT(result.calculated());
    EXPECT(result.payload_symbols == 68);
    EXPECT(result.airtime_us == 164352);
}

void test_common_125khz_reference_vector() {
    const LoRaAirtimeSettings settings{125000, 8, 7, 5, true, true, false};
    const auto result = calculate_lora_airtime(settings, 12);
    EXPECT(result.calculated());
    EXPECT(result.payload_symbols == 28);
    EXPECT(result.airtime_us == 41216);
}

void test_low_data_rate_optimization_vector() {
    const LoRaAirtimeSettings settings{125000, 8, 12, 5, true, true, true};
    const auto result = calculate_lora_airtime(settings, 20);
    EXPECT(result.calculated());
    EXPECT(result.payload_symbols == 28);
    EXPECT(result.airtime_us == 1318912);
}

void test_invalid_settings_and_payload_limit() {
    auto settings = LoRaAirtimeSettings{62500, 8, 7, 5, true, true, false};
    settings.bandwidth_hz = 0;
    EXPECT(calculate_lora_airtime(settings, 38).error ==
           LoRaAirtimeError::invalid_settings);

    settings = {62500, 8, 4, 5, true, true, false};
    EXPECT(calculate_lora_airtime(settings, 38).error ==
           LoRaAirtimeError::invalid_settings);

    settings = {62500, 8, 7, 9, true, true, false};
    EXPECT(calculate_lora_airtime(settings, 38).error ==
           LoRaAirtimeError::invalid_settings);

    settings = {62500, 8, 7, 5, true, true, false};
    EXPECT(calculate_lora_airtime(settings, 256).error ==
           LoRaAirtimeError::payload_too_large);
}

}  // namespace

int main() {
    test_current_bench_settings_and_position_frame();
    test_common_125khz_reference_vector();
    test_low_data_rate_optimization_vector();
    test_invalid_settings_and_payload_limit();
    if (failures != 0) {
        std::cerr << failures << " LoRa airtime assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 4 LoRa airtime scenario groups\n";
    return EXIT_SUCCESS;
}
