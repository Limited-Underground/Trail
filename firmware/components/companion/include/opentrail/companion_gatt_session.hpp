#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "opentrail/companion_request_coordinator.hpp"

namespace opentrail::companion {

inline constexpr std::uint16_t kCompanionGattInvalidConnectionHandle =
    std::numeric_limits<std::uint16_t>::max();
inline constexpr std::uint16_t kCompanionGattDefaultAttMtu = 23;

struct CompanionGattHandles {
    std::uint16_t command_value{0};
    std::uint16_t stream_value{0};
    std::uint16_t stream_cccd{0};
};

struct CompanionGattConnectionEvidence {
    std::uint16_t att_mtu{kCompanionGattDefaultAttMtu};
    bool encrypted{false};
    bool authenticated_bond{false};
    bool application_authorized{false};
    std::uint64_t controller_binding{0};
};

struct CompanionGattLifecyclePolicy {
    std::uint64_t indication_timeout_ms{5000};
    std::uint64_t first_delivery_token{1};
    std::uint64_t final_delivery_token{
        std::numeric_limits<std::uint64_t>::max()};
};

enum class CompanionGattSinkError : std::uint8_t {
    none = 0,
    busy,
    failed,
};

// Target transport seam. reserve() must atomically reserve capacity for one
// indication of max_response_bytes under the exact connection/session/value-
// handle/delivery-token tuple. submit_reserved() consumes that reservation on
// every outcome and copies or otherwise takes ownership of the bytes before it
// returns none. cancel_reservation() releases a pre-submit reservation.
// abandon_indication() idempotently releases target bookkeeping after
// containment, including after a negative completion already cleared transport
// state. It does not authorize another request until the lifecycle observes
// exact disconnect.
class CompanionGattIndicationSink {
public:
    virtual ~CompanionGattIndicationSink() = default;

    [[nodiscard]] virtual CompanionGattSinkError reserve(
        std::uint16_t connection_handle,
        std::uint32_t session_nonce,
        std::uint16_t stream_value_handle,
        std::uint64_t delivery_token,
        std::size_t max_response_bytes) = 0;
    [[nodiscard]] virtual CompanionGattSinkError submit_reserved(
        std::uint16_t connection_handle,
        std::uint32_t session_nonce,
        std::uint16_t stream_value_handle,
        std::uint64_t delivery_token,
        radio::ByteView response) = 0;
    virtual void cancel_reservation(std::uint64_t delivery_token) = 0;
    virtual void abandon_indication(std::uint64_t delivery_token) = 0;
};

enum class CompanionGattLifecycleError : std::uint8_t {
    none = 0,
    invalid_argument,
    handles_not_registered,
    handles_already_registered,
    no_connection,
    connection_in_use,
    wrong_connection,
    wrong_attribute,
    mtu_too_small,
    insecure_link,
    application_unauthorized,
    subscription_required,
    session_not_open,
    session_rejected,
    response_path_busy,
    response_path_unavailable,
    coordinator_rejected,
    indication_submit_failed,
    no_outstanding_indication,
    indication_mismatch,
    indication_failed,
    indication_timeout,
    delivery_token_exhausted,
    clock_rollback,
    blocked_until_disconnect,
    reentrant_call,
    internal_failure,
};

enum class CompanionGattRequestDisposition : std::uint8_t {
    rejected = 0,
    indication_pending,
};

struct CompanionGattOpenResult {
    CompanionGattLifecycleError error{
        CompanionGattLifecycleError::invalid_argument};
    CompanionSessionResult session{};

    [[nodiscard]] constexpr bool opened() const {
        return error == CompanionGattLifecycleError::none && session.opened();
    }
};

struct CompanionGattRequestResult {
    CompanionGattRequestDisposition disposition{
        CompanionGattRequestDisposition::rejected};
    CompanionGattLifecycleError error{
        CompanionGattLifecycleError::invalid_argument};
    CompanionCoordinatorResult coordinator{};
    std::uint64_t delivery_token{0};

