#include "opentrail/companion_gatt_authorization.hpp"

#include <array>
#include <limits>

namespace opentrail::companion {
namespace {

constexpr std::array<std::uint8_t, 4> kProtocolInfoMagic{'O', 'T', 'B', '0'};
void write_u16_le(std::uint8_t* destination, std::uint16_t value) {
    destination[0] = static_cast<std::uint8_t>(value & 0xFFU);
    destination[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void write_u32_le(std::uint8_t* destination, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        destination[index] =
            static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

std::uint16_t read_u16_le(const std::uint8_t* source) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(source[0]) |
        (static_cast<std::uint16_t>(source[1]) << 8U));
}

std::uint32_t read_u32_le(const std::uint8_t* source) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(source[index]) << (index * 8U);
    }
    return value;
}

bool has_protocol_info_magic(const std::uint8_t* source) {
    for (std::size_t index = 0; index < kProtocolInfoMagic.size(); ++index) {
        if (source[index] != kProtocolInfoMagic[index]) {
            return false;
        }
    }
    return true;
}

CompanionAuthorizationProtocolInfoError validate_protocol_info(
    const CompanionAuthorizationProtocolInfo& info) {
    if (info.role != CompanionDeviceRole::screenless_client) {
        return CompanionAuthorizationProtocolInfoError::invalid_argument;
    }
    if (info.capabilities != kCompanionAuthorizationCapabilityMask) {
        return CompanionAuthorizationProtocolInfoError::invalid_capability;
    }
    if (info.max_fragment_payload_bytes <
            kCompanionAuthorizationMinimumFragmentPayloadBytes ||
        info.max_fragment_payload_bytes > kCompanionMaxFragmentPayloadBytes ||
        info.minimum_normal_att_mtu < kCompanionMinimumAttMtu ||
        info.max_fragment_count == 0 ||
        info.max_fragment_count > kCompanionMaxFragmentCount ||
        info.max_active_controllers != 1) {
        return CompanionAuthorizationProtocolInfoError::invalid_limit;
    }
    if (info.provisional_session_nonce == 0) {
        return CompanionAuthorizationProtocolInfoError::invalid_session_nonce;
    }
    return CompanionAuthorizationProtocolInfoError::none;
}

bool valid_handles(const CompanionGattAuthorizationHandles& handles) {
    const std::array<std::uint16_t, 4> values{
        handles.protocol_info_value,
        handles.command_value,
        handles.stream_value,
        handles.stream_cccd,
    };
    for (std::size_t left = 0; left < values.size(); ++left) {
        if (values[left] == 0) {
            return false;
        }
        for (std::size_t right = left + 1; right < values.size(); ++right) {
            if (values[left] == values[right]) {
                return false;
            }
        }
    }
    return true;
}

bool valid_evidence(
    const CompanionGattAuthorizationConnectionEvidence& evidence) {
    const auto& claim = evidence.controller_claim;
    return evidence.att_mtu >= kCompanionGattDefaultAttMtu &&
           valid_bond_identity(claim.bond_identity) &&
           claim.boot_challenge != 0 && claim.session_challenge != 0 &&
           claim.controller_binding != 0;
}

bool same_claim(const CompanionControllerClaim& left,
                const CompanionControllerClaim& right) {
    return left.bond_identity == right.bond_identity &&
           left.boot_challenge == right.boot_challenge &&
           left.session_challenge == right.session_challenge &&
           left.controller_binding == right.controller_binding;
}

bool coherent_decision(
    CompanionAuthorizationPurpose purpose,
    const CompanionControllerClaim& claim,
    const CompanionGattAuthorizationDecision& decision) {
    if (decision.error != CompanionGattAuthorizationAuthorityError::none) {
        return false;
    }
    switch (decision.outcome) {
        case CompanionAuthorizationClaimOutcome::accepted:
            return purpose ==
                       CompanionAuthorizationPurpose::authorize_controller &&
                   decision.reason == CompanionAuthorizationDenyReason::none &&
                   decision.controller_binding == claim.controller_binding;
        case CompanionAuthorizationClaimOutcome::replaced:
            return purpose ==
                       CompanionAuthorizationPurpose::replace_controller &&
                   decision.reason == CompanionAuthorizationDenyReason::none &&
                   decision.controller_binding == claim.controller_binding;
        case CompanionAuthorizationClaimOutcome::denied:
            return decision.reason != CompanionAuthorizationDenyReason::none &&
                   decision.controller_binding == 0;
    }
    return false;
}

class ScopedOperation final {
public:
    explicit ScopedOperation(bool& active) : active_(active) {
        active_ = true;
    }
    ~ScopedOperation() {
        active_ = false;
    }
    ScopedOperation(const ScopedOperation&) = delete;
    ScopedOperation& operator=(const ScopedOperation&) = delete;

private:
    bool& active_;
};

}  // namespace

CompanionAuthorizationProtocolInfoEncodeResult
encode_companion_authorization_protocol_info(
    const CompanionAuthorizationProtocolInfo& info,
    radio::MutableByteView output) {
    if (output.data == nullptr) {
        return {CompanionAuthorizationProtocolInfoError::invalid_argument, 0};
    }
    if (output.size < kCompanionAuthorizationProtocolInfoBytes) {
        return {CompanionAuthorizationProtocolInfoError::output_too_small,
                kCompanionAuthorizationProtocolInfoBytes};
    }
    const auto validation = validate_protocol_info(info);
    if (validation != CompanionAuthorizationProtocolInfoError::none) {
        return {validation, 0};
    }

    std::array<std::uint8_t, kCompanionAuthorizationProtocolInfoBytes>
        candidate{};
    for (std::size_t index = 0; index < kProtocolInfoMagic.size(); ++index) {
        candidate[index] = kProtocolInfoMagic[index];
    }
    candidate[4] = kCompanionAuthorizationProtocolMajor;
    candidate[5] = kCompanionAuthorizationProtocolMinor;
    candidate[6] = static_cast<std::uint8_t>(info.role);
    candidate[7] = info.capabilities;
    write_u16_le(candidate.data() + 8, info.max_fragment_payload_bytes);
    write_u16_le(candidate.data() + 10, info.minimum_normal_att_mtu);
    candidate[12] = info.max_fragment_count;
    candidate[13] = info.max_active_controllers;
    write_u32_le(candidate.data() + 14, info.provisional_session_nonce);
    candidate[18] = 0;
    candidate[19] = 0;

    for (std::size_t index = 0; index < candidate.size(); ++index) {
        output.data[index] = candidate[index];
    }
    return {CompanionAuthorizationProtocolInfoError::none, candidate.size()};
}

