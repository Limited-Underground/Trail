#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/duplicate_window.hpp"
#include "opentrail/radio_transport.hpp"

namespace opentrail::delivery {

inline constexpr std::size_t kSingleRepeaterQueueCapacity = 8;

// These fields are outputs of a future protected-packet adapter. The policy
// cannot create cryptographic evidence and must never be called on raw parsed
// radio metadata in production composition.
struct VerifiedForwardingMetadata {
    std::uint64_t group_context_id{0};
    std::uint64_t source_alias{0};
    std::uint64_t destination_alias{0};  // Zero is group broadcast.
    std::uint32_t group_epoch{0};
    std::uint32_t message_id{0};
    bool source_authenticated{false};
    bool source_authorized{false};
    bool forwarding_permitted{false};
};

struct SingleRepeaterPolicy {
    std::uint8_t configured_authorized_repeaters{0};
    bool local_repeater_authorized{false};
    std::size_t maximum_queue_depth{0};
    std::uint16_t maximum_forwards_per_window{0};
    std::uint32_t rate_window_ms{0};
    std::uint32_t maximum_queue_age_ms{0};
};

enum class SingleRepeaterAdmission : std::uint8_t { allow, denied, uncertain };

// Optional host accounting hook. Runs once only after authenticated fixture
// evidence and a new replay key; it must not throw, retain pointers or reenter
// the forwarder. Uncertainty permanently blocks release until reconstruction.
class SingleRepeaterAdmissionGate {
public:
    virtual ~SingleRepeaterAdmissionGate() = default;
    virtual SingleRepeaterAdmission evaluate(
        const VerifiedForwardingMetadata& metadata, std::size_t frame_bytes,
        std::uint64_t now_ms) noexcept = 0;
};

enum class SingleRepeaterDisposition : std::uint8_t {
    queued = 0,
    invalid_argument,
    invalid_policy,
    source_authentication_required,
    source_unauthorized,
    wrong_group_or_epoch,
    self_source,
    destination_reached,
    forwarding_not_permitted,
    clock_regression,
    duplicate,
    queue_full,
    rate_limited,
    admission_denied,
    admission_uncertain,
    reentrant,
};

struct SingleRepeaterDecision {
    bool queued{false};
    SingleRepeaterDisposition disposition{
        SingleRepeaterDisposition::invalid_argument};
    bool replay_state_changed{false};
};

struct ExactForwardedFrame {
    std::array<std::uint8_t, radio::kMaximumFrameBytes> bytes{};
    std::size_t size{0};
};

struct ForwardingProvenance {
    VerifiedForwardingMetadata metadata{};
    std::uint64_t enqueued_at_ms{0};
    std::uint32_t maximum_queue_age_ms{0};
};

struct ExactForwardedFrameResult {
    bool has_frame{false};
    std::uint8_t expired_frames_dropped{0};
    ExactForwardedFrame frame{};
    ForwardingProvenance provenance{};
};

struct SingleRepeaterStatus {
    std::size_t queue_depth{0};
    std::uint32_t queued{0};
    std::uint32_t authentication_dropped{0};
    std::uint32_t authorization_dropped{0};
    std::uint32_t wrong_context_dropped{0};
    std::uint32_t clock_regression_dropped{0};
    std::uint32_t duplicates_dropped{0};
    std::uint32_t congestion_dropped{0};
    std::uint32_t expired_dropped{0};
    bool admission_uncertain{false};
};

class SingleRepeaterForwarder {
public:
    SingleRepeaterForwarder(
        std::uint64_t local_alias,
        std::uint64_t group_context_id,
        std::uint32_t group_epoch,
        SingleRepeaterPolicy policy,
        DuplicateWindow& duplicate_window,
        SingleRepeaterAdmissionGate* admission_gate = nullptr);

    [[nodiscard]] SingleRepeaterDecision process(
        const VerifiedForwardingMetadata& metadata,
        radio::ByteView exact_protected_frame,
        std::uint64_t now_ms);
    [[nodiscard]] ExactForwardedFrameResult next_forward(std::uint64_t now_ms);
    // Expiry-only bounded service: never pops a live head or releases bytes.
    [[nodiscard]] std::uint8_t prune_expired(std::uint64_t now_ms);
    [[nodiscard]] SingleRepeaterStatus status() const;

private:
    struct QueuedFrame {
        ExactForwardedFrame frame{};
        ForwardingProvenance provenance{};
    };
    struct CallGuard {
        bool& busy;
        explicit CallGuard(bool& value) noexcept : busy(value) { busy = true; }
        ~CallGuard() { busy = false; }
    };

    bool valid_configuration() const;
    void update_rate_window(std::uint64_t now_ms);
    void pop_front();
    std::uint8_t prune_expired_impl(std::uint64_t now_ms);

    std::uint64_t local_alias_{0};
    std::uint64_t group_context_id_{0};
    std::uint32_t group_epoch_{0};
    SingleRepeaterPolicy policy_{};
    DuplicateWindow& duplicate_window_;
    SingleRepeaterAdmissionGate* admission_gate_{nullptr};
    std::array<QueuedFrame, kSingleRepeaterQueueCapacity> queue_{};
    std::size_t queue_head_{0};
    std::size_t queue_tail_{0};
    std::size_t queue_count_{0};
    std::uint64_t rate_window_started_ms_{0};
    std::uint16_t forwards_in_window_{0};
    bool rate_window_initialized_{false};
    std::uint64_t last_process_ms_{0};
    bool process_time_initialized_{false};
    SingleRepeaterStatus counters_{};
    bool busy_{false};
    bool gate_uncertain_{false};
};

}  // namespace opentrail::delivery
