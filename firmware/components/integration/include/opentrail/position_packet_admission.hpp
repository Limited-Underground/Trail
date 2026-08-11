#pragma once

#include <cstddef>
#include <cstdint>

#include "opentrail/position_broadcast_scheduler.hpp"
#include "opentrail/priority_queue.hpp"

namespace opentrail::integration {

enum class PositionPacketMetadataError : std::uint8_t {
    none = 0,
    not_ready,
    exhausted,
    failed,
};

struct PositionPacketMetadata {
    PositionPacketMetadataError error{PositionPacketMetadataError::not_ready};
    std::uint32_t source_node_id{0};
    std::uint32_t network_id{0};
    std::uint32_t message_id{0};

    [[nodiscard]] constexpr bool available() const {
        return error == PositionPacketMetadataError::none;
    }
};

// Production composition must supply identifiers from the selected identity,
// group, and rollback-safe message-counter lifecycle. This interface does not
// derive or persist them.
class PositionPacketMetadataSource {
public:
    virtual ~PositionPacketMetadataSource() = default;

    [[nodiscard]] virtual PositionPacketMetadata next() = 0;
};

struct PositionPacketAdmissionPolicy {
    std::size_t maximum_frame_bytes{0};
    std::uint32_t lifetime_ms{0};
};

enum class PositionPacketAdmissionError : std::uint8_t {
    none = 0,
    invalid_policy,
    invalid_payload,
    metadata_not_ready,
    metadata_exhausted,
    metadata_failed,
    packet_encode_failed,
    queue_rate_limited,
    queue_capacity,
    queue_rejected,
};

struct PositionPacketAdmissionStatus {
    bool policy_valid{false};
    PositionPacketAdmissionError last_error{
        PositionPacketAdmissionError::none};
    std::uint32_t submit_attempts{0};
    std::uint32_t admitted{0};
    std::uint32_t backpressured{0};
    std::uint32_t failures{0};
};

// Experimental packet-v0 adapter only. It validates one canonical current
// position, obtains caller-owned ephemeral envelope metadata, encodes exactly
// one 38-byte v0 frame, and admits it as background position traffic. It does
// not authenticate, encrypt, deliver, transmit, or persist anything.
class PositionPacketAdmissionSink final
    : public location::PositionBroadcastSink {
public:
    PositionPacketAdmissionSink(
        delivery::PriorityTrafficQueue& queue,
        PositionPacketMetadataSource& metadata,
        PositionPacketAdmissionPolicy policy);

    [[nodiscard]] location::PositionBroadcastSinkError submit(
        radio::ByteView payload,
        std::uint64_t now_ms) override;
    [[nodiscard]] PositionPacketAdmissionStatus status() const;

private:
    [[nodiscard]] location::PositionBroadcastSinkError reject(
        PositionPacketAdmissionError error,
        bool backpressure);

    delivery::PriorityTrafficQueue& queue_;
    PositionPacketMetadataSource& metadata_;
    PositionPacketAdmissionPolicy policy_{};
    PositionPacketAdmissionStatus status_{};
};

}  // namespace opentrail::integration
