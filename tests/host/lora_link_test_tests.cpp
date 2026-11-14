// Host tests for the bench LoRa link test beacon (V1 checklist F5, step R1):
// firmware/targets/heltec_v4_bench/main/lora_link_test.hpp.

#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "lora_link_test.hpp"

using namespace opentrail::target::heltec_v4_bench::lora_link_test;

namespace {
int failures = 0;
#define EXPECT(condition)                                                  \
    do {                                                                   \
        if (!(condition)) {                                                \
            ++failures;                                                    \
            std::cerr << "FAIL line " << __LINE__ << ": " #condition "\n"; \
        }                                                                  \
    } while (false)

void test_round_trip_and_layout() {
    const Beacon b{0xA1B2C3D4U, 7, 0x01020304U, 0xFFFFFFFFU};
    const auto wire = encode(b);
    EXPECT(wire.size() == kBeaconBytes);
    EXPECT(wire[0] == 'O' && wire[1] == 'T' && wire[2] == 'L' && wire[3] == 'T' && wire[4] == 1);
    EXPECT(wire[5] == 0xA1 && wire[8] == 0xD4);  // big-endian sender
    EXPECT(wire[12] == 7);
    Beacon d{};
    EXPECT(decode(wire.data(), wire.size(), d));
    EXPECT(d.sender == b.sender && d.sequence == b.sequence && d.heard_from == b.heard_from &&
           d.heard_sequence == b.heard_sequence);
}

void test_rejects() {
    const auto good = encode({5, 1, 0, 0});
    Beacon d{};
    EXPECT(!decode(nullptr, kBeaconBytes, d));
    EXPECT(!decode(good.data(), kBeaconBytes - 1, d));
    std::array<std::uint8_t, kBeaconBytes + 1> longer{};
    for (std::size_t i = 0; i < good.size(); ++i) longer[i] = good[i];
    EXPECT(!decode(longer.data(), longer.size(), d));
    auto w = good; w[0] = 'X'; EXPECT(!decode(w.data(), w.size(), d));
    w = good; w[4] = 2; EXPECT(!decode(w.data(), w.size(), d));
    EXPECT(!decode(encode({0, 1, 0, 0}).data(), kBeaconBytes, d));
    EXPECT(!decode(encode({5, 0, 0, 0}).data(), kBeaconBytes, d));
    // A refused frame leaves the output untouched.
    d = {9, 9, 9, 9};
    EXPECT(!decode(w.data(), w.size(), d) && d.sender == 9);
}

void test_schedule_and_hearing() {
    Beaconer a{0x10}, b{0x20};
    EXPECT(period_ms(0x10) != period_ms(0x20));
    EXPECT(period_ms(0x10) >= kBasePeriodMs && period_ms(0x10) < kBasePeriodMs + 997);
    Beacon out{};
    EXPECT(a.due(0, out) && out.sender == 0x10 && out.sequence == 1 && out.heard_from == 0);
    EXPECT(!a.due(period_ms(0x10) - 1, out));
    EXPECT(a.due(period_ms(0x10), out) && out.sequence == 2);
    // Own echo is ignored; another board is recorded and reported in the next beacon.
    EXPECT(!a.heard({0x10, 2, 0, 0}) && a.heard_count() == 0);
    EXPECT(a.heard({0x20, 41, 0x10, 1}) && a.heard_count() == 1);
    EXPECT(a.due(2 * period_ms(0x10), out) && out.heard_from == 0x20 && out.heard_sequence == 41);
    // A zero tag is replaced (sender 0 would be refused by decode).
    Beaconer z{0};
    EXPECT(z.self() == 1);
    (void)b;
}
}  // namespace

int main() {
    test_round_trip_and_layout();
    test_rejects();
    test_schedule_and_hearing();
    if (failures != 0) {
        std::cerr << failures << " LoRa link test assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 3 LoRa link test beacon scenario groups\n";
    return EXIT_SUCCESS;
}
