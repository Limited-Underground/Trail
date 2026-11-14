#include "opentrail/group_frame.hpp"

#include <algorithm>

namespace opentrail::delivery {
namespace {

void put_u32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

std::uint32_t get_u32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

GroupFrameError check(GroupContent content, const GroupFrameHeader& header, const std::uint8_t* body,
                      std::size_t body_size) {
    if (body_size == 0) return GroupFrameError::empty_body;
    if (body_size > kGroupBodyMaxBytes) return GroupFrameError::body_too_long;
    if (header.roster_version == 0) return GroupFrameError::zero_roster_version;
    if (header.message_number == 0) return GroupFrameError::zero_message_number;
    if (content == GroupContent::text && !strict_utf8(body, body_size)) return GroupFrameError::bad_text;
    return GroupFrameError::none;
}

bool known(GroupContent content) {
    return content == GroupContent::text || content == GroupContent::quick_status ||
           content == GroupContent::position;
}

}  // namespace

bool strict_utf8(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr) return size == 0;
    std::size_t i = 0;
    while (i < size) {
        const std::uint8_t b = data[i];
        if (b < 0x80) { ++i; continue; }
        std::size_t extra = 0;
        std::uint32_t cp = 0, min = 0;
        if (b >= 0xC2 && b <= 0xDF) { extra = 1; cp = b & 0x1FU; min = 0x80; }
        else if (b >= 0xE0 && b <= 0xEF) { extra = 2; cp = b & 0x0FU; min = 0x800; }
        else if (b >= 0xF0 && b <= 0xF4) { extra = 3; cp = b & 0x07U; min = 0x10000; }
        else return false;  // continuation byte, C0/C1 or F5..FF
        if (size - i <= extra) return false;
        for (std::size_t k = 1; k <= extra; ++k) {
            const std::uint8_t c = data[i + k];
            if ((c & 0xC0U) != 0x80U) return false;
            cp = (cp << 6) | (c & 0x3FU);
        }
        if (cp < min || cp > 0x10FFFFU || (cp >= 0xD800U && cp <= 0xDFFFU)) return false;
        i += extra + 1;
    }
    return true;
}

std::size_t encode_group_frame(GroupContent content, const GroupFrameHeader& header, const std::uint8_t* body,
                               std::size_t body_size, std::uint8_t* out, std::size_t capacity,
                               GroupFrameError& error) {
    if (out == nullptr || body == nullptr || !known(content)) {
        error = GroupFrameError::invalid_argument;
        return 0;
    }
    error = check(content, header, body, body_size);
    if (error != GroupFrameError::none) return 0;
    const std::size_t total = kGroupFrameHeaderBytes + body_size;
    if (capacity < total) {
        error = GroupFrameError::invalid_argument;
        return 0;
    }
    out[0] = kGroupFrameVersion;
    out[1] = static_cast<std::uint8_t>(body_size);
    put_u32(out + 2, header.roster_version);
    std::copy(header.digest_prefix.begin(), header.digest_prefix.end(), out + 6);
    put_u32(out + 10, header.message_number);
    std::copy(body, body + body_size, out + kGroupFrameHeaderBytes);
    return total;
}

GroupFrameError decode_group_frame(GroupContent content, const std::uint8_t* in, std::size_t size,
                                   DecodedGroupFrame& out) {
    out = DecodedGroupFrame{};
    if (in == nullptr || !known(content)) return GroupFrameError::invalid_argument;
    if (size < kGroupFrameHeaderBytes) return GroupFrameError::bad_size;
    if (in[0] != kGroupFrameVersion) return GroupFrameError::bad_version;
    const std::size_t body_size = in[1];
    if (size != kGroupFrameHeaderBytes + body_size) return GroupFrameError::bad_size;
    GroupFrameHeader header{};
    header.roster_version = get_u32(in + 2);
    std::copy(in + 6, in + 10, header.digest_prefix.begin());
    header.message_number = get_u32(in + 10);
    const std::uint8_t* body = in + kGroupFrameHeaderBytes;
    if (const GroupFrameError e = check(content, header, body, body_size); e != GroupFrameError::none) return e;
    out.header = header;
    out.body = body;
    out.body_size = body_size;
    return GroupFrameError::none;
}

}  // namespace opentrail::delivery
