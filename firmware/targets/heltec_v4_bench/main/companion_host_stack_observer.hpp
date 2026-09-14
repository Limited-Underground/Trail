#pragma once

#include <atomic>
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace opentrail::target::heltec_v4_bench {

// The runtime owner records a successful creation and clears this observation
// before deleting that task. Only that same owner may inspect the live handle.
// Other tasks read only the atomic owner identity, never the mutable task handle.
class CompanionHostStackObserver final {
public:
    void record_created(TaskHandle_t task) noexcept {
        const auto current = xTaskGetCurrentTaskHandle();
        task_ = task != nullptr && task != current ? task : nullptr;
        owner_.store(task_ != nullptr ? current : nullptr, std::memory_order_release);
    }

    void clear_before_delete() noexcept {
        owner_.store(nullptr, std::memory_order_release);
        task_ = nullptr;
    }

    [[nodiscard]] bool minimum_free_bytes(std::uint32_t& bytes) const noexcept {
        const auto current = xTaskGetCurrentTaskHandle();
        const auto owner = owner_.load(std::memory_order_acquire);
        if (owner == nullptr || current != owner || task_ == nullptr) return false;
        // Pinned ESP-IDF task.h defines this value in bytes, including valid zero.
        bytes = static_cast<std::uint32_t>(uxTaskGetStackHighWaterMark(task_));
        return true;
    }

private:
    std::atomic<TaskHandle_t> owner_{nullptr};
    TaskHandle_t task_{nullptr};
};

}  // namespace opentrail::target::heltec_v4_bench
