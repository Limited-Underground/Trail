#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "opentrail/single_repeater_replay_coordinator.hpp"

namespace opentrail::delivery {

// Host-only composition contract. Providers model trusted evidence; these
// interfaces do not authenticate packets or qualify any target storage/radio.
enum class RepeaterLifecycleState : std::uint8_t {
    disabled, recovering, awaiting_consent, active, suspended, contained, shutdown
};

enum class RepeaterLifecycleFault : std::uint8_t {
    none, wrong_state, invalid_configuration, invalid_request, reentrant,
    clock_regression, generation_exhausted, authority_changed, expired,
    role_storage, replay_storage, frame_rejected, suspended, radio_unavailable,
    uncertain_submission, rearm_failed, admission_uncertain, bridge_required,
    pending_busy
};

struct RepeaterLifecycleScope {
    std::uint64_t instance_id{0};
    std::uint64_t generation{0};
    std::uint64_t group_context_id{0};
    std::uint32_t group_epoch{0};
};

struct RepeaterLifecycleRole {
    std::uint64_t group_context_id{0};
    std::uint32_t group_epoch{0};
    std::uint64_t generation{0};
    bool enabled{false};
};

enum class RepeaterRoleStorageError : std::uint8_t {
    none, not_found, io_failure, commit_uncertain
};

class RepeaterLifecycleRoleStorage {
public:
    virtual ~RepeaterLifecycleRoleStorage() = default;
    virtual RepeaterRoleStorageError read(RepeaterLifecycleRole& role) noexcept = 0;
    virtual RepeaterRoleStorageError write(
        const RepeaterLifecycleRole& role) noexcept = 0;
};

struct RepeaterLifecycleSnapshot {
    RepeaterLifecycleScope scope{};
    RepeaterLifecycleRole role{};
    std::uint64_t deadline_ms{0};
    std::uint64_t readiness_generation{0};
    bool membership_current{false};
    bool protected_namespace_verified{false};
    bool airtime_ready{false};
};

enum class RepeaterLifecycleOperation : std::uint8_t { recover, disable };

struct RepeaterLifecycleRequest {
    RepeaterLifecycleSnapshot authority{};
    bool first_use{false};
    bool enable_role{false};
};

struct RepeaterLifecycleConsent {
    RepeaterLifecycleScope scope{};
    std::uint64_t role_generation{0};
    std::uint64_t readiness_generation{0};
    std::uint64_t deadline_ms{0};
};

// All providers/storage must outlive the nonmovable controller. Calls are
// serialized, finite and noexcept; unavailability is reported, not an exception.
// A true result is a synthetic trusted-provider obligation, never crypto proof.
class RepeaterLifecycleProvider {
public:
    virtual ~RepeaterLifecycleProvider() = default;
    virtual bool consume_request(
        RepeaterLifecycleOperation operation, std::uint64_t handle,
        const RepeaterLifecycleScope& scope, std::uint64_t now_ms,
        RepeaterLifecycleRequest& request) noexcept = 0;
    // Revalidate the already-consumed transaction, including its original
    // scope/deadline. This does not substitute a new desired role or grant.
    virtual bool request_current(
        RepeaterLifecycleOperation operation, std::uint64_t handle,
        const RepeaterLifecycleRequest& request, std::uint64_t now_ms) noexcept = 0;
    virtual bool current(
        const RepeaterLifecycleSnapshot& authority,
        std::uint64_t now_ms) noexcept = 0;
    virtual bool consume_consent(
        std::uint64_t handle, const RepeaterLifecycleSnapshot& authority,
        std::uint64_t now_ms, RepeaterLifecycleConsent& consent) noexcept = 0;
    virtual bool verify_frame(
        std::uint64_t handle, const RepeaterLifecycleSnapshot& authority,
        radio::ByteView immutable_copy, std::uint64_t now_ms,
        VerifiedForwardingMetadata& metadata) noexcept = 0;
};

struct RepeaterLifecycleConfiguration {
    std::uint64_t instance_id{0};
    std::uint64_t initial_generation{1};
    std::uint64_t local_alias{0};
    std::uint64_t group_context_id{0};
    std::uint32_t group_epoch{0};
    std::uint32_t duplicate_retention_ms{0};
    SingleRepeaterPolicy policy{};
    std::size_t maximum_frame_bytes{radio::kMaximumFrameBytes};
    bool bridge_mode{false};
    SingleRepeaterAdmissionGate* admission_gate{nullptr};
    std::uint64_t initial_candidate_id{1};
};

struct RepeaterLifecycleStatus {
    RepeaterLifecycleState state{RepeaterLifecycleState::disabled};
    RepeaterLifecycleFault fault{RepeaterLifecycleFault::none};
    std::uint64_t generation{0};
    std::uint64_t persisted_generation{0};
    std::size_t queue_depth{0};
    bool pending{false};
    std::size_t occupied_frames{0};
    bool claim_in_progress{false};
};

struct RepeaterLifecycleAdmission {
    RepeaterLifecycleFault error{RepeaterLifecycleFault::wrong_state};
    SingleRepeaterDecision forwarding{};
    bool replay_state_persisted{false};
    std::uint64_t persisted_generation{0};
};

struct RepeaterLifecycleCandidate {
    RepeaterLifecycleFault error{RepeaterLifecycleFault::wrong_state};
    std::uint64_t generation{0};
    ExactForwardedFrameResult forwarding{};
};

// Metadata only. Payload ownership remains private to the fixed-mode bridge.
struct RepeaterLifecycleDispatchTicket {
    RepeaterLifecycleScope scope{};
    std::uint64_t role_generation{0};
    std::uint64_t readiness_generation{0};
    std::uint64_t candidate_id{0};
    ForwardingProvenance provenance{};
    std::uint64_t authority_deadline_ms{0};
    std::uint64_t consent_deadline_ms{0};
    std::size_t frame_bytes{0};
};

struct RepeaterLifecyclePrepared {
    RepeaterLifecycleFault error{RepeaterLifecycleFault::wrong_state};
    bool has_ticket{false};
    RepeaterLifecycleDispatchTicket ticket{};
    std::uint8_t expired_frames_dropped{0};
};

class RepeaterLifecycleClaimCallback {
public:
    virtual ~RepeaterLifecycleClaimCallback() = default;
    // Synchronous finite host seam. Copy bytes before return if needed; never
    // retain the borrowed pointer. Invocation is not RF/delivery acceptance.
    virtual void invoke(const RepeaterLifecycleDispatchTicket& ticket,
                        radio::ByteView borrowed_frame) noexcept = 0;
};

struct RepeaterLifecycleClaimResult {
    RepeaterLifecycleFault error{RepeaterLifecycleFault::wrong_state};
    bool callback_invoked{false};
};

struct RepeaterLifecyclePruned {
    RepeaterLifecycleFault error{RepeaterLifecycleFault::wrong_state};
    std::uint8_t queued_dropped{0};
    bool pending_dropped{false};
};

enum class RepeaterLifecycleEvent : std::uint8_t {
    suspend, authority_changed, radio_unavailable, uncertain_submission, rearm_failed
};

class RepeaterLifecycleController {
public:
    RepeaterLifecycleController(
        const RepeaterLifecycleConfiguration& configuration,
        RepeaterLifecycleProvider& provider, RepeaterLifecycleRoleStorage& roles,
        DuplicateCheckpointStorage& replay_storage) noexcept;
    RepeaterLifecycleController(const RepeaterLifecycleController&) = delete;
    RepeaterLifecycleController& operator=(const RepeaterLifecycleController&) = delete;
    RepeaterLifecycleController(RepeaterLifecycleController&&) = delete;
    RepeaterLifecycleController& operator=(RepeaterLifecycleController&&) = delete;

