#include "companion_nimble_runtime.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "companion_authorization_storage.hpp"
#include "companion_nimble_gatt.hpp"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "opentrail/companion_gatt_authorization_adapter.hpp"

namespace opentrail::target::heltec_v4_bench {
namespace {

using namespace opentrail::companion;

constexpr std::size_t kCallbackQueueCapacity = 8;
constexpr CompanionBleRuntimePolicy kRuntimePolicy{10000, 1000, 3};

const ble_uuid128_t kAdvertisedServiceUuid = BLE_UUID128_INIT(
    0xD0, 0xB7, 0x43, 0x1F, 0x4F, 0x0C, 0x10, 0xA2,
    0xA3, 0x4E, 0x6B, 0x7C, 0x00, 0x2A, 0x0F, 0x5E);

class DeniedSnapshotAuthority final : public CompanionSnapshotAuthority {
public:
    CompanionSnapshotAuthorityResult read_snapshot() override {
        return {CompanionAuthorityError::not_ready, {}};
    }
};

class DeniedActionAuthority final : public CompanionActionAuthority {
public:
    CompanionActionAuthorityResult prepare_action(
        const CompanionActionRequest&) override {
        return {CompanionAuthorityError::not_ready,
                CompanionActionDisposition::rejected,
                CompanionActionRejectReason::internal_failure, 0};
    }
    CompanionAuthorityError commit_action(
        const CompanionActionRequest&,
        const CompanionActionAuthorityResult&) override {
        return CompanionAuthorityError::failed;
    }
};

class DeniedBindingAuthority final
    : public CompanionGattTrustedBindingAuthority {
public:
    CompanionGattTrustedBindingResult resolve(
        std::uint16_t,
        std::uint64_t) override {
        return {CompanionGattTrustedBindingError::not_ready, {}, 0};
    }
};

class DeniedCorrelationIssuer final
    : public CompanionGattAuthorizationCorrelationIssuer {
public:
    CompanionGattAuthorizationCorrelationResult issue(
        const CompanionGattAuthorizationCorrelationContext&) override {
        return {CompanionGattAuthorizationCorrelationError::not_ready, {}};
    }
};

class DeniedAuthorizationAuthority final
    : public CompanionGattAuthorizationAuthority {
public:
    CompanionGattAuthorizationDecision apply_claim(
        CompanionAuthorizationPurpose,
        const CompanionControllerClaim&,
        std::uint64_t) override {
        return {CompanionGattAuthorizationAuthorityError::not_ready,
                CompanionAuthorizationClaimOutcome::denied,
                CompanionAuthorizationDenyReason::unknown, 0};
    }
    CompanionGattAuthorizationAuthorityError release_connection(
        std::uint64_t) override {
        return CompanionGattAuthorizationAuthorityError::failed;
    }
};

DeniedSnapshotAuthority g_snapshot_authority;
DeniedActionAuthority g_action_authority;
CompanionRequestCoordinator g_request_coordinator{
    g_snapshot_authority, g_action_authority};
DeniedBindingAuthority g_binding_authority;
DeniedCorrelationIssuer g_correlation_issuer;
DeniedAuthorizationAuthority g_authorization_authority;
CompanionGattAuthorizationCallbackAdapter g_callback_adapter{
    g_request_coordinator,
    companion_nimble_gatt_indication_port(),
    g_binding_authority,
    g_correlation_issuer,
    g_authorization_authority};

enum class RuntimeEventKind : std::uint8_t {
    host_sync = 1,
    host_reset,
    advertising_interrupted,
    connection_opened,
    connection_closed,
    connection_termination_failed,
};

struct RuntimeEvent {
    RuntimeEventKind kind{RuntimeEventKind::host_reset};
    std::uint16_t connection_handle{kCompanionBleInvalidConnectionHandle};
    std::uint64_t observed_at_ms{0};
};

StaticQueue_t g_event_queue_control{};
std::array<std::uint8_t, kCallbackQueueCapacity * sizeof(RuntimeEvent)>
    g_event_queue_storage{};
QueueHandle_t g_event_queue = nullptr;
std::atomic<bool> g_event_overflow{false};
std::atomic<bool> g_host_exited{false};

int runtime_gap_event(ble_gap_event* event, void* argument);
void runtime_on_sync();
void runtime_on_reset(int reason);
void runtime_host_task(void* argument);

class EspNimbleRuntimePort final : public CompanionBleRuntimePort {
public:
    bool initialize_stack() override {
        if (stack_initialized_ || nimble_port_init() != ESP_OK) {
            return false;
        }
        stack_initialized_ = true;
        return true;
    }

