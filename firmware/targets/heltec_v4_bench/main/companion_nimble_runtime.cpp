#include "companion_nimble_runtime.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "companion_authorization_storage.hpp"
#include "companion_nimble_gatt.hpp"
#include "companion_v1_heltec_adapters.hpp"
#include "heltec_startup_display.hpp"
#include "heltec_v4_factory_reset_storage.hpp"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "opentrail/companion_gatt_authorization_adapter.hpp"
#include "opentrail/companion_factory_reset_authority.hpp"
#include "opentrail/companion_reset_receipt.hpp"
#include "opentrail/companion_v1_bond_owner.hpp"
#include "nvs_flash.h"

extern "C" void ble_store_config_init(void);

namespace opentrail::target::heltec_v4_bench {
namespace {

using namespace opentrail::companion;

constexpr std::size_t kCallbackQueueCapacity = 8;
constexpr CompanionBleRuntimePolicy kRuntimePolicy{10000, 1000, 3, 45000, 2000};
constexpr std::uint64_t kFactoryResetResponseDeadlineMs = 5000;
constexpr std::uint32_t kFactoryResetRetryDelayMs = 1000;

ble_uuid128_t kAdvertisingUuids[] = {
    BLE_UUID128_INIT(
        0xD0, 0xB7, 0x43, 0x1F, 0x4F, 0x0C, 0x10, 0xA2,
        0xA3, 0x4E, 0x6B, 0x7C, 0x00, 0x2A, 0x0F, 0x5E),
    BLE_UUID128_INIT(
        0xD1, 0xB7, 0x43, 0x1F, 0x4F, 0x0C, 0x10, 0xA2,
        0xA3, 0x4E, 0x6B, 0x7C, 0x00, 0x2A, 0x0F, 0x5E),
};

class DeniedSnapshotAuthority final : public CompanionSnapshotAuthority {
public:
    CompanionSnapshotAuthorityResult read_snapshot() override {
        return {CompanionAuthorityError::not_ready, {}};
    }
};

class HeltecV4SnapshotAuthority final : public CompanionSnapshotAuthority {
public:
    CompanionSnapshotAuthorityResult read_snapshot() override {
        CompanionStatusSnapshot snapshot{};
        snapshot.revision = 1;
        snapshot.radio = CompanionRadioState::unavailable;
        snapshot.gnss = CompanionGnssState::unknown;
        snapshot.power = CompanionPowerState::unknown;
        snapshot.position_sharing =
            CompanionPositionSharingState::stopped;
        snapshot.queued_action_count = 0;
        snapshot.pending_critical_alert_id = 0;
        return {CompanionAuthorityError::none, snapshot};
    }
};

class BootLocalCorrelationIssuer final
    : public CompanionGattAuthorizationCorrelationIssuer {
public:
    explicit BootLocalCorrelationIssuer(
        security::SecureRandomSource& random) : random_(random) {}

    CompanionGattAuthorizationCorrelationResult issue(
        const CompanionGattAuthorizationCorrelationContext& context) override {
        if (operation_active_ || context.transport_generation == 0 ||
            context.session_nonce == 0 || context.exchange_id == 0 ||
            next_ == 0) {
            return {CompanionGattAuthorizationCorrelationError::failed, {}};
        }
        operation_active_ = true;
        if (!seed_ready_) {
            if (random_.state() == security::EntropyState::not_ready) {
                operation_active_ = false;
                return {CompanionGattAuthorizationCorrelationError::not_ready,
                        {}};
            }
            if (random_.state() != security::EntropyState::ready) {
                operation_active_ = false;
                return {CompanionGattAuthorizationCorrelationError::failed,
                        {}};
            }
            const auto filled = random_.fill(seed_.data(), seed_.size());
            if (!filled.ok() || filled.bytes_written != seed_.size()) {
                seed_.fill(0);
                operation_active_ = false;
                return {filled.error ==
                                security::RandomFillError::entropy_not_ready
                            ? CompanionGattAuthorizationCorrelationError::
                                  not_ready
                            : CompanionGattAuthorizationCorrelationError::
                                  failed,
                        {}};
            }
            bool any = false;
            for (const auto byte : seed_) any = any || byte != 0;
            if (!any) seed_[0] = 0x80U;
            seed_ready_ = true;
        }
        CompanionAuthorizationCorrelation correlation{};
        std::copy(seed_.begin(), seed_.end(), correlation.bytes.begin());
        for (std::size_t index = 0; index < sizeof(next_); ++index) {
            correlation.bytes[8 + index] = static_cast<std::uint8_t>(
                next_ >> ((sizeof(next_) - 1U - index) * 8U));
        }
        ++next_;
        operation_active_ = false;
        return {CompanionGattAuthorizationCorrelationError::none,
                correlation};
    }

private:
    security::SecureRandomSource& random_;
    std::array<std::uint8_t, 8> seed_{};
    std::uint64_t next_{1};
    bool seed_ready_{false};
    bool operation_active_{false};
};

HeltecV4SnapshotAuthority g_snapshot_authority;
CompanionGattAuthorizationCallbackAdapter* g_callback_adapter = nullptr;
CompanionV1BondOwnerBridge* g_v1_owner_bridge = nullptr;
opentrail::targets::heltec_v4_bench::
    HeltecV4CompanionV1NimbleBondAdapter* g_v1_bond_adapter = nullptr;
DeviceFactoryResetExecutor* g_factory_reset_executor = nullptr;
DeviceFactoryResetMarkerPort* g_factory_reset_marker = nullptr;
CompanionFactoryResetActionAuthority* g_factory_reset_action_authority =
    nullptr;
opentrail::targets::heltec_v4_bench::
    HeltecV4FactoryResetNimbleBondStorage* g_factory_reset_bonds = nullptr;
StartupDisplayOwner* g_startup_display = nullptr;
std::atomic<CompanionAppFactoryResetPhase> g_app_factory_reset_phase{
    CompanionAppFactoryResetPhase::idle};
std::atomic<std::uint64_t> g_app_factory_reset_response_started_ms{0};
bool g_boot_unowned{false};
bool g_boot_pairing_attempted{false};
std::atomic<bool> g_pairable_advertising{false};
std::atomic<std::uint64_t> g_boot_reset_receipt{0};
std::atomic<bool> g_suppress_next_adv_complete{false};

int reject_nimble_store_overflow(ble_store_status_event* event, void*) {
    return event != nullptr && event->event_code == BLE_STORE_EVENT_OVERFLOW
               ? BLE_HS_ESTORE_CAP
               : BLE_HS_ENOTSUP;
}

enum class RuntimeEventKind : std::uint8_t {
    host_sync = 1,
    host_reset,
    advertising_interrupted,
    connection_opened,
    connection_closed,
    connection_termination_failed,
    connection_authorized,
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
struct VerifiedGattProgress {
    std::uint16_t connection_handle{kCompanionBleInvalidConnectionHandle};
    std::uint64_t observed_at_ms{0};
    std::uint64_t transport_generation{0};
};
StaticQueue_t g_verified_gatt_progress_queue_control{};
std::array<std::uint8_t, sizeof(VerifiedGattProgress)>
    g_verified_gatt_progress_queue_storage{};
QueueHandle_t g_verified_gatt_progress_queue = nullptr;
std::atomic<bool> g_event_overflow{false};
std::atomic<bool> g_host_exited{false};
std::atomic<bool> g_orphan_reconciliation_required{false};
StaticSemaphore_t g_pairing_mutex_control{};
SemaphoreHandle_t g_pairing_mutex = nullptr;
StaticSemaphore_t g_factory_reset_mutex_control{};
SemaphoreHandle_t g_factory_reset_mutex = nullptr;
CompanionPairingWindow* g_pairing_window = nullptr;
std::uint16_t g_pairing_connection_handle{
    kCompanionBleInvalidConnectionHandle};
std::uint64_t g_pairing_transport_generation{0};
std::uint16_t g_owner_connection_handle{
    kCompanionBleInvalidConnectionHandle};
std::uint64_t g_owner_gatt_transport_generation{0};
std::atomic<std::uint8_t> g_security_configuration_failure_stage{0};
std::atomic<std::uint8_t> g_security_configuration_failure_detail{0};

bool clear_pending_cccd_value_changes() {
    ble_store_key_cccd key{};
    key.peer_addr = ble_addr_t{};
    key.chr_val_handle = 0;

    for (std::uint16_t index = 0; index <= UINT8_MAX; ++index) {
        key.idx = static_cast<std::uint8_t>(index);
        ble_store_value_cccd cccd{};
        const auto read_result = ble_store_read_cccd(&key, &cccd);
        if (read_result == BLE_HS_ENOENT) return true;
        if (read_result != 0) return false;
        if (!cccd.value_changed) continue;

        // The static Heltec profile has no deferred application-value
        // delivery contract. Retain each bonded peer and its subscription,
        // but consume stale one-shot updates before restored encryption can
        // emit an indication in the middle of Android service discovery.
        cccd.value_changed = 0;
        if (ble_store_write_cccd(&cccd) != 0) return false;
    }
    return false;
}

bool fail_security_configuration(std::uint8_t stage) {
    g_security_configuration_failure_stage.store(stage,
                                                 std::memory_order_release);
    return false;
}

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

class FactoryResetLock final {
public:
    explicit FactoryResetLock(TickType_t wait_ticks)
        : locked_(g_factory_reset_mutex != nullptr &&
                  xSemaphoreTake(g_factory_reset_mutex, wait_ticks) ==
                      pdTRUE) {}
    ~FactoryResetLock() {
        if (locked_) (void)xSemaphoreGive(g_factory_reset_mutex);
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

bool accepted_termination_result(int result) {
    return result == 0 || result == BLE_HS_EALREADY ||
           result == BLE_HS_ENOTCONN;
}

void set_pairable_advertising_state(bool enabled) {
    if (!enabled) {
        g_boot_reset_receipt.store(0, std::memory_order_release);
    }
    g_pairable_advertising.store(enabled, std::memory_order_release);
}

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
        g_security_configuration_failure_stage.store(
            0, std::memory_order_release);
        g_security_configuration_failure_detail.store(
            0, std::memory_order_release);
        if (!stack_initialized_ || host_started_ ||
            g_v1_owner_bridge == nullptr ||
            g_factory_reset_executor == nullptr ||
            g_factory_reset_marker == nullptr ||
            g_factory_reset_bonds == nullptr ||
            g_startup_display == nullptr) {
            return fail_security_configuration(1);
        }
        FactoryResetLock reset_lock{pdMS_TO_TICKS(100)};
        if (!reset_lock.locked()) return fail_security_configuration(2);

        // The reset marker is the first durable authority consulted after the
        // persistent NimBLE store exists. A committed reset never restores an
        // owner. With no marker, the exact owner bridge may first remove the
        // one safe orphan-bond shape left by an interrupted initial claim;
        // the reset executor then verifies the reconciled domains itself.
        ble_store_config_init();
        if (!clear_pending_cccd_value_changes()) {
            return fail_security_configuration(10);
        }
        g_factory_reset_bonds->set_store_access_ready(true);
        const auto reset_marker_snapshot = g_factory_reset_marker->load();
        if (reset_marker_snapshot.error !=
                DeviceFactoryResetPortError::none ||
            (reset_marker_snapshot.state !=
                 DeviceFactoryResetMarkerState::absent &&
             reset_marker_snapshot.state !=
                 DeviceFactoryResetMarkerState::intent_committed &&
             reset_marker_snapshot.state !=
                 DeviceFactoryResetMarkerState::receipt_pending)) {
            return fail_security_configuration(3);
        }
        const auto complete_committed_reset = []() -> bool {
            // Intent is already durable. Enter the non-cancellable recovery
            // overlay directly; never flash the pre-commit confirmation text.
            // Keep the initialized NimBLE store live and retry idempotent
            // cleanup after a bounded delay. A transient failure therefore
            // cannot strand a committed reset in the generic BLE-error loop.
            (void)g_startup_display->show_factory_reset_in_progress();
            while (true) {
                const auto cleaned =
                    g_factory_reset_executor->continue_cleanup();
                if (cleaned.accepted() &&
                    cleaned.phase ==
                        DeviceFactoryResetPhase::
                            reboot_unowned_permitted) {
                    esp_restart();
                }
                vTaskDelay(pdMS_TO_TICKS(kFactoryResetRetryDelayMs));
            }
        };
        const auto consume_committed_receipt = []()
            -> DeviceFactoryResetResult {
            while (true) {
                const auto consumed =
                    g_factory_reset_executor->consume_completion_receipt();
                if (consumed.accepted() &&
                    consumed.phase ==
                        DeviceFactoryResetPhase::idle_unowned &&
                    consumed.reset_receipt != 0) {
                    return consumed;
                }

                // Receipt-pending is durable and cleanup has already erased
                // every user/bond domain. If consumption is uncertain, never
                // escape into the generic BLE-error suspend. Reboot after a
                // bounded delay and let a fresh executor reconcile whether the
                // one-key erase committed. If it did, boot is safely unowned
                // with an app-unknown outcome; otherwise consumption retries.
                (void)g_startup_display->show_factory_reset_in_progress();
                vTaskDelay(pdMS_TO_TICKS(kFactoryResetRetryDelayMs));
                esp_restart();
            }
        };
        DeviceFactoryResetResult reset_restored{};
        bool reset_already_restored = false;
        if (reset_marker_snapshot.state !=
            DeviceFactoryResetMarkerState::absent) {
            reset_restored = g_factory_reset_executor->restore();
            if (!reset_restored.accepted()) {
                return fail_security_configuration(4);
            }
            if (reset_restored.phase ==
                DeviceFactoryResetPhase::cleanup_required) {
                return complete_committed_reset();
            }
            if (reset_restored.phase !=
                DeviceFactoryResetPhase::completion_receipt_pending) {
                return fail_security_configuration(5);
            }
            const auto consumed = consume_committed_receipt();
            // Marker clear/readback happened first. A power loss before this
            // RAM-only publication safely leaves the app outcome unknown.
            g_boot_reset_receipt.store(consumed.reset_receipt,
                                       std::memory_order_release);
            reset_restored = consumed;
            reset_already_restored = true;
        }

        const auto owner_restored = g_v1_owner_bridge->restore();
        if (!owner_restored.accepted()) {
            return fail_security_configuration(6);
        }
        if (!reset_already_restored) {
            reset_restored = g_factory_reset_executor->restore();
            if (!reset_restored.accepted()) {
                g_security_configuration_failure_detail.store(
                    static_cast<std::uint8_t>(reset_restored.error),
                    std::memory_order_release);
                return fail_security_configuration(7);
            }
        }
        // A contradictory second marker read can only fail closed here. If it
        // reports a committed intent, finish cleanup before any GATT/radio UI.
        if (reset_restored.phase ==
            DeviceFactoryResetPhase::cleanup_required) {
            return complete_committed_reset();
        }
        if (reset_restored.phase !=
                DeviceFactoryResetPhase::idle_unowned &&
            reset_restored.phase !=
                DeviceFactoryResetPhase::idle_old_state) {
            return fail_security_configuration(8);
        }

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
        // Persistence is provided by ESP-IDF's configured NimBLE NVS store.
        // Capacity failure is terminal for the write; never evict the owner.
        ble_hs_cfg.store_status_cb = reject_nimble_store_overflow;
        ble_hs_cfg.store_status_arg = nullptr;

        const bool owner_unowned =
            owner_restored.phase ==
            CompanionV1BondOwnerPhase::closed_unowned;
        if (owner_unowned !=
            (reset_restored.phase ==
             DeviceFactoryResetPhase::idle_unowned)) {
            return fail_security_configuration(9);
        }
        g_boot_unowned = owner_unowned;
        return true;
    }

