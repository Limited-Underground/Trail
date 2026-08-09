#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "fake_radio_transport.hpp"

namespace {

using opentrail::radio::ByteView;
using opentrail::radio::LinkMetadata;
using opentrail::radio::MutableByteView;
using opentrail::radio::RadioError;
using opentrail::radio::RadioState;
using opentrail::radio::test_support::FakeRadioTransport;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

void test_initial_state_and_mtu() {
    FakeRadioTransport radio(163, 25);
    const auto status = radio.status();
    EXPECT(radio.mtu() == 163);
    EXPECT(status.mtu_bytes == 163);
    EXPECT(status.state == RadioState::idle);
    EXPECT(status.transmit_queue_depth == 0);
    EXPECT(status.receive_queue_depth == 0);
}

void test_argument_and_size_errors() {
    FakeRadioTransport radio(4);
    const std::array<std::uint8_t, 5> oversized{1, 2, 3, 4, 5};

    EXPECT(radio.send({nullptr, 0}, 0).error == RadioError::invalid_argument);
    EXPECT(radio.send({oversized.data(), oversized.size()}, 0).error ==
           RadioError::payload_too_large);
    EXPECT(radio.status().transmit_queue_depth == 0);
}

void test_opaque_binary_delivery_and_metadata() {
    FakeRadioTransport sender(163, 25);
    FakeRadioTransport receiver(163, 25);
    sender.connect(receiver);

    LinkMetadata metadata{};
    metadata.frequency_hz = 910525000;
    metadata.frequency_valid = true;
    metadata.rssi_dbm = -72;
    metadata.rssi_valid = true;
    metadata.snr_db_quarter = 47;
    metadata.snr_valid = true;
    sender.set_link_metadata(metadata);

    const std::array<std::uint8_t, 6> frame{0x00, 0xFF, 0x10, 0x00, 0x7E, 0x42};
    const auto send = sender.send({frame.data(), frame.size()}, 100);
    EXPECT(send.accepted());
    EXPECT(send.accepted_bytes == frame.size());
    EXPECT(sender.status().state == RadioState::transmitting);

    sender.service(124);
    EXPECT(receiver.status().receive_queue_depth == 0);
    sender.service(125);
    EXPECT(sender.status().state == RadioState::idle);
    EXPECT(receiver.status().state == RadioState::receiving);

    std::array<std::uint8_t, 6> output{};
    const auto receive = receiver.receive({output.data(), output.size()});
    EXPECT(receive.has_frame());
    EXPECT(receive.received_bytes == frame.size());
    EXPECT(output == frame);
    EXPECT(receive.metadata.received_at_ms == 125);
    EXPECT(receive.metadata.frequency_valid);
    EXPECT(receive.metadata.frequency_hz == 910525000);
    EXPECT(receive.metadata.rssi_valid);
    EXPECT(receive.metadata.rssi_dbm == -72);
    EXPECT(receive.metadata.snr_valid);
    EXPECT(receive.metadata.snr_db_quarter == 47);
    EXPECT(sender.status().frames_sent == 1);
    EXPECT(receiver.status().frames_received == 1);
}

void test_small_destination_preserves_frame() {
    FakeRadioTransport sender(16);
    FakeRadioTransport receiver(16);
    sender.connect(receiver);
    const std::array<std::uint8_t, 4> frame{1, 2, 3, 4};
    EXPECT(sender.send({frame.data(), frame.size()}, 0).accepted());
    sender.service(0);

    std::array<std::uint8_t, 2> small{};
    const auto first = receiver.receive({small.data(), small.size()});
    EXPECT(first.error == RadioError::buffer_too_small);
    EXPECT(first.received_bytes == frame.size());
    EXPECT(receiver.status().receive_queue_depth == 1);

    std::array<std::uint8_t, 4> adequate{};
    EXPECT(receiver.receive({adequate.data(), adequate.size()}).has_frame());
    EXPECT(adequate == frame);
}

void test_queue_full_and_recovery() {
    FakeRadioTransport radio(8, 10);
    const std::array<std::uint8_t, 1> frame{0xA5};
    for (std::size_t index = 0; index < FakeRadioTransport::kQueueCapacity; ++index) {
        EXPECT(radio.send({frame.data(), frame.size()}, 0).accepted());
    }
    EXPECT(radio.send({frame.data(), frame.size()}, 0).error == RadioError::queue_full);
    radio.service(10);
    EXPECT(radio.status().transmit_queue_depth == 0);
    EXPECT(radio.status().frames_dropped == FakeRadioTransport::kQueueCapacity);
    EXPECT(radio.status().last_error == RadioError::peer_unavailable);
}

void test_offline_fault_and_injected_error() {
    FakeRadioTransport radio(8);
    const std::array<std::uint8_t, 1> frame{0x01};

    radio.set_available(false);
    EXPECT(radio.status().state == RadioState::offline);
    EXPECT(radio.send({frame.data(), frame.size()}, 0).error == RadioError::not_ready);

    radio.set_available(true);
    radio.set_faulted(true);
    EXPECT(radio.status().state == RadioState::fault);
    EXPECT(radio.send({frame.data(), frame.size()}, 0).error == RadioError::not_ready);

    radio.set_faulted(false);
    radio.fail_next_send(RadioError::busy);
    EXPECT(radio.send({frame.data(), frame.size()}, 0).error == RadioError::busy);
    EXPECT(radio.send({frame.data(), frame.size()}, 0).accepted());
}

void test_deterministic_loss() {
    FakeRadioTransport sender(16);
    FakeRadioTransport receiver(16);
    sender.connect(receiver);
    sender.drop_next_transmissions(1);
    const std::array<std::uint8_t, 2> frame{0x12, 0x34};

    EXPECT(sender.send({frame.data(), frame.size()}, 0).accepted());
    sender.service(0);
    EXPECT(sender.status().frames_dropped == 1);
    EXPECT(receiver.status().receive_queue_depth == 0);
}

void test_offline_service_pauses_queued_frame() {
    FakeRadioTransport sender(16);
    FakeRadioTransport receiver(16);
    sender.connect(receiver);
    const std::array<std::uint8_t, 1> frame{0x5A};

    EXPECT(sender.send({frame.data(), frame.size()}, 0).accepted());
    sender.set_available(false);
    sender.service(0);
    EXPECT(sender.status().transmit_queue_depth == 1);
    EXPECT(receiver.status().receive_queue_depth == 0);

    sender.set_available(true);
    sender.service(0);
    EXPECT(sender.status().transmit_queue_depth == 0);
    EXPECT(receiver.status().receive_queue_depth == 1);
}

}  // namespace

int main() {
    test_initial_state_and_mtu();
    test_argument_and_size_errors();
    test_opaque_binary_delivery_and_metadata();
    test_small_destination_preserves_frame();
    test_queue_full_and_recovery();
    test_offline_fault_and_injected_error();
    test_deterministic_loss();
    test_offline_service_pauses_queued_frame();

    if (failures != 0) {
        std::cerr << failures << " radio transport assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 radio transport scenarios\n";
    return EXIT_SUCCESS;
}
