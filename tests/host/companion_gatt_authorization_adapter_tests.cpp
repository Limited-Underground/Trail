#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/companion_gatt_authorization_adapter.hpp"

namespace {

using namespace opentrail::companion;

#define EXPECT(condition)                                                     \
    do {                                                                      \
        if (!(condition)) {                                                   \
            std::cerr << "FAIL line " << __LINE__ << ": " #condition        \
                      << '\n';                                                \
            std::exit(1);                                                     \
        }                                                                     \
    } while (false)

constexpr std::uint16_t kConnection = 7;
constexpr CompanionGattAuthorizationHandles kHandles{10, 11, 13, 14};
constexpr std::uint32_t kSession = 1;

constexpr std::array<std::uint8_t, 28> kClaimStart{
    0x4F, 0x54, 0x43, 0x30, 0x00, 0x00, 0x03, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x08, 0x00, 0x4F, 0x54, 0x4C, 0x30,
    0x00, 0x00, 0x01, 0x00,
};

constexpr std::array<std::uint8_t, 28> kSnapshotRequest{
    0x4F, 0x54, 0x43, 0x30, 0x00, 0x00, 0x01, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x08, 0x00, 0x4F, 0x54, 0x58, 0x30,
    0x00, 0x00, 0x00, 0x00,
};

CompanionGattAdapterLinkSecurity secure(std::uint16_t mtu = 151) {
    return {true, true, true, 16, mtu};
}

class SnapshotAuthority final : public CompanionSnapshotAuthority {
public:
    CompanionSnapshotAuthorityResult read_snapshot() override {
        CompanionStatusSnapshot snapshot{};
        snapshot.revision = 1;
        return {CompanionAuthorityError::none, snapshot};
    }
};

class ActionAuthority final : public CompanionActionAuthority {
public:
    CompanionActionAuthorityResult prepare_action(
        const CompanionActionRequest&) override {
        return {CompanionAuthorityError::failed};
    }
    CompanionAuthorityError commit_action(
        const CompanionActionRequest&,
        const CompanionActionAuthorityResult&) override {
        return CompanionAuthorityError::failed;
    }
};

class BindingAuthority final : public CompanionGattTrustedBindingAuthority {
public:
    CompanionGattTrustedBindingResult resolve(
        std::uint16_t connection,
        std::uint64_t generation) override {
        ++calls;
        if (reenter && adapter != nullptr) {
            reentry_result = adapter->disconnect(connection);
        }
        if (error != CompanionGattTrustedBindingError::none) {
            return {error, {}, 0};
        }
        CompanionControllerClaim claim{};
        claim.bond_identity = {0x11, 0x22};
        claim.boot_challenge = 0x33;
        claim.session_challenge = generation;
        claim.controller_binding = 0x44;
        return {CompanionGattTrustedBindingError::none, claim, session};
    }

    CompanionGattTrustedBindingError error{
        CompanionGattTrustedBindingError::none};
    std::uint32_t session{kSession};
    std::uint32_t calls{0};
    bool reenter{false};
    CompanionGattAuthorizationCallbackAdapter* adapter{nullptr};
    CompanionGattAdapterError reentry_result{CompanionGattAdapterError::none};
};

class CorrelationIssuer final
    : public CompanionGattAuthorizationCorrelationIssuer {
public:
    CompanionGattAuthorizationCorrelationResult issue(
        const CompanionGattAuthorizationCorrelationContext&) override {
        if (reenter && adapter != nullptr) {
            reentry_result = adapter->disconnect(kConnection);
        }
        CompanionAuthorizationCorrelation value{};
        for (std::size_t index = 0; index < value.bytes.size(); ++index) {
            value.bytes[index] = static_cast<std::uint8_t>(0xA0U + index);
        }
        return {CompanionGattAuthorizationCorrelationError::none, value};
    }

    bool reenter{false};
    CompanionGattAuthorizationCallbackAdapter* adapter{nullptr};
    CompanionGattAdapterError reentry_result{CompanionGattAdapterError::none};
};

class AuthorizationAuthority final : public CompanionGattAuthorizationAuthority {
public:
    CompanionGattAuthorizationDecision apply_claim(
        CompanionAuthorizationPurpose,
        const CompanionControllerClaim&,
        std::uint64_t) override {
        ++apply_calls;
        if (reenter && adapter != nullptr) {
            reentry_result = adapter->disconnect(kConnection);
        }
        if (decision_error !=
            CompanionGattAuthorizationAuthorityError::none) {
            return {decision_error};
        }
        return {
            CompanionGattAuthorizationAuthorityError::none,
            CompanionAuthorizationClaimOutcome::accepted,
            CompanionAuthorizationDenyReason::none,
            0x44,
        };
    }
    CompanionGattAuthorizationAuthorityError release_connection(
        std::uint64_t binding) override {
        ++release_calls;
        if (reenter_release && adapter != nullptr) {
            reentry_result = adapter->disconnect(kConnection);
        }
        return binding == 0x44
                   ? CompanionGattAuthorizationAuthorityError::none
                   : CompanionGattAuthorizationAuthorityError::failed;
    }