    bool register_protected_service() override {
        if (!stack_initialized_ || host_started_) return false;
        ble_svc_gap_init();
        ble_svc_gatt_init();
        return g_callback_adapter != nullptr &&
               register_companion_nimble_gatt_service(g_callback_adapter) == 0;
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
        return configure_advertising_fields();
    }

    bool start_advertising() override {
        if (!host_started_ || !configure_advertising_fields()) return false;
        ble_gap_adv_params parameters{};
        parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
        parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
        return ble_gap_adv_start(own_address_type_, nullptr, BLE_HS_FOREVER,
                                 &parameters, runtime_gap_event, nullptr) == 0;
    }

    bool set_pairable_advertising(bool enabled, bool restart_active) {
        set_pairable_advertising_state(enabled);
        if (!restart_active) return true;
        if (!host_started_) return false;
        g_suppress_next_adv_complete.store(true, std::memory_order_release);
        if (ble_gap_adv_stop() != 0) {
            g_suppress_next_adv_complete.store(false,
                                               std::memory_order_release);
            return false;
        }
        if (!start_advertising()) {
            g_suppress_next_adv_complete.store(false,
                                               std::memory_order_release);
            return false;
        }
        return true;
    }

    bool terminate_connection(std::uint16_t connection_handle) override {
        if (!host_started_ ||
            connection_handle == kCompanionBleInvalidConnectionHandle) {
            return false;
        }
        return accepted_termination_result(
            ble_gap_terminate(connection_handle,
                              BLE_ERR_REM_USER_CONN_TERM));
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
        bool reset_cleanup_ok = true;
        const auto app_reset_phase = g_app_factory_reset_phase.load(
            std::memory_order_acquire);
        if (stop_ok &&
            (app_reset_phase ==
                 CompanionAppFactoryResetPhase::response_confirmed ||
             app_reset_phase ==
                 CompanionAppFactoryResetPhase::response_unknown)) {
            FactoryResetLock reset_lock{pdMS_TO_TICKS(100)};
            reset_cleanup_ok = reset_lock.locked() &&
                               g_factory_reset_executor != nullptr;
            if (reset_cleanup_ok) {
                const auto reset = g_factory_reset_executor->status();
                if (reset.phase ==
                    DeviceFactoryResetPhase::cleanup_required) {
                    const auto cleaned =
                        g_factory_reset_executor->continue_cleanup();
                    reset_cleanup_ok =
                        cleaned.accepted() &&
                        cleaned.phase ==
                            DeviceFactoryResetPhase::
                                reboot_unowned_permitted;
                } else {
                    reset_cleanup_ok =
                        reset.phase ==
                        DeviceFactoryResetPhase::
                            reboot_unowned_permitted;
                }
            }
        }
        // Pinned nimble_port_stop() preempts GAP procedures and completes its
        // connection-stop process while the host/store lock is still live.
        // Reset bond cleanup must finish in that window, never after deinit.
        if (g_factory_reset_bonds != nullptr) {
            g_factory_reset_bonds->set_store_access_ready(false);
        }
        bool deinit_ok = true;
        if (stack_initialized_) {
            deinit_ok = nimble_port_deinit() == ESP_OK;
            stack_initialized_ = false;
        }
        host_started_ = false;
        host_run_exited_ = false;
        shutdown_complete_ = stop_ok && reset_cleanup_ok && deinit_ok;
        return shutdown_complete_;
    }

