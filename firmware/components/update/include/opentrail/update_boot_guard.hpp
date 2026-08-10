#pragma once

#include <cstdint>

namespace opentrail::update {

enum class ImageSlot : std::uint8_t {
    slot_a = 1,
    slot_b = 2,
};

enum class UpdateState : std::uint8_t {
    idle = 0,
    staged,
    pending_reboot,
    trial,
    confirmed,
    rollback_required,
    rolled_back,
};

enum class RollbackReason : std::uint8_t {
    none = 0,
    boot_mismatch,
    confirmation_timeout,
    boot_attempt_limit,
    explicit_health_failure,
};

enum class UpdateGuardError : std::uint8_t {
    none = 0,
    invalid_state,
    invalid_configuration,
    invalid_candidate,
    verification_required,
    boot_mismatch,
    boot_attempt_limit,
    insufficient_health,
    insufficient_stable_time,
    confirmation_timeout,
    clock_regression,
    rollback_mismatch,
};

enum class TrialHealth : std::uint32_t {
    runtime_started = 1U << 0U,
    watchdog_healthy = 1U << 1U,
    configuration_loaded = 1U << 2U,
    protected_state_healthy = 1U << 3U,
    radio_healthy = 1U << 4U,
    local_interface_healthy = 1U << 5U,
    power_safe = 1U << 6U,
};

[[nodiscard]] constexpr std::uint32_t health_bit(TrialHealth health) {
    return static_cast<std::uint32_t>(health);
}

inline constexpr std::uint32_t kAllTrialHealthBits =
    health_bit(TrialHealth::runtime_started) |
    health_bit(TrialHealth::watchdog_healthy) |
    health_bit(TrialHealth::configuration_loaded) |
    health_bit(TrialHealth::protected_state_healthy) |
    health_bit(TrialHealth::radio_healthy) |
    health_bit(TrialHealth::local_interface_healthy) |
    health_bit(TrialHealth::power_safe);

struct UpdateGuardPolicy {
    std::uint32_t hardware_id{0};
    std::uint32_t current_version{0};
    ImageSlot current_slot{ImageSlot::slot_a};
    std::uint32_t required_health_mask{0};
    std::uint64_t minimum_stable_ms{0};
    std::uint64_t confirmation_deadline_ms{0};
    std::uint8_t maximum_trial_boots{0};
    std::uint32_t maximum_image_bytes{0};
};

struct VerifiedUpdateCandidate {
    std::uint32_t hardware_id{0};
    std::uint32_t version{0};
    ImageSlot target_slot{ImageSlot::slot_b};
    std::uint32_t image_bytes{0};
    bool authenticity_verified{false};
    bool integrity_verified{false};
    bool compatibility_verified{false};
    bool rollback_image_verified{false};
};

struct CandidateWriteEvidence {
    std::uint32_t version{0};
    ImageSlot slot{ImageSlot::slot_b};
    bool full_readback_verified{false};
    bool boot_selection_persisted{false};
};

struct BootObservation {
    std::uint32_t boot_session_id{0};
    std::uint32_t version{0};
    ImageSlot slot{ImageSlot::slot_a};
    std::uint64_t monotonic_ms{0};
};

struct UpdateGuardStatus {
    bool running{false};
    UpdateState state{UpdateState::idle};
    RollbackReason rollback_reason{RollbackReason::none};
    VerifiedUpdateCandidate candidate{};
    std::uint8_t trial_boots{0};
    std::uint32_t trial_boot_session_id{0};
    std::uint64_t trial_started_ms{0};
    std::uint64_t last_monotonic_ms{0};
    std::uint32_t observed_health_mask{0};
};

// Pure update lifecycle guard. Signature verification, image writing, boot-slot
// switching, persistence, and rollback execution remain adapter responsibilities.
class UpdateBootGuard {
public:
    [[nodiscard]] UpdateGuardError start(const UpdateGuardPolicy& policy);
    void stop();

    [[nodiscard]] UpdateGuardError stage(
        const VerifiedUpdateCandidate& candidate);
    [[nodiscard]] UpdateGuardError mark_written(
        const CandidateWriteEvidence& evidence);
    [[nodiscard]] UpdateGuardError cancel_staged();
    [[nodiscard]] UpdateGuardError begin_boot(
        const BootObservation& observation);
    [[nodiscard]] UpdateGuardError report_health(
        std::uint32_t boot_session_id,
        std::uint32_t passing_health_mask,
        std::uint64_t now_ms);
    [[nodiscard]] UpdateGuardError tick(std::uint64_t now_ms);
    [[nodiscard]] UpdateGuardError confirm(
        std::uint32_t boot_session_id,
        std::uint64_t now_ms);
    [[nodiscard]] UpdateGuardError request_rollback(
        RollbackReason reason);
    [[nodiscard]] UpdateGuardError complete_rollback(
        const BootObservation& observation);

    [[nodiscard]] UpdateGuardStatus status() const;

private:
    [[nodiscard]] UpdateGuardError advance_clock(std::uint64_t now_ms);
    void require_rollback(RollbackReason reason);

    UpdateGuardPolicy policy_{};
    UpdateGuardStatus status_{};
};

}  // namespace opentrail::update


