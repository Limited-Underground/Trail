#include "opentrail/packet_codec.hpp"

#include <algorithm>
#include <limits>

namespace opentrail::protocol {
namespace {

constexpr std::uint8_t kMagic0 = 0x4F;  // O
constexpr std::uint8_t kMagic1 = 0x54;  // T

void write_u16_le(std::uint8_t* destination, std::uint16_t value) {
    destination[0] = static_cast<std::uint8_t>(value & 0xFFU);
    destination[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void write_u32_le(std::uint8_t* destination, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        destination[index] =
            static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

std::uint16_t read_u16_le(const std::uint8_t* source) {
    return static_cast<std::uint16_t>(source[0]) |
           static_cast<std::uint16_t>(source[1] << 8U);
}

std::uint32_t read_u32_le(const std::uint8_t* source) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(source[index]) << (index * 8U);
    }
    return value;
}

bool supported_type(std::uint8_t value) {
    return value == static_cast<std::uint8_t>(PacketType::position) ||
           value == static_cast<std::uint8_t>(PacketType::experimental_probe);
}

}  // namespace

std::size_t maximum_payload_for_mtu(std::size_t transport_mtu) {
    return transport_mtu < kPacketOverheadBytes
        ? 0
        : transport_mtu - kPacketOverheadBytes;
}

std::uint16_t crc16_ccitt_false(radio::ByteView bytes) {
    std::uint16_t crc = 0xFFFFU;
    for (std::size_t index = 0; index < bytes.size; ++index) {
        crc ^= static_cast<std::uint16_t>(bytes.data[index]) << 8U;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000U) != 0
                ? static_cast<std::uint16_t>((crc << 1U) ^ 0x1021U)
                : static_cast<std::uint16_t>(crc << 1U);
        }
    }
    return crc;
}

EncodeResult encode_packet(
    const PacketView& packet,
    radio::MutableByteView output) {
    if (output.data == nullptr ||
        (packet.payload.size > 0 && packet.payload.data == nullptr) ||
        packet.payload.size > std::numeric_limits<std::uint16_t>::max()) {
        return {PacketCodecError::invalid_argument, 0};
    }
    if (packet.header.version != kExperimentalPacketVersion) {
        return {PacketCodecError::unsupported_version, 0};
    }
    if (!supported_type(static_cast<std::uint8_t>(packet.header.type))) {
        return {PacketCodecError::unsupported_type, 0};
    }
    if (packet.header.flags != 0) {
        return {PacketCodecError::reserved_flags_set, 0};
    }
    if (packet.header.source_node_id == 0 || packet.header.network_id == 0 ||
        packet.header.message_id == 0) {
        return {PacketCodecError::invalid_identity, 0};
    }

    const auto encoded_size = kPacketOverheadBytes + packet.payload.size;
    if (output.size < encoded_size) {
        return {PacketCodecError::output_too_small, encoded_size};
    }

    output.data[0] = kMagic0;
    output.data[1] = kMagic1;
    output.data[2] = packet.header.version;
    output.data[3] = static_cast<std::uint8_t>(packet.header.type);
    output.data[4] = packet.header.flags;
    output.data[5] = static_cast<std::uint8_t>(kPacketHeaderBytes);
    write_u32_le(output.data + 6, packet.header.source_node_id);
    write_u32_le(output.data + 10, packet.header.network_id);
    write_u32_le(output.data + 14, packet.header.message_id);
    write_u16_le(
        output.data + 18,
        static_cast<std::uint16_t>(packet.payload.size));
    if (packet.payload.size > 0) {
        std::copy_n(
            packet.payload.data,
            packet.payload.size,
            output.data + kPacketHeaderBytes);
    }

    const auto checksum = crc16_ccitt_false({
        output.data,
        kPacketHeaderBytes + packet.payload.size,
    });
    write_u16_le(output.data + kPacketHeaderBytes + packet.payload.size, checksum);
    return {PacketCodecError::none, encoded_size};
}

DecodeResult decode_packet(radio::ByteView encoded) {
    if (encoded.data == nullptr) {
        return {PacketCodecError::invalid_argument, {}};
    }
    if (encoded.size < kPacketOverheadBytes || encoded.data[0] != kMagic0 ||
        encoded.data[1] != kMagic1 || encoded.data[5] != kPacketHeaderBytes) {
        return {PacketCodecError::malformed, {}};
    }
    if (encoded.data[2] != kExperimentalPacketVersion) {
        return {PacketCodecError::unsupported_version, {}};
    }
    if (!supported_type(encoded.data[3])) {
        return {PacketCodecError::unsupported_type, {}};
    }
    if (encoded.data[4] != 0) {
        return {PacketCodecError::reserved_flags_set, {}};
    }

    const auto payload_size = read_u16_le(encoded.data + 18);
    const auto expected_size = kPacketOverheadBytes + payload_size;
    if (encoded.size != expected_size) {
        return {PacketCodecError::malformed, {}};
    }

    const auto source_node_id = read_u32_le(encoded.data + 6);
    const auto network_id = read_u32_le(encoded.data + 10);
    const auto message_id = read_u32_le(encoded.data + 14);
    if (source_node_id == 0 || network_id == 0 || message_id == 0) {
        return {PacketCodecError::invalid_identity, {}};
    }

    const auto stored_checksum =
        read_u16_le(encoded.data + kPacketHeaderBytes + payload_size);
    const auto calculated_checksum = crc16_ccitt_false({
        encoded.data,
        kPacketHeaderBytes + payload_size,
    });
    if (stored_checksum != calculated_checksum) {
        return {PacketCodecError::integrity_failure, {}};
    }

    return {
        PacketCodecError::none,
        {
            {
                encoded.data[2],
                static_cast<PacketType>(encoded.data[3]),
                encoded.data[4],
                source_node_id,
                network_id,
                message_id,
            },
            {encoded.data + kPacketHeaderBytes, payload_size},
        },
    };
}

}  // namespace opentrail::protocol
