#include "companion_boot_self_check.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/companion_gatt_authorization.hpp"

namespace opentrail::target::heltec_v4_bench {
namespace {

using namespace opentrail::companion;
using opentrail::protocol::QuickStatusKind;

// These counters observe calls made by the boot self-check. They are not
// device-authority state, queue state, or a resource reservation.
struct SelfCheckObservation {
    std::uint32_t snapshot_calls{0};
    std::uint32_t prepare_calls{0};
    std::uint32_t commit_calls{0};
    std::uint32_t applied_calls{0};
    std::uint32_t authorization_calls{0};
};

template <std::size_t Size>
bool bytes_equal(const std::array<std::uint8_t, Size>& expected,
                 const std::array<std::uint8_t,
                                  kCompanionMaxResponseRecordBytes>& observed,
                 std::size_t observed_bytes) {
    if (observed_bytes != Size) {
        return false;
    }
    for (std::size_t index = 0; index < Size; ++index) {
        if (observed[index] != expected[index]) {
            return false;
        }
    }
    return true;
}

class FixedSnapshotAuthority final : public CompanionSnapshotAuthority {
public:
    explicit FixedSnapshotAuthority(SelfCheckObservation& observation)
        : observation_(observation) {}

    CompanionSnapshotAuthorityResult read_snapshot() override {
        ++observation_.snapshot_calls;
        return {
            CompanionAuthorityError::none,
            {
                7,
                CompanionRadioState::ready,
                CompanionGnssState::current,
                CompanionPowerState::normal,
                CompanionPositionSharingState::active,
                2,
                0x1122334455667788ULL,
            },
        };
    }

private:
    SelfCheckObservation& observation_;
};

class FixedActionAuthority final : public CompanionActionAuthority {
public:
    explicit FixedActionAuthority(SelfCheckObservation& observation)
        : observation_(observation) {}

    CompanionActionAuthorityResult prepare_action(
        const CompanionActionRequest& request) override {
        if (request.kind != CompanionActionKind::quick_status ||
            request.quick_status != QuickStatusKind::available_to_help ||
            request.critical_alert_id != 0) {
            return {CompanionAuthorityError::failed};
        }
        ++observation_.prepare_calls;
        return {
            CompanionAuthorityError::none,
            CompanionActionDisposition::queued,
            CompanionActionRejectReason::none,
            kOperationToken,
        };
    }

    CompanionAuthorityError commit_action(
        const CompanionActionRequest& request,
        const CompanionActionAuthorityResult& prepared) override {
        if (request.kind != CompanionActionKind::quick_status ||
            request.quick_status != QuickStatusKind::available_to_help ||
            request.critical_alert_id != 0 ||
            prepared.error != CompanionAuthorityError::none ||
            prepared.disposition != CompanionActionDisposition::queued ||
            prepared.reject_reason != CompanionActionRejectReason::none ||
            prepared.operation_token != kOperationToken) {
            return CompanionAuthorityError::failed;
        }
        ++observation_.commit_calls;
        ++observation_.applied_calls;
        return CompanionAuthorityError::none;
    }

private:
    static constexpr std::uint32_t kOperationToken = 0xC0DEC0DEU;
    SelfCheckObservation& observation_;
};

class FixedIndicationSink final : public CompanionGattIndicationSink {
public:
    CompanionGattSinkError reserve(
        std::uint16_t connection_handle,
        std::uint32_t session_nonce,
        std::uint16_t stream_value_handle,
        std::uint64_t delivery_token,
        std::size_t max_response_bytes) override {
        if (reserved_ || pending_ || connection_handle != kConnectionHandle ||
            session_nonce != kSessionNonce ||
            stream_value_handle != kStreamValueHandle ||
            delivery_token == 0 ||
            (max_response_bytes != kCompanionMaxResponseRecordBytes &&
             max_response_bytes !=
                 kCompanionAuthorizationMaxResponseBytes)) {
            return CompanionGattSinkError::failed;
        }
        reserved_ = true;
        token_ = delivery_token;
        return CompanionGattSinkError::none;
    }

