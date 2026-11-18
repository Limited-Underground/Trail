#include "opentrail/single_repeater_forwarder.hpp"

#include <algorithm>
#include <limits>

namespace opentrail::delivery {

SingleRepeaterForwarder::SingleRepeaterForwarder(
    std::uint64_t local_alias,
    std::uint64_t group_context_id,
    std::uint32_t group_epoch,
    SingleRepeaterPolicy policy,
    DuplicateWindow& duplicate_window,
    SingleRepeaterAdmissionGate* admission_gate)
    : local_alias_(local_alias),
      group_context_id_(group_context_id),
      group_epoch_(group_epoch),
      policy_(policy),
      duplicate_window_(duplicate_window), admission_gate_(admission_gate) {}

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
    const VerifiedForwardingMetadata& input_metadata,
    radio::ByteView input_frame,
    std::uint64_t now_ms) {
    if (busy_) {
        return {false, SingleRepeaterDisposition::reentrant};
    }
    CallGuard guard(busy_);
    if (gate_uncertain_) {
        return {false, SingleRepeaterDisposition::admission_uncertain};
    }
    const auto metadata = input_metadata;
    if (metadata.group_context_id == 0 || metadata.source_alias == 0 ||
        metadata.group_epoch == 0 || metadata.message_id == 0 ||
        input_frame.data == nullptr || input_frame.size == 0 ||
        input_frame.size > radio::kMaximumFrameBytes) {
        return {false, SingleRepeaterDisposition::invalid_argument};
    }
    // The external gate may indirectly mutate its caller's input. Bind both
    // validation and queued output to private snapshots taken before the call.
    std::array<std::uint8_t, radio::kMaximumFrameBytes> frame_copy{};
    std::copy_n(input_frame.data, input_frame.size, frame_copy.begin());
    const radio::ByteView exact_protected_frame{frame_copy.data(), input_frame.size};
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

    if (admission_gate_ != nullptr) {
        const auto admission = admission_gate_->evaluate(
            metadata, exact_protected_frame.size, now_ms);
        if (admission != SingleRepeaterAdmission::allow) {
            ++counters_.congestion_dropped;
            if (admission != SingleRepeaterAdmission::denied) {
                gate_uncertain_ = true;
                return {false, SingleRepeaterDisposition::admission_uncertain, true};
            }
            return {false, SingleRepeaterDisposition::admission_denied, true};
        }
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
    queued.provenance = {metadata, now_ms, policy_.maximum_queue_age_ms};
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

std::uint8_t SingleRepeaterForwarder::prune_expired_impl(std::uint64_t now_ms) {
    std::uint8_t dropped = 0;
    while (queue_count_ > 0) {
        const auto& queued = queue_[queue_head_];
        if (now_ms < queued.provenance.enqueued_at_ms ||
            now_ms - queued.provenance.enqueued_at_ms >= queued.provenance.maximum_queue_age_ms) {
            pop_front();
            ++counters_.expired_dropped;
            ++dropped;  // At most the eight fixed queue entries.
            continue;
        }
        break;
    }
    return dropped;
}

std::uint8_t SingleRepeaterForwarder::prune_expired(std::uint64_t now_ms) {
    if (busy_) { return 0; }
    CallGuard guard(busy_);
    return prune_expired_impl(now_ms);
}

ExactForwardedFrameResult SingleRepeaterForwarder::next_forward(std::uint64_t now_ms) {
    ExactForwardedFrameResult result{};
    if (busy_) { return result; }
    CallGuard guard(busy_);
    if (gate_uncertain_) { return result; }
    result.expired_frames_dropped = prune_expired_impl(now_ms);
    if (queue_count_ != 0) {
        result.has_frame = true;
        result.frame = queue_[queue_head_].frame;
        result.provenance = queue_[queue_head_].provenance;
        pop_front();
    }
    return result;
}

SingleRepeaterStatus SingleRepeaterForwarder::status() const {
    auto result = counters_;
    result.queue_depth = queue_count_;
    result.admission_uncertain = gate_uncertain_;
    return result;
}

}  // namespace opentrail::delivery
