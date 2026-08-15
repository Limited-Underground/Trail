#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/companion_authorization.hpp"
#include "opentrail/companion_authorization_wire.hpp"
#include "opentrail/companion_gatt_session.hpp"

namespace opentrail::companion {

inline constexpr std::uint8_t kCompanionAuthorizationProtocolMajor = 0;
inline constexpr std::uint8_t kCompanionAuthorizationProtocolMinor = 1;
inline constexpr std::size_t kCompanionAuthorizationProtocolInfoBytes = 20;
inline constexpr std::uint8_t kCompanionAuthorizationClaimCapability = 0x10U;
inline constexpr std::uint8_t kCompanionAuthorizationCapabilityMask =
    kCompanionKnownCapabilityMask | kCompanionAuthorizationClaimCapability;
inline constexpr std::uint16_t kCompanionAuthorizationMinimumAttMtu = 51;
inline constexpr std::uint16_t
    kCompanionAuthorizationMinimumFragmentPayloadBytes =
        static_cast<std::uint16_t>(
            kCompanionAuthorizationClaimResultBytes);
inline constexpr std::uint64_t kCompanionAuthorizationPendingDeliveryToken = 1;
inline constexpr std::uint64_t kCompanionAuthorizationTerminalDeliveryToken = 2;
inline constexpr std::uint64_t kCompanionAuthorizationFirstNormalDeliveryToken = 3;
inline constexpr std::size_t kCompanionAuthorizationMaxResponseBytes =
    kCompanionFragmentHeaderBytes +
    kCompanionAuthorizationClaimResultBytes;

static_assert(kCompanionAuthorizationMaxResponseBytes == 48);
static_assert(kCompanionAuthorizationMinimumAttMtu ==
              kCompanionAuthorizationMaxResponseBytes + 3U);
static_assert(kCompanionAuthorizationPendingDeliveryToken != 0);
static_assert(kCompanionAuthorizationTerminalDeliveryToken ==
              kCompanionAuthorizationPendingDeliveryToken + 1U);
static_assert(kCompanionAuthorizationFirstNormalDeliveryToken ==
              kCompanionAuthorizationTerminalDeliveryToken + 1U);

struct CompanionAuthorizationProtocolInfo {
    CompanionDeviceRole role{CompanionDeviceRole::screenless_client};
    std::uint8_t capabilities{kCompanionAuthorizationCapabilityMask};
    std::uint16_t max_fragment_payload_bytes{
        static_cast<std::uint16_t>(kCompanionMaxFragmentPayloadBytes)};
    std::uint16_t minimum_normal_att_mtu{kCompanionMinimumAttMtu};
    std::uint8_t max_fragment_count{
        static_cast<std::uint8_t>(kCompanionMaxFragmentCount)};
    std::uint8_t max_active_controllers{1};
    std::uint32_t provisional_session_nonce{0};
};

enum class CompanionAuthorizationProtocolInfoError : std::uint8_t {
    none = 0,
    invalid_argument,
    output_too_small,
    malformed,
    unsupported_version,
    invalid_capability,
    invalid_limit,
    invalid_session_nonce,
    reserved_bits_set,
};

struct CompanionAuthorizationProtocolInfoEncodeResult {
    CompanionAuthorizationProtocolInfoError error{
        CompanionAuthorizationProtocolInfoError::invalid_argument};
    std::size_t encoded_bytes{0};

    [[nodiscard]] constexpr bool encoded() const {
        return error == CompanionAuthorizationProtocolInfoError::none;
    }
};

struct CompanionAuthorizationProtocolInfoDecodeResult {
    CompanionAuthorizationProtocolInfoError error{
        CompanionAuthorizationProtocolInfoError::malformed};
    CompanionAuthorizationProtocolInfo info{};

