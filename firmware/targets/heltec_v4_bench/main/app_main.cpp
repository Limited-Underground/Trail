#include <cstdint>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char kLogTag[] = "ot_bench";
constexpr std::uint32_t kHeartbeatPeriodMs = 5000;

}  // namespace

extern "C" void app_main() {
    ESP_LOGI(kLogTag, "build-only bench candidate started");

    while (true) {
        const auto elapsed_ms =
            static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
        ESP_LOGI(kLogTag, "heartbeat elapsed_ms=%llu",
                 static_cast<unsigned long long>(elapsed_ms));
        vTaskDelay(pdMS_TO_TICKS(kHeartbeatPeriodMs));
    }
}
