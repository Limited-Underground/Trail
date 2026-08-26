#include "heltec_v4_battery.hpp"

#include <cstdint>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

namespace opentrail::heltec_v4 {
namespace {

constexpr gpio_num_t kAdcControl = GPIO_NUM_37;
constexpr adc_unit_t kAdcUnit = ADC_UNIT_1;
constexpr adc_channel_t kBatteryChannel = ADC_CHANNEL_0;  // ESP32-S3 GPIO1.
constexpr adc_atten_t kAttenuation = ADC_ATTEN_DB_12;
constexpr adc_bitwidth_t kBitWidth = ADC_BITWIDTH_DEFAULT;
constexpr unsigned kSampleCount = 8;
constexpr std::uint32_t kSettleMilliseconds = 10;
constexpr std::int32_t kDividerNumerator = 490;
constexpr std::int32_t kDividerDenominator = 100;

adc_oneshot_unit_handle_t adc_handle = nullptr;
adc_cali_handle_t calibration_handle = nullptr;
bool initialized = false;

bool disable_divider() {
  return gpio_set_level(kAdcControl, 0) == ESP_OK;
}

class DividerGuard {
 public:
  DividerGuard() : enabled_(gpio_set_level(kAdcControl, 1) == ESP_OK) {}
  ~DividerGuard() { static_cast<void>(disable_divider()); }

  [[nodiscard]] bool enabled() const { return enabled_; }

  DividerGuard(const DividerGuard&) = delete;
  DividerGuard& operator=(const DividerGuard&) = delete;

 private:
  bool enabled_{false};
};

}  // namespace

bool battery_init() {
  if (initialized) {
    return true;
  }

  gpio_config_t control_config{};
  control_config.pin_bit_mask = 1ULL << kAdcControl;
  control_config.mode = GPIO_MODE_OUTPUT;
  control_config.pull_up_en = GPIO_PULLUP_DISABLE;
  control_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  control_config.intr_type = GPIO_INTR_DISABLE;
  if (gpio_config(&control_config) != ESP_OK) {
    return false;
  }
  if (!disable_divider()) {
    return false;
  }

  adc_oneshot_unit_init_cfg_t unit_config{};
  unit_config.unit_id = kAdcUnit;
  unit_config.ulp_mode = ADC_ULP_MODE_DISABLE;
  if (adc_oneshot_new_unit(&unit_config, &adc_handle) != ESP_OK) {
    return false;
  }

  adc_oneshot_chan_cfg_t channel_config{};
  channel_config.atten = kAttenuation;
  channel_config.bitwidth = kBitWidth;
  if (adc_oneshot_config_channel(adc_handle, kBatteryChannel,
                                 &channel_config) != ESP_OK) {
    adc_oneshot_del_unit(adc_handle);
    adc_handle = nullptr;
    return false;
  }

  adc_cali_curve_fitting_config_t calibration_config{};
  calibration_config.unit_id = kAdcUnit;
  calibration_config.chan = kBatteryChannel;
  calibration_config.atten = kAttenuation;
  calibration_config.bitwidth = kBitWidth;
  if (adc_cali_create_scheme_curve_fitting(&calibration_config,
                                           &calibration_handle) != ESP_OK) {
    adc_oneshot_del_unit(adc_handle);
    adc_handle = nullptr;
    return false;
  }

  initialized = true;
  return true;
}

BatteryReading battery_read() {
  if (!battery_init()) {
    static_cast<void>(disable_divider());
    return {false, 0, 0};
  }

  DividerGuard divider_guard;
  if (!divider_guard.enabled()) {
    return {false, 0, 0};
  }
  vTaskDelay(pdMS_TO_TICKS(kSettleMilliseconds));

  std::int64_t calibrated_sum = 0;
  for (unsigned sample = 0; sample < kSampleCount; ++sample) {
    int raw = 0;
    int calibrated_millivolts = 0;
    if (adc_oneshot_read(adc_handle, kBatteryChannel, &raw) != ESP_OK ||
        adc_cali_raw_to_voltage(calibration_handle, raw,
                                &calibrated_millivolts) != ESP_OK) {
      return {false, 0, 0};
    }
    calibrated_sum += calibrated_millivolts;
  }

  const auto adc_millivolts =
      static_cast<std::int32_t>(calibrated_sum / kSampleCount);
  const auto battery_millivolts = static_cast<std::int32_t>(
      (static_cast<std::int64_t>(adc_millivolts) * kDividerNumerator +
       kDividerDenominator / 2) /
      kDividerDenominator);
  return battery_reading_from_millivolts(battery_millivolts);
}

}  // namespace opentrail::heltec_v4
