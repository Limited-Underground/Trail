#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/radio_transport.hpp"

namespace opentrail::delivery {

inline constexpr std::uint32_t kTransientRejectionBackoffMs = 100;

enum class MessageClass : std::uint8_t {
    emergency = 0,
    critical_alert,
    direct_message,
    chat,
    position,
    status,
};

struct DeliveryPolicy {
    bool requires_acknowledgement{false};
    std::uint8_t maximum_attempts{1};
    std::uint32_t retry_interval_ms{0};
    std::uint32_t expiry_ms{0};
};

// Host-test policy values only. Radio/regulatory measurements must replace
// these before any field firmware uses them.
[[nodiscard]] constexpr DeliveryPolicy experimental_policy(
    MessageClass message_class) {
    switch (message_class) {
        case MessageClass::emergency:
            return {true, 4, 2000, 30000};
        case MessageClass::critical_alert:
            return {true, 4, 3000, 30000};
        case MessageClass::direct_message:
            return {true, 3, 4000, 30000};
        case MessageClass::chat:
            return {true, 3, 5000, 45000};
        case MessageClass::position:
            return {false, 1, 0, 10000};
        case MessageClass::status:
            return {false, 1, 0, 15000};
    }
    return {};
}

enum class DeliveryError : std::uint8_t {
    none = 0,
    invalid_argument,
    invalid_policy,
    duplicate_message_id,
    payload_too_large,
    queue_full,
};

enum class DeliveryOutcome : std::uint8_t {
    confirmed = 0,
    sent_unconfirmed,
    expired,
    attempts_exhausted,
    transport_rejected,
};

struct EnqueueResult {
    DeliveryError error{DeliveryError::none};

    [[nodiscard]] constexpr bool accepted() const {
        return error == DeliveryError::none;
    }
};

struct DeliveryEvent {
    std::uint32_t message_id{0};
    MessageClass message_class{MessageClass::status};
    DeliveryOutcome outcome{DeliveryOutcome::expired};
    std::uint8_t attempts{0};
    radio::RadioError transport_error{radio::RadioError::none};
};

struct DeliveryEventResult {
    bool has_event{false};
    DeliveryEvent event{};
};

struct DeliveryStatus {
    std::size_t pending{0};
    std::size_t events_waiting{0};
    std::uint32_t confirmed{0};
    std::uint32_t sent_unconfirmed{0};
    std::uint32_t expired{0};
    std::uint32_t attempts_exhausted{0};
    std::uint32_t transport_rejected{0};
    std::uint32_t events_dropped{0};
};

class DeliveryController {
public:
    static constexpr std::size_t kPendingCapacity = 8;
    static constexpr std::size_t kEventCapacity = 8;

    explicit DeliveryController(radio::RadioTransport& transport);

    EnqueueResult enqueue(
        std::uint32_t message_id,
        MessageClass message_class,
        DeliveryPolicy policy,
        radio::ByteView frame,
        std::uint64_t now_ms);
    void service(std::uint64_t now_ms);
    [[nodiscard]] bool acknowledge(
        std::uint32_t message_id,
        std::uint64_t now_ms);
    [[nodiscard]] DeliveryEventResult next_event();
    [[nodiscard]] DeliveryStatus status() const;

private:
    struct PendingDelivery {
        std::array<std::uint8_t, radio::kMaximumFrameBytes> frame{};
        std::size_t frame_size{0};
        std::uint32_t message_id{0};
        MessageClass message_class{MessageClass::status};
        DeliveryPolicy policy{};
        std::uint64_t expires_at_ms{0};
        std::uint64_t next_attempt_at_ms{0};
        std::uint8_t attempts{0};
        radio::RadioError last_transport_error{radio::RadioError::none};
        bool used{false};
    };

    static bool valid_policy(const DeliveryPolicy& policy);
    static bool permanent_transport_error(radio::RadioError error);
    static std::uint64_t saturating_add(
        std::uint64_t value,
        std::uint32_t increment);
    void complete(
        std::size_t slot,
        DeliveryOutcome outcome,
        radio::RadioError transport_error = radio::RadioError::none);
    void push_event(const DeliveryEvent& event);

    radio::RadioTransport& transport_;
    std::array<PendingDelivery, kPendingCapacity> pending_{};
    std::array<DeliveryEvent, kEventCapacity> events_{};
    std::size_t pending_count_{0};
    std::size_t event_head_{0};
    std::size_t event_tail_{0};
    std::size_t event_count_{0};
    DeliveryStatus counters_{};
};

}  // namespace opentrail::delivery
