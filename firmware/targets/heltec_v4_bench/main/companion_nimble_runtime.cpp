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
#include "freertos/semphr.h"
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
constexpr CompanionBleRuntimePolicy kRuntimePolicy{10000, 1000, 3, 15000, 2000};

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
    pairing_completed,
    pairing_failed,
    pairing_closed,
};

struct RuntimeEvent {
    RuntimeEventKind kind{RuntimeEventKind::host_reset};
    std::uint16_t connection_handle{kCompanionBleInvalidConnectionHandle};
    std::uint64_t observed_at_ms{0};
    std::uint64_t transport_generation{0};
};

StaticQueue_t g_event_queue_control{};
std::array<std::uint8_t, kCallbackQueueCapacity * sizeof(RuntimeEvent)>
    g_event_queue_storage{};
QueueHandle_t g_event_queue = nullptr;
std::atomic<bool> g_event_overflow{false};
std::atomic<bool> g_host_exited{false};
StaticSemaphore_t g_pairing_mutex_control{};
SemaphoreHandle_t g_pairing_mutex = nullptr;
CompanionPairingWindow* g_pairing_window = nullptr;
std::uint16_t g_pairing_connection_handle{
    kCompanionBleInvalidConnectionHandle};
std::uint64_t g_pairing_transport_generation{0};

class PairingLock final {
public:
    explicit PairingLock(TickType_t wait_ticks)
        : locked_(g_pairing_mutex != nullptr &&
                  xSemaphoreTake(g_pairing_mutex, wait_ticks) == pdTRUE) {}
    ~PairingLock() {
        if (locked_) (void)xSemaphoreGive(g_pairing_mutex);
    }
    [[nodiscard]] bool locked() const { return locked_; }

private:
    bool locked_{false};
};

CompanionPairingCandidate active_pairing_candidate(
    std::uint16_t connection_handle) {
    if (connection_handle != g_pairing_connection_handle ||
        g_pairing_transport_generation == 0) {
        return {};
    }
    return {connection_handle, g_pairing_transport_generation};
}

