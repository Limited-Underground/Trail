#include "opentrail/position_broadcast_scheduler.hpp"

#include <array>
#include <limits>

namespace opentrail::location {
namespace {

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

}  // namespace

PositionBroadcastScheduler::PositionBroadcastScheduler(
    PositionBroadcastSink& sink,
    PositionBroadcastSchedulePolicy policy)
    : sink_(sink), policy_(policy) {
    status_.policy_valid =
        policy_.cadence_ms != 0 && policy_.retry_ms != 0;
}

PositionBroadcastScheduleError PositionBroadcastScheduler::start(
    std::uint64_t now_ms) {
    saturating_increment(status_.start_attempts);
    if (!status_.policy_valid) {
        status_.last_error = PositionBroadcastScheduleError::invalid_policy;
        return status_.last_error;
    }
    if (status_.clock_failed) {
        status_.last_error = PositionBroadcastScheduleError::clock_regression;
        return status_.last_error;
    }
    if (status_.time_exhausted) {
        status_.last_error = PositionBroadcastScheduleError::time_exhausted;
        return status_.last_error;
    }
    if (!observe_time(now_ms)) {
        return status_.last_error;
    }
    if (!status_.active) {
        status_.active = true;
        status_.next_attempt_ms = now_ms;
        saturating_increment(status_.starts);
    }
    status_.last_error = PositionBroadcastScheduleError::none;
    return status_.last_error;
}

void PositionBroadcastScheduler::stop() {
    if (status_.active) {
        status_.active = false;
        status_.next_attempt_ms = 0;
        saturating_increment(status_.stops);
    }
}

PositionBroadcastScheduleResult PositionBroadcastScheduler::service(
    const LocationSnapshot& snapshot,
    std::uint64_t now_ms) {
    saturating_increment(status_.service_calls);
    if (!status_.policy_valid) {
        status_.last_error = PositionBroadcastScheduleError::invalid_policy;
        return {
            PositionBroadcastScheduleDisposition::failed,
            status_.last_error,
            0,
        };
    }
    if (status_.clock_failed) {
        status_.last_error = PositionBroadcastScheduleError::clock_regression;
        return {
            PositionBroadcastScheduleDisposition::failed,
            status_.last_error,
            0,
        };
    }
    if (status_.time_exhausted) {
        status_.last_error = PositionBroadcastScheduleError::time_exhausted;
        return {
            PositionBroadcastScheduleDisposition::failed,
            status_.last_error,
            0,
        };
    }
    if (!observe_time(now_ms)) {
        return {
            PositionBroadcastScheduleDisposition::failed,
            status_.last_error,
            0,
        };
    }
    if (!status_.active) {
        status_.last_error = PositionBroadcastScheduleError::none;
        return {};
    }
    if (now_ms < status_.next_attempt_ms) {
        status_.last_error = PositionBroadcastScheduleError::none;
        return {
            PositionBroadcastScheduleDisposition::not_due,
            PositionBroadcastScheduleError::none,
            status_.next_attempt_ms,
        };
    }

    if (!snapshot.usable()) {
        saturating_increment(status_.suppressed);
        status_.last_error = PositionBroadcastScheduleError::no_current_fix;
        schedule_after(now_ms, policy_.retry_ms);
        return {
            PositionBroadcastScheduleDisposition::deferred,
            PositionBroadcastScheduleError::no_current_fix,
            status_.next_attempt_ms,
        };
    }

    std::array<std::uint8_t, kPositionPayloadBytes> payload{};
    const auto encoded = encode_position(
        snapshot, {payload.data(), payload.size()});
    if (!encoded.encoded()) {
        saturating_increment(status_.failures);
        status_.last_error = PositionBroadcastScheduleError::encode_failed;
        schedule_after(now_ms, policy_.retry_ms);
        return {
            PositionBroadcastScheduleDisposition::deferred,
            PositionBroadcastScheduleError::encode_failed,
            status_.next_attempt_ms,
        };
    }

    const auto sink_error = sink_.submit({payload.data(), payload.size()});
    switch (sink_error) {
        case PositionBroadcastSinkError::none:
            saturating_increment(status_.submitted);
            status_.last_error = PositionBroadcastScheduleError::none;
            schedule_after(now_ms, policy_.cadence_ms);
            return {
                PositionBroadcastScheduleDisposition::submitted,
                PositionBroadcastScheduleError::none,
                status_.next_attempt_ms,
            };
        case PositionBroadcastSinkError::not_ready:
            saturating_increment(status_.backpressured);
            status_.last_error =
                PositionBroadcastScheduleError::sink_not_ready;
            break;
        case PositionBroadcastSinkError::full:
            saturating_increment(status_.backpressured);
            status_.last_error = PositionBroadcastScheduleError::sink_full;
            break;
        case PositionBroadcastSinkError::failed:
        default:
            saturating_increment(status_.failures);
            status_.last_error = PositionBroadcastScheduleError::sink_failed;
            break;
    }
    schedule_after(now_ms, policy_.retry_ms);
    return {
        PositionBroadcastScheduleDisposition::deferred,
        status_.last_error,
        status_.next_attempt_ms,
    };
}

PositionBroadcastSchedulerStatus PositionBroadcastScheduler::status() const {
    return status_;
}

bool PositionBroadcastScheduler::observe_time(std::uint64_t now_ms) {
    if (status_.has_time && now_ms < status_.last_now_ms) {
        status_.active = false;
        status_.next_attempt_ms = 0;
        status_.clock_failed = true;
        status_.last_error =
            PositionBroadcastScheduleError::clock_regression;
        saturating_increment(status_.failures);
        return false;
    }
    status_.has_time = true;
    status_.last_now_ms = now_ms;
    return true;
}

void PositionBroadcastScheduler::schedule_after(
    std::uint64_t now_ms,
    std::uint32_t delay_ms) {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (now_ms > maximum - delay_ms) {
        status_.active = false;
        status_.next_attempt_ms = 0;
        if (!status_.time_exhausted) {
            saturating_increment(status_.failures);
        }
        status_.time_exhausted = true;
        return;
    }
    status_.next_attempt_ms = now_ms + delay_ms;
}

}  // namespace opentrail::location
