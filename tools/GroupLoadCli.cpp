#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

#include "opentrail/group_load_model.hpp"

namespace {

bool parse_unsigned(std::string_view text, std::uint64_t& output) {
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, output);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 8) {
        std::cerr
            << "usage: group_load_cli <members> <relays> <duration-minutes> "
               "<position-seconds|0> <status-seconds|0> "
               "<alerts-per-member> <source-attempts>\n";
        return EXIT_FAILURE;
    }

    std::uint64_t values[7]{};
    for (int index = 0; index < 7; ++index) {
        if (!parse_unsigned(argv[index + 1], values[index])) {
            std::cerr << "all arguments must be unsigned integers\n";
            return EXIT_FAILURE;
        }
    }
    if (values[2] > std::numeric_limits<std::uint64_t>::max() / 60000U ||
        values[3] > std::numeric_limits<std::uint32_t>::max() / 1000U ||
        values[4] > std::numeric_limits<std::uint32_t>::max() / 1000U ||
        values[5] > std::numeric_limits<std::uint16_t>::max() ||
        values[6] > std::numeric_limits<std::uint8_t>::max()) {
        std::cerr << "one or more arguments are outside the supported range\n";
        return EXIT_FAILURE;
    }

    const auto position_interval =
        static_cast<std::uint32_t>(values[3] * 1000U);
    const auto status_interval =
        static_cast<std::uint32_t>(values[4] * 1000U);
    const opentrail::simulation::GroupTrafficProfile profile{
        static_cast<std::size_t>(values[0]),
        static_cast<std::size_t>(values[1]),
        values[2] * 60000U,
        position_interval,
        position_interval == 0 ? 0U : 38U,
        status_interval,
        status_interval == 0 ? 0U : 22U,
        static_cast<std::uint16_t>(values[5]),
        values[5] == 0 ? 0U : 64U,
        static_cast<std::uint8_t>(values[6]),
        {62500, 8, 7, 5, true, true, false},
    };
    const auto report =
        opentrail::simulation::estimate_group_load(profile);
    if (!report.estimated()) {
        std::cerr << "group-load model rejected the scenario with error "
                  << static_cast<unsigned>(report.error) << '\n';
        return EXIT_FAILURE;
    }

    std::cout
        << "{\"model\":\"OpenTrail group load v0\","
        << "\"scope\":\"scheduled airtime demand only\","
        << "\"members\":" << profile.member_count << ','
        << "\"forwarding_relays\":" << profile.forwarding_relays << ','
        << "\"duration_ms\":" << profile.duration_ms << ','
        << "\"logical_messages\":" << report.logical_messages << ','
        << "\"source_transmissions\":" << report.source_transmissions << ','
        << "\"relay_transmissions\":" << report.relay_transmissions << ','
        << "\"radio_transmissions\":" << report.radio_transmissions << ','
        << "\"total_airtime_us\":" << report.total_airtime_us << ','
        << "\"channel_utilization_ppm\":"
        << report.channel_utilization_ppm << "}\n";
    return EXIT_SUCCESS;
}
