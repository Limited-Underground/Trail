#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "opentrail/radio_transport.hpp"

namespace opentrail::target::heltec_v4_bench::phone_text_gatt {

inline constexpr std::uint8_t profile = 6;
inline constexpr std::size_t max_record_bytes = 148;
inline constexpr std::size_t authority_query_bytes = 76;
inline constexpr std::size_t quick_status_record_bytes = 59;

enum class Payload { text, quick_status };

#if defined(OPENTRAIL_LORA_F6_PHONE_TEST) && OPENTRAIL_LORA_F6_PHONE_TEST
inline constexpr Payload target_payload = Payload::quick_status;
#else
inline constexpr Payload target_payload = Payload::text;
#endif

[[nodiscard]] inline std::uint8_t quick_status_code(radio::ByteView value) {
    if (!value.data || value.size != 1 || value.data[0] < '1' || value.data[0] > '4') return 0;
    return static_cast<std::uint8_t>(value.data[0] - '0');
}

enum class Kind : std::uint8_t {
    invalid = 0,
    authority_query = 9,
    send = 0x0a,
    authority_result = 0x8b,
    send_result = 0x8c,
    received = 0x8d,
    delivery = 0x8e,
};

struct Header {
    Kind kind{Kind::invalid};
    std::uint32_t nonce{};
    std::uint32_t exchange{};

    [[nodiscard]] bool command() const {
        return kind == Kind::authority_query || kind == Kind::send;
    }

    [[nodiscard]] bool event() const {
        return kind == Kind::received || kind == Kind::delivery;
    }
};

[[nodiscard]] inline std::uint32_t get32(const std::uint8_t* p) {
    return std::uint32_t{p[0]} | std::uint32_t{p[1]} << 8 |
           std::uint32_t{p[2]} << 16 | std::uint32_t{p[3]} << 24;
}

// Routing shape only. The owner backend validates each complete payload.
[[nodiscard]] inline Header decode(radio::ByteView value, Payload payload = Payload::text) {
    const auto* b = value.data;
    if (b == nullptr || value.size < 24 || value.size > max_record_bytes ||
        b[0] != 'O' || b[1] != 'T' || b[2] != 'C' || b[3] != '0' ||
        b[4] != 0 || b[5] != profile || b[7] != 0 || b[16] != 0 ||
        b[17] != 1 || b[18] != value.size - 20 || b[19] != 0 ||
        b[20] != 'O' || b[21] != 'T') {
        return {};
    }
    const auto kind = static_cast<Kind>(b[6]);
    const auto nonce = get32(b + 8);
    const auto exchange = get32(b + 12);
    if (nonce == 0 || exchange == 0) return {};
    if (kind == Kind::authority_query || kind == Kind::authority_result) {
        const auto size = kind == Kind::authority_query ? authority_query_bytes : 136U;
        const bool domain = payload == Payload::quick_status
            ? b[22] == 'Q' && b[23] == 'Y'
            : b[22] == 'Y' && b[23] == '0';
        return value.size == size && domain ? Header{kind, nonce, exchange} : Header{};
    }
    if ((kind == Kind::send || kind == Kind::received) && payload == Payload::quick_status) {
        if (b[22] != 'Q' || b[23] != 'S' || value.size != quick_status_record_bytes ||
            !quick_status_code({b + quick_status_record_bytes - 1, 1})) return {};
        return {kind, nonce, exchange};
    }
    if (b[22] != 'X' || b[23] != 'C') return {};
    switch (kind) {
        case Kind::send:
        case Kind::received:
            if (value.size < 59) return {};
            break;
        case Kind::send_result:
            if (value.size != 68) return {};
            break;
        case Kind::delivery:
            if (value.size != 72) return {};
            break;
        default:
            return {};
    }
    return {kind, nonce, exchange};
}

// Normalize only the mode-specific wire domain. The trial still validates
// the complete carrier, including authority, freshness and correlation.
[[nodiscard]] inline bool normalize_quick_status_command(radio::ByteView input, radio::MutableByteView carrier) {
    if (!carrier.data || carrier.size < input.size) return false;
    const auto route = decode(input, Payload::quick_status);
    if (!route.command()) return false;
    const bool authority = route.kind == Kind::authority_query;
    std::memmove(carrier.data, input.data, input.size);
    carrier.data[22] = authority ? 'Y' : 'X';
    carrier.data[23] = authority ? '0' : 'C';
    return true;
}

[[nodiscard]] inline bool mark_quick_status_output(radio::MutableByteView output) {
    const auto route = decode({output.data, output.size});
    switch (route.kind) {
        case Kind::authority_result:
            output.data[22] = 'Q';
            output.data[23] = 'Y';
            return true;
        case Kind::received:
            if (output.size != quick_status_record_bytes ||
                !quick_status_code({output.data + quick_status_record_bytes - 1, 1})) return false;
            output.data[22] = 'Q';
            output.data[23] = 'S';
            return true;
        case Kind::send_result:
        case Kind::delivery:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] inline bool matches_response(const Header& request, const Header& result) {
    const bool same_exchange = result.nonce == request.nonce && result.exchange == request.exchange;
    return same_exchange &&
        ((request.kind == Kind::authority_query && result.kind == Kind::authority_result) ||
         (request.kind == Kind::send && result.kind == Kind::send_result));
}

}  // namespace opentrail::target::heltec_v4_bench::phone_text_gatt