    [[nodiscard]] constexpr bool decoded() const {
        return error == CompanionAuthorizationProtocolInfoError::none;
    }
};

[[nodiscard]] CompanionAuthorizationProtocolInfoEncodeResult
encode_companion_authorization_protocol_info(
    const CompanionAuthorizationProtocolInfo& info,
    radio::MutableByteView output);
[[nodiscard]] CompanionAuthorizationProtocolInfoDecodeResult
decode_companion_authorization_protocol_info(radio::ByteView encoded);

struct CompanionGattAuthorizationHandles {
    std::uint16_t protocol_info_value{0};
    std::uint16_t command_value{0};
    std::uint16_t stream_value{0};
    std::uint16_t stream_cccd{0};
};

struct CompanionGattAuthorizationConnectionEvidence {
    std::uint16_t att_mtu{kCompanionGattDefaultAttMtu};
    CompanionControllerClaim controller_claim{};
};

struct CompanionGattAuthorizationCorrelationContext {
    std::uint64_t transport_generation{0};
    std::uint32_t session_nonce{0};
    std::uint32_t exchange_id{0};
    CompanionAuthorizationPurpose purpose{
        CompanionAuthorizationPurpose::authorize_controller};
};

enum class CompanionGattAuthorizationCorrelationError : std::uint8_t {
    none = 0,
    not_ready,
    failed,
    exhausted,
};

struct CompanionGattAuthorizationCorrelationResult {
    CompanionGattAuthorizationCorrelationError error{
        CompanionGattAuthorizationCorrelationError::not_ready};
    CompanionAuthorizationCorrelation correlation{};
};

// The issuer is a private, serialized device seam. A successful call must mint
// one fresh nonzero boot-local correlation bound to the exact context. It may
// not return identity/key/address data. A non-none error guarantees no value
// was consumed. Callbacks must not re-enter the lifecycle.
class CompanionGattAuthorizationCorrelationIssuer {
public:
    virtual ~CompanionGattAuthorizationCorrelationIssuer() = default;
    [[nodiscard]] virtual CompanionGattAuthorizationCorrelationResult issue(
        const CompanionGattAuthorizationCorrelationContext& context) = 0;
};

enum class CompanionGattAuthorizationAuthorityError : std::uint8_t {
    none = 0,
    not_ready,
    failed,
};

struct CompanionGattAuthorizationDecision {
    CompanionGattAuthorizationAuthorityError error{
        CompanionGattAuthorizationAuthorityError::not_ready};
    CompanionAuthorizationClaimOutcome outcome{
        CompanionAuthorizationClaimOutcome::denied};
    CompanionAuthorizationDenyReason reason{
        CompanionAuthorizationDenyReason::unknown};
    std::uint64_t controller_binding{0};
};

// Device authority seam. apply_claim() performs the authoritative owner/
// controller decision atomically. not_ready guarantees no durable mutation and
// no active controller lease, so the exact pending claim may be resolved again.
// Any other non-none error or a coherent Denied result must not leave an active
// controller. Accepted/Replaced returns the exact
// private nonzero controller binding now active for this connection. Durable
// owner mutation may already be committed before terminal indication delivery;
// release_connection() releases only the live controller lease, not ownership.
// Callbacks are externally serialized and must not re-enter the lifecycle.
class CompanionGattAuthorizationAuthority {
public:
    virtual ~CompanionGattAuthorizationAuthority() = default;
    [[nodiscard]] virtual CompanionGattAuthorizationDecision apply_claim(
        CompanionAuthorizationPurpose purpose,
        const CompanionControllerClaim& claim,
        std::uint64_t now_ms) = 0;
    [[nodiscard]] virtual CompanionGattAuthorizationAuthorityError
    release_connection(std::uint64_t controller_binding) = 0;
};

struct CompanionGattAuthorizationPolicy {
    std::uint64_t indication_timeout_ms{5000};
    std::uint64_t claim_timeout_ms{30000};
};

enum class CompanionGattAuthorizationPhase : std::uint8_t {
    idle = 0,
    connected,
    provisional,
    pending_indication,
    awaiting_authority,
    terminal_indication,
    promoted,
    blocked_until_disconnect,
};

enum class CompanionGattAuthorizationError : std::uint8_t {
    none = 0,
    invalid_argument,
    handles_not_registered,
    handles_already_registered,
    no_connection,
    connection_in_use,
    wrong_connection,
    wrong_transport_generation,
    stale_transport_generation,
    wrong_attribute,
    insecure_link,
    mtu_too_small,
    session_not_open,
    session_nonce_invalid,
    session_nonce_reused,
    protocol_info_not_read,
    subscription_required,
    response_path_busy,
    response_path_unavailable,
    malformed_request,
    unsupported_request,
    wrong_session,
    stale_exchange,
    correlation_unavailable,
    authority_pending,
    authority_unavailable,
    authority_result_incoherent,
    indication_submit_failed,
    no_outstanding_indication,
    indication_mismatch,
    indication_failed,
    indication_timeout,
    claim_timeout,
    normal_session_not_ready,
    normal_command_rejected,
    blocked_until_disconnect,
    clock_rollback,
    reentrant_call,
    internal_failure,
};

enum class CompanionGattAuthorizationRequestDisposition : std::uint8_t {
    rejected = 0,
    indication_pending,
    normal_indication_pending,
};

struct CompanionGattAuthorizationReadResult {
    CompanionGattAuthorizationError error{
        CompanionGattAuthorizationError::invalid_argument};
    std::size_t encoded_bytes{0};
};

struct CompanionGattAuthorizationRequestResult {
    CompanionGattAuthorizationRequestDisposition disposition{
        CompanionGattAuthorizationRequestDisposition::rejected};
    CompanionGattAuthorizationError error{
        CompanionGattAuthorizationError::invalid_argument};
    std::uint64_t delivery_token{0};
    std::uint32_t exchange_id{0};

