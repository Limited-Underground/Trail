#pragma once

#include <cstring>

#include "opentrail/packet_v1.hpp"

namespace opentrail::companion::otxt {

inline constexpr std::size_t envelope_bytes = 8;
inline constexpr std::size_t max_text_bytes = 90;
inline constexpr std::size_t max_frame_bytes =
    protocol::v1::header_bytes + protocol::v1::tag_bytes + envelope_bytes + max_text_bytes;

// The budget includes the outer Packet header and authentication tag.
constexpr std::size_t text_limit(std::size_t maximum_frame_bytes) {
    constexpr std::size_t overhead = max_frame_bytes - max_text_bytes;
    if (maximum_frame_bytes <= overhead) {
        return 0;
    }
    const auto capacity = maximum_frame_bytes - overhead;
    return capacity < max_text_bytes ? capacity : max_text_bytes;
}

namespace detail {
inline constexpr std::uint8_t magic[] = {0x4f, 0x54, 0x58, 0x54};
inline constexpr std::uint8_t version = 1;

inline bool valid_utf8(protocol::v1::View text) {
    std::size_t offset = 0;
    while (offset < text.size) {
        const auto lead = text.data[offset++];
        if (lead <= 0x7f) {
            continue;
        }

        unsigned continuation_bytes = 0;
        std::uint32_t scalar = 0;
        std::uint32_t minimum = 0;
        if (lead >= 0xc2 && lead <= 0xdf) {
            continuation_bytes = 1;
            scalar = lead & 0x1f;
            minimum = 0x80;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            continuation_bytes = 2;
            scalar = lead & 0x0f;
            minimum = 0x800;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            continuation_bytes = 3;
            scalar = lead & 0x07;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (continuation_bytes > text.size - offset) {
            return false;
        }
        for (unsigned i = 0; i < continuation_bytes; ++i) {
            const auto byte = text.data[offset++];
            if ((byte & 0xc0) != 0x80) {
                return false;
            }
            scalar = (scalar << 6) | (byte & 0x3f);
        }
        if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) {
            return false;
        }
    }
    return true;
}
}

// The text and destination may overlap. Refusal leaves the destination intact.
inline std::size_t encode(protocol::v1::View text, protocol::v1::MutableView payload,
                          std::size_t maximum_frame_bytes = max_frame_bytes) {
    if (!text.data || !payload.data || text.size == 0 || text.size > text_limit(maximum_frame_bytes) ||
        payload.size < envelope_bytes + text.size || !detail::valid_utf8(text)) {
        return 0;
    }

    std::memmove(payload.data + envelope_bytes, text.data, text.size);
    std::memcpy(payload.data, detail::magic, sizeof detail::magic);
    payload.data[4] = detail::version;
    payload.data[5] = 0;
    payload.data[6] = static_cast<std::uint8_t>(text.size >> 8);
    payload.data[7] = static_cast<std::uint8_t>(text.size);
    return envelope_bytes + text.size;
}

// The result borrows the exact UTF-8 bytes from the payload.
inline protocol::v1::View decode(protocol::v1::View payload,
                                std::size_t maximum_frame_bytes = max_frame_bytes) {
    const auto limit = text_limit(maximum_frame_bytes);
    if (!payload.data || payload.size <= envelope_bytes || payload.size > envelope_bytes + limit ||
        std::memcmp(payload.data, detail::magic, sizeof detail::magic) != 0 ||
        payload.data[4] != detail::version || payload.data[5] != 0) {
        return {};
    }

    const std::size_t length = (static_cast<std::size_t>(payload.data[6]) << 8) | payload.data[7];
    if (length != payload.size - envelope_bytes) {
        return {};
    }

    const protocol::v1::View text{payload.data + envelope_bytes, length};
    return detail::valid_utf8(text) ? text : protocol::v1::View{};
}

}  // namespace opentrail::companion::otxt