    CompanionGattSinkError submit_reserved(
        std::uint16_t connection_handle,
        std::uint32_t session_nonce,
        std::uint16_t stream_value_handle,
        std::uint64_t delivery_token,
        opentrail::radio::ByteView response) override {
        if (!reserved_ || pending_ ||
            connection_handle != kConnectionHandle ||
            session_nonce != kSessionNonce ||
            stream_value_handle != kStreamValueHandle ||
            delivery_token != token_ || response.data == nullptr ||
            response.size > response_.size()) {
            return CompanionGattSinkError::failed;
        }
        reserved_ = false;
        pending_ = true;
        response_bytes_ = response.size;
        response_.fill(0);
        for (std::size_t index = 0; index < response.size; ++index) {
            response_[index] = response.data[index];
        }
        return CompanionGattSinkError::none;
    }

    void cancel_reservation(std::uint64_t delivery_token) override {
        if (reserved_ && delivery_token == token_) {
            reserved_ = false;
        }
    }

    void abandon_indication(std::uint64_t delivery_token) override {
        if (delivery_token == token_) {
            pending_ = false;
        }
    }

    template <std::size_t Size>
    [[nodiscard]] bool response_is(
        const std::array<std::uint8_t, Size>& expected) const {
        if (!pending_ || response_bytes_ != expected.size()) {
            return false;
        }
        for (std::size_t index = 0; index < expected.size(); ++index) {
            if (response_[index] != expected[index]) {
                return false;
            }
        }
        return true;
    }

    void observe_completion() {
        pending_ = false;
    }

    [[nodiscard]] bool reserved() const {
        return reserved_;
    }