    std::uint32_t apply_calls{0};
    std::uint32_t release_calls{0};
    bool reenter{false};
    bool reenter_release{false};
    CompanionGattAuthorizationAuthorityError decision_error{
        CompanionGattAuthorizationAuthorityError::none};
    CompanionGattAuthorizationCallbackAdapter* adapter{nullptr};
    CompanionGattAdapterError reentry_result{CompanionGattAdapterError::none};
};

class IndicationPort final : public CompanionGattIndicationPort {
public:
    CompanionGattSinkError reserve(
        std::uint16_t connection,
        std::uint64_t generation,
        std::uint32_t session,
        std::uint16_t stream,
        std::uint64_t token,
        std::size_t maximum) override {
        ++reserve_calls;
        if (reenter_reserve && adapter != nullptr) {
            reentry_result = adapter->disconnect(connection);
        }
        if (reserved || pending || connection != kConnection ||
            generation == 0 || session == 0 ||
            stream != kHandles.stream_value ||
            token == 0 || maximum > response.size()) {
            return CompanionGattSinkError::failed;
        }
        reserved = true;
        transport_generation = generation;
        session_nonce = session;
        delivery_token = token;
        return CompanionGattSinkError::none;
    }
    CompanionGattSinkError submit_reserved(
        std::uint16_t connection,
        std::uint64_t generation,
        std::uint32_t session,
        std::uint16_t stream,
        std::uint64_t token,
        opentrail::radio::ByteView value) override {
        ++submit_calls;
        if (reenter_submit && adapter != nullptr) {
            reentry_result = adapter->disconnect(connection);
        }
        if (fail_submit) {
            reserved = false;
            return CompanionGattSinkError::failed;
        }
        if (!reserved || pending || connection != kConnection ||
            generation != transport_generation ||
            session != session_nonce || stream != kHandles.stream_value ||
            token != delivery_token || value.data == nullptr ||
            value.size > response.size()) {
            return CompanionGattSinkError::failed;
        }
        reserved = false;
        pending = true;
        response_bytes = value.size;
        for (std::size_t index = 0; index < value.size; ++index) {
            response[index] = value.data[index];
        }
        return CompanionGattSinkError::none;
    }
    void cancel_reservation(std::uint64_t token) override {
        if (reenter_cancel && adapter != nullptr) {
            reentry_result = adapter->disconnect(kConnection);
        }
        if (reserved && token == delivery_token) {
            reserved = false;
        }
    }
    void abandon_indication(std::uint64_t token) override {
        if (reenter_abandon && adapter != nullptr) {
            reentry_result = adapter->disconnect(kConnection);
        }
        if (pending && token == delivery_token) {
            pending = false;
        }
    }
    void bind_exchange(
        std::uint64_t token,
        std::uint32_t exchange) override {
        if (reenter_bind && adapter != nullptr) {
            reentry_result = adapter->disconnect(kConnection);
        }
        if (pending && token == delivery_token) {
            exchange_id = exchange;
        }
    }
    void observe_completion(std::uint64_t token) override {
        if (reenter_completion && adapter != nullptr) {
            reentry_result = adapter->disconnect(kConnection);
        }
        if (pending && token == delivery_token) {
            pending = false;
        }
    }

