#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/delivery_controller.hpp"
#include "opentrail/radio_transport.hpp"

namespace opentrail::delivery {

inline constexpr std::size_t kPriorityQueueMaximumCapacity = 12;
inline constexpr std::size_t kPriorityLevelCount = 4;
inline constexpr std::size_t kPriorityQueueEventCapacity = 16;

enum class TrafficPriority : std::uint8_t {
    emergency = 0,
    critical = 1,
    normal = 2,
    background = 3,
};

[[nodiscard]] constexpr TrafficPriority priority_for(
    MessageClass message_class) {
    switch (message_class) {
        case MessageClass::emergency:
            return TrafficPriority::emergency;
        case MessageClass::critical_alert:
            return TrafficPriority::critical;
        case MessageClass::direct_message:
        case MessageClass::chat:
            return TrafficPriority::normal;
        case MessageClass::position:
        case MessageClass::status:
            return TrafficPriority::background;
    }
    return TrafficPriority::background;
}

struct PriorityRateLimit {
    std::uint16_t maximum_accepted{0};
    std::uint32_t window_ms{0};
};

struct PriorityQueuePolicy {
    std::size_t capacity{0};
    std::size_t reserved_urgent_slots{0};
    std::array<PriorityRateLimit, kPriorityLevelCount> rate_limits{};
};

[[nodiscard]] constexpr PriorityQueuePolicy experimental_priority_policy() {
    return {
        8,
        2,
        {{{2, 10000}, {4, 10000}, {8, 10000}, {4, 10000}}},
    };
}

enum class PriorityQueueError : std::uint8_t {
    none = 0,
    invalid_argument,
    invalid_policy,
    duplicate_message_id,
    payload_too_large,
    reserved_capacity,
    queue_full,
    rate_limited,
};

struct PriorityEnqueueResult {
    PriorityQueueError error{PriorityQueueError::none};
    bool preempted{false};
    std::uint32_t preempted_message_id{0};

    [[nodiscard]] constexpr bool accepted() const {
        return error == PriorityQueueError::none;
    }
};

enum class PriorityQueueEventReason : std::uint8_t {
    preempted = 0,
    expired,
};

struct PriorityQueueEvent {
    std::uint32_t message_id{0};
    MessageClass message_class{MessageClass::status};
    TrafficPriority priority{TrafficPriority::background};
    PriorityQueueEventReason reason{PriorityQueueEventReason::expired};
};

struct PriorityQueueEventResult {
    bool has_event{false};
    PriorityQueueEvent event{};
};

struct QueuedTraffic {
    std::uint32_t message_id{0};
    MessageClass message_class{MessageClass::status};
    TrafficPriority priority{TrafficPriority::background};
    std::array<std::uint8_t, radio::kMaximumFrameBytes> frame{};
    std::size_t frame_size{0};
    std::uint64_t created_at_ms{0};
    std::uint64_t expires_at_ms{0};
};

struct QueuedTrafficResult {
    bool has_message{false};
    QueuedTraffic message{};
};

struct PriorityQueueStatus {
    std::size_t queued{0};
    std::size_t events_waiting{0};
    std::uint32_t accepted{0};
    std::uint32_t preempted{0};
    std::uint32_t expired{0};
    std::uint32_t rejected_reserved{0};
    std::uint32_t rejected_full{0};
    std::uint32_t rejected_rate_limited{0};
    std::uint32_t events_dropped{0};
};

class PriorityTrafficQueue {
public:
    explicit PriorityTrafficQueue(PriorityQueuePolicy policy);

    [[nodiscard]] PriorityEnqueueResult enqueue(
        std::uint32_t message_id,
        MessageClass message_class,
        radio::ByteView frame,
        std::uint64_t now_ms,
        std::uint32_t lifetime_ms);
    void purge_expired(std::uint64_t now_ms);
    [[nodiscard]] QueuedTrafficResult take_next(std::uint64_t now_ms);
    [[nodiscard]] PriorityQueueEventResult next_event();
    [[nodiscard]] PriorityQueueStatus status() const;

private:
    struct Entry {
        QueuedTraffic traffic{};
        std::uint64_t sequence{0};
        bool used{false};
    };

    struct RateState {
        std::uint64_t window_started_ms{0};
        std::uint16_t accepted{0};
        bool initialized{false};
    };

    static std::size_t priority_index(TrafficPriority priority);
    static bool urgent(TrafficPriority priority);
    static bool valid_message_class(MessageClass message_class);
    static bool higher_priority(
        TrafficPriority left,
        TrafficPriority right);
    static std::uint64_t saturating_add(
        std::uint64_t value,
        std::uint32_t increment);
    bool valid_policy() const;
    bool rate_available(TrafficPriority priority, std::uint64_t now_ms);
    void record_rate(TrafficPriority priority);
    void remove_entry(std::size_t index);
    void push_event(const PriorityQueueEvent& event);

    PriorityQueuePolicy policy_{};
    std::array<Entry, kPriorityQueueMaximumCapacity> entries_{};
    std::array<RateState, kPriorityLevelCount> rates_{};
    std::array<PriorityQueueEvent, kPriorityQueueEventCapacity> events_{};
    std::size_t queue_count_{0};
    std::size_t event_head_{0};
    std::size_t event_tail_{0};
    std::size_t event_count_{0};
    std::uint64_t next_sequence_{0};
    PriorityQueueStatus counters_{};
};

}  // namespace opentrail::delivery
