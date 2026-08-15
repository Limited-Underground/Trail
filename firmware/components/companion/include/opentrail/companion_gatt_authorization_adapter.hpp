#pragma once

#include <cstddef>
#include <cstdint>

#include "opentrail/companion_gatt_authorization.hpp"

namespace opentrail::companion {

inline constexpr std::uint8_t kCompanionGattMinimumSecurityKeyBytes = 16;

enum class CompanionGattAdapterError : std::uint8_t {
    none = 0,
    invalid_argument,
    not_registered,
    already_registered,
    no_connection,
    connection_in_use,
    wrong_connection,
    wrong_generation,
    generation_exhausted,
    insecure_link,
    binding_unavailable,
    session_unavailable,
    wrong_attribute,
    operation_not_authorized,
    lifecycle_rejected,
    indication_mismatch,
    no_pending_indication,
};

enum class CompanionGattAttributeOperation : std::uint8_t {
    read = 1,
    write = 2,
};

struct CompanionGattAdapterLinkSecurity {
    bool encrypted{false};
    bool authenticated{false};
    bool bonded{false};
    std::uint8_t key_size{0};
    std::uint16_t att_mtu{kCompanionGattDefaultAttMtu};
};

enum class CompanionGattTrustedBindingError : std::uint8_t {
    none = 0,
    not_ready,
    failed,
};

struct CompanionGattTrustedBindingResult {
    CompanionGattTrustedBindingError error{
        CompanionGattTrustedBindingError::not_ready};
    CompanionControllerClaim claim{};
    std::uint32_t provisional_session_nonce{0};
};

// Trusted private adapter seam. It resolves an opaque bond token and private
// boot/session/controller bindings for one exact live connection generation.
// It must not derive identity from peer payload bytes or expose an address,
// public identifier, key, or loggable value. Calls are externally serialized
// and must not re-enter the callback adapter.
class CompanionGattTrustedBindingAuthority {
public:
    virtual ~CompanionGattTrustedBindingAuthority() = default;
    [[nodiscard]] virtual CompanionGattTrustedBindingResult resolve(
        std::uint16_t connection_handle,
        std::uint64_t transport_generation) = 0;
};

// Exact indication transport owned by the serialized GATT host. reserve()
// must retain enough real transport memory for max_response_bytes before
// returning success. submit_reserved() consumes that reservation. Completion
// is observed only after the callback adapter validates the exact tuple.
class CompanionGattIndicationPort {
public:
    virtual ~CompanionGattIndicationPort() = default;
    [[nodiscard]] virtual CompanionGattSinkError reserve(
        std::uint16_t connection_handle,
        std::uint64_t transport_generation,
        std::uint32_t session_nonce,
        std::uint16_t stream_value_handle,
        std::uint64_t delivery_token,
        std::size_t max_response_bytes) = 0;
    [[nodiscard]] virtual CompanionGattSinkError submit_reserved(
        std::uint16_t connection_handle,
        std::uint64_t transport_generation,
        std::uint32_t session_nonce,
        std::uint16_t stream_value_handle,
        std::uint64_t delivery_token,
        radio::ByteView response) = 0;
    virtual void cancel_reservation(std::uint64_t delivery_token) = 0;
    virtual void abandon_indication(std::uint64_t delivery_token) = 0;
    // Called after the lifecycle accepts a request and returns its exact
    // exchange. The serialized host must bind it to the already-submitted
    // indication tuple before a terminal callback can be consumed.
    virtual void bind_exchange(
        std::uint64_t delivery_token,
        std::uint32_t exchange_id) = 0;
    virtual void observe_completion(std::uint64_t delivery_token) = 0;
};

struct CompanionGattAdapterConnectResult {
    CompanionGattAdapterError error{CompanionGattAdapterError::invalid_argument};
    std::uint64_t transport_generation{0};