CompanionAuthorizationProtocolInfoDecodeResult
decode_companion_authorization_protocol_info(radio::ByteView encoded) {
    if (encoded.data == nullptr) {
        return {CompanionAuthorizationProtocolInfoError::invalid_argument, {}};
    }
    if (encoded.size != kCompanionAuthorizationProtocolInfoBytes ||
        !has_protocol_info_magic(encoded.data)) {
        return {CompanionAuthorizationProtocolInfoError::malformed, {}};
    }
    if (encoded.data[4] != kCompanionAuthorizationProtocolMajor ||
        encoded.data[5] != kCompanionAuthorizationProtocolMinor) {
        return {CompanionAuthorizationProtocolInfoError::unsupported_version,
                {}};
    }
    if (encoded.data[18] != 0 || encoded.data[19] != 0) {
        return {CompanionAuthorizationProtocolInfoError::reserved_bits_set,
                {}};
    }

    CompanionAuthorizationProtocolInfo info{};
    info.role = static_cast<CompanionDeviceRole>(encoded.data[6]);
    info.capabilities = encoded.data[7];
    info.max_fragment_payload_bytes = read_u16_le(encoded.data + 8);
    info.minimum_normal_att_mtu = read_u16_le(encoded.data + 10);
    info.max_fragment_count = encoded.data[12];
    info.max_active_controllers = encoded.data[13];
    info.provisional_session_nonce = read_u32_le(encoded.data + 14);
    const auto validation = validate_protocol_info(info);
    if (validation != CompanionAuthorizationProtocolInfoError::none) {
        return {validation, {}};
    }
    return {CompanionAuthorizationProtocolInfoError::none, info};
}

CompanionGattAuthorizationLifecycle::CompanionGattAuthorizationLifecycle(
    CompanionRequestCoordinator& normal_coordinator,
    CompanionGattIndicationSink& indication_sink,
    CompanionGattAuthorizationCorrelationIssuer& correlation_issuer,
    CompanionGattAuthorizationAuthority& authority,
    CompanionGattAuthorizationPolicy policy)
    : indication_sink_(indication_sink),
      correlation_issuer_(correlation_issuer),
      authority_(authority),
      policy_(policy),
      normal_lifecycle_(
          normal_coordinator,
          indication_sink,
          {policy.indication_timeout_ms,
           kCompanionAuthorizationFirstNormalDeliveryToken,
           std::numeric_limits<std::uint64_t>::max()}) {
    if (policy_.indication_timeout_ms == 0 ||
        policy_.claim_timeout_ms == 0 ||
        policy_.claim_timeout_ms > 30000) {
        faulted_ = true;
    }
}

CompanionGattAuthorizationError
CompanionGattAuthorizationLifecycle::register_handles(
    CompanionGattAuthorizationHandles handles) {
    if (operation_active_) {
        return CompanionGattAuthorizationError::reentrant_call;
    }
    ScopedOperation operation(operation_active_);
    if (faulted_) {
        return CompanionGattAuthorizationError::internal_failure;
    }
    if (handles_registered_) {
        return CompanionGattAuthorizationError::handles_already_registered;
    }
    if (phase_ != CompanionGattAuthorizationPhase::idle ||
        !valid_handles(handles)) {
        return CompanionGattAuthorizationError::invalid_argument;
    }
    const auto normal_error = normal_lifecycle_.register_handles(
        {handles.command_value, handles.stream_value, handles.stream_cccd});
    if (normal_error != CompanionGattLifecycleError::none) {
        faulted_ = true;
        return CompanionGattAuthorizationError::internal_failure;
    }
    handles_ = handles;
    handles_registered_ = true;
    return CompanionGattAuthorizationError::none;
}

CompanionGattAuthorizationError CompanionGattAuthorizationLifecycle::connect(
    std::uint16_t connection_handle,
    std::uint64_t transport_generation) {
    if (operation_active_) {
        return CompanionGattAuthorizationError::reentrant_call;
    }
    ScopedOperation operation(operation_active_);
    if (faulted_) {
        return CompanionGattAuthorizationError::internal_failure;
    }
    if (!handles_registered_) {
        return CompanionGattAuthorizationError::handles_not_registered;
    }
    if (connection_handle == kCompanionGattInvalidConnectionHandle ||
        transport_generation == 0) {
        return CompanionGattAuthorizationError::invalid_argument;
    }
    if (phase_ != CompanionGattAuthorizationPhase::idle) {
        return connection_handle == connection_handle_ &&
                       transport_generation == transport_generation_
                   ? CompanionGattAuthorizationError::connection_in_use
                   : CompanionGattAuthorizationError::wrong_connection;
    }
    if (transport_generation <= last_transport_generation_) {
        return CompanionGattAuthorizationError::stale_transport_generation;
    }
    connection_handle_ = connection_handle;
    transport_generation_ = transport_generation;
    last_transport_generation_ = transport_generation;
    evidence_ = {};
    bound_evidence_ = {};
    phase_ = CompanionGattAuthorizationPhase::connected;
    time_observed_ = false;
    last_now_ms_ = 0;
    return CompanionGattAuthorizationError::none;
}

