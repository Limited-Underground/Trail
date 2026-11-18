#pragma once

// Group frame header for Trail V1 pairwise groups (docs/security/GROUP_PAIRWISE_V1.md,
// "Group messages"; implementation order step 3). Host-only logic.
//
// Each group message travels as one Packet-v1 frame per other member, over that member's
// pair; Packet-v1 authenticates and encrypts it. Inside the plaintext, a 14-byte group
// header replaces the 8-byte OTXT envelope (integers big-endian):
//
//   format version (1) | body length (1) | roster version the sender used (4) |
//   first 4 bytes of that roster's digest (4) | sender's group message number (4) | body
//
// The digest prefix only detects roster disagreement; it is not used for authentication.
// The plaintext is 94 bytes (Heltec V4 154-byte radio MTU minus 60 bytes of Packet-v1), so
// the body is at most 80 bytes. Group frames are never split; if the radio settings change
// the MTU, these limits must be recomputed.

#include <array>
#include <cstddef>
#include <cstdint>

namespace opentrail::delivery {

inline constexpr std::uint8_t kGroupFrameVersion = 1;
inline constexpr std::size_t kGroupDigestPrefixBytes = 4;
inline constexpr std::size_t kGroupFrameHeaderBytes = 1 + 1 + 4 + kGroupDigestPrefixBytes + 4;
inline constexpr std::size_t kGroupPlaintextBytes = 94;
inline constexpr std::size_t kGroupBodyMaxBytes = kGroupPlaintextBytes - kGroupFrameHeaderBytes;
static_assert(kGroupFrameHeaderBytes == 14 && kGroupBodyMaxBytes == 80);

using GroupDigestPrefix = std::array<std::uint8_t, kGroupDigestPrefixBytes>;

// Packet-v1 type values for group content (proposed in the design; carried by Packet-v1,
// not by this header).
enum class GroupContent : std::uint8_t { text = 3, quick_status = 4, position = 5 };

struct GroupFrameHeader {
    std::uint32_t roster_version{0};
    GroupDigestPrefix digest_prefix{};
    std::uint32_t message_number{0};
};

enum class GroupFrameError : std::uint8_t {
    none = 0,
    invalid_argument,     // null pointer or output too small
    bad_size,             // shorter than a header, or not exactly header + body length
    bad_version,
    empty_body,
    body_too_long,
    zero_roster_version,
    zero_message_number,  // numbers start at 1
    bad_text,             // group text is not strict UTF-8
};

struct DecodedGroupFrame {
    GroupFrameHeader header{};
    const std::uint8_t* body{nullptr};  // points into the decoded input
    std::size_t body_size{0};
};

// Writes header and body; returns the frame size, or 0 with `error` set. Text bodies must be
// strict UTF-8 (same rules as OTXT v1: exact bytes, whitespace and line endings preserved).
[[nodiscard]] std::size_t encode_group_frame(GroupContent content, const GroupFrameHeader& header,
                                             const std::uint8_t* body, std::size_t body_size, std::uint8_t* out,
                                             std::size_t capacity, GroupFrameError& error);

// Strict decode: the input must be exactly one frame. Nothing is returned on error.
[[nodiscard]] GroupFrameError decode_group_frame(GroupContent content, const std::uint8_t* in, std::size_t size,
                                                 DecodedGroupFrame& out);

// Strict UTF-8: no overlong forms, no surrogates, nothing above U+10FFFF, no truncation.
[[nodiscard]] bool strict_utf8(const std::uint8_t* data, std::size_t size);

}  // namespace opentrail::delivery
