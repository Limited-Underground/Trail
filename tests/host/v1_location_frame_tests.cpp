#include "opentrail/v1_location_frame.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>

#include "opentrail/packet_v1.hpp"
#include "opentrail/radio_profile.hpp"

namespace {

using namespace opentrail::location::v1;
using Buffer = std::array<std::uint8_t, kMaximumLocationFrameBytes + 1>;

const Scope kDirect{ScopeKind::direct, 0x0102030405060708, 0, {}};
const Scope kGroup{ScopeKind::group, 3, 0x01020304, {0, 1, 2, 3, 4, 5}};

void check_cleared(const Frame& frame) {
    assert(frame.scope == Scope{});
    assert(frame.event == Event{});
    assert(frame.position.latitude_e4 == 0 && frame.position.longitude_e4 == 0);
    assert(frame.fix_age_ms == 0);
}

void check_invalid_decode(const std::uint8_t* data, std::size_t size) {
    Frame decoded{kGroup, Event::position, {123, -456}, 1000};
    assert(!decode_frame(data, size, decoded));
    check_cleared(decoded);
}

void check_invalid_encode(const Frame& frame) {
    Buffer bytes{};
    bytes.fill(0x5a);
    assert(encode_frame(frame, bytes.data(), bytes.size()) == 0);
    assert(std::all_of(bytes.begin(), bytes.end(),
                       [](std::uint8_t byte) { return byte == 0x5a; }));
}

void test_position_bytes() {
    const Frame direct{kDirect, Event::position, {-123456, 1799999}, 5000};
    Buffer bytes{};
    const auto size = encode_frame(direct, bytes.data(), bytes.size());
    const std::array<std::uint8_t, 28> expected{{
        'O', 'T', 'L', 'P', 1, 2, 1, 0, 1, 2, 3, 4, 5, 6, 7, 8,
        0xff, 0xfe, 0x1d, 0xc0, 0, 0x1b, 0x77, 0x3f, 0, 0, 0x13, 0x88,
    }};
    assert(size == expected.size());
    assert(std::equal(expected.begin(), expected.end(), bytes.begin()));

    Frame decoded{};
    assert(decode_frame(bytes.data(), size, decoded));
    assert(decoded.scope == kDirect && decoded.event == Event::position);
    assert(decoded.position.latitude_e4 == -123456 && decoded.position.longitude_e4 == 1799999);
    assert(decoded.fix_age_ms == 5000);

    const Frame group{kGroup, Event::position, {900000, -1800000}, kFreshFixMs - 1};
    const auto group_size = encode_frame(group, bytes.data(), bytes.size());
    assert(group_size == kMaximumLocationFrameBytes);
    assert(group_size + opentrail::protocol::v1::header_bytes +
           opentrail::protocol::v1::tag_bytes <= opentrail::radio::kMaximumLoraPayload);
    assert(bytes[16] == 1 && bytes[17] == 2 && bytes[18] == 3 && bytes[19] == 4);
    assert(std::equal(kGroup.roster_digest.begin(), kGroup.roster_digest.end(), bytes.begin() + 20));
    assert(decode_frame(bytes.data(), group_size, decoded));
    assert(decoded.scope == kGroup && decoded.event == Event::position);
    assert(decoded.position.latitude_e4 == 900000 && decoded.position.longitude_e4 == -1800000);
    assert(decoded.fix_age_ms == kFreshFixMs - 1);
}

void test_stop_bytes() {
    for (const auto& scope : {kDirect, kGroup}) {
        Buffer bytes{};
        bytes.fill(0x5a);
        const Frame stop{scope, Event::stop, {}, 0};
        const auto size = encode_frame(stop, bytes.data(), bytes.size());
        const auto expected_size = scope.kind == ScopeKind::direct ?
            kDirectStopFrameBytes : kGroupStopFrameBytes;
        assert(size == expected_size);
        assert(std::all_of(bytes.begin() + size, bytes.end(),
                           [](std::uint8_t byte) { return byte == 0x5a; }));
        Frame decoded{kDirect, Event::position, {-1200, 3600}, 100};
        assert(decode_frame(bytes.data(), size, decoded));
        assert(decoded.event == Event::stop && decoded.scope == scope);
        assert(decoded.position.latitude_e4 == 0 && decoded.position.longitude_e4 == 0);
        assert(decoded.fix_age_ms == 0);
        check_invalid_decode(bytes.data(), size + 1);
        check_invalid_encode({scope, Event::stop, {0, 1}, 0});
        check_invalid_encode({scope, Event::stop, {}, 1});
    }
}

void test_rejected_encoding() {
    check_invalid_encode({kDirect, Event::position, {900001, 0}, 0});
    check_invalid_encode({kDirect, Event::position, {-900001, 0}, 0});
    check_invalid_encode({kDirect, Event::position, {0, 1800001}, 0});
    check_invalid_encode({kDirect, Event::position, {0, -1800001}, 0});
    check_invalid_encode({kDirect, Event::position, {}, kFreshFixMs});
    check_invalid_encode({kDirect, static_cast<Event>(99), {}, 0});
    check_invalid_encode({{ScopeKind::direct, 0, 0, {}}, Event::stop, {}, 0});
    check_invalid_encode({{ScopeKind::direct, 1, 1, {}}, Event::stop, {}, 0});
    check_invalid_encode({{ScopeKind::direct, 1, 0, {1}}, Event::stop, {}, 0});
    check_invalid_encode({{ScopeKind::group, 1, 0, {1}}, Event::stop, {}, 0});
    check_invalid_encode({{ScopeKind::group, 1, 1, {}}, Event::stop, {}, 0});
    check_invalid_encode({{static_cast<ScopeKind>(99), 1, 0, {}}, Event::stop, {}, 0});

    const Frame frame{kGroup, Event::position, {}, 0};
    Buffer bytes{};
    bytes.fill(0x5a);
    assert(encode_frame(frame, nullptr, bytes.size()) == 0);
    assert(encode_frame(frame, bytes.data(), kMaximumLocationFrameBytes - 1) == 0);
    assert(std::all_of(bytes.begin(), bytes.end(),
                       [](std::uint8_t byte) { return byte == 0x5a; }));
}

void test_rejected_decoding() {
    Buffer valid{};
    const auto size = encode_frame({kGroup, Event::position, {}, 500}, valid.data(), valid.size());
    assert(size == kMaximumLocationFrameBytes);
    check_invalid_decode(nullptr, size);
    for (std::size_t short_size = 0; short_size < size; ++short_size) {
        check_invalid_decode(valid.data(), short_size);
    }
    check_invalid_decode(valid.data(), size + 1);

    const std::array<std::size_t, 5> invalid_header_offsets{{0, 4, 5, 6, 7}};
    for (const auto offset : invalid_header_offsets) {
        auto malformed = valid;
        malformed[offset] = 0xff;
        check_invalid_decode(malformed.data(), size);
    }

    auto malformed = valid;
    std::fill(malformed.begin() + 8, malformed.begin() + 16, 0);
    check_invalid_decode(malformed.data(), size);
    malformed = valid;
    std::fill(malformed.begin() + 16, malformed.begin() + 20, 0);
    check_invalid_decode(malformed.data(), size);
    malformed = valid;
    std::fill(malformed.begin() + 20, malformed.begin() + 52, 0);
    check_invalid_decode(malformed.data(), size);
    malformed = valid;
    malformed[52] = 0x80;
    check_invalid_decode(malformed.data(), size);
    malformed = valid;
    malformed[56] = 0x7f;
    check_invalid_decode(malformed.data(), size);
    malformed = valid;
    malformed[62] = 0x3a;
    malformed[63] = 0x98;
    check_invalid_decode(malformed.data(), size);
}

}  // namespace

int main() {
    test_position_bytes();
    test_stop_bytes();
    test_rejected_encoding();
    test_rejected_decoding();
    std::puts("V1 location frame: 4 test groups passed");
}
