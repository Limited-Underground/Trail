#pragma once
#include <cstdint>
namespace opentrail::security_evaluation {
enum class EnrolledFault : std::uint8_t {
    none=0, preflight=1, input=2, authority_clock=3, entropy=4,
    storage=5, session_protocol=6, display=7, tx_deadline=8,
    radio_io=9, radio_frame=10, output=11, reentry=12
};
// Fixed, public categories only. Times are copied from the rejecting check;
// sampled=false explicitly distinguishes a storage/protocol rejection from a
// clock observation. No consumer may resample time to invent a cause.
enum class EnrolledFailureLayer : std::uint8_t {
    none=0, bench_session=1, enrolled_peer=2, handshake_endpoint=3,
    peer_transport=4, peer_traffic=5, radio_driver=6, diagnostics=7, target=8, storage=9
};
enum class EnrolledFailureReason : std::uint8_t {
    none=0, invalid_context=1, context_changed=2, invalid_clock=3,
    clock_regression=4, session_expired=5, window_before_issued=6,
    window_expired=7, membership_current=8, evidence_current=9,
    boot_current=10, role_current=11, packet_format=12,
    handshake_rejected=13, control_rejected=14, status_rejected=15,
    entropy=16, reentry=17, activation_current=18, record_rejected=19,
    protocol=20, tx_expired=21, storage_read=22, radio_io=23, radio_frame=24, unknown=25
};
struct EnrolledFailureDetail {
    EnrolledFailureLayer layer{EnrolledFailureLayer::none};
    EnrolledFailureReason reason{EnrolledFailureReason::none};
    bool sampled{false}, invitation_active{false}, session_active{false};
    std::uint64_t now_ms{0}, previous_ms{0}, issued_ms{0}, deadline_ms{0}, session_started_ms{0};
};
enum class EnrolledMilestone : std::uint8_t {
    review_ready=1, button_pressed=2, button_released=3,
    confirmation_accepted=4, invitation_started=5, activation_ready=6
};
inline bool enrolled_authority_rejection(EnrolledFailureReason reason) {
    return reason >= EnrolledFailureReason::invalid_context && reason <= EnrolledFailureReason::window_expired;
}
// Optional, synchronous, observational only. Implementations must not reenter
// owners, mutate authority, or perform device/storage I/O. Calls carry no secrets.
class EnrolledSessionObserver {
public:
    virtual ~EnrolledSessionObserver() = default;
    virtual void tick_begin(bool review) = 0;
    virtual void tick_end() = 0;
    virtual void fault(EnrolledFault code) = 0;
    virtual void before_cleanup(bool failed) = 0;
    virtual void rejection(const EnrolledFailureDetail&) {}
    virtual void milestone(EnrolledMilestone, std::uint64_t, std::uint64_t, std::uint64_t) {}
};
} // namespace opentrail::security_evaluation
