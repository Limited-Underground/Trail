#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/power_state.hpp"

namespace opentrail::power::test_support {

class FakePowerStatusSource final : public PowerStatusSource {
public:
    static constexpr std::size_t kCapacity = 16;

    [[nodiscard]] bool enqueue(const RawPowerObservation& observation);
    [[nodiscard]] bool enqueue_not_ready();
    [[nodiscard]] bool enqueue_failure();

    [[nodiscard]] RawPowerObservation read() override;
    [[nodiscard]] std::size_t queued_count() const;
    [[nodiscard]] std::uint32_t read_count() const;

private:
    std::array<RawPowerObservation, kCapacity> observations_{};
    std::size_t head_{0};
    std::size_t size_{0};
    std::uint32_t read_count_{0};
};

}  // namespace opentrail::power::test_support
