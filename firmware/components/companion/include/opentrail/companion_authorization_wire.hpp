#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/companion_protocol.hpp"
#include "opentrail/radio_transport.hpp"

namespace opentrail::companion {

inline constexpr std::uint8_t kCompanionAuthorizationWireMajor = 0;
inline constexpr std::uint8_t kCompanionAuthorizationWireMinor = 0;
inline constexpr std::size_t kCompanionAuthorizationCorrelationBytes = 16;
inline constexpr std::size_t kCompanionAuthorizationClaimStartBytes = 8;
inline constexpr std::size_t kCompanionAuthorizationClaimStatusBytes = 24;
inline constexpr std::size_t kCompanionAuthorizationClaimResultBytes = 28;

// This value is a device-issued, boot-local opaque correlation only. It is not
// a device/controller identity, BLE address, bond token, physical token,
// challenge, credential, or secret and must never be displayed, logged, or
// persisted. The future device issuer must mint a fresh nonzero value bound
// privately to the current boot challenge and exact OTC0 session, exchange,
// and purpose. It must never reuse a value during that boot.
struct CompanionAuthorizationCorrelation {
    std::array<std::uint8_t, kCompanionAuthorizationCorrelationBytes> bytes{};
};

[[nodiscard]] constexpr bool operator==(
    const CompanionAuthorizationCorrelation& left,
    const CompanionAuthorizationCorrelation& right) {
    for (std::size_t index = 0; index < left.bytes.size(); ++index) {
        if (left.bytes[index] != right.bytes[index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] constexpr bool operator!=(
    const CompanionAuthorizationCorrelation& left,
    const CompanionAuthorizationCorrelation& right) {
    return !(left == right);
}

[[nodiscard]] constexpr bool valid_authorization_correlation(
    const CompanionAuthorizationCorrelation& correlation) {
    for (const auto byte : correlation.bytes) {
        if (byte != 0) {
            return true;
        }
    }
    return false;
}

enum class CompanionAuthorizationPurpose : std::uint8_t {
    authorize_controller = 1,
    replace_controller = 2,
};

enum class CompanionAuthorizationClaimState : std::uint8_t {
    pending = 1,
};

enum class CompanionAuthorizationClaimOutcome : std::uint8_t {
    accepted = 1,
    denied = 2,
    replaced = 3,
};

// A timeout is never encoded as a denial. Without an authoritative terminal
// record, the phone must report authority as unknown. unknown is an explicit
// device denial whose finer reason is unavailable; unsupported is an explicit
// device denial of the requested v0 operation.
enum class CompanionAuthorizationDenyReason : std::uint8_t {
    none = 0,
    unknown = 1,
    unsupported = 2,
    physical_presence_required = 3,
    physical_presence_expired = 4,
    owner_state_conflict = 5,
    policy_denied = 6,
    persistence_unavailable = 7,
    internal_failure = 8,
};

struct CompanionAuthorizationClaimStart {
    CompanionAuthorizationPurpose purpose{
        CompanionAuthorizationPurpose::authorize_controller};
};

struct CompanionAuthorizationClaimStatus {
    CompanionAuthorizationPurpose purpose{
        CompanionAuthorizationPurpose::authorize_controller};
    CompanionAuthorizationClaimState state{
        CompanionAuthorizationClaimState::pending};
    CompanionAuthorizationCorrelation correlation{};
};

struct CompanionAuthorizationClaimResult {
    CompanionAuthorizationPurpose purpose{
        CompanionAuthorizationPurpose::authorize_controller};
    CompanionAuthorizationClaimOutcome outcome{
        CompanionAuthorizationClaimOutcome::denied};
    CompanionAuthorizationDenyReason reason{
        CompanionAuthorizationDenyReason::unknown};
    CompanionAuthorizationCorrelation correlation{};
};

enum class CompanionAuthorizationWireError : std::uint8_t {
    none = 0,
    invalid_argument,
    output_too_small,
    malformed,
    unsupported_version,
    unknown_purpose,
    unknown_state,
    unknown_outcome,
    unknown_reason,
    invalid_correlation,
    incoherent_result,
    reserved_bits_set,
    unsupported_frame_kind,
    wrong_direction,
    wrong_session,
    wrong_exchange,
    purpose_mismatch,
    correlation_mismatch,
    claim_in_progress,
    no_claim_in_progress,
    response_out_of_order,
    duplicate_response,
    stale_start,
    invalid_transport_generation,
    link_not_encrypted,
    bond_not_authenticated,
    claim_capability_not_negotiated,
    no_provisional_session,
    wrong_transport_generation,
};

struct CompanionAuthorizationWireEncodeResult {
    CompanionAuthorizationWireError error{
        CompanionAuthorizationWireError::invalid_argument};
    std::size_t encoded_bytes{0};

    [[nodiscard]] constexpr bool encoded() const {
        return error == CompanionAuthorizationWireError::none;
    }
};

template <typename T>
struct CompanionAuthorizationWireDecodeResult {
    CompanionAuthorizationWireError error{
        CompanionAuthorizationWireError::malformed};
    T value{};

    [[nodiscard]] constexpr bool decoded() const {
        return error == CompanionAuthorizationWireError::none;
    }
};

[[nodiscard]] CompanionAuthorizationWireEncodeResult
encode_companion_authorization_claim_start(
    const CompanionAuthorizationClaimStart& start,
    radio::MutableByteView output);
[[nodiscard]] CompanionAuthorizationWireDecodeResult<
    CompanionAuthorizationClaimStart>
decode_companion_authorization_claim_start(radio::ByteView encoded);

[[nodiscard]] CompanionAuthorizationWireEncodeResult
encode_companion_authorization_claim_status(
    const CompanionAuthorizationClaimStatus& status,
    radio::MutableByteView output);
[[nodiscard]] CompanionAuthorizationWireDecodeResult<
    CompanionAuthorizationClaimStatus>
decode_companion_authorization_claim_status(radio::ByteView encoded);

[[nodiscard]] CompanionAuthorizationWireEncodeResult
encode_companion_authorization_claim_result(
    const CompanionAuthorizationClaimResult& result,
    radio::MutableByteView output);
[[nodiscard]] CompanionAuthorizationWireDecodeResult<
    CompanionAuthorizationClaimResult>
decode_companion_authorization_claim_result(radio::ByteView encoded);

// Enforces exact v0 payload-to-OTC0-kind binding. Start is client-to-device;
// pending status and terminal result are device-to-client. All are complete,
// one-fragment records.
[[nodiscard]] CompanionAuthorizationWireError
validate_companion_authorization_fragment(const CompanionFragment& fragment);

enum class CompanionAuthorizationResponsePhase : std::uint8_t {
    idle = 0,
    awaiting_pending,
    pending,
    terminal,
};

struct CompanionAuthorizationResponseObservation {
    CompanionAuthorizationWireError error{
        CompanionAuthorizationWireError::no_claim_in_progress};
    CompanionAuthorizationResponsePhase phase{
        CompanionAuthorizationResponsePhase::idle};
    CompanionAuthorizationClaimOutcome outcome{
        CompanionAuthorizationClaimOutcome::denied};
    CompanionAuthorizationDenyReason reason{
        CompanionAuthorizationDenyReason::unknown};

    [[nodiscard]] constexpr bool accepted() const {
        return error == CompanionAuthorizationWireError::none;
    }

    [[nodiscard]] constexpr bool terminal() const {
        return accepted() &&
               phase == CompanionAuthorizationResponsePhase::terminal;
    }
};

struct CompanionAuthorizationResponseStatus {
    CompanionAuthorizationResponsePhase phase{
        CompanionAuthorizationResponsePhase::idle};
    CompanionAuthorizationPurpose purpose{
        CompanionAuthorizationPurpose::authorize_controller};
    bool correlation_present{false};
    bool terminal_received{false};
    bool provisional_session_open{false};
    bool application_authorized{false};
};

struct CompanionAuthorizationProvisionalEvidence {
    // Local owner-generation only; never serialized. It is not an address,
    // public identity, credential, or device-issued correlation.
    std::uint64_t transport_generation{0};
    bool link_encrypted{false};
    bool authenticated_bond{false};
    // Must come from a future explicit exact-version Protocol Info capability,
    // never from a failed write, timeout, advertised UUID, or inference.
    bool claim_wire_supported{false};
};

// Fixed-memory provisional response gate. One externally serialized instance
// belongs to one connection owner. A target/Android adapter may open this
// restricted phase only after the GATT connection is encrypted and the bond is
// authenticated, but before application authorization. Protocol Info and a
// device-issued nonzero provisional session nonce must already be available;
// how the future GATT target exposes that nonce is outside this wire-codec
// slice. During this phase only the three authorization kinds are admissible;
// normal snapshot/action traffic remains denied. Accepted/Replaced transitions
// this exact transport generation and session to a client-observed
// application-authorized result; it is not device authority by itself. The
// future device adapter must independently commit and enforce authorization;
// denied, timeout/cancel, or disconnect does not. A normal authoritative
// snapshot negotiation follows that transition.
//
// OTB0/v0 currently has no authorization-claim capability bit. This codec and
// tracker therefore remain default-disabled and are not evidence that a peer
// supports these kinds. A later live transport must negotiate explicit claim
// capability/version evidence before writing Claim Start. A write failure or
// timeout is local unknown state, never an authoritative unsupported denial.
//
// The gate publishes a terminal result only after an exact pending record from
// the current transport generation/session/exchange/purpose and accepts at most
// one terminal. Timeout is local unknown state and never fabricates a wire
// denial. It retains only redacted public status.
class CompanionAuthorizationResponseTracker {
public:
    [[nodiscard]] CompanionAuthorizationWireError open_provisional_session(
        const CompanionAuthorizationProvisionalEvidence& evidence,
        std::uint32_t device_generated_session_nonce);
    [[nodiscard]] CompanionAuthorizationWireError begin(
        std::uint64_t transport_generation,
        const CompanionFragment& start_fragment);
    [[nodiscard]] CompanionAuthorizationResponseObservation observe(
        std::uint64_t transport_generation,
        const CompanionFragment& response_fragment);
    [[nodiscard]] CompanionAuthorizationWireError cancel(
        std::uint64_t transport_generation);
    [[nodiscard]] CompanionAuthorizationWireError close_transport_generation(
        std::uint64_t transport_generation);
    [[nodiscard]] bool allows_normal_companion_traffic(
        std::uint64_t transport_generation) const;
    [[nodiscard]] CompanionAuthorizationResponseStatus status() const;

private:
    [[nodiscard]] CompanionAuthorizationResponseObservation reject(
        CompanionAuthorizationWireError error) const;

    CompanionAuthorizationResponsePhase phase_{
        CompanionAuthorizationResponsePhase::idle};
    CompanionAuthorizationPurpose purpose_{
        CompanionAuthorizationPurpose::authorize_controller};
    std::uint32_t session_nonce_{0};
    std::uint32_t exchange_id_{0};
    std::uint32_t last_exchange_id_{0};
    std::uint64_t transport_generation_{0};
    std::uint64_t last_transport_generation_{0};
    CompanionAuthorizationCorrelation correlation_{};
    bool provisional_session_open_{false};
    bool application_authorized_{false};
};

}  // namespace opentrail::companion
