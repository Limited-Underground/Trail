#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/position_broadcast_scheduler.hpp"

namespace opentrail::location::test_support {

class FakePositionBroadcastSink final : public PositionBroadcastSink {
public:
    static constexpr std::size_t kCapacity = 16;

    [[nodiscard]] bool enqueue_result(PositionBroadcastSinkError error);
    [[nodiscard]] PositionBroadcastSinkError submit(
        radio::ByteView payload) override;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] const std::array<std::uint8_t, kPositionPayloadBytes>* at(
        std::size_t index) const;
    [[nodiscard]] std::uint32_t submit_attempts() const;

private:
    std::array<PositionBroadcastSinkError, kCapacity> results_{};
    std::size_t result_head_{0};
    std::size_t result_count_{0};
    std::array<
        std::array<std::uint8_t, kPositionPayloadBytes>,
        kCapacity> payloads_{};
    std::size_t payload_count_{0};
    std::uint32_t submit_attempts_{0};
};

}  // namespace opentrail::location::test_support
