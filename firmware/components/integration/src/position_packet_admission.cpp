#include "opentrail/position_packet_admission.hpp"

#include <array>
#include <limits>

#include "opentrail/packet_codec.hpp"
#include "opentrail/position_codec.hpp"

namespace opentrail::integration {
namespace {

constexpr std::size_t kPositionPacketBytes =
    protocol::kPacketOverheadBytes + location::kPositionPayloadBytes;

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

bool valid_metadata(const PositionPacketMetadata& metadata) {
    return metadata.source_node_id != 0 && metadata.network_id != 0 &&
           metadata.message_id != 0;
}

}  // namespace

PositionPacketAdmissionSink::PositionPacketAdmissionSink(
    delivery::PriorityTrafficQueue& queue,
    PositionPacketMetadataSource& metadata,
    PositionPacketAdmissionPolicy policy)
    : queue_(queue), metadata_(metadata), policy_(policy) {
    status_.policy_valid = policy_.lifetime_ms != 0 &&
                           policy_.maximum_frame_bytes >= kPositionPacketBytes &&
                           policy_.maximum_frame_bytes <=
                               radio::kMaximumFrameBytes;
}

location::PositionBroadcastSinkError PositionPacketAdmissionSink::reject(
    PositionPacketAdmissionError error,
    bool backpressure) {
    status_.last_error = error;
    if (backpressure) {
        saturating_increment(status_.backpressured);
        return error == PositionPacketAdmissionError::queue_capacity
            ? location::PositionBroadcastSinkError::full
            : location::PositionBroadcastSinkError::not_ready;
    }
    saturating_increment(status_.failures);
    return location::PositionBroadcastSinkError::failed;
}

location::PositionBroadcastSinkError PositionPacketAdmissionSink::submit(
    radio::ByteView payload,
    std::uint64_t now_ms) {
    saturating_increment(status_.submit_attempts);
    if (!status_.policy_valid) {
        return reject(PositionPacketAdmissionError::invalid_policy, false);
    }

    const auto position = location::decode_position(payload);
    if (!position.decoded() ||
        position.position.state != location::BroadcastPositionState::current) {
        return reject(PositionPacketAdmissionError::invalid_payload, false);
    }

    const auto metadata = metadata_.next();
    switch (metadata.error) {
        case PositionPacketMetadataError::none:
            if (!valid_metadata(metadata)) {
                return reject(
                    PositionPacketAdmissionError::metadata_failed, false);
            }
            break;
        case PositionPacketMetadataError::not_ready:
            return reject(
                PositionPacketAdmissionError::metadata_not_ready, true);
        case PositionPacketMetadataError::exhausted:
            return reject(
                PositionPacketAdmissionError::metadata_exhausted, false);
        case PositionPacketMetadataError::failed:
        default:
            return reject(PositionPacketAdmissionError::metadata_failed, false);
    }

    std::array<std::uint8_t, kPositionPacketBytes> frame{};
    const protocol::PacketView packet{
        {
            protocol::kExperimentalPacketVersion,
            protocol::PacketType::position,
            0,
            metadata.source_node_id,
            metadata.network_id,
            metadata.message_id,
        },
        payload,
    };
    const auto encoded =
        protocol::encode_packet(packet, {frame.data(), frame.size()});
    if (!encoded.encoded() || encoded.encoded_bytes != frame.size()) {
        return reject(
            PositionPacketAdmissionError::packet_encode_failed, false);
    }

    const auto admitted = queue_.enqueue(
        metadata.message_id,
        delivery::MessageClass::position,
        {frame.data(), encoded.encoded_bytes},
        now_ms,
        policy_.lifetime_ms);
    switch (admitted.error) {
        case delivery::PriorityQueueError::none:
            status_.last_error = PositionPacketAdmissionError::none;
            saturating_increment(status_.admitted);
            return location::PositionBroadcastSinkError::none;
        case delivery::PriorityQueueError::rate_limited:
            return reject(
                PositionPacketAdmissionError::queue_rate_limited, true);
        case delivery::PriorityQueueError::reserved_capacity:
        case delivery::PriorityQueueError::queue_full:
            return reject(PositionPacketAdmissionError::queue_capacity, true);
        case delivery::PriorityQueueError::invalid_argument:
        case delivery::PriorityQueueError::invalid_policy:
        case delivery::PriorityQueueError::duplicate_message_id:
        case delivery::PriorityQueueError::payload_too_large:
        default:
            return reject(PositionPacketAdmissionError::queue_rejected, false);
    }
}

PositionPacketAdmissionStatus PositionPacketAdmissionSink::status() const {
    return status_;
}

}  // namespace opentrail::integration