    [[nodiscard]] constexpr bool pending() const {
        return disposition ==
                   CompanionGattRequestDisposition::indication_pending &&
               error == CompanionGattLifecycleError::none;
    }
};

struct CompanionGattLifecycleStatus {
    bool handles_registered{false};
    bool connected{false};
    bool encrypted{false};
    bool authenticated_bond{false};
    bool application_authorized{false};
    bool indication_subscribed{false};
    bool session_active{false};
    bool response_pending{false};
    bool blocked_until_disconnect{false};
    bool faulted{false};
    std::uint16_t att_mtu{0};
    std::uint32_t session_nonce{0};
    std::uint32_t pending_exchange_id{0};
};

// Fixed-memory, serialized one-connection owner above a request coordinator.
// It never derives handles, controller bindings, or security facts from client
// bytes. A request reaches the coordinator only after an exact maximum-size
// indication reservation exists. Sink callbacks must not re-enter this owner.
class CompanionGattSessionLifecycle {
public:
    CompanionGattSessionLifecycle(
        CompanionRequestCoordinator& coordinator,
        CompanionGattIndicationSink& indication_sink,
        CompanionGattLifecyclePolicy policy = {});

    [[nodiscard]] CompanionGattLifecycleError register_handles(
        CompanionGattHandles handles);
    [[nodiscard]] CompanionGattLifecycleError connect(
        std::uint16_t connection_handle);
    [[nodiscard]] CompanionGattLifecycleError update_connection_evidence(
        std::uint16_t connection_handle,
        CompanionGattConnectionEvidence evidence);
    [[nodiscard]] CompanionGattLifecycleError update_indication_subscription(
        std::uint16_t connection_handle,
        std::uint16_t cccd_handle,
        bool indications_enabled);
    [[nodiscard]] CompanionGattOpenResult open_session(
        std::uint16_t connection_handle,
        std::uint32_t device_generated_session_nonce);
    [[nodiscard]] CompanionGattRequestResult service_command(
        std::uint16_t connection_handle,
        std::uint16_t command_value_handle,
        radio::ByteView encoded_request,
        std::uint64_t now_ms);
    [[nodiscard]] CompanionGattLifecycleError complete_indication(
        std::uint16_t connection_handle,
        std::uint32_t session_nonce,
        std::uint16_t stream_value_handle,
        std::uint64_t delivery_token,
        bool confirmed);
    [[nodiscard]] CompanionGattLifecycleError service_timeout(
        std::uint64_t now_ms);
    [[nodiscard]] CompanionGattLifecycleError disconnect(
        std::uint16_t connection_handle);
    [[nodiscard]] CompanionGattLifecycleStatus status() const;

private:
    [[nodiscard]] CompanionGattLifecycleError connection_error(
        std::uint16_t connection_handle) const;
    [[nodiscard]] CompanionGattLifecycleError response_path_error() const;
    [[nodiscard]] CompanionSessionEvidence session_evidence() const;
    [[nodiscard]] CompanionGattLifecycleError observe_time(
        std::uint64_t now_ms);
    [[nodiscard]] CompanionGattLifecycleError contain_session(bool block);
    void clear_response_slot();
    void clear_connection();

    CompanionRequestCoordinator& coordinator_;
    CompanionGattIndicationSink& indication_sink_;
    CompanionGattLifecyclePolicy policy_{};
    CompanionGattHandles handles_{};
    CompanionGattConnectionEvidence evidence_{};
    bool handles_registered_{false};
    bool connected_{false};
    bool indication_subscribed_{false};
    bool session_active_{false};
    bool reservation_active_{false};
    bool response_pending_{false};
    bool blocked_until_disconnect_{false};
    bool faulted_{false};
    bool operation_active_{false};
    bool time_observed_{false};
    std::uint16_t connection_handle_{kCompanionGattInvalidConnectionHandle};
    std::uint64_t session_controller_binding_{0};
    std::uint32_t session_nonce_{0};
    std::uint32_t pending_exchange_id_{0};
    std::uint64_t reservation_token_{0};
    std::uint64_t pending_delivery_token_{0};
    std::uint64_t next_delivery_token_{0};
    std::uint64_t pending_since_ms_{0};
    std::uint64_t last_now_ms_{0};
    std::array<std::uint8_t, kCompanionMaxResponseRecordBytes>
        response_buffer_{};
};

}  // namespace opentrail::companion
