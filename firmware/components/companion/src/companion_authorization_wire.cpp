#include "opentrail/companion_authorization_wire.hpp"

#include <array>

namespace opentrail::companion {
namespace {

constexpr std::array<std::uint8_t, 4> kClaimStartMagic{'O', 'T', 'L', '0'};
constexpr std::array<std::uint8_t, 4> kClaimStatusMagic{'O', 'T', 'P', '0'};
constexpr std::array<std::uint8_t, 4> kClaimResultMagic{'O', 'T', 'F', '0'};

template <std::size_t N>
[[nodiscard]] bool has_magic(
    const std::uint8_t* source,
    const std::array<std::uint8_t, N>& magic) {
    for (std::size_t index = 0; index < magic.size(); ++index) {
        if (source[index] != magic[index]) {
            return false;
        }
    }
    return true;
}

template <std::size_t N>
[[nodiscard]] CompanionAuthorizationWireError validate_prefix(
    radio::ByteView encoded,
    const std::array<std::uint8_t, N>& magic,
    std::size_t expected_bytes) {
    if (encoded.data == nullptr) {
        return CompanionAuthorizationWireError::invalid_argument;
    }
    if (encoded.size != expected_bytes || !has_magic(encoded.data, magic)) {
        return CompanionAuthorizationWireError::malformed;
    }
    if (encoded.data[4] != kCompanionAuthorizationWireMajor ||
        encoded.data[5] != kCompanionAuthorizationWireMinor) {
        return CompanionAuthorizationWireError::unsupported_version;
    }
    return CompanionAuthorizationWireError::none;
}

[[nodiscard]] constexpr bool known_purpose(
    CompanionAuthorizationPurpose purpose) {
    return purpose == CompanionAuthorizationPurpose::authorize_controller ||
           purpose == CompanionAuthorizationPurpose::replace_controller;
}

[[nodiscard]] constexpr bool known_state(
    CompanionAuthorizationClaimState state) {
    return state == CompanionAuthorizationClaimState::pending;
}

[[nodiscard]] constexpr bool known_outcome(
    CompanionAuthorizationClaimOutcome outcome) {
    return outcome >= CompanionAuthorizationClaimOutcome::accepted &&
           outcome <= CompanionAuthorizationClaimOutcome::replaced;
}

[[nodiscard]] constexpr bool known_reason(
    CompanionAuthorizationDenyReason reason) {
    return reason >= CompanionAuthorizationDenyReason::none &&
           reason <= CompanionAuthorizationDenyReason::internal_failure;
}

[[nodiscard]] CompanionAuthorizationWireError validate_result(
    const CompanionAuthorizationClaimResult& result) {
    if (!known_purpose(result.purpose)) {
        return CompanionAuthorizationWireError::unknown_purpose;
    }
    if (!known_outcome(result.outcome)) {
        return CompanionAuthorizationWireError::unknown_outcome;
    }
    if (!known_reason(result.reason)) {
        return CompanionAuthorizationWireError::unknown_reason;
    }
    if (!valid_authorization_correlation(result.correlation)) {
        return CompanionAuthorizationWireError::invalid_correlation;
    }
    if (result.outcome == CompanionAuthorizationClaimOutcome::denied) {
        return result.reason == CompanionAuthorizationDenyReason::none
                   ? CompanionAuthorizationWireError::incoherent_result
                   : CompanionAuthorizationWireError::none;
    }
    if (result.reason != CompanionAuthorizationDenyReason::none) {
        return CompanionAuthorizationWireError::incoherent_result;
    }
    if (result.outcome == CompanionAuthorizationClaimOutcome::accepted &&
        result.purpose !=
            CompanionAuthorizationPurpose::authorize_controller) {
        return CompanionAuthorizationWireError::incoherent_result;
    }
    if (result.outcome == CompanionAuthorizationClaimOutcome::replaced &&
        result.purpose != CompanionAuthorizationPurpose::replace_controller) {
        return CompanionAuthorizationWireError::incoherent_result;
    }
    return CompanionAuthorizationWireError::none;
}

template <std::size_t N>
[[nodiscard]] CompanionAuthorizationWireEncodeResult write_candidate(
    const std::array<std::uint8_t, N>& candidate,
    radio::MutableByteView output) {
    if (output.data == nullptr) {
        return {CompanionAuthorizationWireError::invalid_argument, 0};
    }
    if (output.size < candidate.size()) {
        return {CompanionAuthorizationWireError::output_too_small,
                candidate.size()};
    }
    for (std::size_t index = 0; index < candidate.size(); ++index) {
        output.data[index] = candidate[index];
    }
    return {CompanionAuthorizationWireError::none, candidate.size()};
}

template <std::size_t N>
void write_magic(std::array<std::uint8_t, N>& candidate,
                 const std::array<std::uint8_t, 4>& magic) {
    for (std::size_t index = 0; index < magic.size(); ++index) {
        candidate[index] = magic[index];
    }
    candidate[4] = kCompanionAuthorizationWireMajor;
    candidate[5] = kCompanionAuthorizationWireMinor;
}

void write_correlation(
    std::uint8_t* destination,
    const CompanionAuthorizationCorrelation& correlation) {
    for (std::size_t index = 0; index < correlation.bytes.size(); ++index) {
        destination[index] = correlation.bytes[index];
    }
}

[[nodiscard]] CompanionAuthorizationCorrelation read_correlation(
    const std::uint8_t* source) {
    CompanionAuthorizationCorrelation correlation{};
    for (std::size_t index = 0; index < correlation.bytes.size(); ++index) {
        correlation.bytes[index] = source[index];
    }
    return correlation;
}

}  // namespace

CompanionAuthorizationWireEncodeResult
encode_companion_authorization_claim_start(
    const CompanionAuthorizationClaimStart& start,
    radio::MutableByteView output) {
    if (output.data == nullptr) {
        return {CompanionAuthorizationWireError::invalid_argument, 0};
    }
    if (!known_purpose(start.purpose)) {
        return {CompanionAuthorizationWireError::unknown_purpose, 0};
    }
    std::array<std::uint8_t, kCompanionAuthorizationClaimStartBytes>
        candidate{};
    write_magic(candidate, kClaimStartMagic);
    candidate[6] = static_cast<std::uint8_t>(start.purpose);
    return write_candidate(candidate, output);
}

CompanionAuthorizationWireDecodeResult<CompanionAuthorizationClaimStart>
decode_companion_authorization_claim_start(radio::ByteView encoded) {
    const auto prefix = validate_prefix(
        encoded, kClaimStartMagic, kCompanionAuthorizationClaimStartBytes);
    if (prefix != CompanionAuthorizationWireError::none) {
        return {prefix, {}};
    }
    if (encoded.data[7] != 0) {
        return {CompanionAuthorizationWireError::reserved_bits_set, {}};
    }
    CompanionAuthorizationClaimStart start{
        static_cast<CompanionAuthorizationPurpose>(encoded.data[6])};
    if (!known_purpose(start.purpose)) {
        return {CompanionAuthorizationWireError::unknown_purpose, {}};
    }
    return {CompanionAuthorizationWireError::none, start};
}

CompanionAuthorizationWireEncodeResult
encode_companion_authorization_claim_status(
    const CompanionAuthorizationClaimStatus& status,
    radio::MutableByteView output) {
    if (output.data == nullptr) {
        return {CompanionAuthorizationWireError::invalid_argument, 0};
    }
    if (!known_purpose(status.purpose)) {
        return {CompanionAuthorizationWireError::unknown_purpose, 0};
    }
    if (!known_state(status.state)) {
        return {CompanionAuthorizationWireError::unknown_state, 0};
    }
    if (!valid_authorization_correlation(status.correlation)) {
        return {CompanionAuthorizationWireError::invalid_correlation, 0};
    }
    std::array<std::uint8_t, kCompanionAuthorizationClaimStatusBytes>
        candidate{};
    write_magic(candidate, kClaimStatusMagic);
    candidate[6] = static_cast<std::uint8_t>(status.purpose);
    candidate[7] = static_cast<std::uint8_t>(status.state);
    write_correlation(candidate.data() + 8, status.correlation);
    return write_candidate(candidate, output);
}

CompanionAuthorizationWireDecodeResult<CompanionAuthorizationClaimStatus>
decode_companion_authorization_claim_status(radio::ByteView encoded) {
    const auto prefix = validate_prefix(
        encoded, kClaimStatusMagic, kCompanionAuthorizationClaimStatusBytes);
    if (prefix != CompanionAuthorizationWireError::none) {
        return {prefix, {}};
    }
    CompanionAuthorizationClaimStatus status{};
    status.purpose =
        static_cast<CompanionAuthorizationPurpose>(encoded.data[6]);
    status.state =
        static_cast<CompanionAuthorizationClaimState>(encoded.data[7]);
    status.correlation = read_correlation(encoded.data + 8);
    if (!known_purpose(status.purpose)) {
        return {CompanionAuthorizationWireError::unknown_purpose, {}};
    }
    if (!known_state(status.state)) {
        return {CompanionAuthorizationWireError::unknown_state, {}};
    }
    if (!valid_authorization_correlation(status.correlation)) {
        return {CompanionAuthorizationWireError::invalid_correlation, {}};
    }
    return {CompanionAuthorizationWireError::none, status};
}

CompanionAuthorizationWireEncodeResult
encode_companion_authorization_claim_result(
    const CompanionAuthorizationClaimResult& result,
    radio::MutableByteView output) {
    if (output.data == nullptr) {
        return {CompanionAuthorizationWireError::invalid_argument, 0};
    }
    const auto validation = validate_result(result);
    if (validation != CompanionAuthorizationWireError::none) {
        return {validation, 0};
    }
    std::array<std::uint8_t, kCompanionAuthorizationClaimResultBytes>
        candidate{};
    write_magic(candidate, kClaimResultMagic);
    candidate[6] = static_cast<std::uint8_t>(result.purpose);
    candidate[7] = static_cast<std::uint8_t>(result.outcome);
    candidate[8] = static_cast<std::uint8_t>(result.reason);
    write_correlation(candidate.data() + 12, result.correlation);
    return write_candidate(candidate, output);
}

CompanionAuthorizationWireDecodeResult<CompanionAuthorizationClaimResult>
decode_companion_authorization_claim_result(radio::ByteView encoded) {
    const auto prefix = validate_prefix(
        encoded, kClaimResultMagic, kCompanionAuthorizationClaimResultBytes);
    if (prefix != CompanionAuthorizationWireError::none) {
        return {prefix, {}};
    }
    if (encoded.data[9] != 0 || encoded.data[10] != 0 ||
        encoded.data[11] != 0) {
        return {CompanionAuthorizationWireError::reserved_bits_set, {}};
    }
    CompanionAuthorizationClaimResult result{};
    result.purpose =
        static_cast<CompanionAuthorizationPurpose>(encoded.data[6]);
    result.outcome =
        static_cast<CompanionAuthorizationClaimOutcome>(encoded.data[7]);
    result.reason =
        static_cast<CompanionAuthorizationDenyReason>(encoded.data[8]);
    result.correlation = read_correlation(encoded.data + 12);
    const auto validation = validate_result(result);
    return validation == CompanionAuthorizationWireError::none
               ? CompanionAuthorizationWireDecodeResult<
                     CompanionAuthorizationClaimResult>{
                     CompanionAuthorizationWireError::none, result}
               : CompanionAuthorizationWireDecodeResult<
                     CompanionAuthorizationClaimResult>{validation, {}};
}

CompanionAuthorizationWireError validate_companion_authorization_fragment(
    const CompanionFragment& fragment) {
    if (fragment.fragment_index != 0 || fragment.fragment_count != 1 ||
        fragment.payload_bytes > fragment.payload.size()) {
        return CompanionAuthorizationWireError::malformed;
    }
    const radio::ByteView payload{
        fragment.payload.data(), fragment.payload_bytes};
    switch (fragment.kind) {
        case CompanionFrameKind::authorization_claim_start:
            return decode_companion_authorization_claim_start(payload).error;
        case CompanionFrameKind::authorization_claim_status:
            return decode_companion_authorization_claim_status(payload).error;
        case CompanionFrameKind::authorization_claim_result:
            return decode_companion_authorization_claim_result(payload).error;
        case CompanionFrameKind::snapshot_request:
        case CompanionFrameKind::action_request:
        case CompanionFrameKind::snapshot:
        case CompanionFrameKind::action_result:
        case CompanionFrameKind::event:
            return CompanionAuthorizationWireError::unsupported_frame_kind;
    }
    return CompanionAuthorizationWireError::unsupported_frame_kind;
}

CompanionAuthorizationResponseObservation
CompanionAuthorizationResponseTracker::reject(
    CompanionAuthorizationWireError error) const {
    return {error, phase_, CompanionAuthorizationClaimOutcome::denied,
            CompanionAuthorizationDenyReason::unknown};
}

CompanionAuthorizationWireError
CompanionAuthorizationResponseTracker::open_provisional_session(
    const CompanionAuthorizationProvisionalEvidence& evidence,
    std::uint32_t device_generated_session_nonce) {
    if (evidence.transport_generation == 0 ||
        device_generated_session_nonce == 0) {
        return CompanionAuthorizationWireError::invalid_transport_generation;
    }
    if (!evidence.link_encrypted) {
        return CompanionAuthorizationWireError::link_not_encrypted;
    }
    if (!evidence.authenticated_bond) {
        return CompanionAuthorizationWireError::bond_not_authenticated;
    }
    if (!evidence.claim_wire_supported) {
        return CompanionAuthorizationWireError::
            claim_capability_not_negotiated;
    }
    if (transport_generation_ != 0 || provisional_session_open_ ||
        application_authorized_) {
        return CompanionAuthorizationWireError::claim_in_progress;
    }
    if (evidence.transport_generation <= last_transport_generation_) {
        return CompanionAuthorizationWireError::stale_start;
    }

    phase_ = CompanionAuthorizationResponsePhase::idle;
    purpose_ = CompanionAuthorizationPurpose::authorize_controller;
    session_nonce_ = device_generated_session_nonce;
    exchange_id_ = 0;
    last_exchange_id_ = 0;
    transport_generation_ = evidence.transport_generation;
    last_transport_generation_ = evidence.transport_generation;
    correlation_ = {};
    provisional_session_open_ = true;
    application_authorized_ = false;
    return CompanionAuthorizationWireError::none;
}

CompanionAuthorizationWireError CompanionAuthorizationResponseTracker::begin(
    std::uint64_t transport_generation,
    const CompanionFragment& start_fragment) {
    if (!provisional_session_open_) {
        return CompanionAuthorizationWireError::no_provisional_session;
    }
    if (transport_generation == 0 ||
        transport_generation != transport_generation_) {
        return CompanionAuthorizationWireError::wrong_transport_generation;
    }
    if (start_fragment.kind !=
        CompanionFrameKind::authorization_claim_start) {
        return CompanionAuthorizationWireError::wrong_direction;
    }
    const auto validation =
        validate_companion_authorization_fragment(start_fragment);
    if (validation != CompanionAuthorizationWireError::none) {
        return validation;
    }
    if (phase_ == CompanionAuthorizationResponsePhase::awaiting_pending ||
        phase_ == CompanionAuthorizationResponsePhase::pending) {
        return CompanionAuthorizationWireError::claim_in_progress;
    }
    if (start_fragment.session_nonce == 0 ||
        start_fragment.session_nonce != session_nonce_) {
        return CompanionAuthorizationWireError::wrong_session;
    }
    if (start_fragment.exchange_id == 0) {
        return CompanionAuthorizationWireError::wrong_exchange;
    }
    if (start_fragment.exchange_id <= last_exchange_id_) {
        return CompanionAuthorizationWireError::stale_start;
    }

    const auto decoded = decode_companion_authorization_claim_start(
        {start_fragment.payload.data(), start_fragment.payload_bytes});
    if (!decoded.decoded()) {
        return decoded.error;
    }
    purpose_ = decoded.value.purpose;
    exchange_id_ = start_fragment.exchange_id;
    last_exchange_id_ = exchange_id_;
    correlation_ = {};
    phase_ = CompanionAuthorizationResponsePhase::awaiting_pending;
    return CompanionAuthorizationWireError::none;
}

CompanionAuthorizationResponseObservation
CompanionAuthorizationResponseTracker::observe(
    std::uint64_t transport_generation,
    const CompanionFragment& response_fragment) {
    if (transport_generation == 0 ||
        transport_generation != transport_generation_) {
        return reject(
            CompanionAuthorizationWireError::wrong_transport_generation);
    }
    if (phase_ == CompanionAuthorizationResponsePhase::terminal) {
        return reject(CompanionAuthorizationWireError::duplicate_response);
    }
    if (phase_ == CompanionAuthorizationResponsePhase::idle) {
        return reject(
            CompanionAuthorizationWireError::no_claim_in_progress);
    }
    if (response_fragment.kind !=
            CompanionFrameKind::authorization_claim_status &&
        response_fragment.kind !=
            CompanionFrameKind::authorization_claim_result) {
        return reject(CompanionAuthorizationWireError::wrong_direction);
    }
    const auto validation =
        validate_companion_authorization_fragment(response_fragment);
    if (validation != CompanionAuthorizationWireError::none) {
        return reject(validation);
    }
    if (response_fragment.session_nonce != session_nonce_) {
        return reject(CompanionAuthorizationWireError::wrong_session);
    }
    if (response_fragment.exchange_id != exchange_id_) {
        return reject(CompanionAuthorizationWireError::wrong_exchange);
    }

    if (response_fragment.kind ==
        CompanionFrameKind::authorization_claim_status) {
        if (phase_ != CompanionAuthorizationResponsePhase::awaiting_pending) {
            return reject(
                CompanionAuthorizationWireError::duplicate_response);
        }
        const auto decoded = decode_companion_authorization_claim_status(
            {response_fragment.payload.data(),
             response_fragment.payload_bytes});
        if (decoded.value.purpose != purpose_) {
            return reject(CompanionAuthorizationWireError::purpose_mismatch);
        }
        correlation_ = decoded.value.correlation;
        phase_ = CompanionAuthorizationResponsePhase::pending;
        return {CompanionAuthorizationWireError::none, phase_,
                CompanionAuthorizationClaimOutcome::denied,
                CompanionAuthorizationDenyReason::unknown};
    }

    if (phase_ != CompanionAuthorizationResponsePhase::pending) {
        return reject(
            CompanionAuthorizationWireError::response_out_of_order);
    }
    const auto decoded = decode_companion_authorization_claim_result(
        {response_fragment.payload.data(), response_fragment.payload_bytes});
    if (decoded.value.purpose != purpose_) {
        return reject(CompanionAuthorizationWireError::purpose_mismatch);
    }
    if (decoded.value.correlation != correlation_) {
        return reject(
            CompanionAuthorizationWireError::correlation_mismatch);
    }
    phase_ = CompanionAuthorizationResponsePhase::terminal;
    application_authorized_ =
        decoded.value.outcome ==
            CompanionAuthorizationClaimOutcome::accepted ||
        decoded.value.outcome ==
            CompanionAuthorizationClaimOutcome::replaced;
    provisional_session_open_ = false;
    return {CompanionAuthorizationWireError::none, phase_,
            decoded.value.outcome, decoded.value.reason};
}

CompanionAuthorizationWireError CompanionAuthorizationResponseTracker::cancel(
    std::uint64_t transport_generation) {
    if (transport_generation == 0 ||
        transport_generation != transport_generation_) {
        return CompanionAuthorizationWireError::wrong_transport_generation;
    }
    if (phase_ == CompanionAuthorizationResponsePhase::terminal) {
        return CompanionAuthorizationWireError::duplicate_response;
    }
    phase_ = CompanionAuthorizationResponsePhase::idle;
    purpose_ = CompanionAuthorizationPurpose::authorize_controller;
    exchange_id_ = 0;
    correlation_ = {};
    provisional_session_open_ = false;
    application_authorized_ = false;
    return CompanionAuthorizationWireError::none;
}

CompanionAuthorizationWireError
CompanionAuthorizationResponseTracker::close_transport_generation(
    std::uint64_t transport_generation) {
    if (transport_generation == 0 ||
        transport_generation != transport_generation_) {
        return CompanionAuthorizationWireError::wrong_transport_generation;
    }
    phase_ = CompanionAuthorizationResponsePhase::idle;
    purpose_ = CompanionAuthorizationPurpose::authorize_controller;
    session_nonce_ = 0;
    exchange_id_ = 0;
    last_exchange_id_ = 0;
    transport_generation_ = 0;
    correlation_ = {};
    provisional_session_open_ = false;
    application_authorized_ = false;
    return CompanionAuthorizationWireError::none;
}

bool CompanionAuthorizationResponseTracker::allows_normal_companion_traffic(
    std::uint64_t transport_generation) const {
    return transport_generation != 0 &&
           transport_generation == transport_generation_ &&
           application_authorized_;
}

CompanionAuthorizationResponseStatus
CompanionAuthorizationResponseTracker::status() const {
    return {phase_, purpose_, valid_authorization_correlation(correlation_),
            phase_ == CompanionAuthorizationResponsePhase::terminal,
            provisional_session_open_, application_authorized_};
}

}  // namespace opentrail::companion
