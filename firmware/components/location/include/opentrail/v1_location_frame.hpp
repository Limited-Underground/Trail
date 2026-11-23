#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace opentrail::location::v1 {

enum class ScopeKind : std::uint8_t { group = 1, direct = 2 };

struct Scope {
    ScopeKind kind{};
    std::uint64_t id{};
    std::uint32_t roster_version{};
    std::array<std::uint8_t, 32> roster_digest{};

    bool operator==(const Scope& other) const;
};

[[nodiscard]] bool valid_scope(const Scope& scope);

enum class Event : std::uint8_t { position = 1, stop = 2 };

struct Coordinates {
    std::int32_t latitude_e4{};
    std::int32_t longitude_e4{};
};

[[nodiscard]] bool valid_coordinates(const Coordinates& coordinates);

inline constexpr std::uint32_t kFreshFixMs = 15000;
inline constexpr std::size_t kDirectStopFrameBytes = 16;
inline constexpr std::size_t kGroupStopFrameBytes = 52;
inline constexpr std::size_t kPositionFieldsBytes = 12;
inline constexpr std::size_t kMaximumLocationFrameBytes =
    kGroupStopFrameBytes + kPositionFieldsBytes;

struct Frame {
    Scope scope{};
    Event event{};
    Coordinates position{};
    std::uint32_t fix_age_ms{};
};

// Candidate payload for an authenticated Packet-v1 envelope. The caller owns
// peer, epoch and audience admission, durable replay checks and freshness proof.
// A successful codec result grants no receive or transmit authority.
[[nodiscard]] std::size_t encode_frame(const Frame& frame, std::uint8_t* output,
                                       std::size_t capacity);
[[nodiscard]] bool decode_frame(const std::uint8_t* input, std::size_t size,
                                Frame& frame);

}  // namespace opentrail::location::v1
