#pragma once
// LoRa framing for first board pairing (V1 F5, step R2a; D-076, D-085).
//
// Framing only: magic, version, message type, the pairing code and an exact
// payload size per type. It carries public enrollment-session messages, never
// private keys. Every received payload stays untrusted input for the
// enrollment session, which authenticates it. The pairing code is not a secret
// and not authentication: it only lets a board ignore frames from a different
// pairing attempt nearby, so they never reach (and terminate) its session.
//
// Layout: 'O' 'P' | version (high nibble) and type (low nibble) |
//         pairing code, 3 bytes little-endian | payload (exact size per type).
// The signed invitation (188 + 64 bytes) travels as two frames, so the largest
// frame is 6 + 188 = 194 bytes, under the product radio's 255-byte MTU.

#include <array>
#include <cstddef>
#include <cstdint>

namespace opentrail::protocol::pairing {

inline constexpr std::uint8_t kMagic0 = 'O';
inline constexpr std::uint8_t kMagic1 = 'P';
inline constexpr std::uint8_t kVersion = 1;
inline constexpr std::size_t kHeaderBytes = 6;
inline constexpr std::uint32_t kCodeMin = 1;
inline constexpr std::uint32_t kCodeMax = 999999;

enum class FrameType : std::uint8_t {
    candidate = 1,             // version u32, profile u32, public identity 32
    offer = 2,                 // enrollment offer
    possession = 3,            // possession signature 64
    clock_mark = 4,            // clock mark
    invitation_payload = 5,    // invitation payload 188
    invitation_signature = 6,  // issuer's invitation signature 64
    invitation_cosign = 7,     // responder's signature over the invitation 64
    handshake = 8,             // version, step, length u16, payload 128
    control = 9,               // control record
    status = 10,               // status record
};

// Exact payload size for each type; 0 means unknown type.
inline constexpr std::size_t payload_bytes(FrameType type) {
    switch (type) {
        case FrameType::candidate: return 4 + 4 + 32;
        case FrameType::offer: return 4 + 1 + 8 + 32 + (16 + 8 + 8) + 32;
        case FrameType::possession: return 64;
        case FrameType::clock_mark: return 4 + (16 + 8 + 8) + 8;
        case FrameType::invitation_payload: return 188;
        case FrameType::invitation_signature: return 64;
        case FrameType::invitation_cosign: return 64;
        case FrameType::handshake: return 1 + 1 + 2 + 128;
        case FrameType::control: return 8 + 8 + 4 + 32 + 32 + 24;
        case FrameType::status: return 8 + 8 + 4 + 32 + 32 + 24;
    }
    return 0;
}

inline constexpr std::size_t kMaxPayloadBytes = 188;
inline constexpr std::size_t kMaxFrameBytes = kHeaderBytes + kMaxPayloadBytes;
using FrameBuffer = std::array<std::uint8_t, kMaxFrameBytes>;

inline constexpr bool code_valid(std::uint32_t code) {
    return code >= kCodeMin && code <= kCodeMax;
}

// Builds one frame. Refuses an unknown type, invalid code, null payload or a
// payload whose size is not exactly the size for its type.
inline bool encode(FrameType type, std::uint32_t code,
                   const std::uint8_t* payload, std::size_t size,
                   FrameBuffer& out, std::size_t& out_size) {
    const auto expected = payload_bytes(type);
    if (expected == 0 || size != expected || payload == nullptr ||
        !code_valid(code)) {
        return false;
    }
    out[0] = kMagic0;
    out[1] = kMagic1;
    out[2] = static_cast<std::uint8_t>((kVersion << 4) |
                                       static_cast<std::uint8_t>(type));
    out[3] = static_cast<std::uint8_t>(code & 0xffU);
    out[4] = static_cast<std::uint8_t>((code >> 8) & 0xffU);
    out[5] = static_cast<std::uint8_t>((code >> 16) & 0xffU);
    for (std::size_t i = 0; i < size; ++i) out[kHeaderBytes + i] = payload[i];
    out_size = kHeaderBytes + size;
    return true;
}

struct Frame {
    FrameType type{};
    const std::uint8_t* payload{nullptr};  // points into the received buffer
    std::size_t size{0};
};

enum class DecodeResult : std::uint8_t {
    ok,
    not_pairing,      // other LoRa traffic: wrong magic or too short
    bad_version,
    bad_type,
    bad_size,
    other_attempt,    // well-formed, but a different pairing code: ignore
};

// Accepts only an exact, current-version frame for this pairing code.
inline DecodeResult decode(const std::uint8_t* data, std::size_t size,
                           std::uint32_t expected_code, Frame& out) {
    if (data == nullptr || size < kHeaderBytes || data[0] != kMagic0 ||
        data[1] != kMagic1) {
        return DecodeResult::not_pairing;
    }
    if ((data[2] >> 4) != kVersion) return DecodeResult::bad_version;
    const auto type = static_cast<FrameType>(data[2] & 0x0fU);
    const auto expected = payload_bytes(type);
    if (expected == 0) return DecodeResult::bad_type;
    if (size != kHeaderBytes + expected) return DecodeResult::bad_size;
    const std::uint32_t code = static_cast<std::uint32_t>(data[3]) |
                               (static_cast<std::uint32_t>(data[4]) << 8) |
                               (static_cast<std::uint32_t>(data[5]) << 16);
    if (!code_valid(expected_code) || code != expected_code) {
        return DecodeResult::other_attempt;
    }
    out = {type, data + kHeaderBytes, expected};
    return DecodeResult::ok;
}

}  // namespace opentrail::protocol::pairing
