#include "opentrail/outbound_service_coordinator.hpp"

#include <limits>

namespace opentrail::integration {
namespace {

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

}  // namespace

OutboundServiceCoordinator::OutboundServiceCoordinator(
    time::CheckedMonotonicClock& clock,
    location::LocationTracker& location,
    location::PositionBroadcastScheduler& position,
    PriorityDeliveryHandoff& handoff,
    delivery::DeliveryController& delivery,
    radio::RadioTransport& radio)
    : clock_(clock),
      location_(location),
      position_(position),
      handoff_(handoff),
      delivery_(delivery),
      radio_(radio) {}

OutboundServiceResult OutboundServiceCoordinator::service() {
    saturating_increment(status_.service_calls);
    if (status_.faulted) {
        saturating_increment(status_.latched_refusals);
        OutboundServiceResult result{};
        result.disposition = OutboundServiceDisposition::failed;
        result.clock_error = time::MonotonicClockError::fault_latched;
        return result;
    }

    const auto now = clock_.now();
    if (!now.ok()) {
        OutboundServiceResult result{};
        result.clock_error = now.error;
        if (now.error == time::MonotonicClockError::not_ready) {
            result.disposition = OutboundServiceDisposition::deferred;
            saturating_increment(status_.clock_deferred);
            return result;
        }

        latch_clock_fault(now.error);
        result.disposition = OutboundServiceDisposition::failed;
        return result;
    }

    observe_time(now.value_ms);

    location::LocationSnapshot snapshot{};
    if (position_.status().active) {
        snapshot = location_.snapshot(now.value_ms);
    }

    OutboundServiceResult result{};
    result.disposition = OutboundServiceDisposition::serviced;
    result.clock_error = time::MonotonicClockError::none;
    result.now_ms = now.value_ms;
    result.position = position_.service(snapshot, now.value_ms);
    result.handoff = handoff_.service(now.value_ms);
    delivery_.service(now.value_ms);
    result.delivery_serviced = true;
    radio_.service(now.value_ms);
    result.radio_serviced = true;
    saturating_increment(status_.serviced_cycles);
    return result;
}

OutboundPositionCommandResult
OutboundServiceCoordinator::start_position_sharing() {
    saturating_increment(status_.position_command_calls);
    if (status_.faulted) {
        saturating_increment(status_.position_command_failures);
        return {
            OutboundPositionCommandError::clock_faulted,
            time::MonotonicClockError::fault_latched,
        };
    }

    const auto now = clock_.now();
    if (!now.ok()) {
        if (now.error == time::MonotonicClockError::not_ready) {
            saturating_increment(status_.clock_deferred);
            saturating_increment(status_.position_commands_deferred);
            return {
                OutboundPositionCommandError::clock_not_ready,
                now.error,
            };
        }

        latch_clock_fault(now.error);
        saturating_increment(status_.position_command_failures);
        return {
            OutboundPositionCommandError::clock_faulted,
            now.error,
        };
    }

    observe_time(now.value_ms);
    const auto was_active = position_.status().active;
    const auto scheduler_error = position_.start(now.value_ms);
    if (scheduler_error !=
        location::PositionBroadcastScheduleError::none) {
        saturating_increment(status_.position_command_failures);
        return {
            OutboundPositionCommandError::scheduler_rejected,
            time::MonotonicClockError::none,
            scheduler_error,
        };
    }

    saturating_increment(status_.position_commands_applied);
    return {
        OutboundPositionCommandError::none,
        time::MonotonicClockError::none,
        location::PositionBroadcastScheduleError::none,
        !was_active && position_.status().active,
    };
}

OutboundPositionCommandResult
OutboundServiceCoordinator::stop_position_sharing() {
    saturating_increment(status_.position_command_calls);
    const auto was_active = position_.status().active;
    position_.stop();
    saturating_increment(status_.position_commands_applied);
    return {
        OutboundPositionCommandError::none,
        time::MonotonicClockError::none,
        location::PositionBroadcastScheduleError::none,
        was_active && !position_.status().active,
    };
}

OutboundServiceStatus OutboundServiceCoordinator::status() const {
    return status_;
}

location::PositionBroadcastSchedulerStatus
OutboundServiceCoordinator::position_status() const {
    return position_.status();
}

void OutboundServiceCoordinator::observe_time(std::uint64_t now_ms) {
    status_.has_time = true;
    status_.last_now_ms = now_ms;
}

void OutboundServiceCoordinator::latch_clock_fault(
    time::MonotonicClockError error) {
    status_.faulted = true;
    status_.latched_clock_error = error;
    saturating_increment(status_.clock_failures);
    position_.stop();
}

}  // namespace opentrail::integration