CompanionGattAuthorizationError
CompanionGattAuthorizationLifecycle::connection_error(
    std::uint16_t connection_handle,
    std::uint64_t transport_generation) const {
    if (faulted_) {
        return CompanionGattAuthorizationError::internal_failure;
    }
    if (phase_ == CompanionGattAuthorizationPhase::idle) {
        return CompanionGattAuthorizationError::no_connection;
    }
    if (connection_handle != connection_handle_) {
        return CompanionGattAuthorizationError::wrong_connection;
    }
    if (transport_generation == 0 ||
        transport_generation != transport_generation_) {
        return CompanionGattAuthorizationError::wrong_transport_generation;
    }
    if (phase_ ==
        CompanionGattAuthorizationPhase::blocked_until_disconnect) {
        return CompanionGattAuthorizationError::blocked_until_disconnect;
    }
    return CompanionGattAuthorizationError::none;
}

CompanionGattAuthorizationError
CompanionGattAuthorizationLifecycle::secure_error() const {
    if (!valid_evidence(evidence_) ||
        !evidence_.controller_claim.link_encrypted ||
        !evidence_.controller_claim.authenticated_bond) {
        return CompanionGattAuthorizationError::insecure_link;
    }
    return CompanionGattAuthorizationError::none;
}

CompanionGattAuthorizationError
CompanionGattAuthorizationLifecycle::claim_path_error() const {
    const auto security = secure_error();
    if (security != CompanionGattAuthorizationError::none) {
        return security;
    }
    if (evidence_.att_mtu < kCompanionAuthorizationMinimumAttMtu) {
        return CompanionGattAuthorizationError::mtu_too_small;
    }
    if (!protocol_info_read_) {
        return CompanionGattAuthorizationError::protocol_info_not_read;
    }
    if (!indication_subscribed_) {
        return CompanionGattAuthorizationError::subscription_required;
    }
    return CompanionGattAuthorizationError::none;
}

CompanionGattAuthorizationError
CompanionGattAuthorizationLifecycle::update_connection_evidence(
    std::uint16_t connection_handle,
    std::uint64_t transport_generation,
    CompanionGattAuthorizationConnectionEvidence evidence) {
    if (operation_active_) {
        return CompanionGattAuthorizationError::reentrant_call;
    }
    ScopedOperation operation(operation_active_);
    const auto state_error =
        connection_error(connection_handle, transport_generation);
    if (state_error != CompanionGattAuthorizationError::none) {
        return state_error;
    }
    const bool evidence_valid = valid_evidence(evidence);
    if (phase_ != CompanionGattAuthorizationPhase::connected &&
        (!evidence_valid ||
         !same_claim(evidence.controller_claim,
                     bound_evidence_.controller_claim) ||
         !evidence.controller_claim.link_encrypted ||
         !evidence.controller_claim.authenticated_bond ||
         evidence.att_mtu < bound_evidence_.att_mtu)) {
        const auto contained = contain(true);
        return contained == CompanionGattAuthorizationError::none
                   ? CompanionGattAuthorizationError::insecure_link
                   : contained;
    }
    if (!evidence_valid) {
        return CompanionGattAuthorizationError::invalid_argument;
    }
    evidence_ = evidence;
    if (phase_ != CompanionGattAuthorizationPhase::connected) {
        bound_evidence_.att_mtu = evidence.att_mtu;
    }
    if (application_authorized_) {
        if (!normal_connected_) {
            return promote_normal_if_ready();
        }
        const auto normal_error = normal_lifecycle_.update_connection_evidence(
            connection_handle_,
            {evidence_.att_mtu, true, true, true,
             authority_controller_binding_});
        if (normal_error != CompanionGattLifecycleError::none) {
            const auto contained = contain(true);
            return contained == CompanionGattAuthorizationError::none
                       ? CompanionGattAuthorizationError::normal_session_not_ready
                       : contained;
        }
    }
    return CompanionGattAuthorizationError::none;
}

CompanionGattAuthorizationError
CompanionGattAuthorizationLifecycle::open_provisional_session(
    std::uint16_t connection_handle,
    std::uint64_t transport_generation,
    std::uint32_t device_generated_session_nonce) {
    if (operation_active_) {
        return CompanionGattAuthorizationError::reentrant_call;
    }
    ScopedOperation operation(operation_active_);
    const auto state_error =
        connection_error(connection_handle, transport_generation);
    if (state_error != CompanionGattAuthorizationError::none) {
        return state_error;
    }
    if (phase_ != CompanionGattAuthorizationPhase::connected) {
        return CompanionGattAuthorizationError::session_not_open;
    }
    const auto security = secure_error();
    if (security != CompanionGattAuthorizationError::none) {
        return security;
    }
    if (device_generated_session_nonce == 0) {
        return CompanionGattAuthorizationError::session_nonce_invalid;
    }
    if (device_generated_session_nonce == last_session_nonce_) {
        return CompanionGattAuthorizationError::session_nonce_reused;
    }
    session_nonce_ = device_generated_session_nonce;
    last_session_nonce_ = device_generated_session_nonce;
    bound_evidence_ = evidence_;
    protocol_info_read_ = false;
    indication_subscribed_ = false;
    last_exchange_id_ = 0;
    phase_ = CompanionGattAuthorizationPhase::provisional;
    return CompanionGattAuthorizationError::none;
}