    bool configure_secure_connections_bonding() override {
        if (!stack_initialized_ || host_started_) return false;
        ble_hs_cfg.reset_cb = runtime_on_reset;
        ble_hs_cfg.sync_cb = runtime_on_sync;
        ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
        ble_hs_cfg.sm_bonding = 1;
        ble_hs_cfg.sm_mitm = 1;
        ble_hs_cfg.sm_sc = 1;
        ble_hs_cfg.sm_our_key_dist =
            BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
        ble_hs_cfg.sm_their_key_dist =
            BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
        // No store callback is installed: OT-054 protected storage admission
        // is denied, so a transient stack bond is never authority evidence.
        ble_hs_cfg.store_status_cb = nullptr;
        return true;
    }

    bool register_protected_service() override {
        if (!stack_initialized_ || host_started_) return false;
        ble_svc_gap_init();
        ble_svc_gatt_init();
        return register_companion_nimble_gatt_service(&g_callback_adapter) == 0;
    }

    bool start_host_task() override {
        if (!stack_initialized_ || host_started_) return false;
        const auto result = xTaskCreatePinnedToCore(
            runtime_host_task, "ot_ble_host", NIMBLE_HS_STACK_SIZE, nullptr,
            configMAX_PRIORITIES - 4, &host_task_, NIMBLE_CORE);
        if (result != pdPASS) return false;
        host_started_ = true;
        return true;
    }

    bool configure_public_service_advertising() override {
        if (!host_started_) return false;
        if (ble_hs_util_ensure_addr(1) != 0 ||
            ble_hs_id_infer_auto(1, &own_address_type_) != 0) {
            return false;
        }
        ble_hs_adv_fields fields{};
        fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
        fields.uuids128 = const_cast<ble_uuid128_t*>(&kAdvertisedServiceUuid);
        fields.num_uuids128 = 1;
        fields.uuids128_is_complete = 1;
        // Deliberately no name, manufacturer data, address, public identifier,
        // user/group identity, or peer-derived field.
        return ble_gap_adv_set_fields(&fields) == 0;
    }

    bool start_advertising() override {
        if (!host_started_) return false;
        ble_gap_adv_params parameters{};
        parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
        parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
        return ble_gap_adv_start(own_address_type_, nullptr, BLE_HS_FOREVER,
                                 &parameters, runtime_gap_event, nullptr) == 0;
    }

    void terminate_connection(std::uint16_t connection_handle) override {
        if (host_started_ &&
            connection_handle != kCompanionBleInvalidConnectionHandle) {
            (void)ble_gap_terminate(connection_handle,
                                    BLE_ERR_REM_USER_CONN_TERM);
        }
    }

    bool contain_stack() override {
        if (contained_) return shutdown_complete_;
        contained_ = true;
        bool stop_ok = true;
        if (host_started_ && !host_run_exited_) {
            stop_ok = nimble_port_stop() == 0;
        }
        // The owner context owns the task handle. Whether synchronous stop
        // returned or the run loop had already published its exit, delete the
        // exact task before deinitializing host/controller state. No callback
        // is required for cleanup.
        if (host_task_ != nullptr) {
            vTaskDelete(host_task_);
            host_task_ = nullptr;
        }
        bool deinit_ok = true;
        if (stack_initialized_) {
            deinit_ok = nimble_port_deinit() == ESP_OK;
            stack_initialized_ = false;
        }
        host_started_ = false;
        host_run_exited_ = false;
        shutdown_complete_ = stop_ok && deinit_ok;
        return shutdown_complete_;
    }

