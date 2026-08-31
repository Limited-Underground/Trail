#include <array>
#include <cstdint>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "companion_boot_self_check.hpp"
#include "companion_authorization_storage.hpp"
#include "companion_nimble_gatt.hpp"
#include "companion_nimble_runtime.hpp"
#include "heltec_startup_display.hpp"
#include "heltec_v4_battery.hpp"
#include "heltec_v4_gnss.hpp"
#include "heltec_v4_oled.hpp"
#include "heltec_v4_pairing_input.hpp"
#include "heltec_v4_secure_random.hpp"
#include "opentrail/companion_protocol.hpp"
#include "opentrail/companion_pairing_window.hpp"
#include "opentrail/companion_semantics.hpp"

namespace {
using opentrail::target::heltec_v4_bench::HeltecV4Oled;
using opentrail::target::heltec_v4_bench::CompactStatusSnapshot;
using opentrail::target::heltec_v4_bench::HeltecV4Gnss;
using opentrail::target::heltec_v4_bench::StartupDisplayFrame;
using opentrail::target::heltec_v4_bench::StartupDisplayOwner;
using opentrail::target::heltec_v4_bench::startup_display_frame_for_ble_phase;
using opentrail::target::heltec_v4_bench::companion_nimble_runtime_status;
using opentrail::target::heltec_v4_bench::PairingInputEvent;
using opentrail::target::heltec_v4_bench::PairingPinDisplayPortAdapter;
using opentrail::target::heltec_v4_bench::HeltecV4PairingInput;
using opentrail::target::heltec_v4_bench::HeltecV4SecureRandom;

constexpr char kLogTag[] = "ot_bench";
constexpr std::uint32_t kHeartbeatPeriodMs = 5000;
constexpr std::uint32_t kMinimumLogoPeriodMs = 1200;
constexpr std::uint64_t kBatterySamplePeriodMs = 30'000;
constexpr std::uint64_t kBatteryFreshForMs = 60'000;
constexpr std::uint64_t kGnssFreshForMs = 5'000;
HeltecV4Oled g_oled_port;
StartupDisplayOwner g_startup_display{g_oled_port};
PairingPinDisplayPortAdapter g_pairing_display{g_startup_display};
HeltecV4SecureRandom g_pairing_random;
opentrail::companion::CompanionPairingWindow g_pairing_window{
    g_pairing_random, g_pairing_display};
HeltecV4PairingInput g_pairing_input;
HeltecV4Gnss g_gnss;
opentrail::ui::compact_status_footer::Metric g_battery_percent{};
bool g_display_failure_logged{false};
std::uint64_t g_pairing_physical_event{0};

void observe_display_result(bool succeeded) {
    if (!succeeded && !g_display_failure_logged) {
        ESP_LOGW(kLogTag, "startup display unavailable; runtime continues");
        g_display_failure_logged = true;
    }
}

CompactStatusSnapshot compact_status_snapshot(std::uint64_t now_ms) {
    using opentrail::target::heltec_v4_bench::GnssSatelliteState;
    using opentrail::ui::compact_status_footer::ObservationState;

    CompactStatusSnapshot snapshot{};
    snapshot.battery_percent = g_battery_percent;
    snapshot.freshness = {kBatteryFreshForMs, kGnssFreshForMs};
    snapshot.render_now_ms = now_ms;

    const auto gnss = g_gnss.satellites(now_ms, kGnssFreshForMs);
    if (gnss.state == GnssSatelliteState::valid) {
        snapshot.gps_satellites = {
            ObservationState::valid, gnss.satellites, gnss.sampled_at_ms};
    } else if (gnss.state == GnssSatelliteState::invalid) {
        snapshot.gps_satellites.state = ObservationState::invalid;
    }
    return snapshot;
}

bool run_companion_codec_self_check() {
    using namespace opentrail::companion;

    const CompanionProtocolInfo info{
        CompanionDeviceRole::screenless_client,
        kCompanionKnownCapabilityMask,
        static_cast<std::uint16_t>(kCompanionMaxFragmentPayloadBytes),
        kCompanionMinimumAttMtu,
        static_cast<std::uint8_t>(kCompanionMaxFragmentCount),
        1,
    };
    constexpr std::array<std::uint8_t, kCompanionProtocolInfoBytes>
        kExpectedInfo{
            0x4F, 0x54, 0x42, 0x30, 0x00, 0x00, 0x01, 0x0F,
            0x80, 0x00, 0x97, 0x00, 0x10, 0x01, 0x00, 0x00,
        };
    std::array<std::uint8_t, kCompanionProtocolInfoBytes> encoded_info{};
    const auto info_result = encode_companion_protocol_info(
        info, {encoded_info.data(), encoded_info.size()});
    if (!info_result.encoded() || encoded_info != kExpectedInfo ||
        !decode_companion_protocol_info(
             {encoded_info.data(), encoded_info.size()}).decoded()) {
        return false;
    }

    const CompanionActionRequest action{
        CompanionActionKind::quick_status,
        opentrail::protocol::QuickStatusKind::available_to_help,
        0,
    };
    constexpr std::array<std::uint8_t, kCompanionActionRequestBytes>
        kExpectedAction{
            0x4F, 0x54, 0x41, 0x30, 0x00, 0x00, 0x01, 0x04,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
        };
    CompanionFragment fragment{};
    fragment.kind = CompanionFrameKind::action_request;
    fragment.session_nonce = 1;
    fragment.exchange_id = 1;
    const auto action_result = encode_companion_action_request(
        action, {fragment.payload.data(), fragment.payload.size()});
    if (!action_result.encoded() ||
        action_result.encoded_bytes != kCompanionActionRequestBytes) {
        return false;
    }
    fragment.payload_bytes =
        static_cast<std::uint16_t>(action_result.encoded_bytes);
    for (std::size_t index = 0; index < kExpectedAction.size(); ++index) {
        if (fragment.payload[index] != kExpectedAction[index]) {
            return false;
        }
    }

    std::array<std::uint8_t, kCompanionMaxFragmentBytes> encoded_fragment{};
    const auto fragment_result = encode_companion_fragment(
        fragment, {encoded_fragment.data(), encoded_fragment.size()});
    constexpr std::array<
        std::uint8_t,
        kCompanionFragmentHeaderBytes + kCompanionActionRequestBytes>
        kExpectedFragment{
            0x4F, 0x54, 0x43, 0x30, 0x00, 0x00, 0x02, 0x00,
            0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
            0x00, 0x01, 0x14, 0x00,
            0x4F, 0x54, 0x41, 0x30, 0x00, 0x00, 0x01, 0x04,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
        };
    if (!fragment_result.encoded() ||
        fragment_result.encoded_bytes != kExpectedFragment.size()) {
        return false;
    }
    for (std::size_t index = 0; index < kExpectedFragment.size(); ++index) {
        if (encoded_fragment[index] != kExpectedFragment[index]) {
            return false;
        }
    }
    const auto decoded_fragment = decode_companion_fragment(
        {encoded_fragment.data(), fragment_result.encoded_bytes});
    if (!decoded_fragment.decoded() ||
        validate_companion_semantic_fragment(decoded_fragment.fragment) !=
            CompanionSemanticCodecError::none) {
        return false;
    }
    const auto decoded_action = decode_companion_action_request(
        {decoded_fragment.fragment.payload.data(),
         decoded_fragment.fragment.payload_bytes});
    return decoded_action.decoded() &&
           decoded_action.value.kind == CompanionActionKind::quick_status &&
           decoded_action.value.quick_status ==
               opentrail::protocol::QuickStatusKind::available_to_help &&
           decoded_action.value.critical_alert_id == 0;
}

[[noreturn]] void contain_self_check_failure() {
    observe_display_result(
        g_startup_display.show(StartupDisplayFrame::self_check_failed));
    ESP_LOGE(kLogTag, "companion boot self-check FAIL");
    while (true) {
        vTaskSuspend(nullptr);
    }
}

[[noreturn]] void contain_runtime_failure() {
    (void)opentrail::target::heltec_v4_bench::
        fault_companion_pairing_window();
    observe_display_result(
        g_startup_display.show(StartupDisplayFrame::ble_error));
    ESP_LOGE(kLogTag, "companion runtime FAIL");
    while (true) {
        vTaskSuspend(nullptr);
    }
}

}  // namespace