    static constexpr std::uint16_t kConnectionHandle = 7;
    static constexpr std::uint16_t kCommandValueHandle = 11;
    static constexpr std::uint16_t kStreamValueHandle = 13;
    static constexpr std::uint16_t kStreamCccdHandle = 14;
    static constexpr std::uint32_t kSessionNonce = 1;

private:
    bool reserved_{false};
    bool pending_{false};
    std::uint64_t token_{0};
    std::size_t response_bytes_{0};
    std::array<std::uint8_t, kCompanionMaxResponseRecordBytes> response_{};
};

class FixedCorrelationIssuer final
    : public CompanionGattAuthorizationCorrelationIssuer {
public:
    CompanionGattAuthorizationCorrelationResult issue(
        const CompanionGattAuthorizationCorrelationContext& context) override {
        if (context.transport_generation != 1 ||
            context.session_nonce != FixedIndicationSink::kSessionNonce ||
            context.exchange_id != 1 ||
            context.purpose !=
                CompanionAuthorizationPurpose::authorize_controller) {
            return {CompanionGattAuthorizationCorrelationError::failed, {}};
        }
        CompanionAuthorizationCorrelation correlation{};
        for (std::size_t index = 0; index < correlation.bytes.size(); ++index) {
            correlation.bytes[index] =
                static_cast<std::uint8_t>(0xA0U + index);
        }
        return {CompanionGattAuthorizationCorrelationError::none,
                correlation};
    }
};

class FixedAuthorizationAuthority final
    : public CompanionGattAuthorizationAuthority {
public:
    FixedAuthorizationAuthority(SelfCheckObservation& observation,
                                FixedIndicationSink& sink)
        : observation_(observation), sink_(sink) {}

    CompanionGattAuthorizationDecision apply_claim(
        CompanionAuthorizationPurpose purpose,
        const CompanionControllerClaim& claim,
        std::uint64_t now_ms) override {
        if (!sink_.reserved() ||
            purpose != CompanionAuthorizationPurpose::authorize_controller ||
            claim.controller_binding != kControllerBinding ||
            !claim.link_encrypted || !claim.authenticated_bond ||
            now_ms != 2) {
            return {CompanionGattAuthorizationAuthorityError::failed};
        }
        ++observation_.authorization_calls;
        return {
            CompanionGattAuthorizationAuthorityError::none,
            CompanionAuthorizationClaimOutcome::accepted,
            CompanionAuthorizationDenyReason::none,
            kControllerBinding,
        };
    }

    CompanionGattAuthorizationAuthorityError release_connection(
        std::uint64_t) override {
        return CompanionGattAuthorizationAuthorityError::failed;
    }

    static constexpr std::uint64_t kControllerBinding = 0xA5;

private:
    SelfCheckObservation& observation_;
    FixedIndicationSink& sink_;
};

constexpr CompanionSessionEvidence kEvidence{0xA5, true, true, true};

constexpr std::array<std::uint8_t, 20> kAuthorizationProtocolInfo{
    0x4F, 0x54, 0x42, 0x30, 0x00, 0x01, 0x01, 0x1F,
    0x80, 0x00, 0x97, 0x00, 0x10, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00,
};

constexpr std::array<std::uint8_t, 28> kAuthorizationClaimStart{
    0x4F, 0x54, 0x43, 0x30, 0x00, 0x00, 0x03, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x08, 0x00, 0x4F, 0x54, 0x4C, 0x30,
    0x00, 0x00, 0x01, 0x00,
};

constexpr std::array<std::uint8_t, 44> kAuthorizationPending{
    0x4F, 0x54, 0x43, 0x30, 0x00, 0x00, 0x84, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x18, 0x00, 0x4F, 0x54, 0x50, 0x30,
    0x00, 0x00, 0x01, 0x01, 0xA0, 0xA1, 0xA2, 0xA3,
    0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB,
    0xAC, 0xAD, 0xAE, 0xAF,
};

constexpr std::array<std::uint8_t, 48> kAuthorizationAccepted{
    0x4F, 0x54, 0x43, 0x30, 0x00, 0x00, 0x85, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x1C, 0x00, 0x4F, 0x54, 0x46, 0x30,
    0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
};

constexpr std::array<std::uint8_t, 40> kActionRequest{
    0x4F, 0x54, 0x43, 0x30, 0x00, 0x00, 0x02, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x14, 0x00,
    0x4F, 0x54, 0x41, 0x30, 0x00, 0x00, 0x01, 0x04,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};

constexpr std::array<std::uint8_t, 40> kActionResponse{
    0x4F, 0x54, 0x43, 0x30, 0x00, 0x00, 0x82, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x14, 0x00,
    0x4F, 0x54, 0x52, 0x30, 0x00, 0x00, 0x02, 0x00,
    0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};

constexpr std::array<std::uint8_t, 28> kSnapshotRequest{
    0x4F, 0x54, 0x43, 0x30, 0x00, 0x00, 0x01, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x08, 0x00,
    0x4F, 0x54, 0x58, 0x30, 0x00, 0x00, 0x00, 0x00,
};

constexpr std::array<std::uint8_t, 52> kSnapshotResponse{
    0x4F, 0x54, 0x43, 0x30, 0x00, 0x00, 0x81, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x20, 0x00,
    0x4F, 0x54, 0x4E, 0x30, 0x00, 0x00, 0x02, 0x03,
    0x02, 0x02, 0x02, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static_assert(kActionRequest.size() == kCompanionMaxRequestRecordBytes);
static_assert(kActionResponse.size() ==
              kCompanionFragmentHeaderBytes + kCompanionActionResultBytes);
static_assert(kSnapshotRequest.size() ==
              kCompanionFragmentHeaderBytes + kCompanionSnapshotRequestBytes);
static_assert(kSnapshotResponse.size() == kCompanionMaxResponseRecordBytes);

}  // namespace

bool run_companion_request_coordinator_self_check() {
    SelfCheckObservation observation{};
    FixedSnapshotAuthority snapshots(observation);
    FixedActionAuthority actions(observation);

    {
        CompanionRequestCoordinator coordinator(snapshots, actions);
        if (!coordinator.open_session(kEvidence, 1).opened()) {
            return false;
        }
        std::array<std::uint8_t, kCompanionMaxResponseRecordBytes> response{};
        const auto first = coordinator.service(
            kEvidence,
            {kActionRequest.data(), kActionRequest.size()},
            {response.data(), response.size()});
        if (first.disposition != CompanionCoordinatorDisposition::processed_new ||
            first.error != CompanionCoordinatorError::none ||
            !bytes_equal(kActionResponse, response, first.response_bytes) ||
            observation.prepare_calls != 1 || observation.commit_calls != 1 ||
            observation.applied_calls != 1) {
            return false;
        }

        response.fill(0xA5);
        const auto duplicate = coordinator.service(
            kEvidence,
            {kActionRequest.data(), kActionRequest.size()},
            {response.data(), response.size()});
        if (duplicate.disposition !=
                CompanionCoordinatorDisposition::replayed_cached_response ||
            duplicate.error != CompanionCoordinatorError::none ||
            !bytes_equal(kActionResponse, response, duplicate.response_bytes) ||
            observation.prepare_calls != 1 || observation.commit_calls != 1 ||
            observation.applied_calls != 1) {
            return false;
        }
    }

    {
        CompanionRequestCoordinator coordinator(snapshots, actions);
        if (!coordinator.open_session(kEvidence, 3).opened()) {
            return false;
        }
        std::array<std::uint8_t, kCompanionMaxResponseRecordBytes> response{};
        const auto result = coordinator.service(
            kEvidence,
            {kSnapshotRequest.data(), kSnapshotRequest.size()},
            {response.data(), response.size()});
        if (result.disposition !=
                CompanionCoordinatorDisposition::processed_new ||
            result.error != CompanionCoordinatorError::none ||
            !bytes_equal(kSnapshotResponse, response, result.response_bytes) ||
            observation.snapshot_calls != 1 || observation.prepare_calls != 1 ||
            observation.commit_calls != 1 || observation.applied_calls != 1) {
            return false;
        }
    }

    return true;
}

bool run_companion_gatt_session_self_check() {
    SelfCheckObservation observation{};
    FixedSnapshotAuthority snapshots(observation);
    FixedActionAuthority actions(observation);
    CompanionRequestCoordinator coordinator(snapshots, actions);
    FixedIndicationSink sink;
    CompanionGattSessionLifecycle lifecycle(coordinator, sink);
    constexpr CompanionGattHandles handles{
        FixedIndicationSink::kCommandValueHandle,
        FixedIndicationSink::kStreamValueHandle,
        FixedIndicationSink::kStreamCccdHandle,
    };
    constexpr CompanionGattConnectionEvidence evidence{
        kCompanionMinimumAttMtu,
        true,
        true,
        true,
        kEvidence.controller_binding,
    };
    if (lifecycle.register_handles(handles) !=
            CompanionGattLifecycleError::none ||
        lifecycle.connect(FixedIndicationSink::kConnectionHandle) !=
            CompanionGattLifecycleError::none ||
        lifecycle.update_connection_evidence(
            FixedIndicationSink::kConnectionHandle, evidence) !=
            CompanionGattLifecycleError::none ||
        lifecycle.update_indication_subscription(
            FixedIndicationSink::kConnectionHandle,
            FixedIndicationSink::kStreamCccdHandle, true) !=
            CompanionGattLifecycleError::none ||
        !lifecycle.open_session(
             FixedIndicationSink::kConnectionHandle,
             FixedIndicationSink::kSessionNonce).opened()) {
        return false;
    }

    const auto first = lifecycle.service_command(
        FixedIndicationSink::kConnectionHandle,
        FixedIndicationSink::kCommandValueHandle,
        {kActionRequest.data(), kActionRequest.size()}, 0);
    if (!first.pending() || first.delivery_token == 0 ||
        !sink.response_is(kActionResponse) ||
        observation.prepare_calls != 1 || observation.commit_calls != 1 ||
        observation.applied_calls != 1) {
        return false;
    }
    sink.observe_completion();
    if (lifecycle.complete_indication(
            FixedIndicationSink::kConnectionHandle,
            FixedIndicationSink::kSessionNonce,
            FixedIndicationSink::kStreamValueHandle,
            first.delivery_token, true) != CompanionGattLifecycleError::none) {
        return false;
    }

    const auto duplicate = lifecycle.service_command(
        FixedIndicationSink::kConnectionHandle,
        FixedIndicationSink::kCommandValueHandle,
        {kActionRequest.data(), kActionRequest.size()}, 1);
    return duplicate.pending() &&
           duplicate.coordinator.duplicate() &&
           duplicate.delivery_token != first.delivery_token &&
           sink.response_is(kActionResponse) &&
           observation.prepare_calls == 1 &&
           observation.commit_calls == 1 &&
           observation.applied_calls == 1;
}

bool run_companion_gatt_authorization_self_check() {
    SelfCheckObservation observation{};
    FixedSnapshotAuthority snapshots(observation);
    FixedActionAuthority actions(observation);
    CompanionRequestCoordinator coordinator(snapshots, actions);
    FixedIndicationSink sink;
    FixedCorrelationIssuer issuer;
    FixedAuthorizationAuthority authority(observation, sink);
    CompanionGattAuthorizationLifecycle lifecycle(
        coordinator, sink, issuer, authority);
    constexpr CompanionGattAuthorizationHandles handles{
        10,
        FixedIndicationSink::kCommandValueHandle,
        FixedIndicationSink::kStreamValueHandle,
        FixedIndicationSink::kStreamCccdHandle,
    };
    constexpr CompanionGattAuthorizationConnectionEvidence evidence{
        kCompanionMinimumAttMtu,
        {
            {0x0102030405060708ULL, 0x1112131415161718ULL},
            0x2122232425262728ULL,
            1,
            FixedAuthorizationAuthority::kControllerBinding,
            true,
            true,
        },
    };
    if (lifecycle.register_handles(handles) !=
            CompanionGattAuthorizationError::none ||
        lifecycle.connect(FixedIndicationSink::kConnectionHandle, 1) !=
            CompanionGattAuthorizationError::none ||
        lifecycle.update_connection_evidence(
            FixedIndicationSink::kConnectionHandle, 1, evidence) !=
            CompanionGattAuthorizationError::none ||
        lifecycle.open_provisional_session(
            FixedIndicationSink::kConnectionHandle, 1,
            FixedIndicationSink::kSessionNonce) !=
            CompanionGattAuthorizationError::none) {
        return false;
    }

    std::array<std::uint8_t,
               kCompanionAuthorizationProtocolInfoBytes> protocol_info{};
    const auto read = lifecycle.read_protocol_info(
        FixedIndicationSink::kConnectionHandle, 1,
        handles.protocol_info_value,
        {protocol_info.data(), protocol_info.size()});
    if (read.error != CompanionGattAuthorizationError::none ||
        protocol_info != kAuthorizationProtocolInfo ||
        lifecycle.update_indication_subscription(
            FixedIndicationSink::kConnectionHandle, 1,
            FixedIndicationSink::kStreamCccdHandle, true) !=
            CompanionGattAuthorizationError::none) {
        return false;
    }

    const auto pending = lifecycle.service_command(
        FixedIndicationSink::kConnectionHandle, 1,
        FixedIndicationSink::kCommandValueHandle,
        {kAuthorizationClaimStart.data(), kAuthorizationClaimStart.size()}, 0);
    if (!pending.pending() ||
        pending.delivery_token !=
            kCompanionAuthorizationPendingDeliveryToken ||
        !sink.response_is(kAuthorizationPending) ||
        observation.authorization_calls != 0) {
        return false;
    }
    sink.observe_completion();
    if (lifecycle.complete_indication(
            FixedIndicationSink::kConnectionHandle, 1,
            FixedIndicationSink::kSessionNonce,
            FixedIndicationSink::kStreamValueHandle,
            pending.delivery_token, true, 1) !=
            CompanionGattAuthorizationError::none ||
        observation.authorization_calls != 0) {
        return false;
    }

    const auto terminal = lifecycle.resolve_claim(
        FixedIndicationSink::kConnectionHandle, 1,
        FixedIndicationSink::kSessionNonce, 1, 2);
    if (!terminal.pending() ||
        terminal.delivery_token !=
            kCompanionAuthorizationTerminalDeliveryToken ||
        !sink.response_is(kAuthorizationAccepted) ||
        observation.authorization_calls != 1 ||
        lifecycle.status().application_authorized) {
        return false;
    }
    sink.observe_completion();
    if (lifecycle.complete_indication(
            FixedIndicationSink::kConnectionHandle, 1,
            FixedIndicationSink::kSessionNonce,
            FixedIndicationSink::kStreamValueHandle,
            terminal.delivery_token, true, 3) !=
        CompanionGattAuthorizationError::none) {
        return false;
    }
    const auto status = lifecycle.status();
    return status.application_authorized && status.normal_session_active &&
           !status.response_pending && status.exchange_id == 0 &&
           observation.authorization_calls == 1;
}

}  // namespace opentrail::target::heltec_v4_bench