    RepeaterLifecycleFault recover(std::uint64_t request, std::uint64_t now_ms) noexcept;
    RepeaterLifecycleFault consent(std::uint64_t handle, std::uint64_t now_ms) noexcept;
    RepeaterLifecycleAdmission admit(
        std::uint64_t handle, radio::ByteView bytes, std::uint64_t now_ms) noexcept;
    // Observation only. There is no radio submission/claim/rearm/debit here.
    RepeaterLifecycleCandidate take_candidate(std::uint64_t now_ms) noexcept;
    RepeaterLifecyclePrepared prepare_dispatch(std::uint64_t now_ms) noexcept;
    RepeaterLifecycleClaimResult claim_dispatch(
        const RepeaterLifecycleDispatchTicket& ticket, std::uint64_t now_ms,
        RepeaterLifecycleClaimCallback& callback) noexcept;
    RepeaterLifecyclePruned prune_expired(std::uint64_t now_ms) noexcept;
    RepeaterLifecycleFault notify(
        std::uint64_t generation, RepeaterLifecycleEvent event,
        std::uint64_t now_ms) noexcept;
    RepeaterLifecycleFault disable(std::uint64_t request, std::uint64_t now_ms) noexcept;
    RepeaterLifecycleFault shutdown() noexcept;
    [[nodiscard]] RepeaterLifecycleStatus status() const noexcept;

private:
    struct Bundle {
        DuplicateWindow duplicates;
        DuplicateCheckpointStore store;
        SingleRepeaterForwarder forwarder;
        SingleRepeaterReplayCoordinator coordinator;
        Bundle(const RepeaterLifecycleConfiguration& c,
               DuplicateCheckpointStorage& storage,
               SingleRepeaterAdmissionGate* gate) noexcept;
        Bundle(const Bundle&) = delete;
        Bundle& operator=(const Bundle&) = delete;
    };
    class BridgeAdmissionGate final : public SingleRepeaterAdmissionGate {
    public:
        explicit BridgeAdmissionGate(RepeaterLifecycleController& owner) noexcept : owner_(owner) {}
        SingleRepeaterAdmission evaluate(
            const VerifiedForwardingMetadata& metadata, std::size_t frame_bytes,
            std::uint64_t now_ms) noexcept override;
    private:
        RepeaterLifecycleController& owner_;
    };
    struct Pending {
        ExactForwardedFrame frame{};
        RepeaterLifecycleDispatchTicket ticket{};
        bool ticket_live{true};
    };
    struct CallGuard {
        bool& busy;
        explicit CallGuard(bool& value) noexcept : busy(value) { busy = true; }
        ~CallGuard() { busy = false; }
    };