extern "C" void app_main() {
    const auto boot_started_at_ms =
        static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
    observe_display_result(g_startup_display.start());

    if (!run_companion_codec_self_check() ||
        !opentrail::target::heltec_v4_bench::
             run_companion_request_coordinator_self_check() ||
        !opentrail::target::heltec_v4_bench::
             run_companion_gatt_session_self_check() ||
        !opentrail::target::heltec_v4_bench::
             run_companion_gatt_authorization_self_check() ||
        !opentrail::target::heltec_v4_bench::
             run_companion_gatt_authorization_adapter_self_check() ||
        !opentrail::target::heltec_v4_bench::
             run_companion_authorization_storage_self_check() ||
        !opentrail::target::heltec_v4_bench::
             run_companion_ble_runtime_owner_self_check() ||
        !opentrail::target::heltec_v4_bench::
             companion_nimble_gatt_definition_self_check()) {
        contain_self_check_failure();
    }
    ESP_LOGI(kLogTag, "companion boot self-check PASS");
    const auto started_at_ms =
        static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
    if (opentrail::target::heltec_v4_bench::
            start_companion_nimble_runtime(
                started_at_ms, g_pairing_window, g_pairing_random) !=
        opentrail::companion::CompanionBleRuntimeError::none) {
        contain_runtime_failure();
    }
    ESP_LOGI(kLogTag, "companion runtime started");

    if (!g_pairing_input.initialize(started_at_ms)) {
        contain_runtime_failure();
    }

    if (!g_gnss.initialize()) {
        ESP_LOGW(kLogTag, "GNSS unavailable; GPS status remains unknown");
    }
    if (!opentrail::heltec_v4::battery_init()) {
        ESP_LOGW(kLogTag, "battery ADC unavailable; battery status remains unknown");
    }

    std::uint64_t next_heartbeat_ms = started_at_ms;
    std::uint64_t next_battery_sample_ms = started_at_ms;
    while (true) {
        const auto elapsed_ms =
            static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
        const auto runtime_result =
            opentrail::target::heltec_v4_bench::
                service_companion_nimble_runtime(elapsed_ms);
        if (runtime_result !=
            opentrail::companion::CompanionBleRuntimeError::none) {
            contain_runtime_failure();
        }
        const auto runtime_status = companion_nimble_runtime_status();
        using opentrail::companion::CompanionBleRuntimePhase;
        const bool entropy_ready =
            runtime_status.phase == CompanionBleRuntimePhase::advertising ||
            runtime_status.phase == CompanionBleRuntimePhase::connected;
        g_pairing_random.set_entropy_state(
            entropy_ready
                ? opentrail::security::EntropyState::ready
                : opentrail::security::EntropyState::not_ready);
        const auto pairing_service =
            opentrail::target::heltec_v4_bench::
                service_companion_pairing_window(elapsed_ms);
        if (pairing_service !=
                opentrail::companion::CompanionPairingWindowError::none &&
            pairing_service !=
                opentrail::companion::CompanionPairingWindowError::
                    window_expired) {
            contain_runtime_failure();
        }
        if (g_pairing_input.poll(elapsed_ms) ==
            PairingInputEvent::long_press_released) {
            ++g_pairing_physical_event;
            if (g_pairing_physical_event == 0) ++g_pairing_physical_event;
            const auto open_result =
                opentrail::target::heltec_v4_bench::
                    open_companion_pairing_window(
                        elapsed_ms,
                        g_pairing_physical_event,
                        opentrail::companion::
                            kCompanionPairingMinimumHoldMs);
            if (open_result ==
                    opentrail::companion::CompanionPairingWindowError::
                        display_failed ||
                open_result ==
                    opentrail::companion::CompanionPairingWindowError::
                        display_clear_failed ||
                open_result ==
                    opentrail::companion::CompanionPairingWindowError::
                        clock_rollback ||
                open_result ==
                    opentrail::companion::CompanionPairingWindowError::
                        deadline_overflow ||
                open_result ==
                    opentrail::companion::CompanionPairingWindowError::
                        entropy_failed ||
                open_result ==
                    opentrail::companion::CompanionPairingWindowError::
                        random_rejection_exhausted) {
                contain_runtime_failure();
            }
        }
        g_gnss.service(elapsed_ms);
        if (elapsed_ms >= next_battery_sample_ms) {
            const auto reading = opentrail::heltec_v4::battery_read();
            const auto sampled_at_ms =
                static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
            g_battery_percent = reading.valid
                ? opentrail::ui::compact_status_footer::Metric{
                      opentrail::ui::compact_status_footer::ObservationState::valid,
                      reading.percent,
                      sampled_at_ms}
                : opentrail::ui::compact_status_footer::Metric{
                      opentrail::ui::compact_status_footer::ObservationState::invalid,
                      0,
                      sampled_at_ms};
            next_battery_sample_ms = sampled_at_ms + kBatterySamplePeriodMs;
        }
        const auto render_now_ms =
            static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
        if (render_now_ms - boot_started_at_ms >= kMinimumLogoPeriodMs) {
            observe_display_result(g_startup_display.show_compact_status(
                startup_display_frame_for_ble_phase(
                    runtime_status.phase),
                compact_status_snapshot(render_now_ms)));
        }
        if (elapsed_ms >= next_heartbeat_ms) {
            ESP_LOGI(kLogTag, "heartbeat elapsed_ms=%llu",
                     static_cast<unsigned long long>(elapsed_ms));
            next_heartbeat_ms = elapsed_ms + kHeartbeatPeriodMs;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
