#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "opentrail/companion_ble_runtime_owner.hpp"

namespace {

using namespace opentrail::companion;

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) fail(message);
}

class FakePort final : public CompanionBleRuntimePort {
public:
    bool initialize_stack() override {
        calls.emplace_back("init");
        maybe_reenter("init");
        return init_ok;
    }
    bool configure_secure_connections_bonding() override {
        calls.emplace_back("security");
        maybe_reenter("security");
        return security_ok;
    }
    bool register_protected_service() override {
        calls.emplace_back("service");
        maybe_reenter("service");
        return service_ok;
    }
    bool start_host_task() override {
        calls.emplace_back("host");
        maybe_reenter("host");
        return host_ok;
    }
    bool configure_public_service_advertising() override {
        calls.emplace_back("adv_config");
        maybe_reenter("adv_config");
        return adv_config_ok;
    }
    bool start_advertising() override {
        calls.emplace_back("adv_start");
        maybe_reenter("adv_start");
        if (advertise_failures != 0) {
            --advertise_failures;
            return false;
        }
        return advertise_ok;
    }
    void terminate_connection(std::uint16_t handle) override {
        calls.emplace_back("terminate");
        terminated = handle;
        maybe_reenter("terminate");
    }
    bool contain_stack() override {
        calls.emplace_back("contain");
        ++contain_calls;
        maybe_reenter("contain");
        return contain_ok;
    }

    void maybe_reenter(const char* call) {
        if (reenter_at == call && owner != nullptr) {
            reenter_at.clear();
            reentry_result = owner->service_watchdog(0);
        }
    }

    bool init_ok{true};
    bool security_ok{true};
    bool service_ok{true};
    bool host_ok{true};
    bool adv_config_ok{true};
    bool advertise_ok{true};
    bool contain_ok{true};
    std::uint8_t advertise_failures{0};
    std::string reenter_at{};
    CompanionBleRuntimeOwner* owner{nullptr};
    CompanionBleRuntimeError reentry_result{CompanionBleRuntimeError::none};
    std::uint16_t terminated{kCompanionBleInvalidConnectionHandle};
    std::uint32_t contain_calls{0};
    std::vector<std::string> calls{};
};

CompanionBleRuntimeOwner ready_owner(FakePort& port) {
    CompanionBleRuntimeOwner owner{port, {10, 5, 3}};
    require(owner.start(1, true) == CompanionBleRuntimeError::none,
            "owner start");
    require(owner.host_synced(2) == CompanionBleRuntimeError::none,
            "host sync");
    return owner;
}

void test_start_requires_closed_authorization_and_policy() {
    FakePort port{};
    CompanionBleRuntimeOwner owner{port, {10, 5, 3}};
    require(owner.start(1, false) ==
                CompanionBleRuntimeError::invalid_argument,
            "open authorization rejected");
    require(port.calls.empty(), "invalid start no port calls");
    FakePort second_port{};
    CompanionBleRuntimeOwner invalid{second_port, {0, 5, 3}};
    require(invalid.start(1, true) ==
                CompanionBleRuntimeError::invalid_argument,
            "invalid policy rejected");
}

void test_exact_start_order_and_permanent_closed_status() {
    FakePort port{};
    CompanionBleRuntimeOwner owner{port, {10, 5, 3}};
    require(owner.start(100, true) == CompanionBleRuntimeError::none,
            "start accepted");
    require(port.calls == std::vector<std::string>{"init", "security",
                                                   "service", "host"},
            "exact start order");
    const auto status = owner.status();
    require(status.phase == CompanionBleRuntimePhase::waiting_for_host_sync,
            "waiting sync");
    require(status.authorization_claims_closed &&
                status.normal_commands_closed,
            "all commands closed");
    require(owner.start(101, true) ==
                CompanionBleRuntimeError::already_started,
            "one owner start");
}

void test_each_partial_start_failure_contains() {
    for (int failure = 0; failure < 4; ++failure) {
        FakePort port{};
        if (failure == 0) port.init_ok = false;
        if (failure == 1) port.security_ok = false;
        if (failure == 2) port.service_ok = false;
        if (failure == 3) port.host_ok = false;
        CompanionBleRuntimeOwner owner{port, {10, 5, 3}};
        require(owner.start(1, true) != CompanionBleRuntimeError::none,
                "partial failure rejected");
        require(owner.status().phase == CompanionBleRuntimePhase::contained,
                "partial failure contained");
        require(port.contain_calls == 1, "contain exactly once");
    }
}

