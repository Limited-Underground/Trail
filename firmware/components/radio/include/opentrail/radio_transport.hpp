#pragma once

#include <cstddef>
#include <cstdint>

namespace opentrail::radio {

// Storage ceiling for transport implementations. Each concrete adapter reports
// its smaller, usable MTU at runtime.
inline constexpr std::size_t kMaximumFrameBytes = 255;

struct ByteView {
    const std::uint8_t* data{nullptr};
    std::size_t size{0};
};

struct MutableByteView {
    std::uint8_t* data{nullptr};
    std::size_t size{0};
};

enum class RadioError : std::uint8_t {
    none = 0,
    no_data,
    invalid_argument,
    payload_too_large,
    buffer_too_small,
    not_ready,
    busy,
    queue_full,
    peer_unavailable,
    io_failure,
    internal_failure,
};

enum class RadioState : std::uint8_t {
    offline = 0,
    idle,
    transmitting,
    receiving,
    fault,
};

struct LinkMetadata {
    std::uint64_t received_at_ms{0};
    std::uint32_t frequency_hz{0};
    std::int16_t rssi_dbm{0};
    std::int16_t snr_db_quarter{0};
    bool frequency_valid{false};
    bool rssi_valid{false};
    bool snr_valid{false};
};

struct SendResult {
    RadioError error{RadioError::none};
    std::size_t accepted_bytes{0};

    [[nodiscard]] constexpr bool accepted() const {
        return error == RadioError::none;
    }
};

struct ReceiveResult {
    RadioError error{RadioError::no_data};
    std::size_t received_bytes{0};
    LinkMetadata metadata{};

    [[nodiscard]] constexpr bool has_frame() const {
        return error == RadioError::none;
    }
};

struct TransportStatus {
    RadioState state{RadioState::offline};
    RadioError last_error{RadioError::none};
    std::size_t mtu_bytes{0};
    std::size_t transmit_queue_depth{0};
    std::size_t receive_queue_depth{0};
    std::uint32_t frames_sent{0};
    std::uint32_t frames_received{0};
    std::uint32_t frames_dropped{0};
};

// The transport moves opaque frames only. Identity, addressing, encryption,
// retries, acknowledgements, duplicate handling, and forwarding belong above
// this boundary.
class RadioTransport {
public:
    virtual ~RadioTransport() = default;

    [[nodiscard]] virtual std::size_t mtu() const = 0;
    [[nodiscard]] virtual TransportStatus status() const = 0;

    // Accepts and copies a frame for non-blocking transmission. Success means
    // queued by the transport, not delivered or acknowledged by another node.
    virtual SendResult send(ByteView frame, std::uint64_t now_ms) = 0;

    // Retrieves one complete frame. A buffer-too-small result must leave the
    // frame queued so the caller can retry with adequate storage.
    virtual ReceiveResult receive(MutableByteView destination) = 0;

    // Advances cooperative drivers without blocking the UI or application.
    virtual void service(std::uint64_t now_ms) = 0;
};

}  // namespace opentrail::radio