CompanionGattAuthorizationReadResult
CompanionGattAuthorizationLifecycle::read_protocol_info(
    std::uint16_t connection_handle,
    std::uint64_t transport_generation,
    std::uint16_t protocol_info_value_handle,
    radio::MutableByteView output) {
    CompanionGattAuthorizationReadResult result{};
    if (operation_active_) {
        result.error = CompanionGattAuthorizationError::reentrant_call;
        return result;
    }
    ScopedOperation operation(operation_active_);
    const auto state_error =
        connection_error(connection_handle, transport_generation);
    if (state_error != CompanionGattAuthorizationError::none) {
        result.error = state_error;
        return result;
    }
    if (protocol_info_value_handle != handles_.protocol_info_value) {
        result.error = CompanionGattAuthorizationError::wrong_attribute;
        return result;
    }
    if (phase_ == CompanionGattAuthorizationPhase::connected) {
        result.error = CompanionGattAuthorizationError::session_not_open;
        return result;
    }
    const auto security = secure_error();
    if (security != CompanionGattAuthorizationError::none) {
        result.error = security;
        return result;
    }
    const CompanionAuthorizationProtocolInfo info{
        CompanionDeviceRole::screenless_client,
        kCompanionAuthorizationCapabilityMask,
        static_cast<std::uint16_t>(kCompanionMaxFragmentPayloadBytes),
        kCompanionMinimumAttMtu,
        static_cast<std::uint8_t>(kCompanionMaxFragmentCount),
        1,
        session_nonce_,
    };
    const auto encoded =
        encode_companion_authorization_protocol_info(info, output);
    if (!encoded.encoded()) {
        result.error =
            encoded.error ==
                    CompanionAuthorizationProtocolInfoError::output_too_small
                ? CompanionGattAuthorizationError::response_path_unavailable
                : CompanionGattAuthorizationError::internal_failure;
        return result;
    }
    protocol_info_read_ = true;
    result.error = CompanionGattAuthorizationError::none;
    result.encoded_bytes = encoded.encoded_bytes;
    return result;
}

CompanionGattAuthorizationError
CompanionGattAuthorizationLifecycle::update_indication_subscription(
    std::uint16_t connection_handle,
    std::uint64_t transport_generation,
    std::uint16_t cccd_handle,
    bool indications_enabled) {
    if (operation_active_) {
        return CompanionGattAuthorizationError::reentrant_call;
    }
    ScopedOperation operation(operation_active_);
    const auto state_error =
        connection_error(connection_handle, transport_generation);
    if (state_error != CompanionGattAuthorizationError::none) {
        return state_error;
    }
    if (cccd_handle != handles_.stream_cccd) {
        return CompanionGattAuthorizationError::wrong_attribute;
    }
    if (phase_ == CompanionGattAuthorizationPhase::connected) {
        return CompanionGattAuthorizationError::session_not_open;
    }
    if (!indications_enabled) {
        const auto contained = contain(true);
        return contained == CompanionGattAuthorizationError::none
                   ? CompanionGattAuthorizationError::subscription_required
                   : contained;
    }
    const auto security = secure_error();
    if (security != CompanionGattAuthorizationError::none) {
        return security;
    }
    indication_subscribed_ = true;
    if (normal_connected_) {
        const auto normal_error =
            normal_lifecycle_.update_indication_subscription(
                connection_handle_, handles_.stream_cccd, true);
        if (normal_error != CompanionGattLifecycleError::none) {
            const auto contained = contain(true);
            return contained == CompanionGattAuthorizationError::none
                       ? CompanionGattAuthorizationError::internal_failure
                       : contained;
        }
    }
    return CompanionGattAuthorizationError::none;
}

CompanionGattAuthorizationError
CompanionGattAuthorizationLifecycle::observe_time(std::uint64_t now_ms) {
    if (time_observed_ && now_ms < last_now_ms_) {
        const auto contained = contain(true);
        return contained == CompanionGattAuthorizationError::none
                   ? CompanionGattAuthorizationError::clock_rollback
                   : contained;
    }
    time_observed_ = true;
    last_now_ms_ = now_ms;
    return CompanionGattAuthorizationError::none;
}