    [[nodiscard]] constexpr bool connected() const {
        return error == CompanionGattAdapterError::none;
    }
};

struct CompanionGattAdapterPendingIndication {
    bool valid{false};
    std::uint16_t connection_handle{kCompanionGattInvalidConnectionHandle};
    std::uint64_t transport_generation{0};
    std::uint32_t session_nonce{0};
    std::uint32_t exchange_id{0};
    std::uint16_t stream_value_handle{0};
    std::uint64_t delivery_token{0};
};

struct CompanionGattAdapterStatus {
    bool handles_registered{false};
    bool connected{false};
    bool secure_bond{false};
    std::uint64_t transport_generation{0};
    CompanionGattAdapterPendingIndication pending{};
    CompanionGattAuthorizationStatus lifecycle{};
};

// Fixed-memory callback owner above one serialized GATT host. Raw platform
// callbacks must refresh current-link encryption/authentication/bond evidence
// before every protected access. The trusted private binding is never decoded
// from OTC0. Protocol Info and claim traffic may proceed before application
// authorization; normal requests remain delegated to the lifecycle and denied
// until exact Accepted/Replaced indication confirmation promotes this session.
// This class is a synchronous re-entry guard, not a thread-safety primitive.
class CompanionGattAuthorizationCallbackAdapter final
    : public CompanionGattIndicationSink {
public:
    CompanionGattAuthorizationCallbackAdapter(
        CompanionRequestCoordinator& normal_coordinator,
        CompanionGattIndicationPort& indication_port,
        CompanionGattTrustedBindingAuthority& binding_authority,
        CompanionGattAuthorizationCorrelationIssuer& correlation_issuer,
        CompanionGattAuthorizationAuthority& authorization_authority,
        CompanionGattAuthorizationPolicy policy = {});

    [[nodiscard]] CompanionGattAdapterError register_handles(
        CompanionGattAuthorizationHandles handles);
    [[nodiscard]] CompanionGattAdapterConnectResult connect(
        std::uint16_t connection_handle);
    [[nodiscard]] CompanionGattAdapterError refresh_security(
        std::uint16_t connection_handle,
        CompanionGattAdapterLinkSecurity security);
    [[nodiscard]] bool authorize_attribute(
        std::uint16_t connection_handle,
        std::uint16_t attribute_handle,
        CompanionGattAttributeOperation operation);
    [[nodiscard]] CompanionGattAuthorizationReadResult read_protocol_info(
        std::uint16_t connection_handle,
        std::uint16_t attribute_handle,
        radio::MutableByteView output);
    [[nodiscard]] CompanionGattAdapterError update_stream_subscription(
        std::uint16_t connection_handle,
        std::uint16_t observed_stream_value_handle,
        bool indications_enabled);
    [[nodiscard]] CompanionGattAuthorizationRequestResult service_command(
        std::uint16_t connection_handle,
        std::uint16_t command_value_handle,
        radio::ByteView encoded_request,
        std::uint64_t now_ms);
    [[nodiscard]] CompanionGattAuthorizationRequestResult resolve_claim(
        std::uint16_t connection_handle,
        std::uint64_t transport_generation,
        std::uint32_t session_nonce,
        std::uint32_t exchange_id,
        CompanionGattAdapterLinkSecurity current_security,
        std::uint64_t now_ms);
    [[nodiscard]] CompanionGattAdapterError complete_indication(
        const CompanionGattAdapterPendingIndication& expected,
        bool confirmed,
        std::uint64_t now_ms);
    [[nodiscard]] CompanionGattAdapterError service_timeout(
        const CompanionGattAdapterPendingIndication& expected,
        std::uint64_t now_ms);
    [[nodiscard]] CompanionGattAdapterError disconnect(
        std::uint16_t connection_handle);
    [[nodiscard]] CompanionGattAdapterStatus status() const;

    CompanionGattSinkError reserve(
        std::uint16_t connection_handle,
        std::uint32_t session_nonce,
        std::uint16_t stream_value_handle,
        std::uint64_t delivery_token,
        std::size_t max_response_bytes) override;
    CompanionGattSinkError submit_reserved(
        std::uint16_t connection_handle,
        std::uint32_t session_nonce,
        std::uint16_t stream_value_handle,
        std::uint64_t delivery_token,
        radio::ByteView response) override;
    void cancel_reservation(std::uint64_t delivery_token) override;
    void abandon_indication(std::uint64_t delivery_token) override;

private:
    [[nodiscard]] CompanionGattAdapterError connection_error(
        std::uint16_t connection_handle) const;
    [[nodiscard]] CompanionGattAdapterError refresh_security_impl(
        std::uint16_t connection_handle,
        CompanionGattAdapterLinkSecurity security);
    [[nodiscard]] bool attribute_authorized(
        std::uint16_t connection_handle,
        std::uint16_t attribute_handle,
        CompanionGattAttributeOperation operation) const;
    [[nodiscard]] CompanionGattAdapterError map_lifecycle_error(
        CompanionGattAuthorizationError error) const;
    [[nodiscard]] bool valid_security(
        const CompanionGattAdapterLinkSecurity& security) const;
    [[nodiscard]] bool exact_pending(
        const CompanionGattAdapterPendingIndication& expected) const;
    void observe_request(
        const CompanionGattAuthorizationRequestResult& result);
    void clear_connection();

    CompanionGattIndicationPort& indication_port_;
    CompanionGattTrustedBindingAuthority& binding_authority_;
    CompanionGattAuthorizationLifecycle lifecycle_;
    CompanionGattAuthorizationHandles handles_{};
    CompanionGattAdapterLinkSecurity security_{};
    CompanionControllerClaim claim_{};
    CompanionGattAdapterPendingIndication pending_{};
    bool handles_registered_{false};
    bool connected_{false};
    bool secure_bond_{false};
    bool session_open_{false};
    std::uint16_t connection_handle_{kCompanionGattInvalidConnectionHandle};
    std::uint64_t transport_generation_{0};
    std::uint64_t next_transport_generation_{1};
    std::uint32_t session_nonce_{0};
    bool operation_active_{false};
};

}  // namespace opentrail::companion
