#include "opentrail/delivery_controller.hpp"

#include <algorithm>
#include <limits>

namespace opentrail::delivery {

DeliveryController::DeliveryController(radio::RadioTransport& transport)
    : transport_(transport) {}

bool DeliveryController::valid_policy(const DeliveryPolicy& policy) {
    if (policy.maximum_attempts == 0 || policy.expiry_ms == 0) {
        return false;
    }
    if (policy.requires_acknowledgement) {
        return policy.retry_interval_ms > 0;
    }
    return policy.maximum_attempts == 1 && policy.retry_interval_ms == 0;
}

bool DeliveryController::permanent_transport_error(radio::RadioError error) {
    return error == radio::RadioError::invalid_argument ||
           error == radio::RadioError::payload_too_large ||
           error == radio::RadioError::buffer_too_small ||
           error == radio::RadioError::internal_failure;
}

std::uint64_t DeliveryController::saturating_add(
    std::uint64_t value,
    std::uint32_t increment) {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    return value > maximum - increment ? maximum : value + increment;
}

EnqueueResult DeliveryController::enqueue(
    std::uint32_t message_id,
    MessageClass message_class,
    DeliveryPolicy policy,
    radio::ByteView frame,
    std::uint64_t now_ms) {
    if (message_id == 0 || frame.data == nullptr || frame.size == 0) {
        return {DeliveryError::invalid_argument};
    }
    if (!valid_policy(policy)) {
        return {DeliveryError::invalid_policy};
    }
    if (frame.size > transport_.mtu() ||
        frame.size > radio::kMaximumFrameBytes) {
        return {DeliveryError::payload_too_large};
    }
    for (const auto& pending : pending_) {
        if (pending.used && pending.message_id == message_id) {
            return {DeliveryError::duplicate_message_id};
        }
    }
    if (pending_count_ == kPendingCapacity) {
        return {DeliveryError::queue_full};
    }

    for (auto& pending : pending_) {
        if (pending.used) {
            continue;
        }
        std::copy_n(frame.data, frame.size, pending.frame.begin());
        pending.frame_size = frame.size;
        pending.message_id = message_id;
        pending.message_class = message_class;
        pending.policy = policy;
        pending.expires_at_ms = saturating_add(now_ms, policy.expiry_ms);
        pending.next_attempt_at_ms = now_ms;
        pending.attempts = 0;
        pending.last_transport_error = radio::RadioError::none;
        pending.used = true;
        ++pending_count_;
        return {};
    }

    return {DeliveryError::queue_full};
}

void DeliveryController::push_event(const DeliveryEvent& event) {
    if (event_count_ == kEventCapacity) {
        ++counters_.events_dropped;
        return;
    }
    events_[event_tail_] = event;
    event_tail_ = (event_tail_ + 1) % kEventCapacity;
    ++event_count_;
}

void DeliveryController::complete(
    std::size_t slot,
    DeliveryOutcome outcome,
    radio::RadioError transport_error) {
    const auto& pending = pending_[slot];
    push_event({
        pending.message_id,
        pending.message_class,
        outcome,
        pending.attempts,
        transport_error,
    });
    switch (outcome) {
        case DeliveryOutcome::confirmed:
            ++counters_.confirmed;
            break;
        case DeliveryOutcome::sent_unconfirmed:
            ++counters_.sent_unconfirmed;
            break;
        case DeliveryOutcome::expired:
            ++counters_.expired;
            break;
        case DeliveryOutcome::attempts_exhausted:
            ++counters_.attempts_exhausted;
            break;
        case DeliveryOutcome::transport_rejected:
            ++counters_.transport_rejected;
            break;
    }
    pending_[slot] = {};
    --pending_count_;
}

void DeliveryController::service(std::uint64_t now_ms) {
    for (std::size_t slot = 0; slot < pending_.size(); ++slot) {
        auto& pending = pending_[slot];
        if (!pending.used) {
            continue;
        }
        if (now_ms >= pending.expires_at_ms) {
            complete(slot, DeliveryOutcome::expired);
            continue;
        }
        if (now_ms < pending.next_attempt_at_ms) {
            continue;
        }
        if (pending.attempts >= pending.policy.maximum_attempts) {
            complete(slot, DeliveryOutcome::attempts_exhausted);
            continue;
        }

        const auto result = transport_.send(
            {pending.frame.data(), pending.frame_size}, now_ms);
        if (!result.accepted()) {
            pending.last_transport_error = result.error;
            if (permanent_transport_error(result.error)) {
                complete(slot, DeliveryOutcome::transport_rejected, result.error);
            } else {
                pending.next_attempt_at_ms = saturating_add(
                    now_ms,
                    pending.policy.requires_acknowledgement
                        ? pending.policy.retry_interval_ms
                        : kTransientRejectionBackoffMs);
            }
            continue;
        }

        ++pending.attempts;
        pending.last_transport_error = radio::RadioError::none;
        if (!pending.policy.requires_acknowledgement) {
            complete(slot, DeliveryOutcome::sent_unconfirmed);
        } else {
            pending.next_attempt_at_ms = saturating_add(
                now_ms,
                pending.policy.retry_interval_ms);
        }
    }
}

bool DeliveryController::acknowledge(
    std::uint32_t message_id,
    std::uint64_t now_ms) {
    if (message_id == 0) {
        return false;
    }
    for (std::size_t slot = 0; slot < pending_.size(); ++slot) {
        const auto& pending = pending_[slot];
        if (!pending.used || pending.message_id != message_id ||
            !pending.policy.requires_acknowledgement || pending.attempts == 0 ||
            now_ms >= pending.expires_at_ms) {
            continue;
        }
        complete(slot, DeliveryOutcome::confirmed);
        return true;
    }
    return false;
}

DeliveryEventResult DeliveryController::next_event() {
    if (event_count_ == 0) {
        return {};
    }
    const auto event = events_[event_head_];
    events_[event_head_] = {};
    event_head_ = (event_head_ + 1) % kEventCapacity;
    --event_count_;
    return {true, event};
}

DeliveryStatus DeliveryController::status() const {
    auto status = counters_;
    status.pending = pending_count_;
    status.events_waiting = event_count_;
    return status;
}

}  // namespace opentrail::delivery
