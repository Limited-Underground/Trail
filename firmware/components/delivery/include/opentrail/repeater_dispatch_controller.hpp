#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/lora_airtime.hpp"
#include "opentrail/repeater_lifecycle_controller.hpp"

namespace opentrail::delivery {

inline constexpr std::size_t kRepeaterDispatchSources = 2;
inline constexpr std::uint8_t kRepeaterDispatchLedgerVersion = 1;

struct RepeaterDispatchSource {
    std::uint64_t group_context_id{0};
    std::uint32_t group_epoch{0};
    std::uint64_t source_alias{0};
};

struct RepeaterDispatchSourceCredit {
    RepeaterDispatchSource source{};
    std::uint64_t remaining{0};
};

struct RepeaterDispatchLedger {
    std::uint8_t version{kRepeaterDispatchLedgerVersion};
    std::uint64_t domain{0};
    std::uint64_t budget_epoch{0};
    std::uint64_t sequence{0};
    std::uint64_t global_remaining{0};
    std::uint64_t airtime_remaining_us{0};
    std::size_t source_count{0};
    std::array<RepeaterDispatchSourceCredit, kRepeaterDispatchSources> sources{};
};

enum class RepeaterDispatchLedgerError : std::uint8_t {
    none, missing, invalid, unavailable, conflict, commit_uncertain
};

// Synthetic host persistence seam. CAS compares the entire expected record.
// No initialization/refill/reset operation exists. Ports outlive the controller.
class RepeaterDispatchLedgerPort {
public:
    virtual ~RepeaterDispatchLedgerPort() = default;
    virtual RepeaterDispatchLedgerError read(RepeaterDispatchLedger& record) noexcept = 0;
    virtual RepeaterDispatchLedgerError compare_exchange(
        const RepeaterDispatchLedger& expected,
        const RepeaterDispatchLedger& desired) noexcept = 0;
};

struct RepeaterDispatchRadioProof {
    std::uint64_t lineage_id{0};
    std::uint64_t driver_generation{0};
    std::uint64_t readiness_generation{0};
    std::uint64_t quiescence_receipt{0};
    bool verified_quiescent{false};
    bool receive_ready{false};
};

struct RepeaterDispatchAttempt {
    std::uint64_t attempt_id{0};
    RepeaterLifecycleDispatchTicket ticket{};
    std::uint64_t budget_domain{0};
    std::uint64_t budget_epoch{0};
    std::uint64_t budget_receipt_sequence{0};
    std::uint64_t radio_lineage_id{0};
    std::uint64_t driver_generation{0};
    std::uint64_t readiness_generation{0};
    std::uint64_t quiescence_receipt{0};
};

enum class RepeaterDispatchSubmitOutcome : std::uint8_t {
    tuple_refused, accepted_pending, busy, rejected, partial, uncertain
};
struct RepeaterDispatchSubmitResult {
    RepeaterDispatchSubmitOutcome outcome{RepeaterDispatchSubmitOutcome::uncertain};
    std::size_t accepted_bytes{0};
};
enum class RepeaterDispatchTerminalOutcome : std::uint8_t { completed, failed, uncertain };
struct RepeaterDispatchTerminal {
    RepeaterDispatchAttempt attempt{};
    RepeaterDispatchTerminalOutcome outcome{RepeaterDispatchTerminalOutcome::uncertain};
};
struct RepeaterDispatchRearm {
    RepeaterDispatchAttempt attempt{};
    RepeaterDispatchRadioProof proof{};
};

// Trusted synthetic port, not a RadioTransport adapter. submit must atomically
// enforce the expected proof BEFORE copying any bytes, then retain no borrowed
// pointer. Terminal/rearm handles consume finite retained evidence exactly once.
// A fresh/default-ready object is not verified recovery/quiescence evidence.
class RepeaterDispatchRadioPort {
public:
    virtual ~RepeaterDispatchRadioPort() = default;
    virtual bool observe(RepeaterDispatchRadioProof& proof) noexcept = 0;
    // Idempotently retain uncertainty/not-ready state across controller
    // reconstruction. Does not mint a new proof/generation or perform rearm.
    virtual void invalidate_quiescence() noexcept = 0;
    virtual RepeaterDispatchSubmitResult submit(
        const RepeaterDispatchAttempt& attempt,
        const RepeaterDispatchRadioProof& expected,
        radio::ByteView borrowed_frame) noexcept = 0;
    virtual bool consume_terminal(std::uint64_t handle,
        const RepeaterDispatchAttempt& expected,
        RepeaterDispatchTerminal& evidence) noexcept = 0;
    virtual bool consume_rearm(std::uint64_t handle,
        const RepeaterDispatchAttempt& expected,
        RepeaterDispatchRearm& evidence) noexcept = 0;
};

struct RepeaterDispatchConfiguration {
    RepeaterLifecycleConfiguration lifecycle{};
    std::uint64_t budget_domain{0};
    std::uint64_t budget_epoch{0};
    std::uint64_t radio_lineage_id{0};
    std::size_t source_count{0};
    std::array<RepeaterDispatchSource, kRepeaterDispatchSources> sources{};
    std::uint64_t per_source_credit_limit{1};
    std::uint64_t global_credit_limit{2};
    std::uint64_t airtime_ceiling_us{0};
    std::size_t logical_byte_cap{0};
    radio::LoRaAirtimeSettings phy{125000, 8, 7, 5, true, true, false};
    std::uint64_t terminal_timeout_ms{1000};
    std::uint64_t rearm_timeout_ms{1000};
    std::uint64_t initial_attempt_id{1};
};

enum class RepeaterDispatchState : std::uint8_t {
    disabled, awaiting_consent, ready, awaiting_terminal, awaiting_rearm,
    suspended, contained, shutdown
};
enum class RepeaterDispatchFault : std::uint8_t {
    none, wrong_state, invalid_configuration, reentrant, clock_regression,
    lifecycle, ledger_unavailable, ledger_mismatch, ledger_uncertain,
    budget_exhausted, radio_not_ready, radio_changed, invalid_event,
    terminal_timeout, rearm_timeout, uncertain_emission, identity_exhausted,
    suspended, time_overflow
};

struct RepeaterDispatchDiagnostics {
    std::uint32_t admitted{0}, denied{0}, ledger_commits{0}, claim_callbacks{0};
    std::uint32_t submit_requests{0}, accepted_copies{0}, terminals{0}, rearms{0};
    std::uint32_t expired{0}, uncertain{0};
    bool overflow{false};
};
// The same pure bounded operation used by every real diagnostic counter.
void increment_repeater_dispatch_counter(std::uint32_t& counter, bool& overflow) noexcept;

struct RepeaterDispatchStatus {
    RepeaterDispatchState state{RepeaterDispatchState::disabled};
    RepeaterDispatchFault fault{RepeaterDispatchFault::none};
    RepeaterLifecycleStatus lifecycle{};
    std::size_t occupied_frames{0};
    std::size_t logical_payload_bytes{0};
    bool possible_emission{false};
    RepeaterDispatchDiagnostics diagnostics{};
};

class RepeaterDispatchController {
public:
    RepeaterDispatchController(const RepeaterDispatchConfiguration& configuration,
        RepeaterLifecycleProvider& authority, RepeaterLifecycleRoleStorage& roles,
        DuplicateCheckpointStorage& replay, RepeaterDispatchLedgerPort& ledger,
        RepeaterDispatchRadioPort& radio) noexcept;
    RepeaterDispatchController(const RepeaterDispatchController&) = delete;
    RepeaterDispatchController& operator=(const RepeaterDispatchController&) = delete;
    RepeaterDispatchController(RepeaterDispatchController&&) = delete;
    RepeaterDispatchController& operator=(RepeaterDispatchController&&) = delete;