CompanionGattAuthorizationRequestResult
CompanionGattAuthorizationLifecycle::service_command(
    std::uint16_t connection_handle,
    std::uint64_t transport_generation,
    std::uint16_t command_value_handle,
    radio::ByteView encoded_request,
    std::uint64_t now_ms) {
    CompanionGattAuthorizationRequestResult result{};
    if (operation_active_) {
        result.error = CompanionGattAuthorizationError::reentrant_call;
        return result;
    }
    ScopedOperation operation(operation_active_);
    const auto state_error =
        connection_error(connection_handle, transport_generation);
    if (state_error != CompanionGattAuthorizationError::none) {
        result.error = state_error;
        return result;
    }
    if (command_value_handle != handles_.command_value) {
        result.error = CompanionGattAuthorizationError::wrong_attribute;
        return result;
    }
    if (encoded_request.data == nullptr || encoded_request.size == 0) {
        result.error = CompanionGattAuthorizationError::invalid_argument;
        return result;
    }
    const auto time_error = observe_time(now_ms);
    if (time_error != CompanionGattAuthorizationError::none) {
        result.error = time_error;
        return result;
    }

    if (application_authorized_) {
        if (!normal_connected_ ||
            !normal_lifecycle_.status().session_active) {
            result.error =
                CompanionGattAuthorizationError::normal_session_not_ready;
            return result;
        }
        const auto normal = normal_lifecycle_.service_command(
            connection_handle_, handles_.command_value, encoded_request,
            now_ms);
        if (normal.pending()) {
            normal_pending_delivery_token_ = normal.delivery_token;
            normal_pending_exchange_id_ = normal.coordinator.exchange_id;
            result.disposition = CompanionGattAuthorizationRequestDisposition::
                normal_indication_pending;
            result.error = CompanionGattAuthorizationError::none;
            result.delivery_token = normal.delivery_token;
            result.exchange_id = normal.coordinator.exchange_id;
            return result;
        }
        if (normal_lifecycle_.status().blocked_until_disconnect) {
            const auto contained = contain(true);
            result.error = contained == CompanionGattAuthorizationError::none
                               ? CompanionGattAuthorizationError::
                                     normal_command_rejected
                               : contained;
            return result;
        }
        result.error = CompanionGattAuthorizationError::normal_command_rejected;
        return result;
    }

    if (phase_ != CompanionGattAuthorizationPhase::provisional) {
        result.error = CompanionGattAuthorizationError::response_path_busy;
        return result;
    }
    const auto path_error = claim_path_error();
    if (path_error != CompanionGattAuthorizationError::none) {
        result.error = path_error;
        return result;
    }

    const auto decoded = decode_companion_fragment(encoded_request);
    if (!decoded.decoded()) {
        result.error = CompanionGattAuthorizationError::malformed_request;
        return result;
    }
    if (decoded.fragment.kind !=
        CompanionFrameKind::authorization_claim_start) {
        result.error = CompanionGattAuthorizationError::unsupported_request;
        return result;
    }
    if (decoded.fragment.session_nonce != session_nonce_) {
        result.error = CompanionGattAuthorizationError::wrong_session;
        return result;
    }
    if (decoded.fragment.exchange_id <= last_exchange_id_) {
        result.error = CompanionGattAuthorizationError::stale_exchange;
        return result;
    }
    if (validate_companion_authorization_fragment(decoded.fragment) !=
        CompanionAuthorizationWireError::none) {
        result.error = CompanionGattAuthorizationError::malformed_request;
        return result;
    }
    const auto start = decode_companion_authorization_claim_start(
        {decoded.fragment.payload.data(), decoded.fragment.payload_bytes});
    if (!start.decoded()) {
        result.error = CompanionGattAuthorizationError::malformed_request;
        return result;
    }

    reservation_active_ = true;
    reservation_token_ = kCompanionAuthorizationPendingDeliveryToken;
    const auto reserve_error = indication_sink_.reserve(
        connection_handle_, session_nonce_, handles_.stream_value,
        reservation_token_, kCompanionAuthorizationMaxResponseBytes);
    if (reserve_error == CompanionGattSinkError::busy) {
        reservation_active_ = false;
        reservation_token_ = 0;
        result.error = CompanionGattAuthorizationError::response_path_busy;
        return result;
    }
    if (reserve_error != CompanionGattSinkError::none) {
        reservation_active_ = false;
        reservation_token_ = 0;
        const auto contained = contain(true);
        result.error = contained == CompanionGattAuthorizationError::none
                           ? CompanionGattAuthorizationError::
                                 response_path_unavailable
                           : contained;
        return result;
    }

    const CompanionGattAuthorizationCorrelationContext context{
        transport_generation_, session_nonce_, decoded.fragment.exchange_id,
        start.value.purpose};
    const auto issued = correlation_issuer_.issue(context);
    if (issued.error != CompanionGattAuthorizationCorrelationError::none ||
        !valid_authorization_correlation(issued.correlation)) {
        const auto contained = contain(true);
        result.error = contained == CompanionGattAuthorizationError::none
                           ? CompanionGattAuthorizationError::
                                 correlation_unavailable
                           : contained;
        return result;
    }

    purpose_ = start.value.purpose;
    correlation_ = issued.correlation;
    exchange_id_ = decoded.fragment.exchange_id;
    response_buffer_.fill(0);
    CompanionAuthorizationClaimStatus status{};
    status.purpose = purpose_;
    status.correlation = correlation_;
    std::array<std::uint8_t, kCompanionAuthorizationClaimStatusBytes> payload{};
    const auto payload_result = encode_companion_authorization_claim_status(
        status, {payload.data(), payload.size()});
    if (!payload_result.encoded()) {
        const auto contained = contain(true);
        result.error = contained == CompanionGattAuthorizationError::none
                           ? CompanionGattAuthorizationError::internal_failure
                           : contained;
        return result;
    }
    CompanionFragment response{};
    response.kind = CompanionFrameKind::authorization_claim_status;
    response.session_nonce = session_nonce_;
    response.exchange_id = exchange_id_;
    response.payload_bytes = static_cast<std::uint16_t>(payload.size());
    for (std::size_t index = 0; index < payload.size(); ++index) {
        response.payload[index] = payload[index];
    }
    const auto encoded_response = encode_companion_fragment(
        response, {response_buffer_.data(), response_buffer_.size()});
    if (!encoded_response.encoded()) {
        const auto contained = contain(true);
        result.error = contained == CompanionGattAuthorizationError::none
                           ? CompanionGattAuthorizationError::internal_failure
                           : contained;
        return result;
    }
    const auto submit_error = indication_sink_.submit_reserved(
        connection_handle_, session_nonce_, handles_.stream_value,
        reservation_token_,
        {response_buffer_.data(), encoded_response.encoded_bytes});
    reservation_active_ = false;
    reservation_token_ = 0;
    if (submit_error != CompanionGattSinkError::none) {
        const auto contained = contain(true);
        result.error = contained == CompanionGattAuthorizationError::none
                           ? CompanionGattAuthorizationError::
                                 indication_submit_failed
                           : contained;
        return result;
    }
    response_pending_ = true;
    pending_delivery_token_ = kCompanionAuthorizationPendingDeliveryToken;
    pending_since_ms_ = now_ms;
    claim_started_ms_ = now_ms;
    last_exchange_id_ = exchange_id_;
    phase_ = CompanionGattAuthorizationPhase::pending_indication;
    result.disposition =
        CompanionGattAuthorizationRequestDisposition::indication_pending;
    result.error = CompanionGattAuthorizationError::none;
    result.delivery_token = kCompanionAuthorizationPendingDeliveryToken;
    result.exchange_id = exchange_id_;
    return result;
}

