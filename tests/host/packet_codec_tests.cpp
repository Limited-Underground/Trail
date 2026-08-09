#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/packet_codec.hpp"

namespace {

using opentrail::protocol::DecodeResult;
using opentrail::protocol::PacketCodecError;
using opentrail::protocol::PacketHeader;
using opentrail::protocol::PacketType;
using opentrail::protocol::PacketView;
using opentrail::protocol::decode_packet;
using opentrail::protocol::encode_packet;
using opentrail::protocol::kPacketOverheadBytes;
using opentrail::protocol::maximum_payload_for_mtu;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

PacketView valid_packet(const std::uint8_t* payload, std::size_t size) {
    return {
        {0, PacketType::experimental_probe, 0, 0x01020304, 0x11223344, 7},
        {payload, size},
    };
}

std::array<std::uint8_t, 64> encode_valid(std::size_t& encoded_size) {
    const std::array<std::uint8_t, 5> payload{0x00, 0xFF, 0x10, 0x20, 0x30};
    std::array<std::uint8_t, 64> encoded{};
    const auto result = encode_packet(
        valid_packet(payload.data(), payload.size()),
        {encoded.data(), encoded.size()});
    EXPECT(result.encoded());
    encoded_size = result.encoded_bytes;
    return encoded;
}

void test_budget() {
    EXPECT(maximum_payload_for_mtu(0) == 0);
    EXPECT(maximum_payload_for_mtu(kPacketOverheadBytes - 1) == 0);
    EXPECT(maximum_payload_for_mtu(kPacketOverheadBytes) == 0);
    EXPECT(maximum_payload_for_mtu(163) == 141);
    EXPECT(maximum_payload_for_mtu(255) == 233);
}

void test_standard_crc_vector() {
    const std::array<std::uint8_t, 9> input{
        '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT(opentrail::protocol::crc16_ccitt_false(
               {input.data(), input.size()}) == 0x29B1);
}

void test_round_trip_binary_payload() {
    const std::array<std::uint8_t, 5> payload{0x00, 0xFF, 0x10, 0x20, 0x30};
    std::array<std::uint8_t, 64> encoded{};
    const auto encode = encode_packet(
        valid_packet(payload.data(), payload.size()),
        {encoded.data(), encoded.size()});
    EXPECT(encode.encoded());
    EXPECT(encode.encoded_bytes == kPacketOverheadBytes + payload.size());

    const DecodeResult decode = decode_packet({encoded.data(), encode.encoded_bytes});
    EXPECT(decode.decoded());
    EXPECT(decode.packet.header.version == 0);
    EXPECT(decode.packet.header.type == PacketType::experimental_probe);
    EXPECT(decode.packet.header.source_node_id == 0x01020304);
    EXPECT(decode.packet.header.network_id == 0x11223344);
    EXPECT(decode.packet.header.message_id == 7);
    EXPECT(decode.packet.payload.size == payload.size());
    for (std::size_t index = 0; index < payload.size(); ++index) {
        EXPECT(decode.packet.payload.data[index] == payload[index]);
    }
}

void test_encode_validation() {
    std::array<std::uint8_t, 64> output{};
    const std::array<std::uint8_t, 1> payload{1};

    auto packet = valid_packet(payload.data(), payload.size());
    packet.header.source_node_id = 0;
    EXPECT(encode_packet(packet, {output.data(), output.size()}).error ==
           PacketCodecError::invalid_identity);

    packet = valid_packet(payload.data(), payload.size());
    packet.header.flags = 1;
    EXPECT(encode_packet(packet, {output.data(), output.size()}).error ==
           PacketCodecError::reserved_flags_set);

    packet = valid_packet(payload.data(), payload.size());
    packet.header.version = 1;
    EXPECT(encode_packet(packet, {output.data(), output.size()}).error ==
           PacketCodecError::unsupported_version);

    packet = valid_packet(payload.data(), payload.size());
    packet.header.type = static_cast<PacketType>(1);
    EXPECT(encode_packet(packet, {output.data(), output.size()}).error ==
           PacketCodecError::unsupported_type);

    packet = valid_packet(payload.data(), payload.size());
    EXPECT(encode_packet(packet, {output.data(), 4}).error ==
           PacketCodecError::output_too_small);
}

void test_decode_rejects_malformed_and_incompatible_frames() {
    std::size_t size = 0;
    const auto valid = encode_valid(size);

    auto modified = valid;
    modified[0] = 0;
    EXPECT(decode_packet({modified.data(), size}).error == PacketCodecError::malformed);

    modified = valid;
    modified[2] = 1;
    EXPECT(decode_packet({modified.data(), size}).error ==
           PacketCodecError::unsupported_version);

    modified = valid;
    modified[3] = 1;
    EXPECT(decode_packet({modified.data(), size}).error ==
           PacketCodecError::unsupported_type);

    modified = valid;
    modified[4] = 1;
    EXPECT(decode_packet({modified.data(), size}).error ==
           PacketCodecError::reserved_flags_set);

    modified = valid;
    modified[5] = 19;
    EXPECT(decode_packet({modified.data(), size}).error == PacketCodecError::malformed);

    EXPECT(decode_packet({valid.data(), size - 1}).error == PacketCodecError::malformed);
}

void test_integrity_and_identity_rejection() {
    std::size_t size = 0;
    const auto valid = encode_valid(size);

    auto modified = valid;
    modified[20] ^= 0x80;
    EXPECT(decode_packet({modified.data(), size}).error ==
           PacketCodecError::integrity_failure);

    modified = valid;
    modified[6] = 0;
    modified[7] = 0;
    modified[8] = 0;
    modified[9] = 0;
    EXPECT(decode_packet({modified.data(), size}).error ==
           PacketCodecError::invalid_identity);
}

}  // namespace

int main() {
    test_budget();
    test_standard_crc_vector();
    test_round_trip_binary_payload();
    test_encode_validation();
    test_decode_rejects_malformed_and_incompatible_frames();
    test_integrity_and_identity_rejection();

    if (failures != 0) {
        std::cerr << failures << " packet codec assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 6 packet codec scenario groups\n";
    return EXIT_SUCCESS;
}
