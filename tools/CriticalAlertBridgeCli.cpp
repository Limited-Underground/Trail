#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "opentrail/critical_alert_ack_responder.hpp"

namespace {

using namespace opentrail::integration;

std::uint64_t parse_u64(const char* text) {
    std::size_t consumed = 0;
    const auto value = std::stoull(text, &consumed, 0);
    if (text[consumed] != '\0') {
        throw std::invalid_argument("invalid 64-bit integer");
    }
    return value;
}

std::uint32_t parse_u32(const char* text) {
    const auto value = parse_u64(text);
    if (value > 0xFFFFFFFFULL) {
        throw std::invalid_argument("invalid 32-bit integer");
    }
    return static_cast<std::uint32_t>(value);
}

std::string to_hex(const std::uint8_t* bytes, std::size_t size) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < size; ++index) {
        output << std::setw(2) << static_cast<unsigned>(bytes[index]);
    }
    return output.str();
}

std::vector<std::uint8_t> from_hex(const std::string& text) {
    if (text.size() % 2 != 0) {
        throw std::invalid_argument("hex input must have an even length");
    }
    std::vector<std::uint8_t> bytes(text.size() / 2);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const auto pair = text.substr(index * 2, 2);
        std::size_t consumed = 0;
        const auto value = std::stoul(pair, &consumed, 16);
        if (consumed != pair.size()) {
            throw std::invalid_argument("invalid hex input");
        }
        bytes[index] = static_cast<std::uint8_t>(value);
    }
    return bytes;
}

enum class ResponseMode {
    accepted,
    stale,
    rate_limited,
};

int respond(int argc, char** argv, ResponseMode mode) {
    if (argc != 9) {
        throw std::invalid_argument(
            "response command requires alert-hex authenticated-producer-id "
            "consumer-id boot-session ack-sequence receive-ms response-ms");
    }
    const auto frame = from_hex(argv[2]);
    const auto decoded = decode_critical_alert(frame.data(), frame.size());
    if (!decoded.decoded()) {
        std::cerr << "alert decode error "
                  << static_cast<unsigned>(decoded.error) << '\n';
        return 2;
    }

    const AlertIngressContext context{
        true,
        true,
        parse_u64(argv[3]),
        parse_u64(argv[7]),
        decoded.alert.utc_present,
        decoded.alert.utc_present
            ? decoded.alert.event_time_utc_s +
                  (mode == ResponseMode::stale
                       ? kMaximumUtcAgeSeconds + 1U
                       : 1U)
            : 0U,
    };
    CriticalAlertIngress ingress{};

    if (mode == ResponseMode::rate_limited) {
        for (std::uint8_t index = 0;
             index < kGeneralRateAllowance;
             ++index) {
            auto prior = decoded.alert;
            prior.event_id ^= (1ULL << (60U + index));
            std::array<std::uint8_t, kCriticalAlertFrameBytes> encoded{};
            if (prior.event_id == 0 ||
                !encode_critical_alert(prior, encoded).encoded() ||
                !ingress.process(encoded.data(), encoded.size(), context)
                     .accepted()) {
                std::cerr << "rate prefill failed\n";
                return 3;
            }
        }
    }
    const auto decision = ingress.process(
        frame.data(), frame.size(), context);
    const bool expected_decision =
        (mode == ResponseMode::accepted && decision.accepted()) ||
        (mode == ResponseMode::stale &&
         decision.error == AlertIngressError::stale) ||
        (mode == ResponseMode::rate_limited &&
         decision.error == AlertIngressError::rate_limited);
    if (!expected_decision) {
        std::cerr << "alert ingress error "
                  << static_cast<unsigned>(decision.error) << '\n';
        return 3;
    }

    CriticalAlertAckResponder responder{};
    const auto started = responder.start({
        parse_u64(argv[4]),
        parse_u32(argv[5]),
        parse_u32(argv[6]),
    });
    if (started != CriticalAlertAckResponseError::none) {
        std::cerr << "responder start error "
                  << static_cast<unsigned>(started) << '\n';
        return 4;
    }
    const auto response = responder.respond(
        decision, context, parse_u64(argv[8]));
    if (!response.produced()) {
        std::cerr << "responder error "
                  << static_cast<unsigned>(response.error) << '\n';
        return 5;
    }
    std::cout << to_hex(response.frame.data(), response.frame.size()) << '\n';
    return 0;
}

int decode_ack(int argc, char** argv) {
    if (argc != 3) {
        throw std::invalid_argument("decode-ack requires one hex frame");
    }
    const auto frame = from_hex(argv[2]);
    const auto result = decode_critical_alert_ack(frame.data(), frame.size());
    if (!result.decoded()) {
        std::cerr << "ack decode error "
                  << static_cast<unsigned>(result.error) << '\n';
        return 2;
    }
    const auto& ack = result.acknowledgement;
    std::cout
        << "{\"disposition\":" << static_cast<unsigned>(ack.disposition)
        << ",\"reason\":" << static_cast<unsigned>(ack.reason)
        << ",\"state\":" << static_cast<unsigned>(ack.state)
        << ",\"consumer_id\":" << ack.consumer_id
        << ",\"producer_id\":" << ack.producer_id
        << ",\"event_id\":" << ack.event_id
        << ",\"condition_id\":" << ack.condition_id
        << ",\"boot_session\":" << ack.consumer_boot_session_id
        << ",\"ack_sequence\":" << ack.ack_sequence
        << ",\"observed_age_ms\":" << ack.observed_alert_age_ms
        << "}\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            throw std::invalid_argument(
                "expected respond-accepted, respond-stale, "
                "respond-rate-limited, or decode-ack");
        }
        const std::string command = argv[1];
        if (command == "respond-accepted") {
            return respond(argc, argv, ResponseMode::accepted);
        }
        if (command == "respond-stale") {
            return respond(argc, argv, ResponseMode::stale);
        }
        if (command == "respond-rate-limited") {
            return respond(argc, argv, ResponseMode::rate_limited);
        }
        if (command == "decode-ack") {
            return decode_ack(argc, argv);
        }
        throw std::invalid_argument("unknown command");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
