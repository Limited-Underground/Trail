#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/radio_transport.hpp"

namespace opentrail::radio::test_support {

class FakeRadioTransport final : public RadioTransport {
public:
    static constexpr std::size_t kQueueCapacity = 4;

    explicit FakeRadioTransport(
        std::size_t mtu_bytes = kMaximumFrameBytes,
        std::uint32_t delivery_latency_ms = 0);

    void connect(FakeRadioTransport& peer);
    void disconnect_peer();
    void set_available(bool available);
    void set_faulted(bool faulted);
    void set_link_metadata(const LinkMetadata& metadata);
    void drop_next_transmissions(std::size_t count);
    void fail_next_send(RadioError error);

    [[nodiscard]] std::size_t mtu() const override;
    [[nodiscard]] TransportStatus status() const override;
    SendResult send(ByteView frame, std::uint64_t now_ms) override;
    ReceiveResult receive(MutableByteView destination) override;
    void service(std::uint64_t now_ms) override;

private:
    struct QueuedFrame {
        std::array<std::uint8_t, kMaximumFrameBytes> bytes{};
        std::size_t size{0};
        std::uint64_t ready_at_ms{0};
        LinkMetadata metadata{};
    };

    [[nodiscard]] RadioState current_state() const;
    bool enqueue_received(const QueuedFrame& frame, std::uint64_t now_ms);

    std::array<QueuedFrame, kQueueCapacity> transmit_queue_{};
    std::array<QueuedFrame, kQueueCapacity> receive_queue_{};
    std::size_t transmit_head_{0};
    std::size_t transmit_tail_{0};
    std::size_t transmit_count_{0};
    std::size_t receive_head_{0};
    std::size_t receive_tail_{0};
    std::size_t receive_count_{0};
    std::size_t mtu_bytes_{kMaximumFrameBytes};
    std::uint32_t delivery_latency_ms_{0};
    std::uint32_t frames_sent_{0};
    std::uint32_t frames_received_{0};
    std::uint32_t frames_dropped_{0};
    std::size_t drops_remaining_{0};
    RadioError next_send_error_{RadioError::none};
    RadioError last_error_{RadioError::none};
    LinkMetadata link_metadata_{};
    FakeRadioTransport* peer_{nullptr};
    bool available_{true};
    bool faulted_{false};
};

}  // namespace opentrail::radio::test_support
