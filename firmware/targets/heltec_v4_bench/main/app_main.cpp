#include <array>
#include <cstdint>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "companion_boot_self_check.hpp"
#include "companion_nimble_gatt.hpp"
#include "opentrail/companion_protocol.hpp"
#include "opentrail/companion_semantics.hpp"

namespace {

constexpr char kLogTag[] = "ot_bench";
constexpr std::uint32_t kHeartbeatPeriodMs = 5000;

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
    ESP_LOGE(kLogTag, "companion boot self-check FAIL");
    while (true) {
        vTaskSuspend(nullptr);
    }
}

}  // namespace

extern "C" void app_main() {
    if (!run_companion_codec_self_check() ||
        !opentrail::target::heltec_v4_bench::
             run_companion_request_coordinator_self_check() ||
        !opentrail::target::heltec_v4_bench::
             run_companion_gatt_session_self_check() ||
        !opentrail::target::heltec_v4_bench::
             run_companion_gatt_authorization_self_check() ||
        !opentrail::target::heltec_v4_bench::
             companion_nimble_gatt_definition_self_check()) {
        contain_self_check_failure();
    }
    ESP_LOGI(kLogTag, "companion boot self-check PASS");
    ESP_LOGI(kLogTag, "build-only bench candidate started");

    while (true) {
        const auto elapsed_ms =
            static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
        ESP_LOGI(kLogTag, "heartbeat elapsed_ms=%llu",
                 static_cast<unsigned long long>(elapsed_ms));
        vTaskDelay(pdMS_TO_TICKS(kHeartbeatPeriodMs));
    }
}
