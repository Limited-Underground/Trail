#include "confirmation_nonowning_entropy.hpp"
#include "sdkconfig.h"
#include "esp_bt.h"
namespace opentrail::target::heltec_v4_bench {
ConfirmationNonowningEntropy::ConfirmationNonowningEntropy() : guarded_(raw_, ready, nullptr) {}
bool ConfirmationNonowningEntropy::ready(void*) noexcept {
#if defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3 && defined(CONFIG_BT_CONTROLLER_ENABLED) && CONFIG_BT_CONTROLLER_ENABLED && !defined(CONFIG_BT_CTRL_MODEM_SLEEP) && !defined(CONFIG_PM_ENABLE)
    return esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED;
#else
    return false;
#endif
}
bool ConfirmationNonowningEntropy::activate() {
    raw_.set_entropy_state(security::EntropyState::ready);
    if (guarded_.activate()) return true;
    raw_.set_entropy_state(security::EntropyState::not_ready);
    return false;
}
bool ConfirmationNonowningEntropy::revoke() {
    const bool drained = guarded_.revoke();
    if (drained) raw_.set_entropy_state(security::EntropyState::not_ready);
    return drained;
}
}
