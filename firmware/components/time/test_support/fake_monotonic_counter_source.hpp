#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/monotonic_clock.hpp"

namespace opentrail::time::test_support {

class FakeMonotonicCounterSource final : public MonotonicCounterSource {
public:
    static constexpr std::size_t kCapacity = 16;

    [[nodiscard]] bool enqueue_time(std::uint64_t value_ms);
    [[nodiscard]] bool enqueue_not_ready();
    [[nodiscard]] bool enqueue_failure();

    [[nodiscard]] RawClockSample read() override;
    [[nodiscard]] std::size_t queued_count() const;
    [[nodiscard]] std::uint32_t read_count() const;

private:
    [[nodiscard]] bool enqueue(const RawClockSample& sample);

    std::array<RawClockSample, kCapacity> samples_{};
    std::size_t head_{0};
    std::size_t size_{0};
    std::uint32_t read_count_{0};
};

}  // namespace opentrail::time::test_support