void test_sync_configures_then_advertises() {
    FakePort port{};
    CompanionBleRuntimeOwner owner{port, {10, 5, 3}};
    require(owner.start(1, true) == CompanionBleRuntimeError::none,
            "start");
    require(owner.host_synced(2) == CompanionBleRuntimeError::none,
            "sync");
    require(owner.status().phase == CompanionBleRuntimePhase::advertising,
            "advertising phase");
    require(port.calls[4] == "adv_config" && port.calls[5] == "adv_start",
            "advertising order");
}

void test_sync_failures_and_deadline_contain() {
    FakePort config_port{};
    config_port.adv_config_ok = false;
    CompanionBleRuntimeOwner config_owner{config_port, {10, 5, 3}};
    require(config_owner.start(1, true) == CompanionBleRuntimeError::none,
            "config start");
    require(config_owner.host_synced(2) ==
                CompanionBleRuntimeError::advertising_config_failed,
            "config failure");
    FakePort late_port{};
    CompanionBleRuntimeOwner late_owner{late_port, {10, 5, 3}};
    require(late_owner.start(1, true) == CompanionBleRuntimeError::none,
            "late start");
    require(late_owner.host_synced(11) ==
                CompanionBleRuntimeError::startup_timeout,
            "exact deadline closed");
}

void test_one_connection_and_exact_disconnect() {
    FakePort port{};
    auto owner = ready_owner(port);
    require(owner.connection_opened(7) == CompanionBleRuntimeError::none,
            "first connection");
    require(owner.connection_opened(8) ==
                CompanionBleRuntimeError::connection_in_use,
            "second connection rejected");
    require(port.terminated == 8, "second exact terminated");
    require(owner.connection_closed(8, 10) ==
                CompanionBleRuntimeError::wrong_connection,
            "wrong disconnect rejected");
    require(owner.connection_closed(7, 10) == CompanionBleRuntimeError::none,
            "exact disconnect accepted");
}

void test_disconnect_restart_due_stale_and_success() {
    FakePort port{};
    auto owner = ready_owner(port);
    require(owner.connection_opened(7) == CompanionBleRuntimeError::none,
            "connect");
    require(owner.connection_closed(7, 10) == CompanionBleRuntimeError::none,
            "disconnect");
    const auto token = owner.status().restart_token;
    require(token != 0, "restart token");
    require(owner.service_restart(token + 1, 15) ==
                CompanionBleRuntimeError::stale_restart,
            "stale token rejected");
    require(owner.service_restart(token, 14) ==
                CompanionBleRuntimeError::restart_not_due,
            "early restart rejected");
    require(owner.service_restart(token, 15) == CompanionBleRuntimeError::none,
            "restart success");
    require(owner.status().phase == CompanionBleRuntimePhase::advertising,
            "advertising restored");
    require(owner.status().restart_attempts == 0,
            "successful restart clears attempts");
}

void test_restart_attempts_are_capped_and_tokens_rotate() {
    FakePort port{};
    auto owner = ready_owner(port);
    require(owner.connection_opened(7) == CompanionBleRuntimeError::none,
            "connect");
    require(owner.connection_closed(7, 10) == CompanionBleRuntimeError::none,
            "disconnect");
    port.advertise_failures = 3;
    auto token = owner.status().restart_token;
    require(owner.service_restart(token, 15) == CompanionBleRuntimeError::none,
            "first failure scheduled");
    const auto second = owner.status().restart_token;
    require(second != token, "token rotated");
    require(owner.service_restart(token, 20) ==
                CompanionBleRuntimeError::stale_restart,
            "old callback stale");
    require(owner.service_restart(second, 20) == CompanionBleRuntimeError::none,
            "second failure scheduled");
    token = owner.status().restart_token;
    require(owner.service_restart(token, 25) ==
                CompanionBleRuntimeError::restart_exhausted,
            "third failure terminal");
    require(owner.status().phase == CompanionBleRuntimePhase::contained,
            "restart exhaustion contained");
}