CompanionGattAuthorizationError
CompanionGattAuthorizationLifecycle::submit_terminal(std::uint64_t now_ms) {
    reservation_active_ = true;
    reservation_token_ = kCompanionAuthorizationTerminalDeliveryToken;
    const auto reserve_error = indication_sink_.reserve(
        connection_handle_, session_nonce_, handles_.stream_value,
        reservation_token_, kCompanionAuthorizationMaxResponseBytes);
    if (reserve_error != CompanionGattSinkError::none) {
        reservation_active_ = false;
        reservation_token_ = 0;
        const auto contained = contain(true);
        if (contained != CompanionGattAuthorizationError::none) {
            return contained;
        }
        return reserve_error == CompanionGattSinkError::busy
                   ? CompanionGattAuthorizationError::response_path_busy
                   : CompanionGattAuthorizationError::response_path_unavailable;
    }

    const auto decision = authority_.apply_claim(
        purpose_, bound_evidence_.controller_claim, now_ms);
    if (decision.error ==
        CompanionGattAuthorizationAuthorityError::not_ready) {
        indication_sink_.cancel_reservation(reservation_token_);
        reservation_active_ = false;
        reservation_token_ = 0;
        return CompanionGattAuthorizationError::authority_pending;
    }
    if (decision.error != CompanionGattAuthorizationAuthorityError::none) {
        const auto contained = contain(true);
        return contained == CompanionGattAuthorizationError::none
                   ? CompanionGattAuthorizationError::authority_unavailable
                   : contained;
    }
    if (!coherent_decision(purpose_, bound_evidence_.controller_claim,
                           decision)) {
        if (decision.controller_binding != 0) {
            authority_connection_active_ = true;
            authority_controller_binding_ = decision.controller_binding;
        }
        const auto contained = contain(true);
        return contained == CompanionGattAuthorizationError::none
                   ? CompanionGattAuthorizationError::authority_result_incoherent
                   : contained;
    }

    terminal_outcome_ = decision.outcome;
    terminal_reason_ = decision.reason;
    if (decision.outcome != CompanionAuthorizationClaimOutcome::denied) {
        authority_connection_active_ = true;
        authority_controller_binding_ = decision.controller_binding;
    }
    CompanionAuthorizationClaimResult terminal{};
    terminal.purpose = purpose_;
    terminal.outcome = terminal_outcome_;
    terminal.reason = terminal_reason_;
    terminal.correlation = correlation_;
    std::array<std::uint8_t, kCompanionAuthorizationClaimResultBytes> payload{};
    const auto payload_result = encode_companion_authorization_claim_result(
        terminal, {payload.data(), payload.size()});
    if (!payload_result.encoded()) {
        const auto contained = contain(true);
        return contained == CompanionGattAuthorizationError::none
                   ? CompanionGattAuthorizationError::internal_failure
                   : contained;
    }
    CompanionFragment response{};
    response.kind = CompanionFrameKind::authorization_claim_result;
    response.session_nonce = session_nonce_;
    response.exchange_id = exchange_id_;
    response.payload_bytes = static_cast<std::uint16_t>(payload.size());
    for (std::size_t index = 0; index < payload.size(); ++index) {
        response.payload[index] = payload[index];
    }
    response_buffer_.fill(0);
    const auto encoded_response = encode_companion_fragment(
        response, {response_buffer_.data(), response_buffer_.size()});
    if (!encoded_response.encoded()) {
        const auto contained = contain(true);
        return contained == CompanionGattAuthorizationError::none
                   ? CompanionGattAuthorizationError::internal_failure
                   : contained;
    }
    const auto submit_error = indication_sink_.submit_reserved(
        connection_handle_, session_nonce_, handles_.stream_value,
        reservation_token_,
        {response_buffer_.data(), encoded_response.encoded_bytes});
    reservation_active_ = false;
    reservation_token_ = 0;
    if (submit_error != CompanionGattSinkError::none) {
        const auto contained = contain(true);
        return contained == CompanionGattAuthorizationError::none
                   ? CompanionGattAuthorizationError::indication_submit_failed
                   : contained;
    }
    response_pending_ = true;
    pending_delivery_token_ = kCompanionAuthorizationTerminalDeliveryToken;
    pending_since_ms_ = now_ms;
    phase_ = CompanionGattAuthorizationPhase::terminal_indication;
    return CompanionGattAuthorizationError::none;
}

CompanionGattAuthorizationError
CompanionGattAuthorizationLifecycle::promote_normal_if_ready() {
    if (!application_authorized_ || !authority_connection_active_ ||
        authority_controller_binding_ == 0) {
        return CompanionGattAuthorizationError::internal_failure;
    }
    if (normal_connected_) {
        return CompanionGattAuthorizationError::none;
    }
    if (evidence_.att_mtu < kCompanionMinimumAttMtu) {
        return CompanionGattAuthorizationError::none;
    }
    auto normal_error = normal_lifecycle_.connect(connection_handle_);
    if (normal_error != CompanionGattLifecycleError::none) {
        return CompanionGattAuthorizationError::normal_session_not_ready;
    }
    normal_connected_ = true;
    normal_error = normal_lifecycle_.update_connection_evidence(
        connection_handle_,
        {evidence_.att_mtu, true, true, true,
         authority_controller_binding_});
    if (normal_error == CompanionGattLifecycleError::none) {
        normal_error = normal_lifecycle_.update_indication_subscription(
            connection_handle_, handles_.stream_cccd, true);
    }
    if (normal_error == CompanionGattLifecycleError::none) {
        normal_error = normal_lifecycle_.open_session(
                           connection_handle_, session_nonce_)
                           .error;
    }
    if (normal_error != CompanionGattLifecycleError::none) {
        const auto ignored = normal_lifecycle_.disconnect(connection_handle_);
        (void)ignored;
        normal_connected_ = false;
        return CompanionGattAuthorizationError::normal_session_not_ready;
    }
    return CompanionGattAuthorizationError::none;
}

