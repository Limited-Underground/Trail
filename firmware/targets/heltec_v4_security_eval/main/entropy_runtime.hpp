#pragma once
#include <atomic>
#include <cstdint>
#include "heltec_v4_secure_random.hpp"
#include "opentrail/serialized_secure_random.hpp"
namespace opentrail::target::heltec_v4_security_eval {
enum class EntropyRuntimeError : std::uint8_t {
    none, configuration_rejected, controller_not_idle, init_failed,
    enable_failed, readiness_failed, drain_timeout, disable_failed,
    deinit_failed, invalid_budget
};
// Evaluation-only exclusive controller owner, not a NimBLE host or ISR API.
// Initialize required NVS before start; call sodium_init only after start succeeds.
// All random users must finish before destruction. Only this runtime may change
// controller state. Concurrent start/stop returns false without altering error().
// stop bounds guard draining; ESP-IDF controller calls themselves have no proven
// wall-clock bound. On drain timeout keep the runtime/source alive and retry stop.
class EntropyRuntime final {
public:
    EntropyRuntime();
    bool start();
    bool stop(std::uint32_t drain_budget_us = 20000);
    security::SecureRandomSource& random() { return guarded_; }
    EntropyRuntimeError error() const { return error_; } // lifecycle-owner snapshot
private:
    static bool ready_probe(void*) noexcept;
    bool stop_locked(std::uint32_t);
    heltec_v4_bench::HeltecV4SecureRandom raw_;
    security::SerializedSecureRandomSource guarded_;
    std::atomic_flag lifecycle_ = ATOMIC_FLAG_INIT;
    bool owns_controller_{false};
    EntropyRuntimeError error_{EntropyRuntimeError::none};
};
}