int runtime_gap_event(ble_gap_event* event, void* argument);
void runtime_on_sync();
void runtime_on_reset(int reason);
void runtime_host_task(void* argument);
bool queue_event(RuntimeEvent event);
std::uint64_t current_ms();

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
        ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
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

    bool terminate_connection(std::uint16_t connection_handle) override {
        return host_started_ &&
               connection_handle != kCompanionBleInvalidConnectionHandle &&
               ble_gap_terminate(connection_handle,
                                 BLE_ERR_REM_USER_CONN_TERM) == 0;
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

class NimblePairingPasskeyPort final : public CompanionPairingPasskeyPort {
public:
    bool inject_display_passkey(
        CompanionPairingCandidate candidate,
        std::uint32_t passkey) override {
        if (!(candidate == active_pairing_candidate(
                              candidate.connection_handle)) ||
            passkey >= kCompanionPairingPasskeyCount) {
            return false;
        }
        ble_sm_io input{};
        input.action = BLE_SM_IOACT_DISP;
        input.passkey = passkey;
        const auto result =
            ble_sm_inject_io(candidate.connection_handle, &input);
        input.passkey = 0;
        return result == 0;
    }
};

NimblePairingPasskeyPort g_pairing_passkey_port;

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

bool terminate_connection_or_contain(
    std::uint16_t connection_handle,
    std::uint64_t observed_at_ms) {
    if (ble_gap_terminate(connection_handle,
                          BLE_ERR_REM_USER_CONN_TERM) == 0) {
        return true;
    }
    (void)queue_event({RuntimeEventKind::connection_termination_failed,
                      connection_handle, observed_at_ms});
    return false;
}

bool security_initiation_accepted(std::uint16_t connection_handle) {
    const auto result = ble_gap_security_initiate(connection_handle);
    return result == 0 || result == BLE_HS_EALREADY;
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
            bool pairing_open = false;
            std::uint64_t pairing_generation = 0;
            {
                PairingLock lock{pdMS_TO_TICKS(50)};
                if (lock.locked() && g_pairing_window != nullptr) {
                    g_pairing_connection_handle =
                        event->connect.conn_handle;
                    ++g_pairing_transport_generation;
                    if (g_pairing_transport_generation == 0) {
                        ++g_pairing_transport_generation;
                    }
                    pairing_open = g_pairing_window->status().phase ==
                                   CompanionPairingWindowPhase::open;
                    pairing_generation = g_pairing_transport_generation;
                } else {
                    g_event_overflow.store(true,
                                           std::memory_order_release);
                }
            }
            const bool queued = queue_event({
                RuntimeEventKind::connection_opened,
                event->connect.conn_handle,
                observed,
                pairing_generation});
            if (!adapter_connected || !queued) {
                g_event_overflow.store(true, std::memory_order_release);
            }
            if (pairing_open && !security_initiation_accepted(
                                    event->connect.conn_handle)) {
                (void)queue_event({
                    RuntimeEventKind::pairing_closed,
                    event->connect.conn_handle,
                    observed,
                    pairing_generation});
                (void)terminate_connection_or_contain(
                    event->connect.conn_handle, observed);
            }
            return 0;
        }
        case BLE_GAP_EVENT_DISCONNECT: {
            // OT-052 releases its exact session/indication state before the
            // runtime owner can schedule re-advertising.
            (void)companion_nimble_gatt_gap_event(event,
                                                  &g_callback_adapter);
            std::uint64_t pairing_generation = 0;
            {
                PairingLock lock{pdMS_TO_TICKS(50)};
                if (lock.locked() && g_pairing_connection_handle ==
                                         event->disconnect.conn.conn_handle) {
                    pairing_generation = g_pairing_transport_generation;
                    g_pairing_connection_handle =
                        kCompanionBleInvalidConnectionHandle;
                } else if (!lock.locked()) {
                    g_event_overflow.store(true,
                                           std::memory_order_release);
                }
            }
            (void)queue_event({
                RuntimeEventKind::connection_closed,
                event->disconnect.conn.conn_handle,
                observed,
                pairing_generation});
            return 0;
        }
        case BLE_GAP_EVENT_ADV_COMPLETE:
            (void)queue_event({RuntimeEventKind::advertising_interrupted,
                              kCompanionBleInvalidConnectionHandle, observed});
            return 0;
        case BLE_GAP_EVENT_REPEAT_PAIRING:
            // Never delete/replace a bond without the protected physical
            // replacement authority that OT-054 has not admitted.
            return BLE_GAP_REPEAT_PAIRING_IGNORE;
        case BLE_GAP_EVENT_TERM_FAILURE:
            (void)queue_event({
                RuntimeEventKind::connection_termination_failed,
                event->term_failure.conn_handle,
                observed});
            return 0;
        case BLE_GAP_EVENT_PASSKEY_ACTION: {
            if (event->passkey.params.action != BLE_SM_IOACT_DISP) {
                std::uint64_t pairing_generation = 0;
                {
                    PairingLock lock{pdMS_TO_TICKS(50)};
                    if (lock.locked()) {
                        pairing_generation =
                            active_pairing_candidate(
                                event->passkey.conn_handle)
                                .transport_generation;
                    } else {
                        g_event_overflow.store(true,
                                               std::memory_order_release);
                    }
                }
                (void)queue_event({
                    RuntimeEventKind::pairing_closed,
                    event->passkey.conn_handle,
                    observed,
                    pairing_generation});
                (void)terminate_connection_or_contain(
                    event->passkey.conn_handle, observed);
                return 0;
            }
            bool accepted = false;
            CompanionPairingWindowError action_result{
                CompanionPairingWindowError::window_not_available};
            std::uint64_t pairing_generation = 0;
            {
                PairingLock lock{pdMS_TO_TICKS(50)};
                if (lock.locked() && g_pairing_window != nullptr) {
                    const auto candidate = active_pairing_candidate(
                        event->passkey.conn_handle);
                    pairing_generation = candidate.transport_generation;
                    action_result = g_pairing_window->
                        handle_passkey_action_deferred_cleanup(
                            observed, candidate,
                            g_pairing_passkey_port);
                    accepted = action_result ==
                               CompanionPairingWindowError::none;
                } else {
                    g_event_overflow.store(true,
                                           std::memory_order_release);
                }
            }
            if (!accepted) {
                (void)queue_event({
                    action_result == CompanionPairingWindowError::
                                         passkey_injection_failed
                        ? RuntimeEventKind::pairing_failed
                        : RuntimeEventKind::pairing_closed,
                    event->passkey.conn_handle,
                    observed,
                    pairing_generation});
                (void)terminate_connection_or_contain(
                    event->passkey.conn_handle, observed);
            }
            return 0;
        }
        case BLE_GAP_EVENT_ENC_CHANGE: {
            (void)companion_nimble_gatt_gap_event(event,
                                                  &g_callback_adapter);
            ble_gap_conn_desc description{};
            const bool exact_secure_bond =
                event->enc_change.status == 0 &&
                ble_gap_conn_find(
                    event->enc_change.conn_handle, &description) == 0 &&
                description.sec_state.encrypted != 0 &&
                description.sec_state.authenticated != 0 &&
                description.sec_state.bonded != 0 &&
                description.sec_state.key_size == BLE_SM_PAIR_KEY_SZ_MAX;
            std::uint64_t pairing_generation = 0;
            bool exact_active_attempt = false;
            {
                PairingLock lock{pdMS_TO_TICKS(50)};
                if (lock.locked() && g_pairing_window != nullptr) {
                    const auto candidate = active_pairing_candidate(
                        event->enc_change.conn_handle);
                    exact_active_attempt =
                        candidate.transport_generation != 0 &&
                        g_pairing_window->status().phase ==
                            CompanionPairingWindowPhase::attempt_active;
                    if (exact_active_attempt) {
                        pairing_generation =
                            candidate.transport_generation;
                    }
                } else {
                    g_event_overflow.store(true,
                                           std::memory_order_release);
                }
            }
            if (exact_active_attempt) {
                (void)queue_event({
                    exact_secure_bond
                        ? RuntimeEventKind::pairing_completed
                        : RuntimeEventKind::pairing_failed,
                    event->enc_change.conn_handle,
                    observed,
                    pairing_generation});
            }
            if (!exact_secure_bond) {
                (void)terminate_connection_or_contain(
                    event->enc_change.conn_handle, observed);
            }
            return 0;
        }
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
            {
                PairingLock lock{pdMS_TO_TICKS(50)};
                if (!lock.locked() || g_pairing_window == nullptr) {
                    return CompanionBleRuntimeError::callback_queue_overflow;
                }
                g_pairing_connection_handle =
                    kCompanionBleInvalidConnectionHandle;
                ++g_pairing_transport_generation;
                if (g_pairing_transport_generation == 0) {
                    ++g_pairing_transport_generation;
                }
                const auto pairing_result =
                    g_pairing_window->restart();
                if (pairing_result !=
                    CompanionPairingWindowError::none) {
                    return CompanionBleRuntimeError::invalid_argument;
                }
            }
            return g_runtime_owner.host_reset();
        case RuntimeEventKind::advertising_interrupted:
            return g_runtime_owner.advertising_interrupted(
                event.observed_at_ms);
        case RuntimeEventKind::connection_opened:
            return g_runtime_owner.connection_opened(
                event.connection_handle, event.observed_at_ms);
        case RuntimeEventKind::connection_closed:
            {
                PairingLock lock{pdMS_TO_TICKS(50)};
                if (!lock.locked()) {
                    return CompanionBleRuntimeError::callback_queue_overflow;
                }
                if (g_pairing_window != nullptr) {
                    const auto phase = g_pairing_window->status().phase;
                    if (phase != CompanionPairingWindowPhase::closed &&
                        phase != CompanionPairingWindowPhase::faulted) {
                        const auto result = g_pairing_window->disconnect(
                            event.observed_at_ms,
                            {event.connection_handle,
                             event.transport_generation});
                        if (result != CompanionPairingWindowError::none) {
                            return CompanionBleRuntimeError::invalid_argument;
                        }
                    }
                }
            }
            return g_runtime_owner.connection_closed(
                event.connection_handle, event.observed_at_ms);
        case RuntimeEventKind::connection_termination_failed:
            return g_runtime_owner.connection_termination_failed(
                event.connection_handle);
        case RuntimeEventKind::pairing_completed:
        case RuntimeEventKind::pairing_failed: {
            PairingLock lock{pdMS_TO_TICKS(50)};
            if (!lock.locked() || g_pairing_window == nullptr) {
                return CompanionBleRuntimeError::callback_queue_overflow;
            }
            const auto phase = g_pairing_window->status().phase;
            if (phase == CompanionPairingWindowPhase::closed ||
                phase == CompanionPairingWindowPhase::faulted) {
                return CompanionBleRuntimeError::none;
            }
            const auto result = g_pairing_window->finish_attempt(
                event.observed_at_ms,
                {event.connection_handle, event.transport_generation},
                event.kind == RuntimeEventKind::pairing_completed
                    ? CompanionPairingAttemptTerminal::secure_bond_complete
                    : CompanionPairingAttemptTerminal::pairing_failed);
            if (result == CompanionPairingWindowError::none) {
                return CompanionBleRuntimeError::none;
            }
            if (result == CompanionPairingWindowError::window_expired) {
                return g_runtime_port.terminate_connection(
                           event.connection_handle)
                    ? CompanionBleRuntimeError::none
                    : g_runtime_owner.connection_termination_failed(
                          event.connection_handle);
            }
            return CompanionBleRuntimeError::invalid_argument;
        }
        case RuntimeEventKind::pairing_closed: {
            PairingLock lock{pdMS_TO_TICKS(50)};
            if (!lock.locked() || g_pairing_window == nullptr) {
                return CompanionBleRuntimeError::callback_queue_overflow;
            }
            const auto phase = g_pairing_window->status().phase;
            if (phase == CompanionPairingWindowPhase::closed ||
                phase == CompanionPairingWindowPhase::faulted) {
                return CompanionBleRuntimeError::none;
            }
            const auto result = g_pairing_window->disconnect(
                event.observed_at_ms,
                {event.connection_handle, event.transport_generation});
            return result == CompanionPairingWindowError::none
                ? CompanionBleRuntimeError::none
                : CompanionBleRuntimeError::invalid_argument;
        }
    }
    return CompanionBleRuntimeError::invalid_argument;
}

}  // namespace