CompanionGattAuthorizationError
CompanionGattAuthorizationLifecycle::complete_indication(
    std::uint16_t connection_handle,
    std::uint64_t transport_generation,
    std::uint32_t session_nonce,
    std::uint16_t stream_value_handle,
    std::uint64_t delivery_token,
    bool confirmed,
    std::uint64_t now_ms) {
    if (operation_active_) {
        return CompanionGattAuthorizationError::reentrant_call;
    }
    ScopedOperation operation(operation_active_);
    const auto state_error =
        connection_error(connection_handle, transport_generation);
    if (state_error != CompanionGattAuthorizationError::none) {
        return state_error;
    }
    const auto time_error = observe_time(now_ms);
    if (time_error != CompanionGattAuthorizationError::none) {
        return time_error;
    }
    if (application_authorized_ && normal_connected_ &&
        delivery_token >= kCompanionAuthorizationFirstNormalDeliveryToken) {
        const auto normal_error = normal_lifecycle_.complete_indication(
            connection_handle_, session_nonce, stream_value_handle,
            delivery_token, confirmed);
        if (normal_error == CompanionGattLifecycleError::none) {
            normal_pending_delivery_token_ = 0;
            normal_pending_exchange_id_ = 0;
            return CompanionGattAuthorizationError::none;
        }
        if (normal_lifecycle_.status().blocked_until_disconnect) {
            const auto contained = contain(true);
            return contained == CompanionGattAuthorizationError::none
                       ? CompanionGattAuthorizationError::indication_failed
                       : contained;
        }
        return CompanionGattAuthorizationError::indication_mismatch;
    }
    if (!response_pending_) {
        return CompanionGattAuthorizationError::no_outstanding_indication;
    }
    if (session_nonce != session_nonce_ ||
        stream_value_handle != handles_.stream_value ||
        delivery_token == 0 || delivery_token != pending_delivery_token_) {
        return CompanionGattAuthorizationError::indication_mismatch;
    }
    if (!confirmed) {
        const auto contained = contain(true);
        return contained == CompanionGattAuthorizationError::none
                   ? CompanionGattAuthorizationError::indication_failed
                   : contained;
    }

    if (phase_ == CompanionGattAuthorizationPhase::pending_indication) {
        clear_response_slot();
        phase_ = CompanionGattAuthorizationPhase::awaiting_authority;
        return CompanionGattAuthorizationError::none;
    }
    if (phase_ != CompanionGattAuthorizationPhase::terminal_indication) {
        return CompanionGattAuthorizationError::indication_mismatch;
    }
    clear_response_slot();
    const bool terminal_authorized =
        terminal_outcome_ != CompanionAuthorizationClaimOutcome::denied;
    if (!terminal_authorized) {
        const auto contained = contain(true);
        return contained == CompanionGattAuthorizationError::none
                   ? CompanionGattAuthorizationError::none
                   : contained;
    }
    clear_claim();
    application_authorized_ = true;
    phase_ = CompanionGattAuthorizationPhase::promoted;
    const auto promoted = promote_normal_if_ready();
    if (promoted != CompanionGattAuthorizationError::none) {
        const auto contained = contain(true);
        return contained == CompanionGattAuthorizationError::none
                   ? promoted
                   : contained;
    }
    return CompanionGattAuthorizationError::none;
}

CompanionGattAuthorizationRequestResult
CompanionGattAuthorizationLifecycle::resolve_claim(
    std::uint16_t connection_handle,
    std::uint64_t transport_generation,
    std::uint32_t session_nonce,
    std::uint32_t exchange_id,
    std::uint64_t now_ms) {
    CompanionGattAuthorizationRequestResult result{};
    if (operation_active_) {
        result.error = CompanionGattAuthorizationError::reentrant_call;
        return result;
    }
    ScopedOperation operation(operation_active_);
    const auto state_error =
        connection_error(connection_handle, transport_generation);
    if (state_error != CompanionGattAuthorizationError::none) {
        result.error = state_error;
        return result;
    }
    const auto time_error = observe_time(now_ms);
    if (time_error != CompanionGattAuthorizationError::none) {
        result.error = time_error;
        return result;
    }
    if (phase_ != CompanionGattAuthorizationPhase::awaiting_authority) {
        result.error = CompanionGattAuthorizationError::response_path_busy;
        return result;
    }
    if (session_nonce == 0 || session_nonce != session_nonce_) {
        result.error = CompanionGattAuthorizationError::wrong_session;
        return result;
    }
    if (exchange_id == 0 || exchange_id != exchange_id_) {
        result.error = CompanionGattAuthorizationError::stale_exchange;
        return result;
    }
    const auto path_error = claim_path_error();
    if (path_error != CompanionGattAuthorizationError::none) {
        result.error = path_error;
        return result;
    }
    if (now_ms - claim_started_ms_ >= policy_.claim_timeout_ms) {
        const auto contained = contain(true);
        result.error = contained == CompanionGattAuthorizationError::none
                           ? CompanionGattAuthorizationError::claim_timeout
                           : contained;
        return result;
    }

    const auto terminal_error = submit_terminal(now_ms);
    result.error = terminal_error;
    if (terminal_error == CompanionGattAuthorizationError::none) {
        result.disposition =
            CompanionGattAuthorizationRequestDisposition::indication_pending;
        result.delivery_token =
            kCompanionAuthorizationTerminalDeliveryToken;
        result.exchange_id = exchange_id_;
    }
    return result;
}

CompanionGattAuthorizationError
CompanionGattAuthorizationLifecycle::service_timeout(
    std::uint16_t connection_handle,
    std::uint64_t transport_generation,
    std::uint32_t session_nonce,
    std::uint32_t exchange_id,
    std::uint64_t delivery_token,
    std::uint64_t now_ms) {
    if (operation_active_) {
        return CompanionGattAuthorizationError::reentrant_call;
    }
    ScopedOperation operation(operation_active_);
    const auto state_error =
        connection_error(connection_handle, transport_generation);
    if (state_error != CompanionGattAuthorizationError::none) {
        return state_error;
    }
    if (session_nonce == 0 || session_nonce != session_nonce_) {
        return CompanionGattAuthorizationError::wrong_session;
    }
    const auto time_error = observe_time(now_ms);
    if (time_error != CompanionGattAuthorizationError::none) {
        return time_error;
    }
    if (application_authorized_ && normal_connected_) {
        if (exchange_id == 0 ||
            exchange_id != normal_pending_exchange_id_ ||
            delivery_token == 0 ||
            delivery_token != normal_pending_delivery_token_) {
            return CompanionGattAuthorizationError::indication_mismatch;
        }
        const auto normal_error = normal_lifecycle_.service_timeout(now_ms);
        if (normal_lifecycle_.status().blocked_until_disconnect) {
            const auto contained = contain(true);
            return contained == CompanionGattAuthorizationError::none
                       ? CompanionGattAuthorizationError::indication_timeout
                       : contained;
        }
        return normal_error == CompanionGattLifecycleError::none
                   ? CompanionGattAuthorizationError::none
                   : CompanionGattAuthorizationError::normal_command_rejected;
    }
    if (exchange_id == 0 || exchange_id != exchange_id_) {
        return CompanionGattAuthorizationError::stale_exchange;
    }
    if (now_ms - claim_started_ms_ >= policy_.claim_timeout_ms) {
        const auto contained = contain(true);
        return contained == CompanionGattAuthorizationError::none
                   ? CompanionGattAuthorizationError::claim_timeout
                   : contained;
    }
    if (phase_ == CompanionGattAuthorizationPhase::awaiting_authority) {
        if (delivery_token != 0) {
            return CompanionGattAuthorizationError::indication_mismatch;
        }
        return CompanionGattAuthorizationError::none;
    }
    if (!response_pending_ || delivery_token == 0 ||
        delivery_token != pending_delivery_token_) {
        return CompanionGattAuthorizationError::indication_mismatch;
    }
    if (now_ms - pending_since_ms_ < policy_.indication_timeout_ms) {
        return CompanionGattAuthorizationError::none;
    }
    const auto contained = contain(true);
    return contained == CompanionGattAuthorizationError::none
               ? CompanionGattAuthorizationError::indication_timeout
               : contained;
}

