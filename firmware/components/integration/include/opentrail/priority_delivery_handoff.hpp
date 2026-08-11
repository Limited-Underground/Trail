#pragma once

#include <cstdint>

#include "opentrail/delivery_controller.hpp"
#include "opentrail/priority_queue.hpp"

namespace opentrail::integration {

enum class PriorityDeliveryHandoffDisposition : std::uint8_t {
    idle = 0,
    transferred,
    deferred,
    failed,
};

enum class PriorityDeliveryHandoffError : std::uint8_t {
    none = 0,
    delivery_queue_full,
    delivery_rejected,
    invalid_time,
    queue_commit_failed,
    latched_failure,
};

struct PriorityDeliveryHandoffResult {
    PriorityDeliveryHandoffDisposition disposition{
        PriorityDeliveryHandoffDisposition::idle};
    PriorityDeliveryHandoffError error{
        PriorityDeliveryHandoffError::none};
    delivery::DeliveryError delivery_error{delivery::DeliveryError::none};
    std::uint32_t message_id{0};
    bool queue_retained{false};

    [[nodiscard]] constexpr bool transferred() const {
        return disposition ==
               PriorityDeliveryHandoffDisposition::transferred;
    }
};

struct PriorityDeliveryHandoffStatus {
    bool faulted{false};
    std::uint32_t service_calls{0};
    std::uint32_t idle{0};
    std::uint32_t transferred{0};
    std::uint32_t deferred{0};
    std::uint32_t failures{0};
};

// Single-owner, fixed-memory queue-to-delivery transfer. The priority entry is
// committed only after DeliveryController accepts its copy. The remaining
// queue lifetime can shorten, but never extend, the class delivery policy.
class PriorityDeliveryHandoff {
public:
    PriorityDeliveryHandoff(
        delivery::PriorityTrafficQueue& queue,
        delivery::DeliveryController& delivery);

    [[nodiscard]] PriorityDeliveryHandoffResult service(
        std::uint64_t now_ms);
    [[nodiscard]] PriorityDeliveryHandoffStatus status() const;

private:
    delivery::PriorityTrafficQueue& queue_;
    delivery::DeliveryController& delivery_;
    PriorityDeliveryHandoffStatus status_{};
};

}  // namespace opentrail::integration
