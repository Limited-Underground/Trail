#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "fake_radio_transport.hpp"
#include "opentrail/delivery_controller.hpp"
#include "opentrail/duplicate_window.hpp"
#include "opentrail/packet_codec.hpp"

namespace {

using opentrail::delivery::DeliveryController;
using opentrail::delivery::DeliveryOutcome;
using opentrail::delivery::DuplicateObservation;
using opentrail::delivery::DuplicateWindow;
using opentrail::delivery::MessageClass;
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

void test_lost_ack_causes_retry_but_not_duplicate_delivery() {
    FakeRadioTransport sender_radio(163);
    FakeRadioTransport receiver_radio(163);
    sender_radio.connect(receiver_radio);
    DeliveryController delivery(sender_radio);
    DuplicateWindow duplicate_window(10000);

    const std::array<std::uint8_t, 4> payload{'D', 'A', 'T', 'A'};
    const PacketView packet{
        {0, PacketType::experimental_probe, 0, 0x101, 0x202, 77},
        {payload.data(), payload.size()},
    };
    std::array<std::uint8_t, 163> frame{};
    const auto encode = encode_packet(packet, {frame.data(), frame.size()});
    EXPECT(encode.encoded());
    EXPECT(delivery.enqueue(
               77,
               MessageClass::direct_message,
               {true, 3, 100, 1000},
               {frame.data(), encode.encoded_bytes},
               0)
               .accepted());

    std::array<std::uint8_t, 163> received{};
    delivery.service(0);
    sender_radio.service(0);
    auto receive = receiver_radio.receive({received.data(), received.size()});
    EXPECT(receive.has_frame());
    auto decoded = decode_packet({received.data(), receive.received_bytes});
    EXPECT(decoded.decoded());
    EXPECT(duplicate_window.observe(
               {
                   decoded.packet.header.source_node_id,
                   1,
                   decoded.packet.header.message_id,
               },
               0)
               .observation == DuplicateObservation::accepted);

    // Simulate a lost acknowledgement. The sender retries at 100 ms.
    delivery.service(100);
    sender_radio.service(100);
    receive = receiver_radio.receive({received.data(), received.size()});
    EXPECT(receive.has_frame());
    decoded = decode_packet({received.data(), receive.received_bytes});
    EXPECT(duplicate_window.observe(
               {
                   decoded.packet.header.source_node_id,
                   1,
                   decoded.packet.header.message_id,
               },
               100)
               .observation == DuplicateObservation::duplicate);

    EXPECT(delivery.acknowledge(77, 110));
    const auto event = delivery.next_event();
    EXPECT(event.has_event);
    EXPECT(event.event.outcome == DeliveryOutcome::confirmed);
    EXPECT(event.event.attempts == 2);
    EXPECT(duplicate_window.status(110).accepted == 1);
    EXPECT(duplicate_window.status(110).duplicates == 1);
}

}  // namespace

int main() {
    test_lost_ack_causes_retry_but_not_duplicate_delivery();
    if (failures != 0) {
        std::cerr << failures << " delivery integration assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: lost-ack retry/duplicate integration scenario\n";
    return EXIT_SUCCESS;
}
