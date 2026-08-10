#pragma once

#include <cstddef>
#include <cstdint>

#include "opentrail/radio_transport.hpp"

namespace opentrail::protocol {

inline constexpr std::uint8_t kExperimentalPacketVersion = 0;
inline constexpr std::size_t kPacketHeaderBytes = 20;
inline constexpr std::size_t kPacketChecksumBytes = 2;
inline constexpr std::size_t kPacketOverheadBytes =
    kPacketHeaderBytes + kPacketChecksumBytes;

enum class PacketType : std::uint8_t {
    position = 0x01,
    experimental_probe = 0xF0,
};

enum class PacketCodecError : std::uint8_t {
    none = 0,
    invalid_argument,
    output_too_small,
    malformed,
    unsupported_version,
    unsupported_type,
    reserved_flags_set,
    invalid_identity,
    integrity_failure,
};

struct PacketHeader {
    std::uint8_t version{kExperimentalPacketVersion};
    PacketType type{PacketType::experimental_probe};
    std::uint8_t flags{0};
    std::uint32_t source_node_id{0};
    std::uint32_t network_id{0};
    std::uint32_t message_id{0};
};

struct PacketView {
    PacketHeader header{};
    radio::ByteView payload{};
};

struct EncodeResult {
    PacketCodecError error{PacketCodecError::none};
    std::size_t encoded_bytes{0};

    [[nodiscard]] constexpr bool encoded() const {
        return error == PacketCodecError::none;
    }
};

struct DecodeResult {
    PacketCodecError error{PacketCodecError::malformed};
    PacketView packet{};

    [[nodiscard]] constexpr bool decoded() const {
        return error == PacketCodecError::none;
    }
};

[[nodiscard]] std::size_t maximum_payload_for_mtu(std::size_t transport_mtu);
[[nodiscard]] std::uint16_t crc16_ccitt_false(radio::ByteView bytes);
[[nodiscard]] EncodeResult encode_packet(
    const PacketView& packet,
    radio::MutableByteView output);
[[nodiscard]] DecodeResult decode_packet(radio::ByteView encoded);

}  // namespace opentrail::protocol
