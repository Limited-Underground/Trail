#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/local_interface.hpp"

namespace opentrail::ui::test_support {

class FakeDisplaySink final : public DisplaySink {
public:
    static constexpr std::size_t kCapacity = 16;

    [[nodiscard]] bool enqueue_result(DisplayWriteError error);
    [[nodiscard]] DisplayWriteError present(const UiFrame& frame) override;
    [[nodiscard]] bool has_presented_frame() const;
    [[nodiscard]] UiFrame latest_frame() const;
    [[nodiscard]] std::size_t queued_result_count() const;
    [[nodiscard]] std::uint32_t present_count() const;
    [[nodiscard]] std::uint32_t success_count() const;

private:
    std::array<DisplayWriteError, kCapacity> results_{};
    std::size_t head_{0};
    std::size_t size_{0};
    bool has_frame_{false};
    UiFrame latest_frame_{};
    std::uint32_t present_count_{0};
    std::uint32_t success_count_{0};
};

class FakeLocalInputSource final : public LocalInputSource {
public:
    static constexpr std::size_t kCapacity = 16;

    [[nodiscard]] bool enqueue(const LocalInputEvent& event);
    [[nodiscard]] bool enqueue_action(std::uint32_t frame_revision,
                                      std::uint8_t action_slot,
                                      InputGesture gesture = InputGesture::activate);
    [[nodiscard]] bool enqueue_not_ready();
    [[nodiscard]] bool enqueue_failure();
    [[nodiscard]] LocalInputEvent read() override;
    [[nodiscard]] std::size_t queued_count() const;
    [[nodiscard]] std::uint32_t read_count() const;

private:
    std::array<LocalInputEvent, kCapacity> events_{};
    std::size_t head_{0};
    std::size_t size_{0};
    std::uint32_t read_count_{0};
};

}  // namespace opentrail::ui::test_support
