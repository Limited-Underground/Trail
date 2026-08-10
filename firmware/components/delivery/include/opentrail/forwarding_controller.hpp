#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/duplicate_window.hpp"
#include "opentrail/radio_transport.hpp"

namespace opentrail::delivery {

inline constexpr std::size_t kForwardQueueCapacity = 8;

struct RoutingMetadata {
    std::uint64_t source_alias{0};
    std::uint64_t destination_alias{0};  // Zero means group broadcast.
    std::uint64_t group_id{0};
    std::uint32_t group_epoch{0};
    std::uint32_t message_id{0};
    std::uint8_t hops_remaining{0};
    bool forwarding_permitted{false};
};

struct ForwardingPolicy {
    bool forwarding_enabled{false};
    std::size_t maximum_queue_depth{0};
    std::uint16_t maximum_forwards_per_window{0};
    std::uint32_t rate_window_ms{0};
};

enum class ForwardingDisposition : std::uint8_t {
    queued = 0,
    invalid_argument,
    invalid_policy,
    wrong_group_or_epoch,
    duplicate,
    self_source_untracked,
    destination_reached,
    forwarding_not_permitted,
    forwarding_disabled,
    ttl_exhausted,
    queue_full,
    rate_limited,
};

struct ForwardingDecision {
    bool deliver_local{false};
    bool queued_for_forward{false};
    ForwardingDisposition disposition{ForwardingDisposition::invalid_argument};
};

struct ForwardedFrame {
    RoutingMetadata metadata{};
    std::array<std::uint8_t, radio::kMaximumFrameBytes> bytes{};
    std::size_t size{0};
};

struct ForwardedFrameResult {
    bool has_frame{false};
    ForwardedFrame frame{};
};

struct ForwardingStatus {
    std::size_t queue_depth{0};
    std::uint32_t frames_accepted{0};
    std::uint32_t local_deliveries{0};
    std::uint32_t frames_queued{0};
    std::uint32_t duplicates_dropped{0};
    std::uint32_t wrong_group_dropped{0};
    std::uint32_t ttl_dropped{0};
    std::uint32_t congestion_dropped{0};
};

class ForwardingController {
public:
    ForwardingController(
        std::uint64_t local_alias,
        std::uint64_t group_id,
        std::uint32_t group_epoch,
        ForwardingPolicy policy,
        DuplicateWindow& duplicate_window);

    [[nodiscard]] ForwardingDecision process(
        const RoutingMetadata& metadata,
        radio::ByteView frame,
        std::uint64_t now_ms);
    [[nodiscard]] DuplicateError record_originated(
        std::uint32_t message_id,
        std::uint64_t now_ms);
    [[nodiscard]] ForwardedFrameResult next_forward();
    [[nodiscard]] ForwardingStatus status() const;

private:
    static bool valid_metadata(
        const RoutingMetadata& metadata,
        radio::ByteView frame);
    bool valid_configuration() const;
    void update_rate_window(std::uint64_t now_ms);
    void record_congestion_drop();

    std::uint64_t local_alias_{0};
    std::uint64_t group_id_{0};
    std::uint32_t group_epoch_{0};
    ForwardingPolicy policy_{};
    DuplicateWindow& duplicate_window_;
    std::array<ForwardedFrame, kForwardQueueCapacity> queue_{};
    std::size_t queue_head_{0};
    std::size_t queue_tail_{0};
    std::size_t queue_count_{0};
    std::uint64_t rate_window_started_ms_{0};
    std::uint16_t forwards_in_window_{0};
    bool rate_window_initialized_{false};
    ForwardingStatus counters_{};
};

}  // namespace opentrail::delivery
