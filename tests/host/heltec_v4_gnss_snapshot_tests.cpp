#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "heltec_v4_gnss_snapshot.hpp"

using namespace opentrail::target::heltec_v4_bench;
using namespace opentrail::companion;

namespace {
unsigned checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}

std::string sentence(const std::string& body) {
    const char* hex = "0123456789ABCDEF";
    unsigned char checksum = 0;
    for (auto c : body) checksum ^= static_cast<unsigned char>(c);
    return "$" + body + "*" + hex[checksum >> 4] + hex[checksum & 15] + "\r\n";
}
void feed(NmeaSatelliteObserver& observer, const std::string& text,
          std::uint64_t now) {
    for (unsigned char c : text) (void)observer.ingest(c, now);
}

CompanionStatusSnapshot read(HeltecV4GnssSnapshot& state, std::uint64_t now,
                             CompanionGnssState expected) {
    const auto result = state.read(now);
    require(result.ready(), "snapshot must be ready");
    const auto s = result.snapshot;
    require(s.gnss == expected, "incorrect coarse GNSS state");
    require(s.revision != 0 && s.radio == CompanionRadioState::unavailable &&
                s.power == CompanionPowerState::unknown &&
                s.position_sharing == CompanionPositionSharingState::stopped &&
                s.queued_action_count == 0 && s.pending_critical_alert_id == 0,
            "unrelated fields changed or revision invalid");
    return s;
}

void vector(const char* name, const CompanionStatusSnapshot& snapshot) {
    std::array<std::uint8_t, 32> bytes{};
    const auto encoded = encode_companion_status_snapshot(
        snapshot, {bytes.data(), bytes.size()});
    require(encoded.encoded() && encoded.encoded_bytes == 32,
            "existing OTN0 encoder must emit exactly 32 bytes");
    for (unsigned i = 24; i != 32; ++i)
        require(bytes[i] == 0, "reserved byte not zero");
    std::ostringstream actual;
    actual << name << ' ';
    for (auto byte : bytes)
        actual << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<unsigned>(byte);
    std::ifstream fixture(std::filesystem::path(__FILE__).parent_path()
        .parent_path() / "fixtures/heltec_v4_gnss_snapshots.txt");
    bool matches = false;
    for (std::string line; std::getline(fixture, line); ) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        matches = matches || line == actual.str();
    }
    require(matches, "C++ snapshot must match shared Kotlin golden vector");
    std::cout << actual.str() << '\n';
}
}  // namespace

int main() {
    HeltecV4GnssSnapshot state;
    vector("UNKNOWN", read(state, 0, CompanionGnssState::unknown));
    state.publish(false, {}, 1);
    vector("UNAVAILABLE", read(state, 1, CompanionGnssState::unavailable));
    state.publish(true, {}, 2);
    read(state, 2, CompanionGnssState::unknown);

    NmeaSatelliteObserver observer;
    feed(observer, sentence("GPGGA,123519,4807.038,N,01131.000,E,0,08,0.9,545.4,M,46.9,M,,"), 100);
    require(observer.fix(100, 5000).state == GnssFixState::no_fix,
            "satellites alone must not establish fix");
    state.publish(true, observer.fix(100, 5000), 100);
    const auto searching = read(state, 100, CompanionGnssState::searching);
    vector("SEARCHING", searching);
    require(read(state, 4999, CompanionGnssState::searching).revision ==
                searching.revision, "unchanged reading changed revision");

    feed(observer, sentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"), 5000);
    state.publish(true, observer.fix(5000, 5000), 5000);
    const auto current = read(state, 5000, CompanionGnssState::current);
    require(current.revision > searching.revision, "fix change needs new revision");
    vector("CURRENT", current);
    feed(observer, "$GPGGA,broken*00\r\n", 5100);
    state.publish(true, observer.fix(5100, 5000), 5100);
    require(read(state, 9999, CompanionGnssState::current).revision ==
                current.revision, "bad NMEA must preserve still-valid fix");
    // No later app publication: callback-time expiry still applies.
    const auto stale = read(state, 10000, CompanionGnssState::stale);
    require(stale.revision > current.revision, "expiry needs new revision");
    vector("STALE", stale);
    vector("FAULT", read(state, 9999, CompanionGnssState::fault));

    state.publish(true, {GnssFixState::valid, 10001}, 10000);
    read(state, 10001, CompanionGnssState::fault);
    state.publish(true, {GnssFixState::invalid, 0}, 10002);
    read(state, 10002, CompanionGnssState::fault);
    state.publish(true, {GnssFixState::valid, 10003}, 10003);
    read(state, 10003, CompanionGnssState::current);
    state.publish(true, {GnssFixState::no_fix, 10002}, 10002);
    read(state, 10003, CompanionGnssState::fault);
    state.publish(true, {static_cast<GnssFixState>(255), 0}, 10004);
    read(state, 10004, CompanionGnssState::fault);
    state.publish(true, {GnssFixState::stale, 0}, 10005);
    read(state, 10005, CompanionGnssState::stale);

    std::cerr << "GNSS snapshot: " << checks << " checks PASS\n";
}