    CompanionGattAuthorizationCallbackAdapter* adapter{nullptr};
    bool reenter_reserve{false};
    bool reenter_submit{false};
    bool reenter_bind{false};
    bool reenter_cancel{false};
    bool reenter_abandon{false};
    bool reenter_completion{false};
    bool fail_submit{false};
    CompanionGattAdapterError reentry_result{CompanionGattAdapterError::none};
    bool reserved{false};
    bool pending{false};
    std::uint64_t delivery_token{0};
    std::uint64_t transport_generation{0};
    std::uint32_t session_nonce{0};
    std::uint32_t exchange_id{0};
    std::uint32_t reserve_calls{0};
    std::uint32_t submit_calls{0};
    std::size_t response_bytes{0};
    std::array<std::uint8_t, kCompanionMaxResponseRecordBytes> response{};
};

struct Harness {
    SnapshotAuthority snapshots;
    ActionAuthority actions;
    CompanionRequestCoordinator coordinator{snapshots, actions};
    IndicationPort port;
    BindingAuthority binding;
    CorrelationIssuer correlations;
    AuthorizationAuthority authorization;
    CompanionGattAuthorizationCallbackAdapter adapter{
        coordinator, port, binding, correlations, authorization};

    Harness() {
        port.adapter = &adapter;
        binding.adapter = &adapter;
        correlations.adapter = &adapter;
        authorization.adapter = &adapter;
    }

    std::uint64_t connect_and_secure(std::uint16_t mtu = 151) {
        EXPECT(adapter.register_handles(kHandles) ==
               CompanionGattAdapterError::none);
        const auto connected = adapter.connect(kConnection);
        EXPECT(connected.connected());
        EXPECT(adapter.refresh_security(kConnection, secure(mtu)) ==
               CompanionGattAdapterError::none);
        return connected.transport_generation;
    }

    void read_and_subscribe(std::uint32_t expected_session = kSession) {
        std::array<std::uint8_t,
                   kCompanionAuthorizationProtocolInfoBytes> info{};
        const auto read = adapter.read_protocol_info(
            kConnection, kHandles.protocol_info_value,
            {info.data(), info.size()});
        EXPECT(read.error == CompanionGattAuthorizationError::none);
        EXPECT(read.encoded_bytes == info.size());
        const auto decoded = decode_companion_authorization_protocol_info(
            {info.data(), info.size()});
        EXPECT(decoded.decoded());
        EXPECT(decoded.info.provisional_session_nonce == expected_session);
        EXPECT(adapter.update_stream_subscription(
                   kConnection, kHandles.stream_value, true) ==
               CompanionGattAdapterError::none);
    }

