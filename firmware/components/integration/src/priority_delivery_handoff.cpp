#include "opentrail/priority_delivery_handoff.hpp"

#include <algorithm>
#include <limits>

namespace opentrail::integration {
namespace {

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

}  // namespace

PriorityDeliveryHandoff::PriorityDeliveryHandoff(
    delivery::PriorityTrafficQueue& queue,
    delivery::DeliveryController& delivery)
    : queue_(queue), delivery_(delivery) {}

PriorityDeliveryHandoffResult PriorityDeliveryHandoff::service(
    std::uint64_t now_ms) {
    saturating_increment(status_.service_calls);
    if (status_.faulted) {
        return {
            PriorityDeliveryHandoffDisposition::failed,
            PriorityDeliveryHandoffError::latched_failure,
        };
    }

    const auto next = queue_.peek_next(now_ms);
    if (!next.has_message) {
        saturating_increment(status_.idle);
        return {};
    }

    const auto& message = next.message;
    if (message.expires_at_ms <= now_ms ||
        message.expires_at_ms - now_ms >
            std::numeric_limits<std::uint32_t>::max()) {
        saturating_increment(status_.failures);
        return {
            PriorityDeliveryHandoffDisposition::failed,
            PriorityDeliveryHandoffError::invalid_time,
            delivery::DeliveryError::none,
            message.message_id,
            true,
        };
    }

    auto policy = delivery::experimental_policy(message.message_class);
    const auto remaining_ms = static_cast<std::uint32_t>(
        message.expires_at_ms - now_ms);
    policy.expiry_ms = std::min(policy.expiry_ms, remaining_ms);
    const auto admitted = delivery_.enqueue(
        message.message_id,
        message.message_class,
        policy,
        {message.frame.data(), message.frame_size},
        now_ms);
    if (!admitted.accepted()) {
        if (admitted.error == delivery::DeliveryError::queue_full) {
            saturating_increment(status_.deferred);
            return {
                PriorityDeliveryHandoffDisposition::deferred,
                PriorityDeliveryHandoffError::delivery_queue_full,
                admitted.error,
                message.message_id,
                true,
            };
        }
        saturating_increment(status_.failures);
        return {
            PriorityDeliveryHandoffDisposition::failed,
            PriorityDeliveryHandoffError::delivery_rejected,
            admitted.error,
            message.message_id,
            true,
        };
    }

    if (!queue_.commit_next(message.message_id, now_ms)) {
        status_.faulted = true;
        saturating_increment(status_.failures);
        return {
            PriorityDeliveryHandoffDisposition::failed,
            PriorityDeliveryHandoffError::queue_commit_failed,
            delivery::DeliveryError::none,
            message.message_id,
            true,
        };
    }

    saturating_increment(status_.transferred);
    return {
        PriorityDeliveryHandoffDisposition::transferred,
        PriorityDeliveryHandoffError::none,
        delivery::DeliveryError::none,
        message.message_id,
        false,
    };
}

PriorityDeliveryHandoffStatus PriorityDeliveryHandoff::status() const {
    return status_;
}

}  // namespace opentrail::integration
