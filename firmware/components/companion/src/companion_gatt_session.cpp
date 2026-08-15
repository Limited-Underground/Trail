#include "opentrail/companion_gatt_session.hpp"

namespace opentrail::companion {
namespace {

bool valid_handles(const CompanionGattHandles& handles) {
    return handles.command_value != 0 && handles.stream_value != 0 &&
           handles.stream_cccd != 0 &&
           handles.command_value != handles.stream_value &&
           handles.command_value != handles.stream_cccd &&
           handles.stream_value != handles.stream_cccd;
}

bool coherent_evidence(const CompanionGattConnectionEvidence& evidence) {
    return evidence.att_mtu >= kCompanionGattDefaultAttMtu &&
           (evidence.application_authorized
                ? evidence.controller_binding != 0
                : evidence.controller_binding == 0);
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

CompanionGattSessionLifecycle::CompanionGattSessionLifecycle(
    CompanionRequestCoordinator& coordinator,
    CompanionGattIndicationSink& indication_sink,
    CompanionGattLifecyclePolicy policy)
    : coordinator_(coordinator),
      indication_sink_(indication_sink),
      policy_(policy),
      next_delivery_token_(policy.first_delivery_token) {
    if (policy_.indication_timeout_ms == 0 ||
        policy_.first_delivery_token == 0 ||
        policy_.final_delivery_token == 0 ||
        policy_.first_delivery_token > policy_.final_delivery_token) {
        faulted_ = true;
        next_delivery_token_ = 0;
    }
}

CompanionGattLifecycleError CompanionGattSessionLifecycle::register_handles(
    CompanionGattHandles handles) {
    if (operation_active_) {
        return CompanionGattLifecycleError::reentrant_call;
    }
    ScopedOperation operation(operation_active_);
    if (faulted_) {
        return CompanionGattLifecycleError::internal_failure;
    }
    if (handles_registered_) {
        return CompanionGattLifecycleError::handles_already_registered;
    }
    if (connected_ || !valid_handles(handles)) {
        return CompanionGattLifecycleError::invalid_argument;
    }
    handles_ = handles;
    handles_registered_ = true;
    return CompanionGattLifecycleError::none;
}

CompanionGattLifecycleError CompanionGattSessionLifecycle::connect(
    std::uint16_t connection_handle) {
    if (operation_active_) {
        return CompanionGattLifecycleError::reentrant_call;
    }
    ScopedOperation operation(operation_active_);
    if (faulted_) {
        return CompanionGattLifecycleError::internal_failure;
    }
    if (!handles_registered_) {
        return CompanionGattLifecycleError::handles_not_registered;
    }
    if (connection_handle == kCompanionGattInvalidConnectionHandle) {
        return CompanionGattLifecycleError::invalid_argument;
    }
    if (connected_) {
        return connection_handle == connection_handle_
                   ? CompanionGattLifecycleError::connection_in_use
                   : CompanionGattLifecycleError::wrong_connection;
    }
    connected_ = true;
    connection_handle_ = connection_handle;
    evidence_ = {};
    indication_subscribed_ = false;
    blocked_until_disconnect_ = false;
    time_observed_ = false;
    last_now_ms_ = 0;
    return CompanionGattLifecycleError::none;
}

CompanionGattLifecycleError CompanionGattSessionLifecycle::connection_error(
    std::uint16_t connection_handle) const {
    if (faulted_) {
        return CompanionGattLifecycleError::internal_failure;
    }
    if (!connected_) {
        return CompanionGattLifecycleError::no_connection;
    }
    if (connection_handle != connection_handle_) {
        return CompanionGattLifecycleError::wrong_connection;
    }
    if (blocked_until_disconnect_) {
        return CompanionGattLifecycleError::blocked_until_disconnect;
    }
    return CompanionGattLifecycleError::none;
}

CompanionSessionEvidence
CompanionGattSessionLifecycle::session_evidence() const {
    return {
        evidence_.controller_binding,
        evidence_.encrypted,
        evidence_.authenticated_bond,
        evidence_.application_authorized,
    };
}

CompanionGattLifecycleError
CompanionGattSessionLifecycle::update_connection_evidence(
    std::uint16_t connection_handle,
    CompanionGattConnectionEvidence evidence) {
    if (operation_active_) {
        return CompanionGattLifecycleError::reentrant_call;
    }
    ScopedOperation operation(operation_active_);
    const auto state_error = connection_error(connection_handle);
    if (state_error != CompanionGattLifecycleError::none) {
        return state_error;
    }
    if (!coherent_evidence(evidence)) {
        return CompanionGattLifecycleError::invalid_argument;
    }
    const bool invalidates_session = session_active_ &&
        (!evidence.encrypted || !evidence.authenticated_bond ||
         !evidence.application_authorized ||
         evidence.controller_binding != session_controller_binding_ ||
         evidence.att_mtu < kCompanionMinimumAttMtu);
    if (invalidates_session) {
        const auto contained = contain_session(true);
        if (contained != CompanionGattLifecycleError::none) {
            return contained;
        }
    }
    evidence_ = evidence;
    return CompanionGattLifecycleError::none;
}

CompanionGattLifecycleError
CompanionGattSessionLifecycle::update_indication_subscription(
    std::uint16_t connection_handle,
    std::uint16_t cccd_handle,
    bool indications_enabled) {
    if (operation_active_) {
        return CompanionGattLifecycleError::reentrant_call;
    }
    ScopedOperation operation(operation_active_);
    const auto state_error = connection_error(connection_handle);
    if (state_error != CompanionGattLifecycleError::none) {
        return state_error;
    }
    if (cccd_handle != handles_.stream_cccd) {
        return CompanionGattLifecycleError::wrong_attribute;
    }
    if (indications_enabled) {
        const auto path_error = response_path_error();
        if (path_error != CompanionGattLifecycleError::subscription_required &&
            path_error != CompanionGattLifecycleError::none) {
            return path_error;
        }
        indication_subscribed_ = true;
        return CompanionGattLifecycleError::none;
    }
    if (session_active_ || reservation_active_ || response_pending_) {
        const auto contained = contain_session(true);
        if (contained != CompanionGattLifecycleError::none) {
            return contained;
        }
    }
    indication_subscribed_ = false;
    return CompanionGattLifecycleError::none;
}

CompanionGattLifecycleError
CompanionGattSessionLifecycle::response_path_error() const {
    if (evidence_.att_mtu < kCompanionMinimumAttMtu) {
        return CompanionGattLifecycleError::mtu_too_small;
    }
    if (!evidence_.encrypted || !evidence_.authenticated_bond) {
        return CompanionGattLifecycleError::insecure_link;
    }
    if (!evidence_.application_authorized ||
        evidence_.controller_binding == 0) {
        return CompanionGattLifecycleError::application_unauthorized;
    }
    if (!indication_subscribed_) {
        return CompanionGattLifecycleError::subscription_required;
    }
    return CompanionGattLifecycleError::none;
}

CompanionGattOpenResult CompanionGattSessionLifecycle::open_session(
    std::uint16_t connection_handle,
    std::uint32_t device_generated_session_nonce) {
    CompanionGattOpenResult result{};
    if (operation_active_) {
        result.error = CompanionGattLifecycleError::reentrant_call;
        return result;
    }
    ScopedOperation operation(operation_active_);
    const auto state_error = connection_error(connection_handle);
    if (state_error != CompanionGattLifecycleError::none) {
        result.error = state_error;
        return result;
    }
    const auto path_error = response_path_error();
    if (path_error != CompanionGattLifecycleError::none) {
        result.error = path_error;
        return result;
    }
    if (reservation_active_ || response_pending_) {
        result.error = CompanionGattLifecycleError::response_path_busy;
        return result;
    }
    result.session = coordinator_.open_session(
        session_evidence(), device_generated_session_nonce);
    if (!result.session.opened()) {
        result.error = CompanionGattLifecycleError::session_rejected;
        return result;
    }
    session_active_ = true;
    session_controller_binding_ = evidence_.controller_binding;
    session_nonce_ = result.session.session_nonce;
    result.error = CompanionGattLifecycleError::none;
    return result;
}

CompanionGattLifecycleError CompanionGattSessionLifecycle::observe_time(
    std::uint64_t now_ms) {
    if (time_observed_ && now_ms < last_now_ms_) {
        const auto contained = contain_session(true);
        if (contained == CompanionGattLifecycleError::internal_failure) {
            return contained;
        }
        return CompanionGattLifecycleError::clock_rollback;
    }
    time_observed_ = true;
    last_now_ms_ = now_ms;
    return CompanionGattLifecycleError::none;
}

CompanionGattRequestResult CompanionGattSessionLifecycle::service_command(
    std::uint16_t connection_handle,
    std::uint16_t command_value_handle,
    radio::ByteView encoded_request,
    std::uint64_t now_ms) {
    CompanionGattRequestResult result{};
    if (operation_active_) {
        result.error = CompanionGattLifecycleError::reentrant_call;
        return result;
    }
    ScopedOperation operation(operation_active_);
    const auto state_error = connection_error(connection_handle);
    if (state_error != CompanionGattLifecycleError::none) {
        result.error = state_error;
        return result;
    }
    if (command_value_handle != handles_.command_value) {
        result.error = CompanionGattLifecycleError::wrong_attribute;
        return result;
    }
    if (encoded_request.data == nullptr || encoded_request.size == 0) {
        result.error = CompanionGattLifecycleError::invalid_argument;
        return result;
    }
    if (!session_active_) {
        result.error = CompanionGattLifecycleError::session_not_open;
        return result;
    }
    const auto path_error = response_path_error();
    if (path_error != CompanionGattLifecycleError::none) {
        result.error = path_error;
        return result;
    }
    if (reservation_active_ || response_pending_) {
        result.error = CompanionGattLifecycleError::response_path_busy;
        return result;
    }
    const auto time_error = observe_time(now_ms);
    if (time_error != CompanionGattLifecycleError::none) {
        result.error = time_error;
        return result;
    }
    if (next_delivery_token_ == 0) {
        result.error = CompanionGattLifecycleError::delivery_token_exhausted;
        return result;
    }

    const auto delivery_token = next_delivery_token_;
    reservation_active_ = true;
    reservation_token_ = delivery_token;
    const auto reserve_error = indication_sink_.reserve(
        connection_handle_, session_nonce_, handles_.stream_value,
        delivery_token, kCompanionMaxResponseRecordBytes);
    if (reserve_error == CompanionGattSinkError::busy) {
        reservation_active_ = false;
        reservation_token_ = 0;
        result.error = CompanionGattLifecycleError::response_path_busy;
        return result;
    }
    if (reserve_error != CompanionGattSinkError::none) {
        reservation_active_ = false;
        reservation_token_ = 0;
        result.error = CompanionGattLifecycleError::response_path_unavailable;
        return result;
    }
    next_delivery_token_ = delivery_token == policy_.final_delivery_token
                               ? 0
                               : delivery_token + 1;

    result.coordinator = coordinator_.service(
        session_evidence(), encoded_request,
        {response_buffer_.data(), response_buffer_.size()});
    if (!result.coordinator.responded()) {
        indication_sink_.cancel_reservation(reservation_token_);
        reservation_active_ = false;
        reservation_token_ = 0;
        response_buffer_.fill(0);
        result.error = CompanionGattLifecycleError::coordinator_rejected;
        return result;
    }

    const auto submit_error = indication_sink_.submit_reserved(
        connection_handle_, session_nonce_, handles_.stream_value,
        reservation_token_,
        {response_buffer_.data(), result.coordinator.response_bytes});
    reservation_active_ = false;
    reservation_token_ = 0;
    if (submit_error != CompanionGattSinkError::none) {
        result.error = CompanionGattLifecycleError::indication_submit_failed;
        const auto contained = contain_session(true);
        if (contained == CompanionGattLifecycleError::internal_failure) {
            result.error = contained;
        }
        return result;
    }

    response_pending_ = true;
    pending_delivery_token_ = delivery_token;
    pending_exchange_id_ = result.coordinator.exchange_id;
    pending_since_ms_ = now_ms;
    result.disposition = CompanionGattRequestDisposition::indication_pending;
    result.error = CompanionGattLifecycleError::none;
    result.delivery_token = delivery_token;
    return result;
}

CompanionGattLifecycleError
CompanionGattSessionLifecycle::complete_indication(
    std::uint16_t connection_handle,
    std::uint32_t session_nonce,
    std::uint16_t stream_value_handle,
    std::uint64_t delivery_token,
    bool confirmed) {
    if (operation_active_) {
        return CompanionGattLifecycleError::reentrant_call;
    }
    ScopedOperation operation(operation_active_);
    const auto state_error = connection_error(connection_handle);
    if (state_error != CompanionGattLifecycleError::none) {
        return state_error;
    }
    if (!response_pending_) {
        return CompanionGattLifecycleError::no_outstanding_indication;
    }
    if (stream_value_handle != handles_.stream_value ||
        session_nonce != session_nonce_ ||
        delivery_token == 0 ||
        delivery_token != pending_delivery_token_) {
        return CompanionGattLifecycleError::indication_mismatch;
    }
    if (!confirmed) {
        const auto contained = contain_session(true);
        return contained == CompanionGattLifecycleError::none
                   ? CompanionGattLifecycleError::indication_failed
                   : contained;
    }
    clear_response_slot();
    return CompanionGattLifecycleError::none;
}

CompanionGattLifecycleError CompanionGattSessionLifecycle::service_timeout(
    std::uint64_t now_ms) {
    if (operation_active_) {
        return CompanionGattLifecycleError::reentrant_call;
    }
    ScopedOperation operation(operation_active_);
    if (faulted_) {
        return CompanionGattLifecycleError::internal_failure;
    }
    if (!connected_) {
        return CompanionGattLifecycleError::no_connection;
    }
    if (blocked_until_disconnect_) {
        return CompanionGattLifecycleError::blocked_until_disconnect;
    }
    const auto time_error = observe_time(now_ms);
    if (time_error != CompanionGattLifecycleError::none) {
        return time_error;
    }
    if (!response_pending_ ||
        now_ms - pending_since_ms_ < policy_.indication_timeout_ms) {
        return CompanionGattLifecycleError::none;
    }
    const auto contained = contain_session(true);
    return contained == CompanionGattLifecycleError::none
               ? CompanionGattLifecycleError::indication_timeout
               : contained;
}

void CompanionGattSessionLifecycle::clear_response_slot() {
    reservation_active_ = false;
    response_pending_ = false;
    reservation_token_ = 0;
    pending_delivery_token_ = 0;
    pending_exchange_id_ = 0;
    pending_since_ms_ = 0;
    response_buffer_.fill(0);
}

CompanionGattLifecycleError
CompanionGattSessionLifecycle::contain_session(bool block) {
    if (reservation_active_) {
        indication_sink_.cancel_reservation(reservation_token_);
    }
    if (response_pending_) {
        indication_sink_.abandon_indication(pending_delivery_token_);
    }
    clear_response_slot();
    CompanionSessionError close_error = CompanionSessionError::none;
    if (session_active_) {
        close_error = coordinator_.close_session(session_controller_binding_);
    }
    session_active_ = false;
    session_controller_binding_ = 0;
    session_nonce_ = 0;
    if (block) {
        blocked_until_disconnect_ = true;
    }
    if (close_error != CompanionSessionError::none) {
        faulted_ = true;
        return CompanionGattLifecycleError::internal_failure;
    }
    return CompanionGattLifecycleError::none;
}

void CompanionGattSessionLifecycle::clear_connection() {
    connected_ = false;
    connection_handle_ = kCompanionGattInvalidConnectionHandle;
    evidence_ = {};
    indication_subscribed_ = false;
    blocked_until_disconnect_ = false;
    time_observed_ = false;
    last_now_ms_ = 0;
}

CompanionGattLifecycleError CompanionGattSessionLifecycle::disconnect(
    std::uint16_t connection_handle) {
    if (operation_active_) {
        return CompanionGattLifecycleError::reentrant_call;
    }
    ScopedOperation operation(operation_active_);
    if (faulted_) {
        return CompanionGattLifecycleError::internal_failure;
    }
    if (!connected_) {
        return CompanionGattLifecycleError::no_connection;
    }
    if (connection_handle != connection_handle_) {
        return CompanionGattLifecycleError::wrong_connection;
    }
    const auto contained = contain_session(false);
    clear_connection();
    return contained;
}

CompanionGattLifecycleStatus CompanionGattSessionLifecycle::status() const {
    return {
        handles_registered_,
        connected_,
        evidence_.encrypted,
        evidence_.authenticated_bond,
        evidence_.application_authorized,
        indication_subscribed_,
        session_active_,
        response_pending_,
        blocked_until_disconnect_,
        faulted_,
        connected_ ? evidence_.att_mtu : static_cast<std::uint16_t>(0),
        session_active_ ? session_nonce_ : 0,
        response_pending_ ? pending_exchange_id_ : 0,
    };
}

}  // namespace opentrail::companion