    [[nodiscard]] constexpr bool pending() const {
        return error == CompanionGattAuthorizationError::none &&
               disposition !=
                   CompanionGattAuthorizationRequestDisposition::rejected;
    }
};

struct CompanionGattAuthorizationStatus {
    CompanionGattAuthorizationPhase phase{
        CompanionGattAuthorizationPhase::idle};
    bool handles_registered{false};
    bool encrypted{false};
    bool authenticated_bond{false};
    bool protocol_info_read{false};
    bool indication_subscribed{false};
    bool application_authorized{false};
    bool normal_session_active{false};
    bool response_pending{false};
    bool faulted{false};
    std::uint16_t att_mtu{0};
    std::uint32_t session_nonce{0};
    std::uint32_t exchange_id{0};
};

// Fixed-memory, externally serialized owner for one provisional authorization
// connection. The exact Protocol Info read is allowed after encrypted,
// authenticated-bond evidence but before application authorization. Claim
// Start requires an exact registered Command handle, a registered/subscribed
// Stream CCCD, and ATT MTU >= 51 so the 48-byte terminal can be indicated.
// Pending and terminal use fixed distinct tokens 1 and 2, always admitted with
// the exact connection/generation/session/exchange tuple; the composed normal
// lifecycle owns the non-overlapping exhaustion-checked range [3, UINT64_MAX].
// Pending is confirmed before terminal authority work. Terminal capacity is
// reserved before authority mutation. Accepted/Replaced promotes only after
// the exact terminal indication tuple is confirmed. Normal snapshot/action
// requests remain denied until promotion and normal-session opening.
class CompanionGattAuthorizationLifecycle {
public:
    CompanionGattAuthorizationLifecycle(
        CompanionRequestCoordinator& normal_coordinator,
        CompanionGattIndicationSink& indication_sink,
        CompanionGattAuthorizationCorrelationIssuer& correlation_issuer,
        CompanionGattAuthorizationAuthority& authority,
        CompanionGattAuthorizationPolicy policy = {});

