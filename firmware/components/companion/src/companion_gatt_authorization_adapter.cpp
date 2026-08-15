#include "opentrail/companion_gatt_authorization_adapter.hpp"

#include <limits>

namespace opentrail::companion {
namespace {

class ScopedOperation {
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

bool valid_private_claim(const CompanionControllerClaim& claim) {
    return valid_bond_identity(claim.bond_identity) &&
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

}  // namespace

CompanionGattAuthorizationCallbackAdapter::
CompanionGattAuthorizationCallbackAdapter(
    CompanionRequestCoordinator& normal_coordinator,
    CompanionGattIndicationPort& indication_port,
    CompanionGattTrustedBindingAuthority& binding_authority,
    CompanionGattAuthorizationCorrelationIssuer& correlation_issuer,
    CompanionGattAuthorizationAuthority& authorization_authority,
    CompanionGattAuthorizationPolicy policy)
    : indication_port_(indication_port),
      binding_authority_(binding_authority),
      lifecycle_(normal_coordinator,
                 *this,
                 correlation_issuer,
                 authorization_authority,
                 policy) {}

CompanionGattAdapterError
CompanionGattAuthorizationCallbackAdapter::register_handles(
    CompanionGattAuthorizationHandles handles) {
    if (operation_active_) {
        return CompanionGattAdapterError::lifecycle_rejected;
    }
    ScopedOperation operation(operation_active_);
    if (handles.protocol_info_value == 0 || handles.command_value == 0 ||
        handles.stream_value == 0 || handles.stream_cccd == 0 ||
        handles.protocol_info_value == handles.command_value ||
        handles.protocol_info_value == handles.stream_value ||
        handles.protocol_info_value == handles.stream_cccd ||
        handles.command_value == handles.stream_value ||
        handles.command_value == handles.stream_cccd ||
        handles.stream_value == handles.stream_cccd) {
        return CompanionGattAdapterError::invalid_argument;
    }
    if (handles_registered_) {
        return CompanionGattAdapterError::already_registered;
    }
    const auto result = lifecycle_.register_handles(handles);
    if (result != CompanionGattAuthorizationError::none) {
        return CompanionGattAdapterError::lifecycle_rejected;
    }
    handles_ = handles;
    handles_registered_ = true;
    return CompanionGattAdapterError::none;
}

CompanionGattAdapterConnectResult
CompanionGattAuthorizationCallbackAdapter::connect(
    std::uint16_t connection_handle) {
    CompanionGattAdapterConnectResult result{};
    if (operation_active_) {
        result.error = CompanionGattAdapterError::lifecycle_rejected;
        return result;
    }
    ScopedOperation operation(operation_active_);
    if (!handles_registered_) {
        result.error = CompanionGattAdapterError::not_registered;
        return result;
    }
    if (connection_handle == kCompanionGattInvalidConnectionHandle) {
        result.error = CompanionGattAdapterError::invalid_argument;
        return result;
    }
    if (connected_) {
        result.error = CompanionGattAdapterError::connection_in_use;
        return result;
    }
    if (next_transport_generation_ == 0) {
        result.error = CompanionGattAdapterError::generation_exhausted;
        return result;
    }
    const auto generation = next_transport_generation_;
    if (generation == std::numeric_limits<std::uint64_t>::max()) {
        next_transport_generation_ = 0;
    } else {
        ++next_transport_generation_;
    }
    const auto opened = lifecycle_.connect(connection_handle, generation);
    if (opened != CompanionGattAuthorizationError::none) {
        result.error = CompanionGattAdapterError::lifecycle_rejected;
        return result;
    }
    connected_ = true;
    connection_handle_ = connection_handle;
    transport_generation_ = generation;
    security_ = {};
    result.error = CompanionGattAdapterError::none;
    result.transport_generation = generation;
    return result;
}

bool CompanionGattAuthorizationCallbackAdapter::valid_security(
    const CompanionGattAdapterLinkSecurity& security) const {
    return security.encrypted && security.authenticated && security.bonded &&
           security.key_size >= kCompanionGattMinimumSecurityKeyBytes &&
           security.att_mtu >= kCompanionGattDefaultAttMtu;
}

CompanionGattAdapterError
CompanionGattAuthorizationCallbackAdapter::connection_error(
    std::uint16_t connection_handle) const {
    if (!connected_) {
        return CompanionGattAdapterError::no_connection;
    }
    if (connection_handle != connection_handle_) {
        return CompanionGattAdapterError::wrong_connection;
    }
    return CompanionGattAdapterError::none;
}

CompanionGattAdapterError
CompanionGattAuthorizationCallbackAdapter::map_lifecycle_error(
    CompanionGattAuthorizationError error) const {
    switch (error) {
        case CompanionGattAuthorizationError::none:
            return CompanionGattAdapterError::none;
        case CompanionGattAuthorizationError::wrong_connection:
            return CompanionGattAdapterError::wrong_connection;
        case CompanionGattAuthorizationError::wrong_transport_generation:
        case CompanionGattAuthorizationError::stale_transport_generation:
            return CompanionGattAdapterError::wrong_generation;
        case CompanionGattAuthorizationError::wrong_attribute:
            return CompanionGattAdapterError::wrong_attribute;
        case CompanionGattAuthorizationError::insecure_link:
            return CompanionGattAdapterError::insecure_link;
        default:
            return CompanionGattAdapterError::lifecycle_rejected;
    }
}

CompanionGattAdapterError
CompanionGattAuthorizationCallbackAdapter::refresh_security(
    std::uint16_t connection_handle,
    CompanionGattAdapterLinkSecurity security) {
    if (operation_active_) {
        return CompanionGattAdapterError::lifecycle_rejected;
    }
    ScopedOperation operation(operation_active_);
    return refresh_security_impl(connection_handle, security);
}

CompanionGattAdapterError
CompanionGattAuthorizationCallbackAdapter::refresh_security_impl(
    std::uint16_t connection_handle,
    CompanionGattAdapterLinkSecurity security) {
    const auto connection = connection_error(connection_handle);
    if (connection != CompanionGattAdapterError::none) {
        return connection;
    }
    if (!valid_security(security)) {
        CompanionGattAuthorizationConnectionEvidence insecure{};
        insecure.att_mtu = security.att_mtu;
        const auto ignored = lifecycle_.update_connection_evidence(
            connection_handle_, transport_generation_, insecure);
        (void)ignored;
        secure_bond_ = false;
        security_ = security;
        return CompanionGattAdapterError::insecure_link;
    }

    const auto binding = binding_authority_.resolve(
        connection_handle_, transport_generation_);
    if (binding.error != CompanionGattTrustedBindingError::none ||
        !valid_private_claim(binding.claim)) {
        CompanionGattAuthorizationConnectionEvidence invalid{};
        invalid.att_mtu = security.att_mtu;
        const auto ignored = lifecycle_.update_connection_evidence(
            connection_handle_, transport_generation_, invalid);
        (void)ignored;
        secure_bond_ = false;
        return CompanionGattAdapterError::binding_unavailable;
    }
    if (binding.provisional_session_nonce == 0) {
        CompanionGattAuthorizationConnectionEvidence invalid{};
        invalid.att_mtu = security.att_mtu;
        const auto ignored = lifecycle_.update_connection_evidence(
            connection_handle_, transport_generation_, invalid);
        (void)ignored;
        secure_bond_ = false;
        return CompanionGattAdapterError::session_unavailable;
    }

    auto claim = binding.claim;
    claim.link_encrypted = true;
    claim.authenticated_bond = true;
    if (session_open_ &&
        (!same_claim(claim_, claim) ||
         session_nonce_ != binding.provisional_session_nonce)) {
        CompanionGattAuthorizationConnectionEvidence changed{};
        changed.att_mtu = security.att_mtu;
        changed.controller_claim = claim;
        const auto ignored = lifecycle_.update_connection_evidence(
            connection_handle_, transport_generation_, changed);
        (void)ignored;
        secure_bond_ = false;
        return CompanionGattAdapterError::binding_unavailable;
    }

    CompanionGattAuthorizationConnectionEvidence evidence{};
    evidence.att_mtu = security.att_mtu;
    evidence.controller_claim = claim;
    const auto updated = lifecycle_.update_connection_evidence(
        connection_handle_, transport_generation_, evidence);
    if (updated != CompanionGattAuthorizationError::none) {
        secure_bond_ = false;
        return map_lifecycle_error(updated);
    }
    if (!session_open_) {
        const auto opened = lifecycle_.open_provisional_session(
            connection_handle_, transport_generation_,
            binding.provisional_session_nonce);
        if (opened != CompanionGattAuthorizationError::none) {
            secure_bond_ = false;
            return map_lifecycle_error(opened);
        }
        session_open_ = true;
        session_nonce_ = binding.provisional_session_nonce;
        claim_ = claim;
    }
    security_ = security;
    secure_bond_ = true;
    return CompanionGattAdapterError::none;
}

bool CompanionGattAuthorizationCallbackAdapter::attribute_authorized(
    std::uint16_t connection_handle,
    std::uint16_t attribute_handle,
    CompanionGattAttributeOperation operation) const {
    if (connection_error(connection_handle) != CompanionGattAdapterError::none ||
        !secure_bond_ || !session_open_) {
        return false;
    }
    const auto state = lifecycle_.status();
    if (attribute_handle == handles_.protocol_info_value) {
        return operation == CompanionGattAttributeOperation::read;
    }
    if (attribute_handle == handles_.stream_cccd) {
        return operation == CompanionGattAttributeOperation::write &&
               !state.response_pending;
    }
    if (attribute_handle == handles_.command_value) {
        return operation == CompanionGattAttributeOperation::write &&
               state.protocol_info_read && state.indication_subscribed &&
               !state.response_pending &&
               (state.phase == CompanionGattAuthorizationPhase::provisional ||
                state.phase == CompanionGattAuthorizationPhase::promoted);
    }
    return false;
}

bool CompanionGattAuthorizationCallbackAdapter::authorize_attribute(
    std::uint16_t connection_handle,
    std::uint16_t attribute_handle,
    CompanionGattAttributeOperation operation) {
    if (operation_active_) {
        return false;
    }
    ScopedOperation guarded(operation_active_);
    return attribute_authorized(connection_handle, attribute_handle, operation);
}

CompanionGattAuthorizationReadResult
CompanionGattAuthorizationCallbackAdapter::read_protocol_info(
    std::uint16_t connection_handle,
    std::uint16_t attribute_handle,
    radio::MutableByteView output) {
    if (operation_active_) {
        return {CompanionGattAuthorizationError::reentrant_call, 0};
    }
    ScopedOperation operation(operation_active_);
    if (!attribute_authorized(connection_handle, attribute_handle,
                              CompanionGattAttributeOperation::read)) {
        return {CompanionGattAuthorizationError::insecure_link, 0};
    }
    return lifecycle_.read_protocol_info(
        connection_handle_, transport_generation_, attribute_handle, output);
}

CompanionGattAdapterError
CompanionGattAuthorizationCallbackAdapter::update_stream_subscription(
    std::uint16_t connection_handle,
    std::uint16_t observed_stream_value_handle,
    bool indications_enabled) {
    if (operation_active_) {
        return CompanionGattAdapterError::lifecycle_rejected;
    }
    ScopedOperation operation(operation_active_);
    const auto connection = connection_error(connection_handle);
    if (connection != CompanionGattAdapterError::none) {
        return connection;
    }
    if (observed_stream_value_handle != handles_.stream_value) {
        return CompanionGattAdapterError::wrong_attribute;
    }
    if (!secure_bond_ || !session_open_) {
        return CompanionGattAdapterError::insecure_link;
    }
    return map_lifecycle_error(lifecycle_.update_indication_subscription(
        connection_handle_, transport_generation_, handles_.stream_cccd,
        indications_enabled));
}

void CompanionGattAuthorizationCallbackAdapter::observe_request(
    const CompanionGattAuthorizationRequestResult& result) {
    if (!result.pending()) {
        return;
    }
    const auto state = lifecycle_.status();
    pending_ = {
        true,
        connection_handle_,
        transport_generation_,
        state.session_nonce,
        result.exchange_id,
        handles_.stream_value,
        result.delivery_token,
    };
    indication_port_.bind_exchange(
        result.delivery_token, result.exchange_id);
}

CompanionGattAuthorizationRequestResult
CompanionGattAuthorizationCallbackAdapter::service_command(
    std::uint16_t connection_handle,
    std::uint16_t command_value_handle,
    radio::ByteView encoded_request,
    std::uint64_t now_ms) {
    if (operation_active_) {
        return {
            CompanionGattAuthorizationRequestDisposition::rejected,
            CompanionGattAuthorizationError::reentrant_call,
            0,
            0,
        };
    }
    ScopedOperation operation(operation_active_);
    if (!attribute_authorized(connection_handle, command_value_handle,
                              CompanionGattAttributeOperation::write)) {
        return {
            CompanionGattAuthorizationRequestDisposition::rejected,
            CompanionGattAuthorizationError::insecure_link,
            0,
            0,
        };
    }
    const auto result = lifecycle_.service_command(
        connection_handle_, transport_generation_, command_value_handle,
        encoded_request, now_ms);
    observe_request(result);
    return result;
}

CompanionGattAuthorizationRequestResult
CompanionGattAuthorizationCallbackAdapter::resolve_claim(
    std::uint16_t connection_handle,
    std::uint64_t transport_generation,
    std::uint32_t session_nonce,
    std::uint32_t exchange_id,
    CompanionGattAdapterLinkSecurity current_security,
    std::uint64_t now_ms) {
    if (operation_active_) {
        return {
            CompanionGattAuthorizationRequestDisposition::rejected,
            CompanionGattAuthorizationError::reentrant_call,
            0,
            0,
        };
    }
    ScopedOperation operation(operation_active_);
    if (connection_error(connection_handle) != CompanionGattAdapterError::none ||
        transport_generation != transport_generation_ ||
        session_nonce != session_nonce_) {
        return {
            CompanionGattAuthorizationRequestDisposition::rejected,
            CompanionGattAuthorizationError::wrong_transport_generation,
            0,
            0,
        };
    }
    if (refresh_security_impl(connection_handle, current_security) !=
        CompanionGattAdapterError::none) {
        return {
            CompanionGattAuthorizationRequestDisposition::rejected,
            CompanionGattAuthorizationError::insecure_link,
            0,
            0,
        };
    }
    const auto result = lifecycle_.resolve_claim(
        connection_handle_, transport_generation_, session_nonce, exchange_id,
        now_ms);
    observe_request(result);
    return result;
}

bool CompanionGattAuthorizationCallbackAdapter::exact_pending(
    const CompanionGattAdapterPendingIndication& expected) const {
    return pending_.valid && expected.valid &&
           pending_.connection_handle == expected.connection_handle &&
           pending_.transport_generation == expected.transport_generation &&
           pending_.session_nonce == expected.session_nonce &&
           pending_.exchange_id == expected.exchange_id &&
           pending_.stream_value_handle == expected.stream_value_handle &&
           pending_.delivery_token == expected.delivery_token;
}

CompanionGattAdapterError
CompanionGattAuthorizationCallbackAdapter::complete_indication(
    const CompanionGattAdapterPendingIndication& expected,
    bool confirmed,
    std::uint64_t now_ms) {
    if (operation_active_) {
        return CompanionGattAdapterError::lifecycle_rejected;
    }
    ScopedOperation operation(operation_active_);
    if (!pending_.valid) {
        return CompanionGattAdapterError::no_pending_indication;
    }
    if (!exact_pending(expected)) {
        return CompanionGattAdapterError::indication_mismatch;
    }
    const auto result = lifecycle_.complete_indication(
        expected.connection_handle,
        expected.transport_generation,
        expected.session_nonce,
        expected.stream_value_handle,
        expected.delivery_token,
        confirmed,
        now_ms);
    if (result == CompanionGattAuthorizationError::none) {
        indication_port_.observe_completion(expected.delivery_token);
        pending_ = {};
        return CompanionGattAdapterError::none;
    }
    if (!confirmed) {
        pending_ = {};
    }
    return map_lifecycle_error(result);
}

CompanionGattAdapterError
CompanionGattAuthorizationCallbackAdapter::service_timeout(
    const CompanionGattAdapterPendingIndication& expected,
    std::uint64_t now_ms) {
    if (operation_active_) {
        return CompanionGattAdapterError::lifecycle_rejected;
    }
    ScopedOperation operation(operation_active_);
    if (!exact_pending(expected)) {
        return CompanionGattAdapterError::indication_mismatch;
    }
    const auto result = lifecycle_.service_timeout(
        expected.connection_handle,
        expected.transport_generation,
        expected.session_nonce,
        expected.exchange_id,
        expected.delivery_token,
        now_ms);
    if (result == CompanionGattAuthorizationError::indication_timeout ||
        result == CompanionGattAuthorizationError::claim_timeout ||
        result == CompanionGattAuthorizationError::none) {
        pending_ = {};
    }
    return map_lifecycle_error(result);
}

void CompanionGattAuthorizationCallbackAdapter::clear_connection() {
    security_ = {};
    claim_ = {};
    pending_ = {};
    secure_bond_ = false;
    session_open_ = false;
    connected_ = false;
    connection_handle_ = kCompanionGattInvalidConnectionHandle;
    transport_generation_ = 0;
    session_nonce_ = 0;
}

CompanionGattAdapterError
CompanionGattAuthorizationCallbackAdapter::disconnect(
    std::uint16_t connection_handle) {
    if (operation_active_) {
        return CompanionGattAdapterError::lifecycle_rejected;
    }
    ScopedOperation operation(operation_active_);
    const auto connection = connection_error(connection_handle);
    if (connection != CompanionGattAdapterError::none) {
        return connection;
    }
    const auto result = lifecycle_.disconnect(
        connection_handle_, transport_generation_);
    if (result != CompanionGattAuthorizationError::none) {
        return map_lifecycle_error(result);
    }
    clear_connection();
    return CompanionGattAdapterError::none;
}

CompanionGattAdapterStatus
CompanionGattAuthorizationCallbackAdapter::status() const {
    return {
        handles_registered_,
        connected_,
        secure_bond_,
        transport_generation_,
        pending_,
        lifecycle_.status(),
    };
}

CompanionGattSinkError
CompanionGattAuthorizationCallbackAdapter::reserve(
    std::uint16_t connection_handle,
    std::uint32_t session_nonce,
    std::uint16_t stream_value_handle,
    std::uint64_t delivery_token,
    std::size_t max_response_bytes) {
    if (!connected_ || connection_handle != connection_handle_ ||
        session_nonce != session_nonce_ ||
        stream_value_handle != handles_.stream_value || delivery_token == 0) {
        return CompanionGattSinkError::failed;
    }
    return indication_port_.reserve(
        connection_handle, transport_generation_, session_nonce,
        stream_value_handle, delivery_token, max_response_bytes);
}

CompanionGattSinkError
CompanionGattAuthorizationCallbackAdapter::submit_reserved(
    std::uint16_t connection_handle,
    std::uint32_t session_nonce,
    std::uint16_t stream_value_handle,
    std::uint64_t delivery_token,
    radio::ByteView response) {
    if (!connected_ || connection_handle != connection_handle_ ||
        session_nonce != session_nonce_ ||
        stream_value_handle != handles_.stream_value || delivery_token == 0) {
        return CompanionGattSinkError::failed;
    }
    return indication_port_.submit_reserved(
        connection_handle, transport_generation_, session_nonce,
        stream_value_handle, delivery_token, response);
}

void CompanionGattAuthorizationCallbackAdapter::cancel_reservation(
    std::uint64_t delivery_token) {
    indication_port_.cancel_reservation(delivery_token);
}

void CompanionGattAuthorizationCallbackAdapter::abandon_indication(
    std::uint64_t delivery_token) {
    indication_port_.abandon_indication(delivery_token);
    if (pending_.valid && pending_.delivery_token == delivery_token) {
        pending_ = {};
    }
}

}  // namespace opentrail::companion