    CompanionGattAdapterPendingIndication begin_claim(
        const std::array<std::uint8_t, 28>& request = kClaimStart) {
        const auto pending = adapter.service_command(
            kConnection, kHandles.command_value,
            {request.data(), request.size()}, 0);
        EXPECT(pending.pending());
        EXPECT(pending.delivery_token ==
               kCompanionAuthorizationPendingDeliveryToken);
        return adapter.status().pending;
    }
};

void test_registration_security_and_protocol_info() {
    Harness h;
    const auto generation = h.connect_and_secure();
    EXPECT(generation == 1);
    EXPECT(h.binding.calls == 1);
    h.read_and_subscribe();
    const auto status = h.adapter.status();
    EXPECT(status.connected && status.secure_bond);
    EXPECT(status.lifecycle.protocol_info_read);
    EXPECT(status.lifecycle.indication_subscribed);
}

void test_security_and_private_binding_fail_closed() {
    for (std::uint8_t variant = 0; variant < 4; ++variant) {
        Harness h;
        EXPECT(h.adapter.register_handles(kHandles) ==
               CompanionGattAdapterError::none);
        EXPECT(h.adapter.connect(kConnection).connected());
        auto evidence = secure();
        if (variant == 0) evidence.encrypted = false;
        if (variant == 1) evidence.authenticated = false;
        if (variant == 2) evidence.bonded = false;
        if (variant == 3) evidence.key_size = 15;
        EXPECT(h.adapter.refresh_security(kConnection, evidence) ==
               CompanionGattAdapterError::insecure_link);
        EXPECT(!h.adapter.authorize_attribute(
            kConnection, kHandles.protocol_info_value,
            CompanionGattAttributeOperation::read));
    }

    Harness h;
    h.connect_and_secure();
    h.read_and_subscribe();
    h.binding.error = CompanionGattTrustedBindingError::failed;
    EXPECT(h.adapter.refresh_security(kConnection, secure()) ==
           CompanionGattAdapterError::binding_unavailable);
    const auto state = h.adapter.status();
    EXPECT(!state.secure_bond);
    EXPECT(state.lifecycle.phase ==
           CompanionGattAuthorizationPhase::blocked_until_disconnect);
}

void test_exact_attribute_and_subscription_binding() {
    Harness h;
    h.connect_and_secure();
    EXPECT(h.adapter.authorize_attribute(
        kConnection, kHandles.protocol_info_value,
        CompanionGattAttributeOperation::read));
    EXPECT(!h.adapter.authorize_attribute(
        kConnection, kHandles.protocol_info_value,
        CompanionGattAttributeOperation::write));
    EXPECT(!h.adapter.authorize_attribute(
        kConnection, kHandles.command_value,
        CompanionGattAttributeOperation::write));
    h.read_and_subscribe();
    EXPECT(h.adapter.authorize_attribute(
        kConnection, kHandles.command_value,
        CompanionGattAttributeOperation::write));
    EXPECT(h.adapter.update_stream_subscription(
               kConnection, kHandles.stream_value + 1, true) ==
           CompanionGattAdapterError::wrong_attribute);
}

void test_claim_only_before_promotion_and_exact_promotion() {
    Harness h;
    const auto generation = h.connect_and_secure();
    h.read_and_subscribe();
    const auto normal = h.adapter.service_command(
        kConnection, kHandles.command_value,
        {kSnapshotRequest.data(), kSnapshotRequest.size()}, 0);
    EXPECT(!normal.pending());
    EXPECT(normal.error == CompanionGattAuthorizationError::unsupported_request);
    EXPECT(h.authorization.apply_calls == 0);

    const auto pending = h.begin_claim();
    EXPECT(h.adapter.complete_indication(pending, true, 1) ==
           CompanionGattAdapterError::none);
    const auto terminal = h.adapter.resolve_claim(
        kConnection, generation, kSession, 1, secure(), 2);
    EXPECT(terminal.pending());
    EXPECT(h.authorization.apply_calls == 1);
    const auto terminal_tuple = h.adapter.status().pending;
    EXPECT(h.adapter.complete_indication(terminal_tuple, true, 3) ==
           CompanionGattAdapterError::none);
    EXPECT(h.adapter.status().lifecycle.application_authorized);
}

void test_resolve_requires_fresh_security_and_binding() {
    Harness h;
    const auto generation = h.connect_and_secure();
    h.read_and_subscribe();
    const auto pending = h.begin_claim();
    EXPECT(h.adapter.complete_indication(pending, true, 1) ==
           CompanionGattAdapterError::none);
    auto insecure = secure();
    insecure.encrypted = false;
    const auto result = h.adapter.resolve_claim(
        kConnection, generation, kSession, 1, insecure, 2);
    EXPECT(!result.pending());
    EXPECT(h.authorization.apply_calls == 0);
    EXPECT(h.adapter.status().lifecycle.phase ==
           CompanionGattAuthorizationPhase::blocked_until_disconnect);
}

void test_completion_requires_exact_full_tuple() {
    Harness h;
    h.connect_and_secure();
    h.read_and_subscribe();
    const auto exact = h.begin_claim();
    auto wrong = exact;
    ++wrong.transport_generation;
    EXPECT(h.adapter.complete_indication(wrong, true, 1) ==
           CompanionGattAdapterError::indication_mismatch);
    wrong = exact;
    ++wrong.delivery_token;
    EXPECT(h.adapter.complete_indication(wrong, true, 1) ==
           CompanionGattAdapterError::indication_mismatch);
    EXPECT(h.adapter.status().pending.valid);
    EXPECT(h.adapter.complete_indication(exact, true, 1) ==
           CompanionGattAdapterError::none);
}

void test_disconnect_reuse_rejects_stale_completion() {
    Harness h;
    const auto first_generation = h.connect_and_secure();
    h.read_and_subscribe();
    const auto stale = h.begin_claim();
    EXPECT(h.adapter.disconnect(kConnection) == CompanionGattAdapterError::none);
    const auto second = h.adapter.connect(kConnection);
    EXPECT(second.connected());
    EXPECT(second.transport_generation > first_generation);
    h.binding.session = 2;
    EXPECT(h.adapter.refresh_security(kConnection, secure()) ==
           CompanionGattAdapterError::none);
    h.read_and_subscribe(2);
    auto second_claim = kClaimStart;
    second_claim[8] = 2;
    const auto current = h.begin_claim(second_claim);
    EXPECT(current.valid);
    EXPECT(current.transport_generation == second.transport_generation);
    EXPECT(h.adapter.complete_indication(stale, true, 1) ==
           CompanionGattAdapterError::indication_mismatch);
    EXPECT(h.adapter.status().pending.delivery_token ==
           current.delivery_token);
    EXPECT(h.adapter.status().pending.transport_generation ==
           current.transport_generation);
}

void test_unsubscribe_and_timeout_contain() {
    {
        Harness h;
        h.connect_and_secure();
        h.read_and_subscribe();
        EXPECT(h.adapter.update_stream_subscription(
                   kConnection, kHandles.stream_value, false) ==
               CompanionGattAdapterError::lifecycle_rejected);
        EXPECT(h.adapter.status().lifecycle.phase ==
               CompanionGattAuthorizationPhase::blocked_until_disconnect);
    }
    {
        Harness h;
        h.connect_and_secure();
        h.read_and_subscribe();
        const auto pending = h.begin_claim();
        EXPECT(h.adapter.service_timeout(pending, 5000) ==
               CompanionGattAdapterError::lifecycle_rejected);
        EXPECT(!h.adapter.status().pending.valid);
        EXPECT(h.adapter.status().lifecycle.phase ==
               CompanionGattAuthorizationPhase::blocked_until_disconnect);
    }
}

void test_reentry_is_rejected_across_injected_callbacks() {
    {
        Harness h;
        EXPECT(h.adapter.register_handles(kHandles) ==
               CompanionGattAdapterError::none);
        EXPECT(h.adapter.connect(kConnection).connected());
        h.binding.reenter = true;
        EXPECT(h.adapter.refresh_security(kConnection, secure()) ==
               CompanionGattAdapterError::none);
        EXPECT(h.binding.reentry_result ==
               CompanionGattAdapterError::lifecycle_rejected);
        EXPECT(h.adapter.status().connected);
    }
    {
        Harness h;
        h.connect_and_secure();
        h.read_and_subscribe();
        h.port.reenter_reserve = true;
        const auto pending = h.begin_claim();
        EXPECT(pending.valid);
        EXPECT(h.port.reentry_result ==
               CompanionGattAdapterError::lifecycle_rejected);
    }
    {
        Harness h;
        h.connect_and_secure();
        h.read_and_subscribe();
        h.correlations.reenter = true;
        const auto pending = h.begin_claim();
        EXPECT(pending.valid);
        EXPECT(h.correlations.reentry_result ==
               CompanionGattAdapterError::lifecycle_rejected);
    }
    {
        Harness h;
        h.connect_and_secure();
        h.read_and_subscribe();
        h.port.reenter_submit = true;
        const auto pending = h.begin_claim();
        EXPECT(pending.valid);
        EXPECT(h.port.reentry_result ==
               CompanionGattAdapterError::lifecycle_rejected);
    }
    {
        Harness h;
        const auto generation = h.connect_and_secure();
        h.read_and_subscribe();
        const auto pending = h.begin_claim();
        EXPECT(h.adapter.complete_indication(pending, true, 1) ==
               CompanionGattAdapterError::none);
        h.authorization.decision_error =
            CompanionGattAuthorizationAuthorityError::not_ready;
        h.port.reenter_abandon = true;
        h.port.reenter_cancel = true;
        const auto failed = h.adapter.resolve_claim(
            kConnection, generation, kSession, 1, secure(), 2);
        EXPECT(!failed.pending());
        EXPECT(h.port.reentry_result ==
               CompanionGattAdapterError::lifecycle_rejected);
    }
    {
        Harness h;
        h.connect_and_secure();
        h.read_and_subscribe();
        const auto pending = h.begin_claim();
        h.port.reenter_completion = true;
        EXPECT(h.adapter.complete_indication(pending, true, 1) ==
               CompanionGattAdapterError::none);
        EXPECT(h.port.reentry_result ==
               CompanionGattAdapterError::lifecycle_rejected);
    }
    {
        Harness h;
        h.connect_and_secure();
        h.read_and_subscribe();
        const auto pending = h.begin_claim();
        h.port.reenter_abandon = true;
        EXPECT(h.adapter.service_timeout(pending, 5000) ==
               CompanionGattAdapterError::lifecycle_rejected);
        EXPECT(h.port.reentry_result ==
               CompanionGattAdapterError::lifecycle_rejected);
    }
    {
        Harness h;
        h.connect_and_secure();
        h.read_and_subscribe();
        h.port.reenter_bind = true;
        const auto pending = h.begin_claim();
        EXPECT(pending.valid);
        EXPECT(h.port.reentry_result ==
               CompanionGattAdapterError::lifecycle_rejected);
    }
    {
        Harness h;
        const auto generation = h.connect_and_secure();
        h.read_and_subscribe();
        const auto pending = h.begin_claim();
        EXPECT(h.adapter.complete_indication(pending, true, 1) ==
               CompanionGattAdapterError::none);
        h.authorization.reenter = true;
        EXPECT(h.adapter.resolve_claim(
                   kConnection, generation, kSession, 1, secure(), 2)
                   .pending());
        EXPECT(h.authorization.reentry_result ==
               CompanionGattAdapterError::lifecycle_rejected);
    }
    {
        Harness h;
        const auto generation = h.connect_and_secure();
        h.read_and_subscribe();
        const auto pending = h.begin_claim();
        EXPECT(h.adapter.complete_indication(pending, true, 1) ==
               CompanionGattAdapterError::none);
        const auto terminal = h.adapter.resolve_claim(
            kConnection, generation, kSession, 1, secure(), 2);
        EXPECT(terminal.pending());
        h.authorization.reenter_release = true;
        EXPECT(h.adapter.complete_indication(
                   h.adapter.status().pending, false, 3) ==
               CompanionGattAdapterError::lifecycle_rejected);
        EXPECT(h.authorization.reentry_result ==
               CompanionGattAdapterError::lifecycle_rejected);
    }
}

void test_wrong_connection_generation_and_malformed_inputs() {
    Harness h;
    const auto generation = h.connect_and_secure();
    h.read_and_subscribe();
    EXPECT(h.adapter.refresh_security(kConnection + 1, secure()) ==
           CompanionGattAdapterError::wrong_connection);
    const auto rejected = h.adapter.resolve_claim(
        kConnection, generation + 1, kSession, 1, secure(), 0);
    EXPECT(!rejected.pending());
    EXPECT(rejected.error ==
           CompanionGattAuthorizationError::wrong_transport_generation);
    const auto malformed = h.adapter.service_command(
        kConnection, kHandles.command_value,
        {kClaimStart.data(), kClaimStart.size() - 1}, 0);
    EXPECT(!malformed.pending());
    EXPECT(malformed.error ==
           CompanionGattAuthorizationError::malformed_request);
}

}  // namespace

int main() {
    test_registration_security_and_protocol_info();
    test_security_and_private_binding_fail_closed();
    test_exact_attribute_and_subscription_binding();
    test_claim_only_before_promotion_and_exact_promotion();
    test_resolve_requires_fresh_security_and_binding();
    test_completion_requires_exact_full_tuple();
    test_disconnect_reuse_rejects_stale_completion();
    test_unsubscribe_and_timeout_contain();
    test_reentry_is_rejected_across_injected_callbacks();
    test_wrong_connection_generation_and_malformed_inputs();
    std::cout << "PASS: 10 companion GATT authorization adapter groups\n";
    return 0;
}