    [[nodiscard]] RepeaterLifecycleScope scope() const noexcept;
    RepeaterLifecycleFault observe_time(std::uint64_t now_ms) noexcept;
    RepeaterLifecycleFault validate_current(std::uint64_t now_ms) noexcept;
    bool valid_snapshot(const RepeaterLifecycleSnapshot& s,
                        std::uint64_t now_ms, bool enabled) const noexcept;
    bool retire(RepeaterLifecycleState state, RepeaterLifecycleFault fault) noexcept;
    bool save_role(const RepeaterLifecycleRole& role) noexcept;
    static bool age_valid(const ForwardingProvenance& provenance,
                          std::uint64_t now_ms) noexcept;

    const RepeaterLifecycleConfiguration configuration_;
    RepeaterLifecycleProvider& provider_;
    RepeaterLifecycleRoleStorage& roles_;
    DuplicateCheckpointStorage& replay_storage_;
    BridgeAdmissionGate bridge_gate_{*this};
    std::optional<Bundle> bundle_{};
    std::optional<Pending> pending_{};
    std::array<std::uint8_t, radio::kMaximumFrameBytes> scratch_{};
    RepeaterLifecycleSnapshot bound_{};
    std::uint64_t lease_deadline_ms_{0};
    std::uint64_t generation_{0};
    std::uint64_t last_now_ms_{0};
    RepeaterLifecycleState state_{RepeaterLifecycleState::disabled};
    RepeaterLifecycleFault fault_{RepeaterLifecycleFault::none};
    bool configured_{false};
    bool busy_{false};
    bool time_observed_{false};
    bool first_use_spent_{false};
    std::uint64_t next_candidate_id_{0};
};

}  // namespace opentrail::delivery
