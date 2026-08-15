#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/radio_transport.hpp"

namespace opentrail::companion {

inline constexpr std::uint8_t kCompanionProtocolMajor = 0;
inline constexpr std::uint8_t kCompanionProtocolMinor = 0;
inline constexpr std::size_t kCompanionProtocolInfoBytes = 16;
inline constexpr std::size_t kCompanionFragmentHeaderBytes = 20;
inline constexpr std::size_t kCompanionMaxFragmentPayloadBytes = 128;
inline constexpr std::size_t kCompanionMaxFragmentCount = 16;
inline constexpr std::size_t kCompanionMaxFragmentBytes =
    kCompanionFragmentHeaderBytes + kCompanionMaxFragmentPayloadBytes;
inline constexpr std::uint16_t kCompanionMinimumAttMtu =
    static_cast<std::uint16_t>(kCompanionMaxFragmentBytes + 3U);

enum class CompanionDeviceRole : std::uint8_t {
    screenless_client = 1,
};

enum class CompanionCapability : std::uint8_t {
    quick_status = 1U << 0U,
    critical_alert_acknowledgement = 1U << 1U,
    position_state = 1U << 2U,
    message_history = 1U << 3U,
};

inline constexpr std::uint8_t kCompanionKnownCapabilityMask = 0x0FU;

enum class CompanionFrameKind : std::uint8_t {
    snapshot_request = 1,
    action_request = 2,
    authorization_claim_start = 3,
    snapshot = 0x81,
    action_result = 0x82,
    event = 0x83,
    authorization_claim_status = 0x84,
    authorization_claim_result = 0x85,
};

enum class CompanionCodecError : std::uint8_t {
    none = 0,
    invalid_argument,
    output_too_small,
    malformed,
    unsupported_version,
    unknown_role,
    unknown_capability,
    unknown_frame_kind,
    invalid_limit,
    invalid_session_nonce,
    invalid_exchange_id,
    invalid_fragment,
    payload_too_large,
    reserved_bits_set,
};

struct CompanionProtocolInfo {
    CompanionDeviceRole role{CompanionDeviceRole::screenless_client};
    std::uint8_t capabilities{0};
    std::uint16_t max_fragment_payload_bytes{
        static_cast<std::uint16_t>(kCompanionMaxFragmentPayloadBytes)};
    std::uint16_t minimum_att_mtu{kCompanionMinimumAttMtu};
    std::uint8_t max_fragment_count{
        static_cast<std::uint8_t>(kCompanionMaxFragmentCount)};
    std::uint8_t max_active_controllers{1};
};

struct CompanionFragment {
    CompanionFrameKind kind{CompanionFrameKind::snapshot_request};
    std::uint32_t session_nonce{0};
    std::uint32_t exchange_id{0};
    std::uint8_t fragment_index{0};
    std::uint8_t fragment_count{1};
    std::uint16_t payload_bytes{0};
    std::array<std::uint8_t, kCompanionMaxFragmentPayloadBytes> payload{};
};

struct CompanionEncodeResult {
    CompanionCodecError error{CompanionCodecError::invalid_argument};
    std::size_t encoded_bytes{0};

    [[nodiscard]] constexpr bool encoded() const {
        return error == CompanionCodecError::none;
    }
};

struct CompanionInfoDecodeResult {
    CompanionCodecError error{CompanionCodecError::malformed};
    CompanionProtocolInfo info{};

    [[nodiscard]] constexpr bool decoded() const {
        return error == CompanionCodecError::none;
    }
};

struct CompanionFragmentDecodeResult {
    CompanionCodecError error{CompanionCodecError::malformed};
    CompanionFragment fragment{};

    [[nodiscard]] constexpr bool decoded() const {
        return error == CompanionCodecError::none;
    }
};

[[nodiscard]] CompanionEncodeResult encode_companion_protocol_info(
    const CompanionProtocolInfo& info,
    radio::MutableByteView output);
[[nodiscard]] CompanionInfoDecodeResult decode_companion_protocol_info(
    radio::ByteView encoded);
[[nodiscard]] CompanionEncodeResult encode_companion_fragment(
    const CompanionFragment& fragment,
    radio::MutableByteView output);
[[nodiscard]] CompanionFragmentDecodeResult decode_companion_fragment(
    radio::ByteView encoded);

enum class CompanionSessionError : std::uint8_t {
    none = 0,
    invalid_controller_binding,
    link_not_encrypted,
    bond_not_authenticated,
    controller_not_authorized,
    session_in_use,
    session_nonce_invalid,
    session_nonce_reused,
    session_nonce_exhausted,
    no_active_session,
    wrong_controller,
    wrong_session,
    wrong_direction,
    fragmented_request,
    request_id_invalid,
    stale_request,
    request_id_exhausted,
};

struct CompanionSessionEvidence {
    std::uint64_t controller_binding{0};
    bool link_encrypted{false};
    bool authenticated_bond{false};
    bool application_authorized{false};
};

struct CompanionSessionResult {
    CompanionSessionError error{CompanionSessionError::no_active_session};
    std::uint32_t session_nonce{0};

    [[nodiscard]] constexpr bool opened() const {
        return error == CompanionSessionError::none;
    }
};

enum class CompanionRequestDisposition : std::uint8_t {
    rejected = 0,
    accepted_new,
    duplicate,
};

struct CompanionRequestAdmission {
    CompanionRequestDisposition disposition{
        CompanionRequestDisposition::rejected};
    CompanionSessionError error{CompanionSessionError::no_active_session};
    std::uint32_t request_id{0};

    [[nodiscard]] constexpr bool accepted() const {
        return disposition == CompanionRequestDisposition::accepted_new;
    }

    [[nodiscard]] constexpr bool duplicate() const {
        return disposition == CompanionRequestDisposition::duplicate;
    }
};

struct CompanionSessionStatus {
    bool active{false};
    std::uint32_t session_nonce{0};
    std::uint32_t last_request_id{0};
    std::uint32_t accepted_requests{0};
    std::uint32_t duplicate_requests{0};
    std::uint32_t rejected_requests{0};
};

// Holds one boot-local controller lease. The BLE target adapter must derive the
// opaque binding and the three authorization facts from its real security
// subsystem; they are not caller-selectable fields on the wire.
class CompanionSessionGuard {
public:
    [[nodiscard]] CompanionSessionResult open_session(
        const CompanionSessionEvidence& evidence,
        std::uint32_t device_generated_session_nonce);
    [[nodiscard]] CompanionSessionError close_session(
        std::uint64_t controller_binding);
    [[nodiscard]] CompanionRequestAdmission admit_request(
        const CompanionSessionEvidence& evidence,
        const CompanionFragment& fragment);
    [[nodiscard]] CompanionSessionStatus status() const;

private:
    [[nodiscard]] static CompanionSessionError validate_evidence(
        const CompanionSessionEvidence& evidence);
    [[nodiscard]] CompanionRequestAdmission reject(
        CompanionSessionError error,
        std::uint32_t request_id);

    std::uint64_t controller_binding_{0};
    std::uint32_t current_session_nonce_{0};
    std::uint32_t last_session_nonce_{0};
    std::uint32_t last_request_id_{0};
    CompanionSessionStatus status_{};
};

}  // namespace opentrail::companion