CompanionBleRuntimeError start_companion_nimble_runtime(
    std::uint64_t now_ms,
    CompanionPairingWindow& pairing_window) {
    if (g_pairing_mutex == nullptr) {
        g_pairing_mutex = xSemaphoreCreateMutexStatic(
            &g_pairing_mutex_control);
    }
    if (g_pairing_mutex == nullptr || g_pairing_window != nullptr) {
        return CompanionBleRuntimeError::invalid_argument;
    }
    g_pairing_window = &pairing_window;
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
            return g_runtime_owner.callback_overflow();
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

CompanionPairingWindowError open_companion_pairing_window(
    std::uint64_t now_ms,
    std::uint64_t physical_event,
    std::uint64_t hold_ms) {
    CompanionPairingWindowError result{
        CompanionPairingWindowError::window_not_available};
    std::uint16_t connected_handle{kCompanionBleInvalidConnectionHandle};
    {
        PairingLock lock{pdMS_TO_TICKS(50)};
        if (!lock.locked() || g_pairing_window == nullptr) {
            return CompanionPairingWindowError::window_not_available;
        }
        result = g_pairing_window->open_window(
            now_ms, physical_event, hold_ms, true,
            CompanionPairingPurpose::claim);
        connected_handle = g_pairing_connection_handle;
    }
    if (result != CompanionPairingWindowError::none) return result;

    if (connected_handle != kCompanionBleInvalidConnectionHandle &&
        !security_initiation_accepted(connected_handle)) {
        {
            PairingLock lock{pdMS_TO_TICKS(50)};
            if (!lock.locked() || g_pairing_window == nullptr) {
                return CompanionPairingWindowError::window_not_available;
            }
            const auto closed = g_pairing_window->disconnect(
                now_ms, active_pairing_candidate(connected_handle));
            if (closed != CompanionPairingWindowError::none) {
                return closed;
            }
        }
        (void)terminate_connection_or_contain(
            connected_handle, now_ms);
        return CompanionPairingWindowError::window_not_available;
    }
    return CompanionPairingWindowError::none;
}

CompanionPairingWindowError service_companion_pairing_window(
    std::uint64_t now_ms) {
    PairingLock lock{pdMS_TO_TICKS(50)};
    if (!lock.locked() || g_pairing_window == nullptr) {
        return CompanionPairingWindowError::window_not_available;
    }
    return g_pairing_window->service(now_ms);
}

CompanionPairingWindowError fault_companion_pairing_window() {
    PairingLock lock{pdMS_TO_TICKS(50)};
    if (!lock.locked() || g_pairing_window == nullptr) {
        return CompanionPairingWindowError::window_not_available;
    }
    return g_pairing_window->fault();
}

CompanionPairingWindowStatus companion_pairing_window_status() {
    PairingLock lock{0};
    if (!lock.locked() || g_pairing_window == nullptr) return {};
    return g_pairing_window->status();
}

}  // namespace opentrail::target::heltec_v4_bench
