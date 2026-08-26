#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "heltec_v4_gnss.hpp"

namespace gnss = opentrail::target::heltec_v4_bench;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

char hex_digit(std::uint8_t value) {
    return static_cast<char>(value < 10U ? '0' + value : 'A' + value - 10U);
}

std::string sentence(const std::string& body, bool crlf = true) {
    std::uint8_t checksum = 0;
    for (const unsigned char value : body) {
        checksum ^= value;
    }
    std::string result = "$" + body + "*";
    result.push_back(hex_digit(static_cast<std::uint8_t>(checksum >> 4U)));
    result.push_back(hex_digit(static_cast<std::uint8_t>(checksum & 0x0FU)));
    result += crlf ? "\r\n" : "\n";
    return result;
}

gnss::NmeaIngestResult feed(
    gnss::NmeaSatelliteObserver& observer,
    const std::string& value,
    std::uint64_t now_ms) {
    auto result = gnss::NmeaIngestResult::none;
    for (const unsigned char byte : value) {
        const auto current = observer.ingest(byte, now_ms);
        if (current != gnss::NmeaIngestResult::none) {
            result = current;
        }
    }
    return result;
}

void test_exact_v42_binding_constants() {
    require(gnss::kHeltecV4GnssEnableGpio == 34, "GNSS enable GPIO");
    require(gnss::kHeltecV4GnssEnableLevel == 0, "GNSS enable active low");
    require(gnss::kHeltecV4GnssInactiveLevel == 1, "GNSS disabled high");
    require(gnss::kHeltecV4GnssResetGpio == 42, "GNSS reset GPIO");
    require(gnss::kHeltecV4GnssResetAssertedLevel == 0,
            "GNSS reset asserted low");
    require(gnss::kHeltecV4GnssResetReleasedLevel == 1,
            "GNSS reset released high");
    require(gnss::kHeltecV4GnssUartNumber == 1, "GNSS UART1");
    require(gnss::kHeltecV4GnssBaud == 9'600, "GNSS baud");
    require(gnss::kHeltecV4GnssRxGpio == 39, "GNSS RX GPIO");
    require(gnss::kHeltecV4GnssTxGpio == 38, "GNSS TX GPIO");
}

void test_known_gga_and_valid_zero_are_distinct_from_unavailable() {
    gnss::NmeaSatelliteObserver observer;
    require(observer.snapshot(0, 1'000).state ==
                gnss::GnssSatelliteState::unavailable,
            "initially unavailable");

    const std::string known =
        "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
    require(feed(observer, known, 100) ==
                gnss::NmeaIngestResult::observation_accepted,
            "known checksum-valid GGA accepted");
    auto value = observer.snapshot(100, 1'000);
    require(value.fresh() && value.satellites == 8 &&
                value.sampled_at_ms == 100,
            "GGA satellite count only");

    require(feed(observer,
                 sentence("GNGGA,010203.00,,,,,0,00,99.9,,,,,,"),
                 200) == gnss::NmeaIngestResult::observation_accepted,
            "valid zero-satellite GGA accepted");
    value = observer.snapshot(200, 1'000);
    require(value.fresh() && value.satellites == 0,
            "valid zero distinct from unavailable");
}

void test_gns_and_all_supported_talkers() {
    for (const std::string talker : {"GN", "GP", "GA", "BD"}) {
        gnss::NmeaSatelliteObserver observer;
        const auto value = sentence(
            talker + "GNS,092725.00,4717.11399,N,00833.91590,E,AN,12,"
                     "1.0,500.0,46.0,,");
        require(feed(observer, value, 500) ==
                    gnss::NmeaIngestResult::observation_accepted,
                "supported GNS talker accepted");
        require(observer.snapshot(500, 1'000).satellites == 12,
                "GNS satellite count");
    }
}

void test_checksum_type_and_field_fail_closed_without_erasing_last_good() {
    gnss::NmeaSatelliteObserver observer;
    require(feed(observer,
                 sentence("GNGGA,010203.00,,,,,1,09,1.0,,,,,,"),
                 1'000) == gnss::NmeaIngestResult::observation_accepted,
            "baseline accepted");

    auto bad_checksum = sentence("GNGGA,010203.00,,,,,1,10,1.0,,,,,,");
    bad_checksum[bad_checksum.size() - 4] =
        bad_checksum[bad_checksum.size() - 4] == '0' ? '1' : '0';
    require(feed(observer, bad_checksum, 1'100) ==
                gnss::NmeaIngestResult::sentence_rejected,
            "bad checksum rejected");
    require(feed(observer,
                 sentence("GNRMC,010203.00,A,,,,,,,260826,,,A"),
                 1'100) == gnss::NmeaIngestResult::sentence_rejected,
            "unsupported sentence rejected");
    require(feed(observer,
                 sentence("1NGGA,010203.00,,,,,1,10,1.0,,,,,,"),
                 1'100) == gnss::NmeaIngestResult::sentence_rejected,
            "non-letter talker rejected");
    require(feed(observer,
                 sentence("gnGGA,010203.00,,,,,1,10,1.0,,,,,,"),
                 1'100) == gnss::NmeaIngestResult::sentence_rejected,
            "lowercase talker rejected");
    require(feed(observer,
                 sentence("GNGGA,010203.00,,,,,1,,1.0,,,,,,"),
                 1'100) == gnss::NmeaIngestResult::sentence_rejected,
            "empty satellite field rejected");
    require(feed(observer,
                 sentence("GNGGA,010203.00,,,,,1,100,1.0,,,,,,"),
                 1'100) == gnss::NmeaIngestResult::sentence_rejected,
            "three-digit satellite field rejected");

    const auto retained = observer.snapshot(1'100, 1'000);
    require(retained.fresh() && retained.satellites == 9 &&
                retained.sampled_at_ms == 1'000,
            "invalid sentences do not erase last good observation");
}

void test_fresh_stale_future_and_reset_semantics() {
    gnss::NmeaSatelliteObserver observer;
    require(feed(observer,
                 sentence("GNGGA,010203.00,,,,,1,07,1.0,,,,,,", false),
                 1'000) == gnss::NmeaIngestResult::observation_accepted,
            "LF-only sentence accepted");
    require(observer.snapshot(1'099, 100).state ==
                gnss::GnssSatelliteState::valid,
            "fresh immediately before expiry");
    const auto stale = observer.snapshot(1'100, 100);
    require(stale.state == gnss::GnssSatelliteState::stale &&
                stale.satellites == 0 && stale.sampled_at_ms == 1'000,
            "exact freshness boundary stale and hides value");
    require(observer.snapshot(1'000, 0).state ==
                gnss::GnssSatelliteState::stale,
            "zero freshness never publishes");
    require(observer.snapshot(999, 100).state ==
                gnss::GnssSatelliteState::invalid,
            "future observation invalid");
    observer.reset();
    require(observer.snapshot(1'100, 100).state ==
                gnss::GnssSatelliteState::unavailable,
            "reset clears observation");
}

void test_fragmentation_overlength_and_resynchronization() {
    gnss::NmeaSatelliteObserver observer;
    const auto valid = sentence("GNGGA,010203.00,,,,,1,11,1.0,,,,,,");
    for (std::size_t index = 0; index + 1 < valid.size(); ++index) {
        require(observer.ingest(
                    static_cast<std::uint8_t>(valid[index]), 2'000) ==
                    gnss::NmeaIngestResult::none,
                "fragment remains pending");
    }
    require(observer.ingest(static_cast<std::uint8_t>(valid.back()), 2'000) ==
                gnss::NmeaIngestResult::observation_accepted,
            "fragmented sentence accepted on terminator");

    std::string overlong = "$GNGGA,";
    overlong.append(gnss::kHeltecV4MaxNmeaSentenceBytes, '1');
    require(feed(observer, overlong, 2'100) ==
                gnss::NmeaIngestResult::sentence_rejected,
            "overlength sentence rejected");
    require(feed(observer,
                 "noise" + sentence("GNGGA,010203.00,,,,,1,06,1.0,,,,,,"),
                 2'200) == gnss::NmeaIngestResult::observation_accepted,
            "parser resynchronizes on next dollar");
    require(observer.snapshot(2'200, 100).satellites == 6,
            "resynchronized value accepted");
}

void test_host_target_adapter_fails_closed() {
#ifndef ESP_PLATFORM
    gnss::HeltecV4Gnss adapter;
    require(!adapter.initialize(), "host adapter cannot claim target GNSS");
    require(!adapter.initialize(), "host adapter initialization is idempotent");
    adapter.service(100);
    require(!adapter.initialized(), "host adapter remains unavailable");
    require(adapter.satellites(100, 100).state ==
                gnss::GnssSatelliteState::unavailable,
            "host adapter publishes no fabricated satellites");
#endif
}

}  // namespace

int main() {
    test_exact_v42_binding_constants();
    test_known_gga_and_valid_zero_are_distinct_from_unavailable();
    test_gns_and_all_supported_talkers();
    test_checksum_type_and_field_fail_closed_without_erasing_last_good();
    test_fresh_stale_future_and_reset_semantics();
    test_fragmentation_overlength_and_resynchronization();
    test_host_target_adapter_fails_closed();
    std::cout << "7 Heltec V4 GNSS groups passed.\n";
    return 0;
}