    RepeaterDispatchFault recover(std::uint64_t request, std::uint64_t now_ms) noexcept;
    RepeaterDispatchFault consent(std::uint64_t handle, std::uint64_t now_ms) noexcept;
    RepeaterLifecycleAdmission admit(std::uint64_t handle, radio::ByteView frame,
                                    std::uint64_t now_ms) noexcept;
    RepeaterDispatchFault service(std::uint64_t now_ms) noexcept;
    RepeaterDispatchFault terminal(std::uint64_t handle, std::uint64_t now_ms) noexcept;
    RepeaterDispatchFault rearm(std::uint64_t handle, std::uint64_t now_ms) noexcept;
    RepeaterDispatchFault notify(std::uint64_t generation, RepeaterLifecycleEvent event,
                                 std::uint64_t now_ms) noexcept;
    RepeaterDispatchFault disable(std::uint64_t request, std::uint64_t now_ms) noexcept;
    RepeaterDispatchFault shutdown() noexcept;
    RepeaterDispatchFault clear_diagnostics() noexcept;
    [[nodiscard]] RepeaterDispatchStatus status() const noexcept;

private:
    class Gate final : public SingleRepeaterAdmissionGate {
    public:
        explicit Gate(RepeaterDispatchController& owner) noexcept : owner_(owner) {}
        SingleRepeaterAdmission evaluate(const VerifiedForwardingMetadata& metadata,
            std::size_t bytes, std::uint64_t now_ms) noexcept override;
    private:
        RepeaterDispatchController& owner_;
    };
    class Claim final : public RepeaterLifecycleClaimCallback {
    public:
        explicit Claim(RepeaterDispatchController& owner) noexcept : owner_(owner) {}
        void invoke(const RepeaterLifecycleDispatchTicket& ticket,
                    radio::ByteView frame) noexcept override;
    private:
        RepeaterDispatchController& owner_;
    };
    struct Guard {
        bool& busy;
        explicit Guard(bool& value) noexcept : busy(value) { busy = true; }
        ~Guard() { busy = false; }
    };
    static RepeaterLifecycleConfiguration lifecycle_configuration(
        const RepeaterDispatchConfiguration& c, SingleRepeaterAdmissionGate* gate) noexcept;
    bool valid_ledger(const RepeaterDispatchLedger& record) const noexcept;
    bool valid_radio(const RepeaterDispatchRadioProof& proof) const noexcept;
    bool load_ledger(bool recovery) noexcept;
    bool debit(std::size_t source_index, bool airtime) noexcept;
    bool observe_time(std::uint64_t now_ms) noexcept;
    void stop(RepeaterDispatchState state, RepeaterDispatchFault fault,
              std::uint64_t now_ms) noexcept;
    void count(std::uint32_t& counter) noexcept;
    void invalidate_radio() noexcept;
    bool prune(std::uint64_t now_ms) noexcept;

    const RepeaterDispatchConfiguration configuration_;
    RepeaterDispatchLedgerPort& ledger_port_;
    RepeaterDispatchRadioPort& radio_port_;
    Gate gate_{*this};
    Claim claim_{*this};
    RepeaterLifecycleController lifecycle_;
    RepeaterDispatchLedger ledger_{};
    RepeaterDispatchRadioProof claim_radio_{};
    RepeaterDispatchAttempt attempt_{};
    RepeaterDispatchSubmitResult submit_result_{};
    RepeaterDispatchDiagnostics diagnostics_{};
    RepeaterDispatchState state_{RepeaterDispatchState::disabled};
    RepeaterDispatchFault fault_{RepeaterDispatchFault::none};
    std::uint64_t cost_us_{0}, minimum_terminal_ms_{0}, next_attempt_id_{0};
    std::uint64_t phase_started_ms_{0}, claim_started_ms_{0}, last_now_ms_{0};
    bool configured_{false}, busy_{false}, ledger_known_{false}, cached_once_{false};
    bool time_seen_{false}, attempt_live_{false}, possible_emission_{false};
    bool radio_reconciliation_required_{false};
};
}  // namespace opentrail::delivery
