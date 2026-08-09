#include "opentrail/forwarding_controller.hpp"

#include <algorithm>

namespace opentrail::delivery {

ForwardingController::ForwardingController(
    std::uint64_t local_alias,
    std::uint64_t group_id,
    std::uint32_t group_epoch,
    ForwardingPolicy policy,
    DuplicateWindow& duplicate_window)
    : local_alias_(local_alias),
      group_id_(group_id),
      group_epoch_(group_epoch),
      policy_(policy),
      duplicate_window_(duplicate_window) {}

bool ForwardingController::valid_metadata(
    const RoutingMetadata& metadata,
    radio::ByteView frame) {
    return metadata.source_alias != 0 && metadata.group_id != 0 &&
           metadata.group_epoch != 0 && metadata.message_id != 0 &&
           frame.data != nullptr && frame.size > 0 &&
           frame.size <= radio::kMaximumFrameBytes;
}

bool ForwardingController::valid_configuration() const {
    if (local_alias_ == 0 || group_id_ == 0 || group_epoch_ == 0) {
        return false;
    }
    if (!policy_.forwarding_enabled) {
        return true;
    }
    return policy_.maximum_queue_depth > 0 &&
           policy_.maximum_queue_depth <= kForwardQueueCapacity &&
           policy_.maximum_forwards_per_window > 0 &&
           policy_.rate_window_ms > 0;
}

void ForwardingController::update_rate_window(std::uint64_t now_ms) {
    if (!rate_window_initialized_ || now_ms < rate_window_started_ms_ ||
        now_ms - rate_window_started_ms_ >= policy_.rate_window_ms) {
        rate_window_started_ms_ = now_ms;
        forwards_in_window_ = 0;
        rate_window_initialized_ = true;
    }
}

void ForwardingController::record_congestion_drop() {
    ++counters_.congestion_dropped;
}

ForwardingDecision ForwardingController::process(
    const RoutingMetadata& metadata,
    radio::ByteView frame,
    std::uint64_t now_ms) {
    if (!valid_metadata(metadata, frame)) {
        return {false, false, ForwardingDisposition::invalid_argument};
    }
    if (!valid_configuration()) {
        return {false, false, ForwardingDisposition::invalid_policy};
    }
    if (metadata.group_id != group_id_ || metadata.group_epoch != group_epoch_) {
        ++counters_.wrong_group_dropped;
        return {false, false, ForwardingDisposition::wrong_group_or_epoch};
    }

    const auto duplicate = duplicate_window_.observe(
        {metadata.source_alias, metadata.group_epoch, metadata.message_id},
        now_ms);
    if (!duplicate.valid()) {
        return {false, false, ForwardingDisposition::invalid_argument};
    }
    if (duplicate.observation == DuplicateObservation::duplicate) {
        ++counters_.duplicates_dropped;
        return {false, false, ForwardingDisposition::duplicate};
    }
    if (metadata.source_alias == local_alias_) {
        return {false, false, ForwardingDisposition::self_source_untracked};
    }

    ++counters_.frames_accepted;
    const bool deliver_local =
        metadata.destination_alias == 0 || metadata.destination_alias == local_alias_;
    if (deliver_local) {
        ++counters_.local_deliveries;
    }

    if (metadata.destination_alias == local_alias_) {
        return {true, false, ForwardingDisposition::destination_reached};
    }
    if (!metadata.forwarding_permitted) {
        return {deliver_local, false, ForwardingDisposition::forwarding_not_permitted};
    }
    if (!policy_.forwarding_enabled) {
        return {deliver_local, false, ForwardingDisposition::forwarding_disabled};
    }
    if (metadata.hops_remaining == 0) {
        ++counters_.ttl_dropped;
        return {deliver_local, false, ForwardingDisposition::ttl_exhausted};
    }

    update_rate_window(now_ms);
    if (forwards_in_window_ >= policy_.maximum_forwards_per_window) {
        record_congestion_drop();
        return {deliver_local, false, ForwardingDisposition::rate_limited};
    }
    if (queue_count_ >= policy_.maximum_queue_depth) {
        record_congestion_drop();
        return {deliver_local, false, ForwardingDisposition::queue_full};
    }

    auto& queued = queue_[queue_tail_];
    queued.metadata = metadata;
    --queued.metadata.hops_remaining;
    std::copy_n(frame.data, frame.size, queued.bytes.begin());
    queued.size = frame.size;
    queue_tail_ = (queue_tail_ + 1) % kForwardQueueCapacity;
    ++queue_count_;
    ++forwards_in_window_;
    ++counters_.frames_queued;
    return {deliver_local, true, ForwardingDisposition::queued};
}

DuplicateError ForwardingController::record_originated(
    std::uint32_t message_id,
    std::uint64_t now_ms) {
    return duplicate_window_
        .observe({local_alias_, group_epoch_, message_id}, now_ms)
        .error;
}

ForwardedFrameResult ForwardingController::next_forward() {
    if (queue_count_ == 0) {
        return {};
    }
    const auto frame = queue_[queue_head_];
    queue_[queue_head_] = {};
    queue_head_ = (queue_head_ + 1) % kForwardQueueCapacity;
    --queue_count_;
    return {true, frame};
}

ForwardingStatus ForwardingController::status() const {
    auto status = counters_;
    status.queue_depth = queue_count_;
    return status;
}

}  // namespace opentrail::delivery