    // Called only by the app_main owner context after the host task publishes
    // its atomic exit flag. A later containment must not ask an already-exited
    // run loop to service nimble_port_stop(), which would deadlock.
    void observe_host_run_exit() {
        host_run_exited_ = true;
    }

private:
    bool configure_advertising_fields() {
        ble_hs_adv_fields fields{};
        fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
        const bool pairable =
            g_pairable_advertising.load(std::memory_order_acquire);
        fields.uuids128 = &kAdvertisingUuids[pairable ? 1 : 0];
        fields.num_uuids128 = 1;
        fields.uuids128_is_complete = 1;
        // The fixed D1 service marker replaces D0 only while the boot-local
        // PIN window is open. After an app reset only, the scan response also
        // carries the one-time correlation receipt for that same 60-second
        // window. The receipt is not identity or authorization.
        if (ble_gap_adv_set_fields(&fields) != 0) return false;
        ble_hs_adv_fields response{};
        const auto receipt = pairable
            ? g_boot_reset_receipt.load(std::memory_order_acquire)
            : 0;
        const auto encoded =
            encode_companion_reset_receipt_service_data(receipt);
        if (encoded.valid) {
            response.svc_data_uuid128 = encoded.bytes.data();
            response.svc_data_uuid128_len =
                static_cast<std::uint8_t>(encoded.bytes.size());
        }
        // Calling with empty fields explicitly clears any prior receipt.
        return ble_gap_adv_rsp_set_fields(&response) == 0;
    }

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
    if (accepted_termination_result(
            ble_gap_terminate(connection_handle,
                              BLE_ERR_REM_USER_CONN_TERM))) {
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
            const auto adapter_connection_error =
                static_cast<CompanionGattAdapterError>(
                    companion_nimble_gatt_gap_event(
                        event, g_callback_adapter));
            if (adapter_connection_error !=
                CompanionGattAdapterError::none) {
                if (adapter_connection_error !=
                    CompanionGattAdapterError::connection_in_use) {
                    g_event_overflow.store(true,
                                           std::memory_order_release);
                }
                (void)terminate_connection_or_contain(
                    event->connect.conn_handle, observed);
                return 0;
            }
            const auto adapter = companion_nimble_gatt_adapter_status();
            if (!adapter.connected || adapter.transport_generation == 0) {
                g_event_overflow.store(true, std::memory_order_release);
                (void)terminate_connection_or_contain(
                    event->connect.conn_handle, observed);
                return 0;
            }
            std::uint64_t pairing_generation = 0;
            {
                PairingLock lock{pdMS_TO_TICKS(50)};
                if (lock.locked() && g_pairing_window != nullptr) {
                    if (g_pairing_connection_handle ==
                        kCompanionBleInvalidConnectionHandle) {
                        g_pairing_connection_handle =
                            event->connect.conn_handle;
                        ++g_pairing_transport_generation;
                        if (g_pairing_transport_generation == 0) {
                            ++g_pairing_transport_generation;
                        }
                        pairing_generation = g_pairing_transport_generation;
                    }
                } else {
                    g_event_overflow.store(true,
                                           std::memory_order_release);
                }
            }
            const bool queued = queue_event({
                RuntimeEventKind::connection_opened,
                event->connect.conn_handle,
                observed,
                adapter.transport_generation});
            if (!queued) {
                g_event_overflow.store(true, std::memory_order_release);
            }
            // Every link must enter Secure Connections immediately. A saved
            // owner's bond resumes without a PIN; an unknown peer on an owned
            // device reaches the closed-window passkey path and is terminated.
            if (!security_initiation_accepted(
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
            const auto adapter_disconnect_error =
                static_cast<CompanionGattAdapterError>(
                    companion_nimble_gatt_gap_event(
                        event, g_callback_adapter));
            if (adapter_disconnect_error ==
                CompanionGattAdapterError::wrong_connection) {
                // A second controller is terminated before it can enter any
                // runtime or pairing state. Its later disconnect callback
                // must not close or otherwise disturb the exact live session.
                return 0;
            }
            if (adapter_disconnect_error !=
                CompanionGattAdapterError::none) {
                g_event_overflow.store(true,
                                       std::memory_order_release);
                return 0;
            }
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
            if (g_suppress_next_adv_complete.exchange(
                    false, std::memory_order_acq_rel)) {
                return 0;
            }
            (void)queue_event({RuntimeEventKind::advertising_interrupted,
                              kCompanionBleInvalidConnectionHandle, observed});
            return 0;
        case BLE_GAP_EVENT_REPEAT_PAIRING:
            // Owned devices never reopen pairing or replace a bond. Only the
            // exact already-bonded owner may reconnect.
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
            bool callback_state_available = false;
            bool initial_claim_admitted = false;
            bool orphan_reconciliation_required = false;
            {
                PairingLock lock{pdMS_TO_TICKS(50)};
                if (lock.locked() && g_pairing_window != nullptr) {
                    callback_state_available = true;
                    const auto candidate = active_pairing_candidate(
                        event->enc_change.conn_handle);
                    const auto window_status = g_pairing_window->status();
                    exact_active_attempt =
                        candidate.transport_generation != 0 &&
                        window_status.phase ==
                            CompanionPairingWindowPhase::attempt_active;
                    if (exact_active_attempt) {
                        pairing_generation =
                            candidate.transport_generation;
                        if (exact_secure_bond &&
                            g_v1_owner_bridge != nullptr &&
                            g_v1_bond_adapter != nullptr) {
                            // The strict callback timestamp is admitted before
                            // any durable owner write. This reservation performs
                            // no OLED I/O and makes a created bond cleanup-owned
                            // even if inventory or owner publication fails.
                            const auto reservation = g_pairing_window->
                                reserve_secure_bond_terminal(
                                    observed, candidate);
                            if (reservation ==
                                CompanionPairingWindowError::none) {
                                const auto inventory =
                                    g_v1_bond_adapter->snapshot();
                                const auto owner_status =
                                    g_v1_owner_bridge->status();
                                initial_claim_admitted =
                                    inventory.error ==
                                        CompanionV1BondInventoryError::none &&
                                    inventory.bond_count == 1 &&
                                    valid_bond_identity(
                                        inventory.private_references[0]) &&
                                    !valid_bond_identity(
                                        inventory.private_references[1]) &&
                                    owner_status.phase ==
                                        CompanionV1BondOwnerPhase::
                                            closed_unowned &&
                                    g_v1_owner_bridge->accept_initial_bond(
                                        inventory.private_references[0])
                                        .accepted();
                            }
                            orphan_reconciliation_required =
                                !initial_claim_admitted;
                            if (initial_claim_admitted) {
                                g_boot_unowned = false;
                                set_pairable_advertising_state(false);
                            }
                        } else if (exact_secure_bond) {
                            orphan_reconciliation_required = true;
                        }
                    } else if (exact_secure_bond &&
                               g_v1_owner_bridge != nullptr &&
                               g_v1_owner_bridge->status().phase ==
                                   CompanionV1BondOwnerPhase::closed_unowned) {
                        // The serialized deadline may already have closed the
                        // window before NimBLE reports its late secure bond.
                        orphan_reconciliation_required = true;
                    }
                } else {
                    g_event_overflow.store(true,
                                           std::memory_order_release);
                    orphan_reconciliation_required = exact_secure_bond;
                }
            }
            if (orphan_reconciliation_required) {
                // Remove D1 immediately. The app owner will fully contain the
                // stack and reboot into exact orphan reconciliation; this link
                // is never forwarded to the protected GATT authorization path.
                set_pairable_advertising_state(false);
                g_orphan_reconciliation_required.store(
                    true, std::memory_order_release);
            }
            if (exact_active_attempt &&
                (!exact_secure_bond || initial_claim_admitted)) {
                (void)queue_event({
                    initial_claim_admitted
                        ? RuntimeEventKind::pairing_completed
                        : RuntimeEventKind::pairing_failed,
                    event->enc_change.conn_handle,
                    observed,
                    pairing_generation});
            }
            // Only a deadline-admitted and durably published initial owner, or
            // a non-pairing returning-owner link, may reach GATT evaluation.
            if (callback_state_available &&
                !orphan_reconciliation_required &&
                (!exact_active_attempt || initial_claim_admitted)) {
                (void)companion_nimble_gatt_gap_event(
                    event, g_callback_adapter);
            }
            if (!exact_secure_bond ||
                orphan_reconciliation_required ||
                !callback_state_available) {
                (void)terminate_connection_or_contain(
                    event->enc_change.conn_handle, observed);
            }
            return 0;
        }
        default: {
            const auto result = companion_nimble_gatt_gap_event(
                event, g_callback_adapter);
            const auto adapter = companion_nimble_gatt_adapter_status();
            if (event->type == BLE_GAP_EVENT_NOTIFY_TX &&
                adapter.connected &&
                adapter.lifecycle.application_authorized) {
                (void)queue_event({RuntimeEventKind::connection_authorized,
                                  event->notify_tx.conn_handle,
                                  observed});
            }
            return result;
        }
    }
}

CompanionBleRuntimeError apply_event(const RuntimeEvent& event) {
    switch (event.kind) {
        case RuntimeEventKind::host_sync: {
            // A fresh unowned boot gets exactly one automatic PIN window. It
            // is opened before advertising fields are configured, so a
            // pairable marker can never precede the locally visible PIN.
            if (g_boot_unowned) {
                PairingLock lock{pdMS_TO_TICKS(50)};
                if (!lock.locked() || g_pairing_window == nullptr ||
                    g_boot_pairing_attempted) {
                    return CompanionBleRuntimeError::invalid_argument;
                }
                g_boot_pairing_attempted = true;
                const auto opened =
                    g_pairing_window->open_unowned_boot_window(
                        event.observed_at_ms, 1);
                if (opened != CompanionPairingWindowError::none) {
                    return CompanionBleRuntimeError::invalid_argument;
                }
                g_pairable_advertising.store(true,
                                             std::memory_order_release);
            } else {
                set_pairable_advertising_state(false);
            }
            return g_runtime_owner.host_synced(event.observed_at_ms);
        }
        case RuntimeEventKind::host_reset:
            g_owner_connection_handle =
                kCompanionBleInvalidConnectionHandle;
            g_owner_gatt_transport_generation = 0;
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
                set_pairable_advertising_state(false);
            }
            return g_runtime_owner.host_reset();
        case RuntimeEventKind::advertising_interrupted:
            return g_runtime_owner.advertising_interrupted(
                event.observed_at_ms);
        case RuntimeEventKind::connection_opened: {
            const auto opened = g_runtime_owner.connection_opened(
                event.connection_handle, event.observed_at_ms);
            if (opened == CompanionBleRuntimeError::none) {
                g_owner_connection_handle = event.connection_handle;
                g_owner_gatt_transport_generation = event.transport_generation;
            }
            return opened;
        }
        case RuntimeEventKind::connection_closed: {
            {
                PairingLock lock{pdMS_TO_TICKS(50)};
                if (!lock.locked()) {
                    return CompanionBleRuntimeError::callback_queue_overflow;
                }
                if (g_pairing_window != nullptr) {
                    const auto phase = g_pairing_window->status().phase;
                    if (phase != CompanionPairingWindowPhase::closed &&
                        phase != CompanionPairingWindowPhase::faulted &&
                        event.transport_generation != 0) {
                        const auto result = g_pairing_window->disconnect(
                            event.observed_at_ms,
                            {event.connection_handle,
                             event.transport_generation});
                        if (result != CompanionPairingWindowError::none &&
                            result != CompanionPairingWindowError::
                                          window_expired) {
                            return CompanionBleRuntimeError::invalid_argument;
                        }
                        set_pairable_advertising_state(false);
                    }
                }
            }
            const auto closed = g_runtime_owner.connection_closed(
                event.connection_handle, event.observed_at_ms);
            if (closed == CompanionBleRuntimeError::none &&
                event.connection_handle == g_owner_connection_handle) {
                g_owner_connection_handle =
                    kCompanionBleInvalidConnectionHandle;
                g_owner_gatt_transport_generation = 0;
            }
            return closed;
        }
        case RuntimeEventKind::connection_termination_failed:
            return g_runtime_owner.connection_termination_failed(
                event.connection_handle);
        case RuntimeEventKind::connection_authorized:
            return g_runtime_owner.authorize_connection(
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
                set_pairable_advertising_state(false);
                return CompanionBleRuntimeError::none;
            }
            if (result == CompanionPairingWindowError::window_expired) {
                set_pairable_advertising_state(false);
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
            if (result == CompanionPairingWindowError::none ||
                result == CompanionPairingWindowError::window_expired) {
                set_pairable_advertising_state(false);
                return CompanionBleRuntimeError::none;
            }
            return CompanionBleRuntimeError::invalid_argument;
        }
    }
    return CompanionBleRuntimeError::invalid_argument;
}

}  // namespace

void observe_companion_verified_gatt_progress(
    std::uint16_t connection_handle,
    std::uint64_t transport_generation,
    std::uint64_t observed_at_ms) {
    if (connection_handle == kCompanionBleInvalidConnectionHandle ||
        transport_generation == 0) {
        g_event_overflow.store(true, std::memory_order_release);
        return;
    }
    const VerifiedGattProgress progress{
        connection_handle, observed_at_ms, transport_generation};
    if (g_verified_gatt_progress_queue == nullptr ||
        xQueueOverwrite(g_verified_gatt_progress_queue, &progress) != pdPASS) {
        g_event_overflow.store(true, std::memory_order_release);
    }
}

CompanionBleRuntimeError start_companion_nimble_runtime(
    std::uint64_t now_ms,
    CompanionPairingWindow& pairing_window,
    security::SecureRandomSource& random,
    StartupDisplayOwner& display) {
    if (g_pairing_mutex == nullptr) {
        g_pairing_mutex = xSemaphoreCreateMutexStatic(
            &g_pairing_mutex_control);
    }
    if (g_factory_reset_mutex == nullptr) {
        g_factory_reset_mutex = xSemaphoreCreateMutexStatic(
            &g_factory_reset_mutex_control);
    }
    if (g_pairing_mutex == nullptr || g_factory_reset_mutex == nullptr ||
        g_pairing_window != nullptr) {
        return CompanionBleRuntimeError::invalid_argument;
    }
    g_pairing_window = &pairing_window;
    if (g_event_queue == nullptr) {
        g_event_queue = xQueueCreateStatic(
            kCallbackQueueCapacity, sizeof(RuntimeEvent),
            g_event_queue_storage.data(), &g_event_queue_control);
    }
    if (g_verified_gatt_progress_queue == nullptr) {
        g_verified_gatt_progress_queue = xQueueCreateStatic(
            1, sizeof(VerifiedGattProgress),
            g_verified_gatt_progress_queue_storage.data(),
            &g_verified_gatt_progress_queue_control);
    }
    if (g_event_queue == nullptr || g_verified_gatt_progress_queue == nullptr) {
        return CompanionBleRuntimeError::callback_queue_overflow;
    }
    // The ordinary default NVS partition owns only the NimBLE bond and OTV1
    // record. Any initialization error fails closed; never erase and retry.
    if (nvs_flash_init() != ESP_OK) {
        return CompanionBleRuntimeError::invalid_argument;
    }
    const auto admission = companion_authorization_storage_preflight();
    if (admission != CompanionAuthorizationTargetAdmissionError::
                         nvs_encryption_not_configured) {
        return CompanionBleRuntimeError::invalid_argument;
    }
    static opentrail::targets::heltec_v4_bench::
        HeltecV4CompanionV1OwnerStorage owner_storage;
    static opentrail::targets::heltec_v4_bench::
        HeltecV4CompanionV1NimbleBondAdapter bond_adapter{random};
    static opentrail::targets::heltec_v4_bench::
        HeltecV4FactoryResetMarkerStorage reset_marker;
    static opentrail::targets::heltec_v4_bench::
        HeltecV4FactoryResetUserDomainStorage reset_user_domain;
    static opentrail::targets::heltec_v4_bench::
        HeltecV4FactoryResetNimbleBondStorage reset_bonds;
    static DeviceFactoryResetExecutor reset_executor{
        reset_marker, reset_user_domain, reset_bonds};
    static CompanionFactoryResetActionAuthority reset_action_authority{
        reset_executor};
    static CompanionRequestCoordinator request_coordinator{
        g_snapshot_authority, reset_action_authority};
    static CompanionV1BondOwnerBridge owner_bridge{
        owner_storage, bond_adapter, bond_adapter};
    static BootLocalCorrelationIssuer correlation_issuer{random};
    static CompanionV1GattAuthorizationAuthority authorization_authority{
        owner_bridge, 0};
    static CompanionGattAuthorizationCallbackAdapter callback_adapter{
        request_coordinator,
        companion_nimble_gatt_indication_port(),
        bond_adapter,
        correlation_issuer,
        authorization_authority};
    g_v1_bond_adapter = &bond_adapter;
    g_v1_owner_bridge = &owner_bridge;
    g_callback_adapter = &callback_adapter;
    g_factory_reset_executor = &reset_executor;
    g_factory_reset_marker = &reset_marker;
    g_factory_reset_action_authority = &reset_action_authority;
    g_factory_reset_bonds = &reset_bonds;
    g_startup_display = &display;
    g_boot_unowned = false;
    g_boot_pairing_attempted = false;
    g_boot_reset_receipt.store(0, std::memory_order_release);
    set_pairable_advertising_state(false);
    g_suppress_next_adv_complete.store(false, std::memory_order_release);
    g_orphan_reconciliation_required.store(false,
                                           std::memory_order_release);
    g_app_factory_reset_phase.store(
        CompanionAppFactoryResetPhase::idle,
        std::memory_order_release);
    g_app_factory_reset_response_started_ms.store(
        0, std::memory_order_release);
    g_owner_connection_handle = kCompanionBleInvalidConnectionHandle;
    g_owner_gatt_transport_generation = 0;
    return g_runtime_owner.start(now_ms, true);
}

CompanionBleRuntimeError service_companion_nimble_runtime(
    std::uint64_t now_ms) {
    auto app_reset_phase = g_app_factory_reset_phase.load(
        std::memory_order_acquire);
    if (app_reset_phase ==
        CompanionAppFactoryResetPhase::response_pending) {
        const auto started =
            g_app_factory_reset_response_started_ms.load(
                std::memory_order_acquire);
        if (started == 0 || now_ms < started ||
            now_ms - started >= kFactoryResetResponseDeadlineMs) {
            g_app_factory_reset_phase.store(
                CompanionAppFactoryResetPhase::response_unknown,
                std::memory_order_release);
            app_reset_phase =
                CompanionAppFactoryResetPhase::response_unknown;
        }
    }
    if (app_reset_phase ==
            CompanionAppFactoryResetPhase::response_confirmed ||
        app_reset_phase ==
            CompanionAppFactoryResetPhase::response_unknown) {
        set_pairable_advertising_state(false);
        if (g_startup_display != nullptr) {
            (void)g_startup_display->show_factory_reset_in_progress();
        }
        // Intent is already durable. Runtime containment performs cleanup
        // after host stop but before NimBLE deinit, while the store lock and
        // callbacks remain live. Any failure reboots into marker-first retry.
        (void)g_runtime_owner.callback_overflow();
        esp_restart();
        return CompanionBleRuntimeError::contained;
    }
    if (g_orphan_reconciliation_required.exchange(
            false, std::memory_order_acq_rel)) {
        // A secure bond exists without a durably accepted owner. Stop the
        // complete BLE stack in the serialized owner context, then reboot so
        // marker-first startup can remove the exact safe orphan before any
        // radio or protected GATT surface is restored.
        set_pairable_advertising_state(false);
        const auto contained = g_runtime_owner.callback_overflow();
        if (contained !=
            CompanionBleRuntimeError::callback_queue_overflow) {
            return contained;
        }
        esp_restart();
        return CompanionBleRuntimeError::contained;
    }
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
            result != CompanionBleRuntimeError::connection_in_use &&
            result != CompanionBleRuntimeError::wrong_connection &&
            result != CompanionBleRuntimeError::stale_restart) {
            return g_runtime_owner.callback_overflow();
        }
    }
    VerifiedGattProgress progress{};
    if (g_verified_gatt_progress_queue != nullptr &&
        xQueueReceive(g_verified_gatt_progress_queue, &progress, 0) == pdPASS &&
        progress.connection_handle == g_owner_connection_handle &&
        progress.transport_generation == g_owner_gatt_transport_generation) {
        const auto renewed = g_runtime_owner.renew_connection_window(
            progress.connection_handle, progress.observed_at_ms);
        if (renewed != CompanionBleRuntimeError::none &&
            renewed != CompanionBleRuntimeError::wrong_connection) {
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
    bool pairing_active = false;
    {
        PairingLock lock{pdMS_TO_TICKS(50)};
        if (!lock.locked() || g_pairing_window == nullptr) {
            return CompanionBleRuntimeError::callback_queue_overflow;
        }
        const auto phase = g_pairing_window->status().phase;
        pairing_active = phase == CompanionPairingWindowPhase::open ||
                         phase == CompanionPairingWindowPhase::attempt_active;
    }
    if (pairing_active &&
        status.phase == CompanionBleRuntimePhase::connected) {
        const auto renewed = g_runtime_owner.renew_connection_window(
            status.connection_handle, now_ms);
        if (renewed != CompanionBleRuntimeError::none) return renewed;
    }
    return g_runtime_owner.service_watchdog(now_ms);
}

CompanionBleRuntimeStatus companion_nimble_runtime_status() {
    return g_runtime_owner.status();
}

std::uint8_t companion_nimble_security_failure_stage() {
    return g_security_configuration_failure_stage.load(
        std::memory_order_acquire);
}

std::uint8_t companion_nimble_security_failure_detail() {
    return g_security_configuration_failure_detail.load(
        std::memory_order_acquire);
}

bool contain_companion_nimble_runtime_for_recovery() {
    set_pairable_advertising_state(false);
    if (g_runtime_owner.status().phase !=
        CompanionBleRuntimePhase::contained) {
        (void)g_runtime_owner.callback_overflow();
    }
    const auto status = g_runtime_owner.status();
    return status.phase == CompanionBleRuntimePhase::contained &&
           status.stack_shutdown_complete;
}

void observe_companion_app_factory_reset_command(
    bool response_pending,
    std::uint64_t now_ms) {
    // A queued response can represent an ordinary rejection. Only the reset
    // authority's durable-intent or outcome-unknown state starts containment.
    // Rejected, malformed, and otherwise noncommitted requests leave the
    // runtime fully operational.
    if (g_factory_reset_action_authority == nullptr ||
        !g_factory_reset_action_authority->status()
             .protected_operations_blocked) {
        return;
    }
    if (response_pending &&
        now_ms != 0) {
        std::uint64_t no_response_started = 0;
        if (!g_app_factory_reset_response_started_ms.compare_exchange_strong(
                no_response_started, now_ms,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            g_app_factory_reset_phase.store(
                CompanionAppFactoryResetPhase::response_unknown,
                std::memory_order_release);
            return;
        }
        auto expected = CompanionAppFactoryResetPhase::idle;
        if (g_app_factory_reset_phase.compare_exchange_strong(
                expected,
                CompanionAppFactoryResetPhase::response_pending,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return;
        }
    }
    g_app_factory_reset_phase.store(
        CompanionAppFactoryResetPhase::response_unknown,
        std::memory_order_release);
}

void observe_companion_app_factory_reset_response(bool confirmed) {
    auto expected = CompanionAppFactoryResetPhase::response_pending;
    (void)g_app_factory_reset_phase.compare_exchange_strong(
        expected,
        confirmed
            ? CompanionAppFactoryResetPhase::response_confirmed
            : CompanionAppFactoryResetPhase::response_unknown,
        std::memory_order_acq_rel,
        std::memory_order_acquire);
}

bool companion_app_factory_reset_blocks_protected_access() {
    return g_app_factory_reset_phase.load(std::memory_order_acquire) !=
           CompanionAppFactoryResetPhase::idle;
}

CompanionAppFactoryResetStatus companion_app_factory_reset_status() {
    const auto phase = g_app_factory_reset_phase.load(
        std::memory_order_acquire);
    return {phase, phase != CompanionAppFactoryResetPhase::idle};
}

bool acquire_companion_factory_reset_serialization() {
    return g_factory_reset_mutex != nullptr &&
           xSemaphoreTake(g_factory_reset_mutex,
                          pdMS_TO_TICKS(100)) == pdTRUE;
}

void release_companion_factory_reset_serialization() {
    if (g_factory_reset_mutex != nullptr) {
        (void)xSemaphoreGive(g_factory_reset_mutex);
    }
}

DeviceFactoryResetResult begin_companion_factory_reset() {
    FactoryResetLock reset_lock{pdMS_TO_TICKS(100)};
    if (!reset_lock.locked() || g_factory_reset_executor == nullptr ||
        g_factory_reset_executor->status().phase !=
            DeviceFactoryResetPhase::idle_old_state) {
        return {DeviceFactoryResetError::invalid_state,
                g_factory_reset_executor == nullptr
                    ? DeviceFactoryResetPhase::not_restored
                    : g_factory_reset_executor->status().phase};
    }

    // No marker write occurs until NimBLE host/controller containment is
    // complete. A shutdown failure preserves the old state and is rebooted.
    const auto contained = g_runtime_owner.callback_overflow();
    if (contained != CompanionBleRuntimeError::callback_queue_overflow ||
        !g_runtime_owner.status().stack_shutdown_complete) {
        return {DeviceFactoryResetError::invalid_state,
                g_factory_reset_executor->status().phase};
    }
    set_pairable_advertising_state(false);
    return g_factory_reset_executor->begin();
}

DeviceFactoryResetResult
begin_contained_companion_factory_reset_recovery() {
    FactoryResetLock reset_lock{pdMS_TO_TICKS(100)};
    const auto runtime_status = g_runtime_owner.status();
    if (!reset_lock.locked() || g_factory_reset_executor == nullptr ||
        runtime_status.phase != CompanionBleRuntimePhase::contained ||
        !runtime_status.stack_shutdown_complete) {
        return {DeviceFactoryResetError::invalid_state,
                g_factory_reset_executor == nullptr
                    ? DeviceFactoryResetPhase::not_restored
                    : g_factory_reset_executor->status().phase};
    }

    set_pairable_advertising_state(false);
    const auto phase = g_factory_reset_executor->status().phase;
    if (phase == DeviceFactoryResetPhase::idle_old_state) {
        return g_factory_reset_executor->begin();
    }
    if (phase == DeviceFactoryResetPhase::not_restored ||
        phase == DeviceFactoryResetPhase::reconciliation_required) {
        return g_factory_reset_executor->begin_confirmed_recovery();
    }
    return {DeviceFactoryResetError::invalid_state, phase};
}

CompanionPairingWindowError service_companion_pairing_window(
    std::uint64_t now_ms) {
    std::uint16_t terminate_handle{kCompanionBleInvalidConnectionHandle};
    CompanionPairingWindowError result{
        CompanionPairingWindowError::window_not_available};
    {
        PairingLock lock{pdMS_TO_TICKS(50)};
        if (!lock.locked() || g_pairing_window == nullptr) {
            return CompanionPairingWindowError::window_not_available;
        }
        result = g_pairing_window->service(now_ms);
        if (result == CompanionPairingWindowError::window_expired) {
            terminate_handle = g_pairing_connection_handle;
            set_pairable_advertising_state(false);
        }
    }
    if (terminate_handle != kCompanionBleInvalidConnectionHandle) {
        (void)terminate_connection_or_contain(terminate_handle, now_ms);
    } else if (result == CompanionPairingWindowError::window_expired &&
               g_runtime_owner.status().phase ==
                   CompanionBleRuntimePhase::advertising &&
               !g_runtime_port.set_pairable_advertising(false, true)) {
        (void)g_runtime_owner.callback_overflow();
        return CompanionPairingWindowError::faulted;
    }
    return result;
}

CompanionPairingWindowError fault_companion_pairing_window() {
    CompanionPairingWindowError result{
        CompanionPairingWindowError::window_not_available};
    {
        PairingLock lock{pdMS_TO_TICKS(50)};
        if (!lock.locked() || g_pairing_window == nullptr) {
            return CompanionPairingWindowError::window_not_available;
        }
        result = g_pairing_window->fault();
    }
    set_pairable_advertising_state(false);
    const auto runtime_phase = g_runtime_owner.status().phase;
    if (runtime_phase != CompanionBleRuntimePhase::dormant &&
        runtime_phase != CompanionBleRuntimePhase::contained) {
        (void)g_runtime_owner.callback_overflow();
    }
    return result;
}

CompanionPairingWindowStatus companion_pairing_window_status() {
    PairingLock lock{0};
    if (!lock.locked() || g_pairing_window == nullptr) return {};
    return g_pairing_window->status();
}

}  // namespace opentrail::target::heltec_v4_bench
