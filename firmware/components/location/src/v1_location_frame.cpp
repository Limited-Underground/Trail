#include "opentrail/v1_location_frame.hpp"

#include <algorithm>
#include <limits>

#include "opentrail/packet_v1.hpp"
#include "opentrail/radio_profile.hpp"

namespace opentrail::location::v1 {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{{'O', 'T', 'L', 'P'}};
constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kRosterVersionOffset = kDirectStopFrameBytes;
constexpr std::size_t kRosterDigestOffset = kRosterVersionOffset + 4;
static_assert(kRosterDigestOffset + 32 == kGroupStopFrameBytes);
static_assert(kMaximumLocationFrameBytes + protocol::v1::header_bytes +
              protocol::v1::tag_bytes <= radio::kMaximumLoraPayload);

bool has_digest(const Scope& scope) {
    return std::any_of(scope.roster_digest.begin(), scope.roster_digest.end(),
                       [](std::uint8_t value) { return value != 0; });
}

std::size_t scope_bytes(ScopeKind kind) {
    return kind == ScopeKind::group ? kGroupStopFrameBytes : kDirectStopFrameBytes;
}

std::size_t frame_bytes(ScopeKind kind, Event event) {
    switch (event) {
        case Event::position:
            return scope_bytes(kind) + kPositionFieldsBytes;
        case Event::stop:
            return scope_bytes(kind);
    }
    return 0;
}

bool valid_event_payload(const Frame& frame) {
    switch (frame.event) {
        case Event::position:
            return valid_coordinates(frame.position) && frame.fix_age_ms < kFreshFixMs;
        case Event::stop:
            return frame.position.latitude_e4 == 0 && frame.position.longitude_e4 == 0 &&
                   frame.fix_age_ms == 0;
    }
    return false;
}

void write_be(std::uint8_t* output, std::size_t size, std::uint64_t value) {
    while (size != 0) {
        output[--size] = static_cast<std::uint8_t>(value);
        value >>= 8;
    }
}

std::uint64_t read_be(const std::uint8_t* input, std::size_t size) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < size; ++index) {
        value = (value << 8) | input[index];
    }
    return value;
}

std::int32_t read_coordinate(const std::uint8_t* input) {
    const auto value = static_cast<std::uint32_t>(read_be(input, 4));
    if (value <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        return static_cast<std::int32_t>(value);
    }
    return -1 - static_cast<std::int32_t>(std::numeric_limits<std::uint32_t>::max() - value);
}

}  // namespace

bool Scope::operator==(const Scope& other) const {
    return kind == other.kind && id == other.id && roster_version == other.roster_version &&
           roster_digest == other.roster_digest;
}

bool valid_scope(const Scope& scope) {
    if (scope.id == 0) {
        return false;
    }
    switch (scope.kind) {
        case ScopeKind::group:
            return scope.roster_version != 0 && has_digest(scope);
        case ScopeKind::direct:
            return scope.roster_version == 0 && !has_digest(scope);
    }
    return false;
}

bool valid_coordinates(const Coordinates& coordinates) {
    return coordinates.latitude_e4 >= -900000 && coordinates.latitude_e4 <= 900000 &&
           coordinates.longitude_e4 >= -1800000 && coordinates.longitude_e4 <= 1800000;
}

std::size_t encode_frame(const Frame& frame, std::uint8_t* output, std::size_t capacity) {
    const std::size_t size = frame_bytes(frame.scope.kind, frame.event);
    if (!output || capacity < size || !valid_scope(frame.scope) || !valid_event_payload(frame)) {
        return 0;
    }

    std::copy(kMagic.begin(), kMagic.end(), output);
    output[4] = kVersion;
    output[5] = static_cast<std::uint8_t>(frame.scope.kind);
    output[6] = static_cast<std::uint8_t>(frame.event);
    output[7] = 0;
    write_be(output + 8, 8, frame.scope.id);
    if (frame.scope.kind == ScopeKind::group) {
        write_be(output + kRosterVersionOffset, 4, frame.scope.roster_version);
        std::copy(frame.scope.roster_digest.begin(), frame.scope.roster_digest.end(),
                  output + kRosterDigestOffset);
    }

    if (frame.event == Event::position) {
        auto* fields = output + scope_bytes(frame.scope.kind);
        write_be(fields, 4, static_cast<std::uint32_t>(frame.position.latitude_e4));
        write_be(fields + 4, 4, static_cast<std::uint32_t>(frame.position.longitude_e4));
        write_be(fields + 8, 4, frame.fix_age_ms);
    }
    return size;
}

bool decode_frame(const std::uint8_t* input, std::size_t size, Frame& frame) {
    frame = {};
    if (!input || size < kDirectStopFrameBytes || size > kMaximumLocationFrameBytes ||
        !std::equal(kMagic.begin(), kMagic.end(), input) || input[4] != kVersion || input[7] != 0) {
        return false;
    }

    Frame decoded{};
    decoded.scope.kind = static_cast<ScopeKind>(input[5]);
    decoded.event = static_cast<Event>(input[6]);
    if ((decoded.scope.kind != ScopeKind::group && decoded.scope.kind != ScopeKind::direct) ||
        size != frame_bytes(decoded.scope.kind, decoded.event)) {
        return false;
    }

    decoded.scope.id = read_be(input + 8, 8);
    if (decoded.scope.kind == ScopeKind::group) {
        decoded.scope.roster_version = static_cast<std::uint32_t>(read_be(input + kRosterVersionOffset, 4));
        std::copy_n(input + kRosterDigestOffset, decoded.scope.roster_digest.size(),
                    decoded.scope.roster_digest.begin());
    }

    if (decoded.event == Event::position) {
        const auto* fields = input + scope_bytes(decoded.scope.kind);
        decoded.position.latitude_e4 = read_coordinate(fields);
        decoded.position.longitude_e4 = read_coordinate(fields + 4);
        decoded.fix_age_ms = static_cast<std::uint32_t>(read_be(fields + 8, 4));
    }

    if (!valid_scope(decoded.scope) || !valid_event_payload(decoded)) {
        return false;
    }
    frame = decoded;
    return true;
}

}  // namespace opentrail::location::v1
