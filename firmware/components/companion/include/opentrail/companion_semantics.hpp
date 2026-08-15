#pragma once

#include <cstddef>
#include <cstdint>

#include "opentrail/companion_protocol.hpp"
#include "opentrail/quick_status_codec.hpp"
#include "opentrail/radio_transport.hpp"

namespace opentrail::companion {

inline constexpr std::uint8_t kCompanionSemanticMajor = 0;
inline constexpr std::uint8_t kCompanionSemanticMinor = 0;
inline constexpr std::size_t kCompanionSnapshotRequestBytes = 8;
inline constexpr std::size_t kCompanionStatusSnapshotBytes = 32;
inline constexpr std::size_t kCompanionActionRequestBytes = 20;
inline constexpr std::size_t kCompanionActionResultBytes = 20;

enum class CompanionRadioState : std::uint8_t {
    unknown = 0,
    unavailable = 1,
    ready = 2,
    degraded = 3,
    fault = 4,
};

enum class CompanionGnssState : std::uint8_t {
    unknown = 0,
    unavailable = 1,
    searching = 2,
    current = 3,
    stale = 4,
    fault = 5,
};

enum class CompanionPowerState : std::uint8_t {
    unknown = 0,
    external = 1,
    normal = 2,
    low = 3,
    critical = 4,
    fault = 5,
};

enum class CompanionPositionSharingState : std::uint8_t {
    stopped = 0,
    waiting_for_fix = 1,
    active = 2,
    deferred = 3,
    fault = 4,
};

enum class CompanionActionKind : std::uint8_t {
    quick_status = 1,
    acknowledge_critical_alert = 2,
    start_position_sharing = 3,
    stop_position_sharing = 4,
};

enum class CompanionActionDisposition : std::uint8_t {
    admitted = 1,
    queued = 2,
    rejected = 3,
};

enum class CompanionActionRejectReason : std::uint8_t {
    none = 0,
    unsupported_action = 1,
    stale_alert = 2,
    unavailable = 3,
    queue_full = 4,
    policy_denied = 5,
    internal_failure = 6,
};

enum class CompanionSemanticCodecError : std::uint8_t {
    none = 0,
    invalid_argument,
    output_too_small,
    malformed,
    unsupported_version,
    unknown_enum,
    invalid_revision,
    invalid_alert_id,
    invalid_quick_status,
    incoherent_action,
    incoherent_result,
    reserved_bits_set,
    unsupported_frame_kind,
};

struct CompanionSnapshotRequest {};

// Device-owned, presentation-neutral state. A zero pending alert ID means that
// no exact critical alert is currently offered for acknowledgement.
struct CompanionStatusSnapshot {
    std::uint32_t revision{0};
    CompanionRadioState radio{CompanionRadioState::unknown};
    CompanionGnssState gnss{CompanionGnssState::unknown};
    CompanionPowerState power{CompanionPowerState::unknown};
    CompanionPositionSharingState position_sharing{
        CompanionPositionSharingState::stopped};
    std::uint16_t queued_action_count{0};
    std::uint64_t pending_critical_alert_id{0};
};

// detail is a protocol::QuickStatusKind value only for quick_status. subject_id
// is the exact device-owned alert ID only for acknowledge_critical_alert.
struct CompanionActionRequest {
    CompanionActionKind kind{CompanionActionKind::quick_status};
    protocol::QuickStatusKind quick_status{protocol::QuickStatusKind::ok};
    std::uint64_t critical_alert_id{0};
};

// Correlation to the request is supplied by the enclosing OTC0 exchange ID.
// queued means only that device-owned work was queued; it is never radio
// transmission or delivery evidence.
struct CompanionActionResult {
    CompanionActionKind kind{CompanionActionKind::quick_status};
    protocol::QuickStatusKind quick_status{protocol::QuickStatusKind::ok};
    std::uint64_t critical_alert_id{0};
    CompanionActionDisposition disposition{
        CompanionActionDisposition::rejected};
    CompanionActionRejectReason reject_reason{
        CompanionActionRejectReason::internal_failure};
};

struct CompanionSemanticEncodeResult {
    CompanionSemanticCodecError error{
        CompanionSemanticCodecError::invalid_argument};
    std::size_t encoded_bytes{0};

    [[nodiscard]] constexpr bool encoded() const {
        return error == CompanionSemanticCodecError::none;
    }
};

template <typename T>
struct CompanionSemanticDecodeResult {
    CompanionSemanticCodecError error{
        CompanionSemanticCodecError::malformed};
    T value{};

    [[nodiscard]] constexpr bool decoded() const {
        return error == CompanionSemanticCodecError::none;
    }
};

[[nodiscard]] CompanionSemanticEncodeResult encode_companion_snapshot_request(
    const CompanionSnapshotRequest& request,
    radio::MutableByteView output);
[[nodiscard]] CompanionSemanticDecodeResult<CompanionSnapshotRequest>
decode_companion_snapshot_request(radio::ByteView encoded);

[[nodiscard]] CompanionSemanticEncodeResult encode_companion_status_snapshot(
    const CompanionStatusSnapshot& snapshot,
    radio::MutableByteView output);
[[nodiscard]] CompanionSemanticDecodeResult<CompanionStatusSnapshot>
decode_companion_status_snapshot(radio::ByteView encoded);

[[nodiscard]] CompanionSemanticEncodeResult encode_companion_action_request(
    const CompanionActionRequest& request,
    radio::MutableByteView output);
[[nodiscard]] CompanionSemanticDecodeResult<CompanionActionRequest>
decode_companion_action_request(radio::ByteView encoded);

[[nodiscard]] CompanionSemanticEncodeResult encode_companion_action_result(
    const CompanionActionResult& result,
    radio::MutableByteView output);
[[nodiscard]] CompanionSemanticDecodeResult<CompanionActionResult>
decode_companion_action_result(radio::ByteView encoded);

// Dispatches by the enclosing OTC0 frame kind and rejects a semantic record
// placed under any other kind. v0 has no event payload yet.
[[nodiscard]] CompanionSemanticCodecError
validate_companion_semantic_fragment(const CompanionFragment& fragment);

}  // namespace opentrail::companion
