#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "fake_radio_transport.hpp"
#include "opentrail/packet_codec.hpp"

namespace {

using opentrail::protocol::PacketCodecError;
using opentrail::protocol::PacketType;
using opentrail::protocol::PacketView;
using opentrail::protocol::decode_packet;
using opentrail::protocol::encode_packet;
using opentrail::radio::test_support::FakeRadioTransport;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

void test_versioned_packet_crosses_transport_interface() {
    FakeRadioTransport node_a(163, 40);
    FakeRadioTransport node_b(163, 40);
    node_a.connect(node_b);
    node_b.connect(node_a);

    const std::array<std::uint8_t, 7> payload{'O', 'T', '-', 'P', 'I', 'N', 'G'};
    const PacketView outbound{
        {0, PacketType::experimental_probe, 0, 0xA001, 0xB001, 1},
        {payload.data(), payload.size()},
    };
    std::array<std::uint8_t, 163> encoded{};
    const auto encode = encode_packet(outbound, {encoded.data(), encoded.size()});
    EXPECT(encode.encoded());

    const auto send = node_a.send({encoded.data(), encode.encoded_bytes}, 1000);
    EXPECT(send.accepted());
    node_a.service(1039);
    EXPECT(node_b.status().receive_queue_depth == 0);
    node_a.service(1040);
    EXPECT(node_b.status().receive_queue_depth == 1);

    std::array<std::uint8_t, 163> received{};
    const auto receive = node_b.receive({received.data(), received.size()});
    EXPECT(receive.has_frame());
    const auto decode = decode_packet({received.data(), receive.received_bytes});
    EXPECT(decode.decoded());
    EXPECT(decode.packet.header.source_node_id == 0xA001);
    EXPECT(decode.packet.header.network_id == 0xB001);
    EXPECT(decode.packet.header.message_id == 1);
    EXPECT(decode.packet.payload.size == payload.size());
    for (std::size_t index = 0; index < payload.size(); ++index) {
        EXPECT(decode.packet.payload.data[index] == payload[index]);
    }
}

void test_transport_delivers_corruption_for_codec_to_reject() {
    FakeRadioTransport node_a(163);
    FakeRadioTransport node_b(163);
    node_a.connect(node_b);

    const std::array<std::uint8_t, 2> payload{0xAA, 0x55};
    const PacketView outbound{
        {0, PacketType::experimental_probe, 0, 1, 2, 3},
        {payload.data(), payload.size()},
    };
    std::array<std::uint8_t, 163> encoded{};
    const auto encode = encode_packet(outbound, {encoded.data(), encoded.size()});
    EXPECT(encode.encoded());
    encoded[20] ^= 0x01;

    EXPECT(node_a.send({encoded.data(), encode.encoded_bytes}, 0).accepted());
    node_a.service(0);
    std::array<std::uint8_t, 163> received{};
    const auto receive = node_b.receive({received.data(), received.size()});
    EXPECT(receive.has_frame());
    EXPECT(decode_packet({received.data(), receive.received_bytes}).error ==
           PacketCodecError::integrity_failure);
}

}  // namespace

int main() {
    test_versioned_packet_crosses_transport_interface();
    test_transport_delivers_corruption_for_codec_to_reject();

    if (failures != 0) {
        std::cerr << failures << " packet/transport integration assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 2 packet/transport integration scenarios\n";
    return EXIT_SUCCESS;
}
