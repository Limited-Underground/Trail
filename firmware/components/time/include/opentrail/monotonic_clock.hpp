#pragma once

#include <cstdint>

namespace opentrail::time {

enum class RawClockReadError : std::uint8_t {
    none = 0,
    not_ready,
    source_failed,
};

struct RawClockSample {
    RawClockReadError error{RawClockReadError::not_ready};
    std::uint64_t value_ms{0};
};

// Target adapters expose a boot-local counter. They do not expose wall-clock
// UTC, perform synchronization, or decide whether a regression is acceptable.
class MonotonicCounterSource {
public:
    virtual ~MonotonicCounterSource() = default;

    [[nodiscard]] virtual RawClockSample read() = 0;
};

enum class MonotonicClockError : std::uint8_t {
    none = 0,
    not_ready,
    source_failed,
    rollback_detected,
    fault_latched,
};

struct MonotonicTime {
    MonotonicClockError error{MonotonicClockError::not_ready};
    std::uint64_t value_ms{0};

    [[nodiscard]] bool ok() const {
        return error == MonotonicClockError::none;
    }
};

struct MonotonicClockStatus {
    bool initialized{false};
    bool fault_latched{false};
    MonotonicClockError latched_error{MonotonicClockError::none};
    std::uint64_t last_value_ms{0};
    std::uint32_t read_attempts{0};
    std::uint32_t successful_reads{0};
    std::uint32_t not_ready_reads{0};
    std::uint32_t source_failures{0};
    std::uint32_t rollback_failures{0};
    std::uint32_t latched_refusals{0};
};

// Equal millisecond readings are valid. A source failure or decreasing value
// latches the boundary closed for the lifetime of this boot composition. A new
// instance is required after an intentional reboot/reinitialization boundary.
class CheckedMonotonicClock {
public:
    explicit CheckedMonotonicClock(MonotonicCounterSource& source);

    [[nodiscard]] MonotonicTime now();
    [[nodiscard]] MonotonicClockStatus status() const;

private:
    MonotonicCounterSource& source_;
    MonotonicClockStatus status_{};
};

}  // namespace opentrail::time
