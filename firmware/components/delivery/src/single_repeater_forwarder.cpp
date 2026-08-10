#include "opentrail/single_repeater_forwarder.hpp"

#include <algorithm>
#include <limits>

namespace opentrail::delivery {

SingleRepeaterForwarder::SingleRepeaterForwarder(
    std::uint64_t local_alias,
    std::uint64_t group_context_id,
    std::uint32_t group_epoch,
    SingleRepeaterPolicy policy,
    DuplicateWindow& duplicate_window)
    : local_alias_(local_alias),
      group_context_id_(group_context_id),
      group_epoch_(group_epoch),
      policy_(policy),
      duplicate_window_(duplicate_window) {}

bool SingleRepeaterForwarder::valid_configuration() const {
    return local_alias_ != 0 && group_context_id_ != 0 && group_epoch_ != 0 &&
           policy_.configured_authorized_repeaters == 1 &&
           policy_.local_repeater_authorized &&
           policy_.maximum_queue_depth > 0 &&
           policy_.maximum_queue_depth <= kSingleRepeaterQueueCapacity &&
           policy_.maximum_forwards_per_window > 0 &&
           policy_.rate_window_ms > 0 && policy_.maximum_queue_age_ms > 0;
}

void SingleRepeaterForwarder::update_rate_window(std::uint64_t now_ms) {
    if (!rate_window_initialized_ ||
        now_ms - rate_window_started_ms_ >= policy_.rate_window_ms) {
        rate_window_started_ms_ = now_ms;
        forwards_in_window_ = 0;
        rate_window_initialized_ = true;
    }
}

SingleRepeaterDecision SingleRepeaterForwarder::process(
    const VerifiedForwardingMetadata& metadata,
    radio::ByteView exact_protected_frame,
    std::uint64_t now_ms) {
    if (metadata.group_context_id == 0 || metadata.source_alias == 0 ||
        metadata.group_epoch == 0 || metadata.message_id == 0 ||
        exact_protected_frame.data == nullptr ||
        exact_protected_frame.size == 0 ||
        exact_protected_frame.size > radio::kMaximumFrameBytes) {
        return {false, SingleRepeaterDisposition::invalid_argument};
    }
    if (!valid_configuration()) {
        return {false, SingleRepeaterDisposition::invalid_policy};
    }
    if (!metadata.source_authenticated) {
        ++counters_.authentication_dropped;
        return {
            false,
            SingleRepeaterDisposition::source_authentication_required};
    }
    if (!metadata.source_authorized) {
        ++counters_.authorization_dropped;
        return {false, SingleRepeaterDisposition::source_unauthorized};
    }
    if (metadata.group_context_id != group_context_id_ ||
        metadata.group_epoch != group_epoch_) {
        ++counters_.wrong_context_dropped;
        return {false, SingleRepeaterDisposition::wrong_group_or_epoch};
    }
    if (metadata.source_alias == local_alias_) {
        return {false, SingleRepeaterDisposition::self_source};
    }
    if (metadata.destination_alias == local_alias_) {
        return {false, SingleRepeaterDisposition::destination_reached};
    }
    if (!metadata.forwarding_permitted) {
        return {false, SingleRepeaterDisposition::forwarding_not_permitted};
    }
    if (process_time_initialized_ && now_ms < last_process_ms_) {
        ++counters_.clock_regression_dropped;
        return {false, SingleRepeaterDisposition::clock_regression};
    }
    last_process_ms_ = now_ms;
    process_time_initialized_ = true;

    const auto duplicate = duplicate_window_.observe(
        {metadata.source_alias, metadata.group_epoch, metadata.message_id},
        now_ms);
    if (!duplicate.valid()) {
        return {false, SingleRepeaterDisposition::invalid_argument};
    }
    if (duplicate.observation == DuplicateObservation::duplicate) {
        ++counters_.duplicates_dropped;
        return {false, SingleRepeaterDisposition::duplicate};
    }

    update_rate_window(now_ms);
    if (forwards_in_window_ >= policy_.maximum_forwards_per_window) {
        ++counters_.congestion_dropped;
        return {false, SingleRepeaterDisposition::rate_limited, true};
    }
    if (queue_count_ >= policy_.maximum_queue_depth) {
        ++counters_.congestion_dropped;
        return {false, SingleRepeaterDisposition::queue_full, true};
    }

    auto& queued = queue_[queue_tail_];
    std::copy_n(
        exact_protected_frame.data,
        exact_protected_frame.size,
        queued.frame.bytes.begin());
    queued.frame.size = exact_protected_frame.size;
    queued.queued_at_ms = now_ms;
    queue_tail_ = (queue_tail_ + 1) % kSingleRepeaterQueueCapacity;
    ++queue_count_;
    ++forwards_in_window_;
    ++counters_.queued;
    return {true, SingleRepeaterDisposition::queued, true};
}

void SingleRepeaterForwarder::pop_front() {
    queue_[queue_head_] = {};
    queue_head_ = (queue_head_ + 1) % kSingleRepeaterQueueCapacity;
    --queue_count_;
}

ExactForwardedFrameResult SingleRepeaterForwarder::next_forward(
    std::uint64_t now_ms) {
    ExactForwardedFrameResult result{};
    while (queue_count_ > 0) {
        const auto& queued = queue_[queue_head_];
        if (now_ms < queued.queued_at_ms ||
            now_ms - queued.queued_at_ms >= policy_.maximum_queue_age_ms) {
            pop_front();
            ++counters_.expired_dropped;
            if (result.expired_frames_dropped !=
                std::numeric_limits<std::uint8_t>::max()) {
                ++result.expired_frames_dropped;
            }
            continue;
        }
        result.has_frame = true;
        result.frame = queued.frame;
        pop_front();
        break;
    }
    return result;
}

SingleRepeaterStatus SingleRepeaterForwarder::status() const {
    auto result = counters_;
    result.queue_depth = queue_count_;
    return result;
}

}  // namespace opentrail::delivery