    // Called only by the app_main owner context after the host task publishes
    // its atomic exit flag. A later containment must not ask an already-exited
    // run loop to service nimble_port_stop(), which would deadlock.
    void observe_host_run_exit() {
        host_run_exited_ = true;
    }

private:
    bool stack_initialized_{false};
    bool host_started_{false};
    bool contained_{false};
    bool shutdown_complete_{false};
    bool host_run_exited_{false};
    std::uint8_t own_address_type_{BLE_OWN_ADDR_RPA_PUBLIC_DEFAULT};
    TaskHandle_t host_task_{nullptr};
};

EspNimbleRuntimePort g_runtime_port;
CompanionBleRuntimeOwner g_runtime_owner{g_runtime_port, kRuntimePolicy};

std::uint64_t current_ms() {
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
}

bool queue_event(RuntimeEvent event) {
    if (g_event_queue == nullptr ||
        xQueueSend(g_event_queue, &event, 0) != pdPASS) {
        g_event_overflow.store(true, std::memory_order_release);
        return false;
    }
    return true;
}

void runtime_on_sync() {
    (void)queue_event({RuntimeEventKind::host_sync,
                       kCompanionBleInvalidConnectionHandle, current_ms()});
}

void runtime_on_reset(int) {
    (void)queue_event({RuntimeEventKind::host_reset,
                       kCompanionBleInvalidConnectionHandle, current_ms()});
}

void runtime_host_task(void*) {
    nimble_port_run();
    // Publish only liveness. The owner context owns task deletion and all
    // mutable port/deinitialization state, so there is no cross-task data race.
    g_host_exited.store(true, std::memory_order_release);
    vTaskSuspend(nullptr);
}

int runtime_gap_event(ble_gap_event* event, void*) {
    if (event == nullptr) return 0;
    const auto observed = current_ms();
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT: {
            if (event->connect.status != 0) {
                (void)queue_event({RuntimeEventKind::advertising_interrupted,
                                  kCompanionBleInvalidConnectionHandle,
                                  observed});
                return 0;
            }
            (void)companion_nimble_gatt_gap_event(event,
                                                  &g_callback_adapter);
            const bool adapter_connected =
                companion_nimble_gatt_adapter_status().connected;
            const bool queued = queue_event({RuntimeEventKind::connection_opened,
                                             event->connect.conn_handle,
                                             observed});
            // OT-054 protected owner/bond persistence is deliberately denied.
            // Consequently no usable secure application session can be
            // established in this increment. Terminate every connection
            // immediately so an unauthenticated peer cannot monopolize the
            // one-connection runtime or suppress re-advertising indefinitely.
            const int termination = ble_gap_terminate(
                event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            if (termination == BLE_HS_ENOTCONN) {
                // No disconnect callback can follow. Release the exact OT-052
                // session and enqueue the matching close after the open.
                (void)g_callback_adapter.disconnect(
                    event->connect.conn_handle);
                (void)queue_event({RuntimeEventKind::connection_closed,
                                  event->connect.conn_handle, observed});
            } else if (termination != 0) {
                (void)queue_event({
                    RuntimeEventKind::connection_termination_failed,
                    event->connect.conn_handle, observed});
            }
            if (!adapter_connected || !queued) {
                g_event_overflow.store(true, std::memory_order_release);
            }
            return 0;
        }
        case BLE_GAP_EVENT_DISCONNECT:
            // OT-052 releases its exact session/indication state before the
            // runtime owner can schedule re-advertising.
            (void)companion_nimble_gatt_gap_event(event,
                                                  &g_callback_adapter);
            (void)queue_event({RuntimeEventKind::connection_closed,
                              event->disconnect.conn.conn_handle, observed});
            return 0;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            (void)queue_event({RuntimeEventKind::advertising_interrupted,
                              kCompanionBleInvalidConnectionHandle, observed});
            return 0;
        case BLE_GAP_EVENT_REPEAT_PAIRING:
            // Never delete/replace a bond without the protected physical
            // replacement authority that OT-054 has not admitted.
            return BLE_GAP_REPEAT_PAIRING_IGNORE;
        case BLE_GAP_EVENT_PASSKEY_ACTION:
            // No physical comparison/input adapter is admitted. With MITM
            // required, doing nothing fails pairing closed.
            return 0;
        default:
            return companion_nimble_gatt_gap_event(event,
                                                   &g_callback_adapter);
    }
}

CompanionBleRuntimeError apply_event(const RuntimeEvent& event) {
    switch (event.kind) {
        case RuntimeEventKind::host_sync:
            return g_runtime_owner.host_synced(event.observed_at_ms);
        case RuntimeEventKind::host_reset:
            return g_runtime_owner.host_reset();
        case RuntimeEventKind::advertising_interrupted:
            return g_runtime_owner.advertising_interrupted(
                event.observed_at_ms);
        case RuntimeEventKind::connection_opened:
            return g_runtime_owner.connection_opened(event.connection_handle);
        case RuntimeEventKind::connection_closed:
            return g_runtime_owner.connection_closed(
                event.connection_handle, event.observed_at_ms);
        case RuntimeEventKind::connection_termination_failed:
            return g_runtime_owner.connection_termination_failed(
                event.connection_handle);
    }
    return CompanionBleRuntimeError::invalid_argument;
}

}  // namespace

CompanionBleRuntimeError start_companion_nimble_runtime(std::uint64_t now_ms) {
    if (g_event_queue == nullptr) {
        g_event_queue = xQueueCreateStatic(
            kCallbackQueueCapacity, sizeof(RuntimeEvent),
            g_event_queue_storage.data(), &g_event_queue_control);
    }
    if (g_event_queue == nullptr) {
        return CompanionBleRuntimeError::callback_queue_overflow;
    }
    const auto admission = companion_authorization_storage_preflight();
    if (admission != CompanionAuthorizationTargetAdmissionError::
                         nvs_encryption_not_configured) {
        return CompanionBleRuntimeError::invalid_argument;
    }
    return g_runtime_owner.start(now_ms, true);
}

CompanionBleRuntimeError service_companion_nimble_runtime(
    std::uint64_t now_ms) {
    if (g_host_exited.exchange(false, std::memory_order_acq_rel)) {
        g_runtime_port.observe_host_run_exit();
        if (g_runtime_owner.status().phase !=
            CompanionBleRuntimePhase::contained) {
            return g_runtime_owner.host_reset();
        }
    }
    if (g_event_overflow.exchange(false, std::memory_order_acq_rel)) {
        return g_runtime_owner.callback_overflow();
    }
    RuntimeEvent event{};
    for (std::size_t index = 0; index < kCallbackQueueCapacity; ++index) {
        if (g_event_queue == nullptr ||
            xQueueReceive(g_event_queue, &event, 0) != pdPASS) {
            break;
        }
        const auto result = apply_event(event);
        if (result != CompanionBleRuntimeError::none &&
            result != CompanionBleRuntimeError::wrong_connection &&
            result != CompanionBleRuntimeError::stale_restart) {
            return result;
        }
    }
    auto status = g_runtime_owner.status();
    if (status.phase == CompanionBleRuntimePhase::restart_wait) {
        const auto result = g_runtime_owner.service_restart(
            status.restart_token, now_ms);
        if (result != CompanionBleRuntimeError::none &&
            result != CompanionBleRuntimeError::restart_not_due) {
            return result;
        }
    }
    return g_runtime_owner.service_watchdog(now_ms);
}

CompanionBleRuntimeStatus companion_nimble_runtime_status() {
    return g_runtime_owner.status();
}

}  // namespace opentrail::target::heltec_v4_bench
