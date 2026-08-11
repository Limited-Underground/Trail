#pragma once

#include <cstdint>

#include "opentrail/position_codec.hpp"

namespace opentrail::location {

enum class PositionBroadcastSinkError : std::uint8_t {
    none = 0,
    not_ready,
    full,
    failed,
};

class PositionBroadcastSink {
public:
    virtual ~PositionBroadcastSink() = default;

    // The view is valid only for this call. A successful sink must copy or
    // consume the exact payload before returning.
    [[nodiscard]] virtual PositionBroadcastSinkError submit(
        radio::ByteView payload,
        std::uint64_t now_ms) = 0;
};

struct PositionBroadcastSchedulePolicy {
    std::uint32_t cadence_ms{0};
    std::uint32_t retry_ms{0};
};

enum class PositionBroadcastScheduleDisposition : std::uint8_t {
    stopped = 0,
    not_due,
    submitted,
    deferred,
    failed,
};

enum class PositionBroadcastScheduleError : std::uint8_t {
    none = 0,
    invalid_policy,
    clock_regression,
    time_exhausted,
    no_current_fix,
    encode_failed,
    sink_not_ready,
    sink_full,
    sink_failed,
};

struct PositionBroadcastScheduleResult {
    PositionBroadcastScheduleDisposition disposition{
        PositionBroadcastScheduleDisposition::stopped};
    PositionBroadcastScheduleError error{
        PositionBroadcastScheduleError::none};
    std::uint64_t next_attempt_ms{0};

    [[nodiscard]] constexpr bool submitted() const {
        return disposition ==
               PositionBroadcastScheduleDisposition::submitted;
    }
};

struct PositionBroadcastSchedulerStatus {
    bool policy_valid{false};
    bool active{false};
    bool clock_failed{false};
    bool time_exhausted{false};
    bool has_time{false};
    std::uint64_t last_now_ms{0};
    std::uint64_t next_attempt_ms{0};
    PositionBroadcastScheduleError last_error{
        PositionBroadcastScheduleError::none};
    std::uint32_t start_attempts{0};
    std::uint32_t starts{0};
    std::uint32_t stops{0};
    std::uint32_t service_calls{0};
    std::uint32_t submitted{0};
    std::uint32_t suppressed{0};
    std::uint32_t backpressured{0};
    std::uint32_t failures{0};
};

// Start/stop, fixed-memory position cadence. Only current validated fixes are
// encoded. Scheduling advances from actual accepted/deferred work so delayed
// service coalesces rather than creating a catch-up queue.
class PositionBroadcastScheduler {
public:
    PositionBroadcastScheduler(
        PositionBroadcastSink& sink,
        PositionBroadcastSchedulePolicy policy);

    [[nodiscard]] PositionBroadcastScheduleError start(
        std::uint64_t now_ms);
    void stop();
    [[nodiscard]] PositionBroadcastScheduleResult service(
        const LocationSnapshot& snapshot,
        std::uint64_t now_ms);
    [[nodiscard]] PositionBroadcastSchedulerStatus status() const;

private:
    [[nodiscard]] bool observe_time(std::uint64_t now_ms);
    void schedule_after(std::uint64_t now_ms, std::uint32_t delay_ms);

    PositionBroadcastSink& sink_;
    PositionBroadcastSchedulePolicy policy_{};
    PositionBroadcastSchedulerStatus status_{};
};

}  // namespace opentrail::location
