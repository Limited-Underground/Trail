#pragma once

#include <cstdint>

namespace opentrail::companion {

inline constexpr std::uint16_t kCompanionBleInvalidConnectionHandle = 0xFFFF;

enum class CompanionBleRuntimePhase : std::uint8_t {
    dormant = 0,
    waiting_for_host_sync,
    advertising,
    connected,
    restart_wait,
    contained,
};

enum class CompanionBleRuntimeError : std::uint8_t {
    none = 0,
    invalid_argument,
    already_started,
    reentrant_call,
    stack_init_failed,
    security_config_failed,
    service_registration_failed,
    host_task_start_failed,
    host_sync_out_of_order,
    advertising_config_failed,
    advertising_start_failed,
    connection_in_use,
    wrong_connection,
    restart_not_due,
    stale_restart,
    restart_exhausted,
    startup_timeout,
    host_reset,
    callback_queue_overflow,
    connection_termination_failed,
    stack_shutdown_failed,
    contained,
};

struct CompanionBleRuntimePolicy {
    std::uint64_t host_sync_timeout_ms{10000};
    std::uint64_t restart_delay_ms{1000};
    std::uint8_t max_restart_attempts{3};
    std::uint64_t public_link_window_ms{15000};
    std::uint64_t termination_ack_timeout_ms{2000};
};

struct CompanionBleRuntimeStatus {
    CompanionBleRuntimePhase phase{CompanionBleRuntimePhase::dormant};
    CompanionBleRuntimeError terminal_error{CompanionBleRuntimeError::none};
    bool authorization_claims_closed{true};
    bool normal_commands_closed{true};
    std::uint16_t connection_handle{kCompanionBleInvalidConnectionHandle};
    std::uint64_t restart_token{0};
    std::uint8_t restart_attempts{0};
    bool termination_pending{false};
};

// Target port for one immutable runtime owner. Advertising data must contain
// only the flags and exact public service UUID: no name, address, manufacturer
// data, public ID, group/user identity, or peer-derived value. initialize_stack
// and contain_stack own the exact NimBLE controller/host lifetime.
class CompanionBleRuntimePort {
public:
    virtual ~CompanionBleRuntimePort() = default;
    [[nodiscard]] virtual bool initialize_stack() = 0;
    [[nodiscard]] virtual bool configure_secure_connections_bonding() = 0;
    [[nodiscard]] virtual bool register_protected_service() = 0;
    [[nodiscard]] virtual bool start_host_task() = 0;
    [[nodiscard]] virtual bool configure_public_service_advertising() = 0;
    [[nodiscard]] virtual bool start_advertising() = 0;
    [[nodiscard]] virtual bool terminate_connection(
        std::uint16_t connection_handle) = 0;
    [[nodiscard]] virtual bool contain_stack() = 0;
};

// Single-owner, externally serialized state machine. This guard protects only
// synchronous callback reentry; it is not a task/thread synchronization
// primitive. Startup requires an installed fail-closed authorization path.
// Claims are then admitted by the GATT lifecycle while normal commands remain
// closed until the exact live connection is promoted.
class CompanionBleRuntimeOwner {
public:
    CompanionBleRuntimeOwner(CompanionBleRuntimePort& port,
                             CompanionBleRuntimePolicy policy = {});

    [[nodiscard]] CompanionBleRuntimeError start(
        std::uint64_t now_ms,
        bool authorization_path_ready);
    [[nodiscard]] CompanionBleRuntimeError host_synced(std::uint64_t now_ms);
    [[nodiscard]] CompanionBleRuntimeError connection_opened(
        std::uint16_t connection_handle,
        std::uint64_t now_ms);
    [[nodiscard]] CompanionBleRuntimeError connection_closed(
        std::uint16_t connection_handle,
        std::uint64_t now_ms);
    [[nodiscard]] CompanionBleRuntimeError advertising_interrupted(
        std::uint64_t now_ms);
    [[nodiscard]] CompanionBleRuntimeError service_restart(
        std::uint64_t expected_restart_token,
        std::uint64_t now_ms);
    [[nodiscard]] CompanionBleRuntimeError service_watchdog(
        std::uint64_t now_ms);
    [[nodiscard]] CompanionBleRuntimeError renew_connection_window(
        std::uint16_t connection_handle,
        std::uint64_t now_ms);
    [[nodiscard]] CompanionBleRuntimeError authorize_connection(
        std::uint16_t connection_handle);
    [[nodiscard]] CompanionBleRuntimeError host_reset();
    [[nodiscard]] CompanionBleRuntimeError callback_overflow();
    [[nodiscard]] CompanionBleRuntimeError connection_termination_failed(
        std::uint16_t connection_handle);
    [[nodiscard]] CompanionBleRuntimeStatus status() const;

private:
    [[nodiscard]] bool valid_policy() const;
    [[nodiscard]] bool deadline_reached(std::uint64_t now_ms,
                                        std::uint64_t deadline_ms) const;
    [[nodiscard]] bool add_deadline(std::uint64_t now_ms,
                                    std::uint64_t delay_ms,
                                    std::uint64_t& deadline) const;
    [[nodiscard]] CompanionBleRuntimeError contain(
        CompanionBleRuntimeError error);
    [[nodiscard]] CompanionBleRuntimeError next_restart(
        std::uint64_t now_ms);
    [[nodiscard]] bool enter_operation();
    void leave_operation();

    CompanionBleRuntimePort& port_;
    CompanionBleRuntimePolicy policy_{};
    CompanionBleRuntimeStatus status_{};
    std::uint64_t host_sync_deadline_ms_{0};
    std::uint64_t restart_deadline_ms_{0};
    std::uint64_t public_link_deadline_ms_{0};
    std::uint64_t termination_ack_deadline_ms_{0};
    std::uint64_t next_restart_token_{1};
    bool operation_active_{false};
    bool reentry_observed_{false};
};

}  // namespace opentrail::companion