    [[nodiscard]] CompanionGattAuthorizationError register_handles(
        CompanionGattAuthorizationHandles handles);
    [[nodiscard]] CompanionGattAuthorizationError connect(
        std::uint16_t connection_handle,
        std::uint64_t transport_generation);
    [[nodiscard]] CompanionGattAuthorizationError update_connection_evidence(
        std::uint16_t connection_handle,
        std::uint64_t transport_generation,
        CompanionGattAuthorizationConnectionEvidence evidence);
    [[nodiscard]] CompanionGattAuthorizationError open_provisional_session(
        std::uint16_t connection_handle,
        std::uint64_t transport_generation,
        std::uint32_t device_generated_session_nonce);
    [[nodiscard]] CompanionGattAuthorizationReadResult read_protocol_info(
        std::uint16_t connection_handle,
        std::uint64_t transport_generation,
        std::uint16_t protocol_info_value_handle,
        radio::MutableByteView output);
    [[nodiscard]] CompanionGattAuthorizationError
    update_indication_subscription(
        std::uint16_t connection_handle,
        std::uint64_t transport_generation,
        std::uint16_t cccd_handle,
        bool indications_enabled);
    [[nodiscard]] CompanionGattAuthorizationRequestResult service_command(
        std::uint16_t connection_handle,
        std::uint64_t transport_generation,
        std::uint16_t command_value_handle,
        radio::ByteView encoded_request,
        std::uint64_t now_ms);
    [[nodiscard]] CompanionGattAuthorizationError complete_indication(
        std::uint16_t connection_handle,
        std::uint64_t transport_generation,
        std::uint32_t session_nonce,
        std::uint16_t stream_value_handle,
        std::uint64_t delivery_token,
        bool confirmed,
        std::uint64_t now_ms);
    // Called after the exact Pending indication is confirmed, typically when
    // the serialized device authority observes physical presence or another
    // authoritative decision. not_ready leaves the claim pending and reserved
    // capacity is released without owner/controller mutation.
    [[nodiscard]] CompanionGattAuthorizationRequestResult resolve_claim(
        std::uint16_t connection_handle,
        std::uint64_t transport_generation,
        std::uint32_t session_nonce,
        std::uint32_t exchange_id,
        std::uint64_t now_ms);
    [[nodiscard]] CompanionGattAuthorizationError service_timeout(
        std::uint16_t connection_handle,
        std::uint64_t transport_generation,
        std::uint32_t session_nonce,
        std::uint32_t exchange_id,
        std::uint64_t delivery_token,
        std::uint64_t now_ms);
    [[nodiscard]] CompanionGattAuthorizationError disconnect(
        std::uint16_t connection_handle,
        std::uint64_t transport_generation);
    [[nodiscard]] CompanionGattAuthorizationStatus status() const;

private:
    [[nodiscard]] CompanionGattAuthorizationError connection_error(
        std::uint16_t connection_handle,
        std::uint64_t transport_generation) const;
    [[nodiscard]] CompanionGattAuthorizationError secure_error() const;
    [[nodiscard]] CompanionGattAuthorizationError claim_path_error() const;
    [[nodiscard]] CompanionGattAuthorizationError observe_time(
        std::uint64_t now_ms);
    [[nodiscard]] CompanionGattAuthorizationError contain(bool block);
    [[nodiscard]] CompanionGattAuthorizationError promote_normal_if_ready();
    [[nodiscard]] CompanionGattAuthorizationError submit_terminal(
        std::uint64_t now_ms);
    void clear_response_slot();
    void clear_claim();
    void clear_connection();

    CompanionGattIndicationSink& indication_sink_;
    CompanionGattAuthorizationCorrelationIssuer& correlation_issuer_;
    CompanionGattAuthorizationAuthority& authority_;
    CompanionGattAuthorizationPolicy policy_{};
    CompanionGattSessionLifecycle normal_lifecycle_;
    CompanionGattAuthorizationHandles handles_{};
    CompanionGattAuthorizationConnectionEvidence evidence_{};
    CompanionGattAuthorizationConnectionEvidence bound_evidence_{};
    CompanionGattAuthorizationPhase phase_{
        CompanionGattAuthorizationPhase::idle};
    bool handles_registered_{false};
    bool protocol_info_read_{false};
    bool indication_subscribed_{false};
    bool reservation_active_{false};
    bool response_pending_{false};
    bool application_authorized_{false};
    bool authority_connection_active_{false};
    bool normal_connected_{false};
    bool faulted_{false};
    bool operation_active_{false};
    bool time_observed_{false};
    std::uint16_t connection_handle_{kCompanionGattInvalidConnectionHandle};
    std::uint64_t transport_generation_{0};
    std::uint64_t last_transport_generation_{0};
    std::uint32_t session_nonce_{0};
    std::uint32_t last_session_nonce_{0};
    std::uint32_t exchange_id_{0};
    std::uint32_t last_exchange_id_{0};
    CompanionAuthorizationPurpose purpose_{
        CompanionAuthorizationPurpose::authorize_controller};
    CompanionAuthorizationCorrelation correlation_{};
    CompanionAuthorizationClaimOutcome terminal_outcome_{
        CompanionAuthorizationClaimOutcome::denied};
    CompanionAuthorizationDenyReason terminal_reason_{
        CompanionAuthorizationDenyReason::unknown};
    std::uint64_t authority_controller_binding_{0};
    std::uint64_t reservation_token_{0};
    std::uint64_t pending_delivery_token_{0};
    std::uint64_t pending_since_ms_{0};
    std::uint64_t claim_started_ms_{0};
    std::uint64_t normal_pending_delivery_token_{0};
    std::uint32_t normal_pending_exchange_id_{0};
    std::uint64_t last_now_ms_{0};
    std::array<std::uint8_t, kCompanionAuthorizationMaxResponseBytes>
        response_buffer_{};
};

}  // namespace opentrail::companion