void CompanionGattAuthorizationLifecycle::clear_response_slot() {
    reservation_active_ = false;
    response_pending_ = false;
    reservation_token_ = 0;
    pending_delivery_token_ = 0;
    pending_since_ms_ = 0;
    response_buffer_.fill(0);
}

void CompanionGattAuthorizationLifecycle::clear_claim() {
    exchange_id_ = 0;
    purpose_ = CompanionAuthorizationPurpose::authorize_controller;
    correlation_ = {};
    terminal_outcome_ = CompanionAuthorizationClaimOutcome::denied;
    terminal_reason_ = CompanionAuthorizationDenyReason::unknown;
    claim_started_ms_ = 0;
}

CompanionGattAuthorizationError
CompanionGattAuthorizationLifecycle::contain(bool block) {
    if (reservation_active_) {
        indication_sink_.cancel_reservation(reservation_token_);
    }
    if (response_pending_) {
        indication_sink_.abandon_indication(pending_delivery_token_);
    }
    clear_response_slot();
    if (normal_connected_) {
        const auto normal_error =
            normal_lifecycle_.disconnect(connection_handle_);
        normal_connected_ = false;
        if (normal_error != CompanionGattLifecycleError::none) {
            faulted_ = true;
        }
    }
    if (authority_connection_active_) {
        const auto authority_error =
            authority_.release_connection(authority_controller_binding_);
        if (authority_error !=
            CompanionGattAuthorizationAuthorityError::none) {
            faulted_ = true;
        }
    }
    authority_connection_active_ = false;
    authority_controller_binding_ = 0;
    application_authorized_ = false;
    normal_pending_delivery_token_ = 0;
    normal_pending_exchange_id_ = 0;
    indication_subscribed_ = false;
    protocol_info_read_ = false;
    clear_claim();
    if (block && phase_ != CompanionGattAuthorizationPhase::idle) {
        phase_ = CompanionGattAuthorizationPhase::blocked_until_disconnect;
    }
    return faulted_ ? CompanionGattAuthorizationError::internal_failure
                    : CompanionGattAuthorizationError::none;
}

void CompanionGattAuthorizationLifecycle::clear_connection() {
    phase_ = CompanionGattAuthorizationPhase::idle;
    connection_handle_ = kCompanionGattInvalidConnectionHandle;
    transport_generation_ = 0;
    session_nonce_ = 0;
    evidence_ = {};
    bound_evidence_ = {};
    indication_subscribed_ = false;
    protocol_info_read_ = false;
    application_authorized_ = false;
    normal_connected_ = false;
    normal_pending_delivery_token_ = 0;
    normal_pending_exchange_id_ = 0;
    time_observed_ = false;
    last_now_ms_ = 0;
    clear_response_slot();
    clear_claim();
}

CompanionGattAuthorizationError
CompanionGattAuthorizationLifecycle::disconnect(
    std::uint16_t connection_handle,
    std::uint64_t transport_generation) {
    if (operation_active_) {
        return CompanionGattAuthorizationError::reentrant_call;
    }
    ScopedOperation operation(operation_active_);
    if (phase_ == CompanionGattAuthorizationPhase::idle) {
        return CompanionGattAuthorizationError::no_connection;
    }
    if (connection_handle != connection_handle_) {
        return CompanionGattAuthorizationError::wrong_connection;
    }
    if (transport_generation == 0 ||
        transport_generation != transport_generation_) {
        return CompanionGattAuthorizationError::wrong_transport_generation;
    }
    const bool was_faulted = faulted_;
    const auto contained = contain(false);
    clear_connection();
    return was_faulted ? CompanionGattAuthorizationError::internal_failure
                       : contained;
}

CompanionGattAuthorizationStatus
CompanionGattAuthorizationLifecycle::status() const {
    const bool connected = phase_ != CompanionGattAuthorizationPhase::idle;
    const auto normal_status = normal_lifecycle_.status();
    const bool normal_active = normal_connected_ && normal_status.session_active;
    return {
        phase_,
        handles_registered_,
        connected && evidence_.controller_claim.link_encrypted,
        connected && evidence_.controller_claim.authenticated_bond,
        protocol_info_read_,
        indication_subscribed_,
        application_authorized_,
        normal_active,
        normal_active ? normal_status.response_pending : response_pending_,
        faulted_,
        connected ? evidence_.att_mtu : static_cast<std::uint16_t>(0),
        connected ? session_nonce_ : 0,
        connected
            ? (normal_active ? normal_status.pending_exchange_id : exchange_id_)
            : 0,
    };
}

}  // namespace opentrail::companion
