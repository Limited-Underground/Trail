#pragma once

#include <cstdint>

#include "opentrail/delivery_controller.hpp"
#include "opentrail/location_tracker.hpp"
#include "opentrail/monotonic_clock.hpp"
#include "opentrail/position_broadcast_scheduler.hpp"
#include "opentrail/priority_delivery_handoff.hpp"
#include "opentrail/radio_transport.hpp"

namespace opentrail::integration {

enum class OutboundServiceDisposition : std::uint8_t {
    serviced = 0,
    deferred,
    failed,
};

struct OutboundServiceResult {
    OutboundServiceDisposition disposition{
        OutboundServiceDisposition::deferred};
    time::MonotonicClockError clock_error{
        time::MonotonicClockError::not_ready};
    std::uint64_t now_ms{0};
    location::PositionBroadcastScheduleResult position{};
    PriorityDeliveryHandoffResult handoff{};
    bool delivery_serviced{false};
    bool radio_serviced{false};

    [[nodiscard]] constexpr bool serviced() const {
        return disposition == OutboundServiceDisposition::serviced;
    }
};

struct OutboundServiceStatus {
    bool faulted{false};
    bool has_time{false};
    time::MonotonicClockError latched_clock_error{
        time::MonotonicClockError::none};
    std::uint64_t last_now_ms{0};
    std::uint32_t service_calls{0};
    std::uint32_t serviced_cycles{0};
    std::uint32_t clock_deferred{0};
    std::uint32_t clock_failures{0};
    std::uint32_t latched_refusals{0};
};

// Single-owner cooperative outbound cycle. A successful checked clock sample
// is shared by location, scheduling, queue handoff, delivery, and transport.
// Inbound processing, UI input, target scheduling, and synchronization remain
// outside this component.
class OutboundServiceCoordinator {
public:
    OutboundServiceCoordinator(
        time::CheckedMonotonicClock& clock,
        location::LocationTracker& location,
        location::PositionBroadcastScheduler& position,
        PriorityDeliveryHandoff& handoff,
        delivery::DeliveryController& delivery,
        radio::RadioTransport& radio);

    [[nodiscard]] OutboundServiceResult service();
    [[nodiscard]] OutboundServiceStatus status() const;

private:
    time::CheckedMonotonicClock& clock_;
    location::LocationTracker& location_;
    location::PositionBroadcastScheduler& position_;
    PriorityDeliveryHandoff& handoff_;
    delivery::DeliveryController& delivery_;
    radio::RadioTransport& radio_;
    OutboundServiceStatus status_{};
};

}  // namespace opentrail::integration
