#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "fake_gps_provider.hpp"
#include "fake_radio_transport.hpp"
#include "opentrail/location_tracker.hpp"
#include "opentrail/packet_codec.hpp"
#include "opentrail/position_codec.hpp"

namespace {

using opentrail::location::BroadcastPositionState;
using opentrail::location::GpsFix;
using opentrail::location::LocationTracker;
using opentrail::location::decode_position;
using opentrail::location::encode_position;
using opentrail::location::kPositionPayloadBytes;
using opentrail::location::test_support::FakeGpsProvider;
using opentrail::protocol::PacketType;
using opentrail::protocol::PacketView;
using opentrail::protocol::decode_packet;
using opentrail::protocol::encode_packet;
using opentrail::protocol::kPacketOverheadBytes;
using opentrail::radio::test_support::FakeRadioTransport;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

void test_position_crosses_packet_and_transport_boundaries() {
    FakeGpsProvider gps;
    GpsFix fix{};
    fix.latitude_e7 = 449775000;
    fix.longitude_e7 = -677500000;
    fix.horizontal_accuracy_cm = 250;
    fix.horizontal_accuracy_valid = true;
    fix.received_at_ms = 1000;
    fix.utc_valid = false;
    gps.set_fix(fix);
    LocationTracker tracker(gps, 30000);

    std::array<std::uint8_t, kPositionPayloadBytes> position_payload{};
    const auto position_encode = encode_position(
        tracker.snapshot(1250),
        {position_payload.data(), position_payload.size()});
    EXPECT(position_encode.encoded());

    const PacketView outbound{
        {0, PacketType::position, 0, 0xA001, 0xB001, 99},
        {position_payload.data(), position_encode.encoded_bytes},
    };
    std::array<std::uint8_t, 163> frame{};
    const auto packet_encode =
        encode_packet(outbound, {frame.data(), frame.size()});
    EXPECT(packet_encode.encoded());
    EXPECT(packet_encode.encoded_bytes ==
           kPacketOverheadBytes + kPositionPayloadBytes);
    EXPECT(packet_encode.encoded_bytes == 38);

    FakeRadioTransport node_a(163, 25);
    FakeRadioTransport node_b(163, 25);
    node_a.connect(node_b);
    node_b.connect(node_a);
    EXPECT(node_a.send({frame.data(), packet_encode.encoded_bytes}, 2000).accepted());
    node_a.service(2025);

    std::array<std::uint8_t, 163> received{};
    const auto receive = node_b.receive({received.data(), received.size()});
    EXPECT(receive.has_frame());
    const auto packet = decode_packet({received.data(), receive.received_bytes});
    EXPECT(packet.decoded());
    EXPECT(packet.packet.header.type == PacketType::position);
    const auto position = decode_position(packet.packet.payload);
    EXPECT(position.decoded());
    EXPECT(position.position.state == BroadcastPositionState::current);
    EXPECT(position.position.latitude_e7 == fix.latitude_e7);
    EXPECT(position.position.longitude_e7 == fix.longitude_e7);
    EXPECT(position.position.age_seconds == 1);
    EXPECT(position.position.horizontal_accuracy_m == 3);
}

}  // namespace

int main() {
    test_position_crosses_packet_and_transport_boundaries();
    if (failures != 0) {
        std::cerr << failures << " position packet integration assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: position packet/transport integration scenario\n";
    return EXIT_SUCCESS;
}