void test_watchdog_and_host_reset_are_terminal() {
    FakePort timeout_port{};
    CompanionBleRuntimeOwner timeout_owner{timeout_port, {10, 5, 3}};
    require(timeout_owner.start(1, true) == CompanionBleRuntimeError::none,
            "watchdog start");
    require(timeout_owner.service_watchdog(11) ==
                CompanionBleRuntimeError::startup_timeout,
            "watchdog timeout");
    FakePort reset_port{};
    auto reset_owner = ready_owner(reset_port);
    require(reset_owner.host_reset() == CompanionBleRuntimeError::host_reset,
            "host reset contained");
    require(reset_owner.status().phase == CompanionBleRuntimePhase::contained,
            "reset terminal");
    require(reset_owner.host_reset() == CompanionBleRuntimeError::contained,
            "repeated reset is idempotent");
    require(reset_owner.callback_overflow() ==
                CompanionBleRuntimeError::contained,
            "overflow after containment is idempotent");
    require(reset_owner.service_watchdog(100) ==
                CompanionBleRuntimeError::contained,
            "watchdog after containment is idempotent");
    require(reset_port.contain_calls == 1, "stack contained exactly once");
}

void test_failed_denied_connection_termination_is_terminal() {
    FakePort port{};
    auto owner = ready_owner(port);
    require(owner.connection_opened(7) == CompanionBleRuntimeError::none,
            "termination failure connection");
    require(owner.connection_termination_failed(8) ==
                CompanionBleRuntimeError::wrong_connection,
            "wrong termination failure rejected");
    require(owner.connection_termination_failed(7) ==
                CompanionBleRuntimeError::connection_termination_failed,
            "exact termination failure contained");
    require(owner.status().phase == CompanionBleRuntimePhase::contained,
            "termination failure terminal without disconnect callback");
    require(port.contain_calls == 1, "termination failure contain once");
}

void test_shutdown_failure_is_terminal_and_attempted_once() {
    FakePort port{};
    auto owner = ready_owner(port);
    port.contain_ok = false;
    require(owner.host_reset() ==
                CompanionBleRuntimeError::stack_shutdown_failed,
            "shutdown failure surfaced");
    require(owner.status().terminal_error ==
                CompanionBleRuntimeError::stack_shutdown_failed,
            "shutdown failure latched");
    require(owner.callback_overflow() == CompanionBleRuntimeError::contained,
            "shutdown failure containment remains idempotent");
    require(port.contain_calls == 1, "failed shutdown attempted once");
}

void test_each_start_callback_reentry_contains() {
    for (const char* call : {"init", "security", "service", "host"}) {
        FakePort port{};
        CompanionBleRuntimeOwner owner{port, {10, 5, 3}};
        port.owner = &owner;
        port.reenter_at = call;
        require(owner.start(1, true) ==
                    CompanionBleRuntimeError::reentrant_call,
                "startup reentry outer contained");
        require(port.reentry_result ==
                    CompanionBleRuntimeError::reentrant_call,
                "startup reentry inner rejected");
        require(owner.status().phase == CompanionBleRuntimePhase::contained,
                "startup reentry terminal");
        require(port.contain_calls == 1, "startup reentry contain once");
    }
}

void test_advertising_and_termination_reentry_contains() {
    for (const char* call : {"adv_config", "adv_start"}) {
        FakePort port{};
        CompanionBleRuntimeOwner owner{port, {10, 5, 3}};
        port.owner = &owner;
        require(owner.start(1, true) == CompanionBleRuntimeError::none,
                "advertising reentry start");
        port.reenter_at = call;
        require(owner.host_synced(2) ==
                    CompanionBleRuntimeError::reentrant_call,
                "advertising reentry contained");
        require(owner.status().phase == CompanionBleRuntimePhase::contained,
                "advertising reentry terminal");
    }

    FakePort port{};
    auto owner = ready_owner(port);
    port.owner = &owner;
    require(owner.connection_opened(7) == CompanionBleRuntimeError::none,
            "termination reentry first connection");
    port.reenter_at = "terminate";
    require(owner.connection_opened(8) ==
                CompanionBleRuntimeError::reentrant_call,
            "termination reentry contained");
    require(owner.status().phase == CompanionBleRuntimePhase::contained,
            "termination reentry terminal");
}

}  // namespace

int main() {
    test_start_requires_closed_authorization_and_policy();
    test_exact_start_order_and_permanent_closed_status();
    test_each_partial_start_failure_contains();
    test_sync_configures_then_advertises();
    test_sync_failures_and_deadline_contain();
    test_one_connection_and_exact_disconnect();
    test_disconnect_restart_due_stale_and_success();
    test_restart_attempts_are_capped_and_tokens_rotate();
    test_watchdog_and_host_reset_are_terminal();
    test_failed_denied_connection_termination_is_terminal();
    test_shutdown_failure_is_terminal_and_attempted_once();
    test_each_start_callback_reentry_contains();
    test_advertising_and_termination_reentry_contains();
    std::cout << "companion BLE runtime owner tests passed: 13 groups\n";
    return 0;
}
