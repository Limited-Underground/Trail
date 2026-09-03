#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

#include "opentrail/companion_gatt_authorization.hpp"

namespace {

using namespace opentrail::companion;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

constexpr CompanionGattAuthorizationHandles kHandles{10, 11, 12, 13};
constexpr std::uint16_t kConnection = 7;
constexpr std::uint64_t kGeneration = 9;
constexpr std::uint32_t kSession = 0x11223344U;
constexpr std::uint32_t kExchange = 0x55667788U;
constexpr std::uint64_t kControllerBinding = 0x8877665544332211ULL;

struct EncodedRequest {
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> bytes{};
    std::size_t size{0};
};

CompanionAuthorizationCorrelation fixed_correlation() {
    CompanionAuthorizationCorrelation value{};
    for (std::size_t index = 0; index < value.bytes.size(); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(0xA0U + index);
    }
    return value;
}

EncodedRequest envelope(CompanionFrameKind kind,
                        std::uint32_t session,
                        std::uint32_t exchange,
                        const std::uint8_t* payload,
                        std::size_t payload_bytes) {
    CompanionFragment fragment{};
    fragment.kind = kind;
    fragment.session_nonce = session;
    fragment.exchange_id = exchange;
    fragment.payload_bytes = static_cast<std::uint16_t>(payload_bytes);
    for (std::size_t index = 0; index < payload_bytes; ++index) {
        fragment.payload[index] = payload[index];
    }
    EncodedRequest request{};
    const auto encoded = encode_companion_fragment(
        fragment, {request.bytes.data(), request.bytes.size()});
    EXPECT(encoded.encoded());
    request.size = encoded.encoded_bytes;
    return request;
}

EncodedRequest claim_start(
    CompanionAuthorizationPurpose purpose =
        CompanionAuthorizationPurpose::authorize_controller,
    std::uint32_t session = kSession,
    std::uint32_t exchange = kExchange) {
    std::array<std::uint8_t, kCompanionAuthorizationClaimStartBytes> payload{};
    EXPECT(encode_companion_authorization_claim_start(
               {purpose}, {payload.data(), payload.size()}).encoded());
    return envelope(CompanionFrameKind::authorization_claim_start,
                    session, exchange, payload.data(), payload.size());
}

EncodedRequest snapshot_request(std::uint32_t session,
                                std::uint32_t exchange) {
    std::array<std::uint8_t, kCompanionSnapshotRequestBytes> payload{};
    EXPECT(encode_companion_snapshot_request(
               {}, {payload.data(), payload.size()}).encoded());
    return envelope(CompanionFrameKind::snapshot_request, session, exchange,
                    payload.data(), payload.size());
}

CompanionGattAuthorizationConnectionEvidence evidence(
    std::uint16_t mtu = kCompanionMinimumAttMtu,
    bool encrypted = true,
    bool authenticated = true) {
    CompanionGattAuthorizationConnectionEvidence value{};
    value.att_mtu = mtu;
    value.controller_claim = {
        {0x0102030405060708ULL, 0x1112131415161718ULL},
        0x2122232425262728ULL,
        1,
        kControllerBinding,
        encrypted,
        authenticated,
    };
    return value;
}

class FakeSink final : public CompanionGattIndicationSink {
public:
    enum class Callback : std::uint8_t {
        none = 0,
        reserve,
        submit,
        cancel,
        abandon,
    };

    CompanionGattSinkError reserve_result{CompanionGattSinkError::none};
    CompanionGattSinkError submit_result{CompanionGattSinkError::none};
    std::uint32_t reserve_calls{0};
    std::uint32_t submit_calls{0};
    std::uint32_t cancel_calls{0};
    std::uint32_t abandon_calls{0};
    bool reserved{false};
    bool pending{false};
    std::uint16_t connection{0};
    std::uint32_t session{0};
    std::uint16_t stream{0};
    std::uint64_t token{0};
    std::size_t capacity{0};
    std::size_t response_bytes{0};
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> response{};
    CompanionGattAuthorizationLifecycle* lifecycle{nullptr};
    Callback callback{Callback::none};
    CompanionGattAuthorizationError callback_result{
        CompanionGattAuthorizationError::none};

    void reenter(Callback point) {
        if (lifecycle != nullptr && callback == point) {
            callback = Callback::none;
            callback_result = lifecycle->disconnect(connection, kGeneration);
        }
    }

    CompanionGattSinkError reserve(
        std::uint16_t connection_handle,
        std::uint32_t session_nonce,
        std::uint16_t stream_value_handle,
        std::uint64_t delivery_token,
        std::size_t max_response_bytes) override {
        ++reserve_calls;
        connection = connection_handle;
        session = session_nonce;
        stream = stream_value_handle;
        token = delivery_token;
        capacity = max_response_bytes;
        reenter(Callback::reserve);
        if (reserve_result == CompanionGattSinkError::none) {
            reserved = true;
        }
        return reserve_result;
    }

    CompanionGattSinkError submit_reserved(
        std::uint16_t connection_handle,
        std::uint32_t session_nonce,
        std::uint16_t stream_value_handle,
        std::uint64_t delivery_token,
        opentrail::radio::ByteView bytes) override {
        ++submit_calls;
        EXPECT(reserved);
        EXPECT(connection_handle == connection);
        EXPECT(session_nonce == session);
        EXPECT(stream_value_handle == stream);
        EXPECT(delivery_token == token);
        reenter(Callback::submit);
        reserved = false;
        if (submit_result != CompanionGattSinkError::none) {
            return submit_result;
        }
        EXPECT(bytes.data != nullptr);
        EXPECT(bytes.size <= response.size());
        response.fill(0);
        response_bytes = bytes.size;
        for (std::size_t index = 0; index < bytes.size; ++index) {
            response[index] = bytes.data[index];
        }
        pending = true;
        return CompanionGattSinkError::none;
    }

    void cancel_reservation(std::uint64_t delivery_token) override {
        ++cancel_calls;
        EXPECT(reserved);
        EXPECT(delivery_token == token);
        reenter(Callback::cancel);
        reserved = false;
    }

    void abandon_indication(std::uint64_t delivery_token) override {
        ++abandon_calls;
        EXPECT(delivery_token == token);
        reenter(Callback::abandon);
        pending = false;
    }

    void confirm_transport() {
        EXPECT(pending);
        pending = false;
    }
};

class FakeCorrelationIssuer final
    : public CompanionGattAuthorizationCorrelationIssuer {
public:
    CompanionGattAuthorizationCorrelationError next_error{
        CompanionGattAuthorizationCorrelationError::none};
    std::uint32_t calls{0};
    CompanionGattAuthorizationCorrelationContext last{};
    FakeSink* sink{nullptr};
    bool issued_without_reservation{false};
    CompanionGattAuthorizationLifecycle* lifecycle{nullptr};
    bool reenter{false};
    CompanionGattAuthorizationError reentrant_result{
        CompanionGattAuthorizationError::none};

    CompanionGattAuthorizationCorrelationResult issue(
        const CompanionGattAuthorizationCorrelationContext& context) override {
        ++calls;
        last = context;
        if (sink == nullptr || !sink->reserved) {
            issued_without_reservation = true;
        }
        if (reenter && lifecycle != nullptr) {
            reenter = false;
            reentrant_result = lifecycle->disconnect(kConnection, kGeneration);
        }
        return {next_error, fixed_correlation()};
    }
};

class FakeAuthorizationAuthority final
    : public CompanionGattAuthorizationAuthority {
public:
    CompanionGattAuthorizationDecision next{
        CompanionGattAuthorizationAuthorityError::none,
        CompanionAuthorizationClaimOutcome::accepted,
        CompanionAuthorizationDenyReason::none,
        kControllerBinding};
    CompanionGattAuthorizationAuthorityError release_result{
        CompanionGattAuthorizationAuthorityError::none};
    std::uint32_t apply_calls{0};
    std::uint32_t release_calls{0};
    FakeSink* sink{nullptr};
    bool applied_without_reservation{false};
    CompanionGattAuthorizationLifecycle* lifecycle{nullptr};
    bool reenter_apply{false};
    bool reenter_release{false};
    CompanionGattAuthorizationError reentrant_result{
        CompanionGattAuthorizationError::none};

    CompanionGattAuthorizationDecision apply_claim(
        CompanionAuthorizationPurpose,
        const CompanionControllerClaim&,
        std::uint64_t) override {
        ++apply_calls;
        if (sink == nullptr || !sink->reserved) {
            applied_without_reservation = true;
        }
        if (reenter_apply && lifecycle != nullptr) {
            reenter_apply = false;
            reentrant_result = lifecycle->disconnect(kConnection, kGeneration);
        }
        return next;
    }

    CompanionGattAuthorizationAuthorityError release_connection(
        std::uint64_t binding) override {
        ++release_calls;
        EXPECT(binding == kControllerBinding);
        if (reenter_release && lifecycle != nullptr) {
            reenter_release = false;
            reentrant_result = lifecycle->disconnect(kConnection, kGeneration);
        }
        return release_result;
    }
};

class FakeSnapshotAuthority final : public CompanionSnapshotAuthority {
public:
    std::uint32_t calls{0};
    CompanionSnapshotAuthorityResult read_snapshot() override {
        ++calls;
        return {CompanionAuthorityError::none,
                {1, CompanionRadioState::ready,
                 CompanionGnssState::current,
                 CompanionPowerState::normal,
                 CompanionPositionSharingState::stopped, 0, 0}};
    }
};

class FakeActionAuthority final : public CompanionActionAuthority {
public:
    CompanionActionAuthorityResult prepare_action(
        const CompanionActionRequest&) override {
        return {CompanionAuthorityError::none,
                CompanionActionDisposition::admitted,
                CompanionActionRejectReason::none, 1};
    }
    CompanionAuthorityError commit_action(
        const CompanionActionRequest&,
        const CompanionActionAuthorityResult&) override {
        return CompanionAuthorityError::none;
    }
};

struct Harness {
    FakeSink sink{};
    FakeCorrelationIssuer issuer{};
    FakeAuthorizationAuthority authorization{};
    FakeSnapshotAuthority snapshots{};
    FakeActionAuthority actions{};
    CompanionRequestCoordinator coordinator{snapshots, actions};
    CompanionGattAuthorizationLifecycle lifecycle;

    explicit Harness(CompanionGattAuthorizationPolicy policy = {})
        : lifecycle(coordinator, sink, issuer, authorization, policy) {
        sink.lifecycle = &lifecycle;
        issuer.sink = &sink;
        issuer.lifecycle = &lifecycle;
        authorization.sink = &sink;
        authorization.lifecycle = &lifecycle;
    }

    void register_and_connect(std::uint16_t mtu = kCompanionMinimumAttMtu,
                              std::uint64_t generation = kGeneration,
                              std::uint32_t session = kSession) {
        EXPECT(lifecycle.register_handles(kHandles) ==
               CompanionGattAuthorizationError::none);
        EXPECT(lifecycle.connect(kConnection, generation) ==
               CompanionGattAuthorizationError::none);
        EXPECT(lifecycle.update_connection_evidence(
                   kConnection, generation, evidence(mtu)) ==
               CompanionGattAuthorizationError::none);
        EXPECT(lifecycle.open_provisional_session(
                   kConnection, generation, session) ==
               CompanionGattAuthorizationError::none);
    }

    void open_claim_path(std::uint16_t mtu = kCompanionMinimumAttMtu) {
        register_and_connect(mtu);
        std::array<std::uint8_t,
                   kCompanionAuthorizationProtocolInfoBytes> info{};
        EXPECT(lifecycle.read_protocol_info(
                   kConnection, kGeneration, kHandles.protocol_info_value,
                   {info.data(), info.size()}).error ==
               CompanionGattAuthorizationError::none);
        EXPECT(lifecycle.update_indication_subscription(
                   kConnection, kGeneration, kHandles.stream_cccd, true) ==
               CompanionGattAuthorizationError::none);
    }

    CompanionGattAuthorizationRequestResult start_claim(
        CompanionAuthorizationPurpose purpose =
            CompanionAuthorizationPurpose::authorize_controller,
        std::uint64_t now = 100) {
        const auto request = claim_start(purpose);
        return lifecycle.service_command(
            kConnection, kGeneration, kHandles.command_value,
            {request.bytes.data(), request.size}, now);
    }

    void confirm_pending(std::uint64_t now = 101) {
        sink.confirm_transport();
        EXPECT(lifecycle.complete_indication(
                   kConnection, kGeneration, kSession,
                   kHandles.stream_value,
                   kCompanionAuthorizationPendingDeliveryToken,
                   true, now) == CompanionGattAuthorizationError::none);
    }
};

void test_protocol_info_exact_vector_and_strict_decode() {
    static_assert(kCompanionAuthorizationProtocolInfoBytes == 20);
    static_assert(kCompanionAuthorizationMaxResponseBytes == 48);
    static_assert(kCompanionAuthorizationMinimumAttMtu == 51);
    const CompanionAuthorizationProtocolInfo info{
        CompanionDeviceRole::screenless_client, 0x3F, 128, 151, 16, 1,
        kSession};
    std::array<std::uint8_t, 20> encoded{};
    EXPECT(encode_companion_authorization_protocol_info(
               info, {encoded.data(), encoded.size()}).encoded());
    const std::array<std::uint8_t, 20> expected{
        0x4F, 0x54, 0x42, 0x30, 0x00, 0x01, 0x01, 0x3F,
        0x80, 0x00, 0x97, 0x00, 0x10, 0x01, 0x44, 0x33,
        0x22, 0x11, 0x00, 0x00};
    EXPECT(encoded == expected);
    const auto decoded = decode_companion_authorization_protocol_info(
        {encoded.data(), encoded.size()});
    EXPECT(decoded.decoded());
    EXPECT(decoded.info.provisional_session_nonce == kSession);
    encoded[7] = 0x0F;
    EXPECT(decode_companion_authorization_protocol_info(
               {encoded.data(), encoded.size()}).error ==
           CompanionAuthorizationProtocolInfoError::invalid_capability);
    encoded = expected;
    encoded[18] = 1;
    EXPECT(decode_companion_authorization_protocol_info(
               {encoded.data(), encoded.size()}).error ==
           CompanionAuthorizationProtocolInfoError::reserved_bits_set);

    auto too_small = info;
    too_small.max_fragment_payload_bytes =
        static_cast<std::uint16_t>(
            kCompanionAuthorizationMinimumFragmentPayloadBytes - 1U);
    std::array<std::uint8_t, 20> output{};
    EXPECT(encode_companion_authorization_protocol_info(
               too_small, {output.data(), output.size()}).error ==
           CompanionAuthorizationProtocolInfoError::invalid_limit);
    auto exact_minimum = info;
    exact_minimum.max_fragment_payload_bytes =
        kCompanionAuthorizationMinimumFragmentPayloadBytes;
    EXPECT(encode_companion_authorization_protocol_info(
               exact_minimum, {output.data(), output.size()}).encoded());
    EXPECT(decode_companion_authorization_protocol_info(
               {output.data(), output.size()}).decoded());
    output[8] = static_cast<std::uint8_t>(
        kCompanionAuthorizationMinimumFragmentPayloadBytes - 1U);
    output[9] = 0;
    EXPECT(decode_companion_authorization_protocol_info(
               {output.data(), output.size()}).error ==
           CompanionAuthorizationProtocolInfoError::invalid_limit);
}

void test_secure_protocol_read_and_exact_handle_gate() {
    Harness harness;
    EXPECT(harness.lifecycle.register_handles({10, 11, 12, 12}) ==
           CompanionGattAuthorizationError::invalid_argument);
    EXPECT(harness.lifecycle.register_handles(kHandles) ==
           CompanionGattAuthorizationError::none);
    EXPECT(harness.lifecycle.connect(kConnection, kGeneration) ==
           CompanionGattAuthorizationError::none);
    auto insecure = evidence(23, false, true);
    EXPECT(harness.lifecycle.update_connection_evidence(
               kConnection, kGeneration, insecure) ==
           CompanionGattAuthorizationError::none);
    EXPECT(harness.lifecycle.open_provisional_session(
               kConnection, kGeneration, kSession) ==
           CompanionGattAuthorizationError::insecure_link);
    EXPECT(harness.lifecycle.update_connection_evidence(
               kConnection, kGeneration, evidence(23)) ==
           CompanionGattAuthorizationError::none);
    EXPECT(harness.lifecycle.open_provisional_session(
               kConnection, kGeneration, kSession) ==
           CompanionGattAuthorizationError::none);
    EXPECT(harness.lifecycle.update_connection_evidence(
               kConnection, kGeneration, evidence(23)) ==
           CompanionGattAuthorizationError::none);
    std::array<std::uint8_t, 20> info{};
    EXPECT(harness.lifecycle.read_protocol_info(
               kConnection, kGeneration, 99,
               {info.data(), info.size()}).error ==
           CompanionGattAuthorizationError::wrong_attribute);
    EXPECT(harness.lifecycle.read_protocol_info(
               kConnection, kGeneration, kHandles.protocol_info_value,
               {info.data(), info.size()}).error ==
           CompanionGattAuthorizationError::none);
    EXPECT(harness.lifecycle.update_connection_evidence(
               kConnection, kGeneration,
               evidence(kCompanionAuthorizationMinimumAttMtu)) ==
           CompanionGattAuthorizationError::none);
    EXPECT(harness.lifecycle.update_connection_evidence(
               kConnection, kGeneration, evidence(23)) ==
           CompanionGattAuthorizationError::insecure_link);
}

void test_mtu_50_rejects_and_51_accepts_before_mutation() {
    Harness too_small;
    too_small.register_and_connect(50);
    std::array<std::uint8_t, 20> info{};
    EXPECT(too_small.lifecycle.read_protocol_info(
               kConnection, kGeneration, kHandles.protocol_info_value,
               {info.data(), info.size()}).error ==
           CompanionGattAuthorizationError::none);
    EXPECT(too_small.lifecycle.update_indication_subscription(
               kConnection, kGeneration, kHandles.stream_cccd, true) ==
           CompanionGattAuthorizationError::none);
    EXPECT(too_small.start_claim().error ==
           CompanionGattAuthorizationError::mtu_too_small);
    EXPECT(too_small.issuer.calls == 0);
    EXPECT(too_small.authorization.apply_calls == 0);

    Harness exact;
    exact.open_claim_path(51);
    EXPECT(exact.start_claim().pending());
    EXPECT(exact.sink.capacity == 48);
}

void test_pending_exact_vector_and_no_early_authority() {
    Harness harness;
    harness.open_claim_path();
    const auto result = harness.start_claim();
    EXPECT(result.pending());
    EXPECT(result.delivery_token ==
           kCompanionAuthorizationPendingDeliveryToken);
    EXPECT(harness.sink.response_bytes == 44);
    const std::array<std::uint8_t, 44> expected{
        0x4F, 0x54, 0x43, 0x30, 0x00, 0x00, 0x84, 0x00,
        0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55,
        0x00, 0x01, 0x18, 0x00, 0x4F, 0x54, 0x50, 0x30,
        0x00, 0x00, 0x01, 0x01, 0xA0, 0xA1, 0xA2, 0xA3,
        0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB,
        0xAC, 0xAD, 0xAE, 0xAF};
    EXPECT(std::equal(expected.begin(), expected.end(),
                      harness.sink.response.begin()));
    EXPECT(harness.issuer.calls == 1);
    EXPECT(!harness.issuer.issued_without_reservation);
    EXPECT(harness.authorization.apply_calls == 0);
    harness.confirm_pending();
    EXPECT(harness.lifecycle.status().phase ==
           CompanionGattAuthorizationPhase::awaiting_authority);
    EXPECT(harness.authorization.apply_calls == 0);
}

void test_not_ready_then_exact_accepted_terminal_and_promotion() {
    Harness harness;
    harness.open_claim_path();
    EXPECT(harness.start_claim().pending());
    harness.confirm_pending();
    harness.authorization.next.error =
        CompanionGattAuthorizationAuthorityError::not_ready;
    const auto waiting = harness.lifecycle.resolve_claim(
        kConnection, kGeneration, kSession, kExchange, 200);
    EXPECT(waiting.error == CompanionGattAuthorizationError::authority_pending);
    EXPECT(harness.sink.cancel_calls == 1);
    // A non-none authority error is a trusted no-mutation/no-lease contract;
    // outcome and binding fields are deliberately ignored on that path.
    EXPECT(harness.authorization.release_calls == 0);
    EXPECT(harness.lifecycle.status().phase ==
           CompanionGattAuthorizationPhase::awaiting_authority);
    harness.authorization.next = {
        CompanionGattAuthorizationAuthorityError::none,
        CompanionAuthorizationClaimOutcome::accepted,
        CompanionAuthorizationDenyReason::none,
        kControllerBinding};
    const auto terminal = harness.lifecycle.resolve_claim(
        kConnection, kGeneration, kSession, kExchange, 201);
    EXPECT(terminal.pending());
    EXPECT(terminal.delivery_token ==
           kCompanionAuthorizationTerminalDeliveryToken);
    EXPECT(harness.sink.response_bytes == 48);
    const auto decoded = decode_companion_fragment(
        {harness.sink.response.data(), harness.sink.response_bytes});
    EXPECT(decoded.decoded());
    EXPECT(decoded.fragment.session_nonce == kSession);
    EXPECT(decoded.fragment.exchange_id == kExchange);
    const auto result = decode_companion_authorization_claim_result(
        {decoded.fragment.payload.data(), decoded.fragment.payload_bytes});
    EXPECT(result.decoded());
    EXPECT(result.value.purpose ==
           CompanionAuthorizationPurpose::authorize_controller);
    EXPECT(result.value.outcome ==
           CompanionAuthorizationClaimOutcome::accepted);
    EXPECT(result.value.correlation == fixed_correlation());
    EXPECT(!harness.authorization.applied_without_reservation);
    EXPECT(!harness.lifecycle.status().application_authorized);
    harness.sink.confirm_transport();
    EXPECT(harness.lifecycle.complete_indication(
               kConnection, kGeneration, kSession, kHandles.stream_value,
               terminal.delivery_token, true, 202) ==
           CompanionGattAuthorizationError::none);
    EXPECT(harness.lifecycle.status().application_authorized);
    EXPECT(harness.lifecycle.status().normal_session_active);
}

void test_authority_failure_is_local_unknown_and_tombstones() {
    Harness harness;
    harness.open_claim_path();
    EXPECT(harness.start_claim().pending());
    harness.confirm_pending();
    harness.authorization.next = {
        CompanionGattAuthorizationAuthorityError::failed,
        CompanionAuthorizationClaimOutcome::accepted,
        CompanionAuthorizationDenyReason::none,
        kControllerBinding};
    EXPECT(harness.lifecycle.resolve_claim(
               kConnection, kGeneration, kSession, kExchange, 102).error ==
           CompanionGattAuthorizationError::authority_unavailable);
    EXPECT(harness.authorization.apply_calls == 1);
    EXPECT(harness.authorization.release_calls == 0);
    EXPECT(harness.sink.submit_calls == 1);
    EXPECT(harness.lifecycle.status().phase ==
           CompanionGattAuthorizationPhase::blocked_until_disconnect);
    EXPECT(!harness.lifecycle.status().application_authorized);
}

void test_mtu_51_promotes_but_normal_waits_for_151() {
    Harness harness;
    harness.open_claim_path(51);
    EXPECT(harness.start_claim().pending());
    harness.confirm_pending();
    const auto terminal = harness.lifecycle.resolve_claim(
        kConnection, kGeneration, kSession, kExchange, 102);
    EXPECT(terminal.pending());
    harness.sink.confirm_transport();
    EXPECT(harness.lifecycle.complete_indication(
               kConnection, kGeneration, kSession, kHandles.stream_value,
               terminal.delivery_token, true, 103) ==
           CompanionGattAuthorizationError::none);
    EXPECT(harness.lifecycle.status().application_authorized);
    EXPECT(!harness.lifecycle.status().normal_session_active);
    const auto request = snapshot_request(kSession, kExchange + 1U);
    EXPECT(harness.lifecycle.service_command(
               kConnection, kGeneration, kHandles.command_value,
               {request.bytes.data(), request.size}, 104).error ==
           CompanionGattAuthorizationError::normal_session_not_ready);
    EXPECT(harness.lifecycle.update_connection_evidence(
               kConnection, kGeneration, evidence(151)) ==
           CompanionGattAuthorizationError::none);
    EXPECT(harness.lifecycle.status().normal_session_active);
    const auto normal = harness.lifecycle.service_command(
        kConnection, kGeneration, kHandles.command_value,
        {request.bytes.data(), request.size}, 105);
    EXPECT(normal.pending());
    EXPECT(harness.lifecycle.status().response_pending);
    EXPECT(harness.lifecycle.status().exchange_id == kExchange + 1U);
    harness.sink.confirm_transport();
    EXPECT(harness.lifecycle.complete_indication(
               kConnection, kGeneration, kSession, kHandles.stream_value,
               normal.delivery_token, true, 106) ==
           CompanionGattAuthorizationError::none);
    EXPECT(!harness.lifecycle.status().response_pending);
    EXPECT(harness.lifecycle.status().exchange_id == 0);
}

void test_promoted_normal_timeout_reports_and_contains_exact_pending() {
    Harness harness({5, 30});
    harness.open_claim_path();
    EXPECT(harness.start_claim().pending());
    harness.confirm_pending();
    const auto terminal = harness.lifecycle.resolve_claim(
        kConnection, kGeneration, kSession, kExchange, 102);
    EXPECT(terminal.pending());
    harness.sink.confirm_transport();
    EXPECT(harness.lifecycle.complete_indication(
               kConnection, kGeneration, kSession, kHandles.stream_value,
               terminal.delivery_token, true, 103) ==
           CompanionGattAuthorizationError::none);
    const auto request = snapshot_request(kSession, kExchange + 1U);
    const auto normal = harness.lifecycle.service_command(
        kConnection, kGeneration, kHandles.command_value,
        {request.bytes.data(), request.size}, 104);
    EXPECT(normal.pending());
    EXPECT(harness.lifecycle.status().response_pending);
    EXPECT(harness.lifecycle.service_timeout(
               kConnection, kGeneration, kSession, kExchange + 1U,
               normal.delivery_token, 110) ==
           CompanionGattAuthorizationError::indication_timeout);
    EXPECT(harness.lifecycle.status().phase ==
           CompanionGattAuthorizationPhase::blocked_until_disconnect);
    EXPECT(!harness.lifecycle.status().application_authorized);
}

void test_pre_auth_normal_commands_and_wrong_context_never_mutate() {
    Harness harness;
    harness.open_claim_path();
    const auto normal = snapshot_request(kSession, 1);
    EXPECT(harness.lifecycle.service_command(
               kConnection, kGeneration, kHandles.command_value,
               {normal.bytes.data(), normal.size}, 1).error ==
           CompanionGattAuthorizationError::unsupported_request);
    const auto wrong_session = claim_start(
        CompanionAuthorizationPurpose::authorize_controller,
        kSession + 1U, kExchange);
    EXPECT(harness.lifecycle.service_command(
               kConnection, kGeneration, kHandles.command_value,
               {wrong_session.bytes.data(), wrong_session.size}, 2).error ==
           CompanionGattAuthorizationError::wrong_session);
    EXPECT(harness.issuer.calls == 0);
    EXPECT(harness.authorization.apply_calls == 0);
}

void test_security_loss_and_unsubscribe_tombstone() {
    {
        Harness harness;
        harness.open_claim_path();
        CompanionGattAuthorizationConnectionEvidence lost{};
        EXPECT(harness.lifecycle.update_connection_evidence(
                   kConnection, kGeneration, lost) ==
               CompanionGattAuthorizationError::insecure_link);
        EXPECT(harness.lifecycle.status().phase ==
               CompanionGattAuthorizationPhase::blocked_until_disconnect);
    }
    {
        Harness harness;
        harness.open_claim_path();
        EXPECT(harness.lifecycle.update_indication_subscription(
                   kConnection, kGeneration, kHandles.stream_cccd, false) ==
               CompanionGattAuthorizationError::subscription_required);
        EXPECT(harness.lifecycle.status().phase ==
               CompanionGattAuthorizationPhase::blocked_until_disconnect);
    }
}

void test_submit_failures_and_negative_completion_never_promote() {
    {
        Harness harness;
        harness.open_claim_path();
        harness.sink.submit_result = CompanionGattSinkError::failed;
        EXPECT(harness.start_claim().error ==
               CompanionGattAuthorizationError::indication_submit_failed);
        EXPECT(harness.authorization.apply_calls == 0);
        EXPECT(harness.lifecycle.status().phase ==
               CompanionGattAuthorizationPhase::blocked_until_disconnect);
    }
    {
        Harness harness;
        harness.open_claim_path();
        EXPECT(harness.start_claim().pending());
        harness.confirm_pending();
        harness.sink.submit_result = CompanionGattSinkError::failed;
        EXPECT(harness.lifecycle.resolve_claim(
                   kConnection, kGeneration, kSession, kExchange, 102).error ==
               CompanionGattAuthorizationError::indication_submit_failed);
        EXPECT(harness.authorization.apply_calls == 1);
        EXPECT(harness.authorization.release_calls == 1);
        EXPECT(!harness.lifecycle.status().application_authorized);
    }
    {
        Harness harness;
        harness.open_claim_path();
        const auto pending = harness.start_claim();
        EXPECT(pending.pending());
        harness.sink.confirm_transport();
        EXPECT(harness.lifecycle.complete_indication(
                   kConnection, kGeneration, kSession,
                   kHandles.stream_value, pending.delivery_token,
                   false, 101) ==
               CompanionGattAuthorizationError::indication_failed);
        EXPECT(harness.authorization.apply_calls == 0);
    }
}

void test_denied_terminal_blocks_until_exact_disconnect() {
    Harness harness;
    harness.open_claim_path();
    EXPECT(harness.start_claim().pending());
    harness.confirm_pending();
    harness.authorization.next = {
        CompanionGattAuthorizationAuthorityError::none,
        CompanionAuthorizationClaimOutcome::denied,
        CompanionAuthorizationDenyReason::physical_presence_expired,
        0};
    const auto terminal = harness.lifecycle.resolve_claim(
        kConnection, kGeneration, kSession, kExchange, 102);
    EXPECT(terminal.pending());
    harness.sink.confirm_transport();
    EXPECT(harness.lifecycle.complete_indication(
               kConnection, kGeneration, kSession, kHandles.stream_value,
               terminal.delivery_token, true, 103) ==
           CompanionGattAuthorizationError::none);
    EXPECT(harness.lifecycle.status().phase ==
           CompanionGattAuthorizationPhase::blocked_until_disconnect);
    EXPECT(harness.lifecycle.connect(kConnection, kGeneration + 1U) ==
           CompanionGattAuthorizationError::wrong_connection);
    EXPECT(harness.lifecycle.disconnect(kConnection, kGeneration) ==
           CompanionGattAuthorizationError::none);
    EXPECT(harness.lifecycle.connect(kConnection, kGeneration + 1U) ==
           CompanionGattAuthorizationError::none);
}

void test_exact_bound_timeout_and_stale_callback_are_noops() {
    Harness harness({5, 30});
    harness.open_claim_path();
    const auto pending = harness.start_claim(
        CompanionAuthorizationPurpose::authorize_controller, 100);
    EXPECT(pending.pending());
    EXPECT(harness.lifecycle.service_timeout(
               kConnection, kGeneration + 1U, kSession, kExchange,
               pending.delivery_token, 106) ==
           CompanionGattAuthorizationError::wrong_transport_generation);
    EXPECT(harness.lifecycle.status().response_pending);
    EXPECT(harness.lifecycle.service_timeout(
               kConnection, kGeneration, kSession, kExchange,
               pending.delivery_token + 1U, 106) ==
           CompanionGattAuthorizationError::indication_mismatch);
    EXPECT(harness.lifecycle.status().response_pending);
    EXPECT(harness.lifecycle.service_timeout(
               kConnection, kGeneration, kSession, kExchange,
               pending.delivery_token, 106) ==
           CompanionGattAuthorizationError::indication_timeout);
    EXPECT(harness.lifecycle.status().phase ==
           CompanionGattAuthorizationPhase::blocked_until_disconnect);
}

void test_claim_and_terminal_timeouts_never_promote() {
    {
        Harness harness({50, 30});
        harness.open_claim_path();
        EXPECT(harness.start_claim(
                   CompanionAuthorizationPurpose::authorize_controller,
                   100).pending());
        harness.confirm_pending(101);
        EXPECT(harness.lifecycle.service_timeout(
                   kConnection, kGeneration, kSession, kExchange, 0,
                   129) == CompanionGattAuthorizationError::none);
        EXPECT(harness.lifecycle.service_timeout(
                   kConnection, kGeneration, kSession, kExchange, 0,
                   130) == CompanionGattAuthorizationError::claim_timeout);
        EXPECT(harness.authorization.apply_calls == 0);
    }
    {
        Harness harness({5, 30});
        harness.open_claim_path();
        EXPECT(harness.start_claim().pending());
        harness.confirm_pending();
        const auto terminal = harness.lifecycle.resolve_claim(
            kConnection, kGeneration, kSession, kExchange, 102);
        EXPECT(terminal.pending());
        EXPECT(harness.lifecycle.service_timeout(
                   kConnection, kGeneration, kSession, kExchange,
                   terminal.delivery_token, 108) ==
               CompanionGattAuthorizationError::indication_timeout);
        EXPECT(harness.authorization.apply_calls == 1);
        EXPECT(harness.authorization.release_calls == 1);
        EXPECT(!harness.lifecycle.status().application_authorized);
    }
}

void test_replace_requires_replace_outcome_and_promotes() {
    Harness harness;
    harness.open_claim_path();
    EXPECT(harness.start_claim(
               CompanionAuthorizationPurpose::replace_controller).pending());
    harness.confirm_pending();
    harness.authorization.next = {
        CompanionGattAuthorizationAuthorityError::none,
        CompanionAuthorizationClaimOutcome::replaced,
        CompanionAuthorizationDenyReason::none,
        kControllerBinding};
    const auto terminal = harness.lifecycle.resolve_claim(
        kConnection, kGeneration, kSession, kExchange, 102);
    EXPECT(terminal.pending());
    const auto decoded = decode_companion_fragment(
        {harness.sink.response.data(), harness.sink.response_bytes});
    EXPECT(decoded.decoded());
    const auto result = decode_companion_authorization_claim_result(
        {decoded.fragment.payload.data(), decoded.fragment.payload_bytes});
    EXPECT(result.decoded());
    EXPECT(result.value.purpose ==
           CompanionAuthorizationPurpose::replace_controller);
    EXPECT(result.value.outcome ==
           CompanionAuthorizationClaimOutcome::replaced);
    harness.sink.confirm_transport();
    EXPECT(harness.lifecycle.complete_indication(
               kConnection, kGeneration, kSession, kHandles.stream_value,
               terminal.delivery_token, true, 103) ==
           CompanionGattAuthorizationError::none);
    EXPECT(harness.lifecycle.status().application_authorized);
}

void test_changed_private_claim_evidence_contains() {
    Harness harness;
    harness.open_claim_path();
    auto changed = evidence();
    changed.controller_claim.session_challenge += 1U;
    EXPECT(harness.lifecycle.update_connection_evidence(
               kConnection, kGeneration, changed) ==
           CompanionGattAuthorizationError::insecure_link);
    EXPECT(harness.lifecycle.status().phase ==
           CompanionGattAuthorizationPhase::blocked_until_disconnect);
    EXPECT(harness.issuer.calls == 0);
    EXPECT(harness.authorization.apply_calls == 0);
}

void test_fresh_lower_nonce_reconnects_but_exact_reuse_fails() {
    Harness harness;
    harness.register_and_connect();
    EXPECT(harness.lifecycle.disconnect(kConnection, kGeneration) ==
           CompanionGattAuthorizationError::none);
    EXPECT(harness.lifecycle.connect(kConnection, kGeneration + 1U) ==
           CompanionGattAuthorizationError::none);
    EXPECT(harness.lifecycle.update_connection_evidence(
               kConnection, kGeneration + 1U, evidence()) ==
           CompanionGattAuthorizationError::none);
    constexpr std::uint32_t kFreshLowerSession = kSession - 1U;
    EXPECT(harness.lifecycle.open_provisional_session(
               kConnection, kGeneration + 1U, kFreshLowerSession) ==
           CompanionGattAuthorizationError::none);

    EXPECT(harness.lifecycle.disconnect(kConnection, kGeneration + 1U) ==
           CompanionGattAuthorizationError::none);
    EXPECT(harness.lifecycle.connect(kConnection, kGeneration + 2U) ==
           CompanionGattAuthorizationError::none);
    EXPECT(harness.lifecycle.update_connection_evidence(
               kConnection, kGeneration + 2U, evidence()) ==
           CompanionGattAuthorizationError::none);
    EXPECT(harness.lifecycle.open_provisional_session(
               kConnection, kGeneration + 2U, kFreshLowerSession) ==
           CompanionGattAuthorizationError::session_nonce_reused);
}

void test_stale_resolve_and_timeout_cannot_touch_reopened_generation() {
    Harness harness;
    harness.open_claim_path();
    EXPECT(harness.start_claim().pending());
    harness.confirm_pending();
    EXPECT(harness.lifecycle.disconnect(kConnection, kGeneration) ==
           CompanionGattAuthorizationError::none);
    EXPECT(harness.lifecycle.connect(kConnection, kGeneration + 1U) ==
           CompanionGattAuthorizationError::none);
    EXPECT(harness.lifecycle.update_connection_evidence(
               kConnection, kGeneration + 1U, evidence()) ==
           CompanionGattAuthorizationError::none);
    EXPECT(harness.lifecycle.open_provisional_session(
               kConnection, kGeneration + 1U, kSession + 1U) ==
           CompanionGattAuthorizationError::none);
    const auto before = harness.lifecycle.status();
    EXPECT(harness.lifecycle.resolve_claim(
               kConnection, kGeneration, kSession, kExchange, 200).error ==
           CompanionGattAuthorizationError::wrong_transport_generation);
    EXPECT(harness.lifecycle.service_timeout(
               kConnection, kGeneration, kSession, kExchange, 0, 200) ==
           CompanionGattAuthorizationError::wrong_transport_generation);
    const auto after = harness.lifecycle.status();
    EXPECT(after.phase == before.phase);
    EXPECT(after.session_nonce == before.session_nonce);
    EXPECT(harness.authorization.apply_calls == 0);
}

void test_all_injected_callbacks_are_reentry_closed() {
    {
        Harness harness;
        harness.open_claim_path();
        harness.sink.callback = FakeSink::Callback::submit;
        EXPECT(harness.start_claim().pending());
        EXPECT(harness.sink.callback_result ==
               CompanionGattAuthorizationError::reentrant_call);
    }
    {
        Harness harness;
        harness.open_claim_path();
        harness.issuer.reenter = true;
        EXPECT(harness.start_claim().pending());
        EXPECT(harness.issuer.reentrant_result ==
               CompanionGattAuthorizationError::reentrant_call);
    }
    {
        Harness harness;
        harness.open_claim_path();
        EXPECT(harness.start_claim().pending());
        harness.confirm_pending();
        harness.authorization.reenter_apply = true;
        EXPECT(harness.lifecycle.resolve_claim(
                   kConnection, kGeneration, kSession, kExchange, 102).pending());
        EXPECT(harness.authorization.reentrant_result ==
               CompanionGattAuthorizationError::reentrant_call);
    }
    {
        Harness harness;
        harness.open_claim_path();
        EXPECT(harness.start_claim().pending());
        harness.confirm_pending();
        harness.authorization.reenter_release = true;
        harness.sink.submit_result = CompanionGattSinkError::failed;
        EXPECT(harness.lifecycle.resolve_claim(
                   kConnection, kGeneration, kSession, kExchange, 102).error ==
               CompanionGattAuthorizationError::indication_submit_failed);
        EXPECT(harness.authorization.reentrant_result ==
               CompanionGattAuthorizationError::reentrant_call);
    }
    {
        Harness harness;
        harness.open_claim_path();
        EXPECT(harness.start_claim().pending());
        harness.confirm_pending();
        harness.authorization.next.error =
            CompanionGattAuthorizationAuthorityError::not_ready;
        harness.sink.callback = FakeSink::Callback::cancel;
        EXPECT(harness.lifecycle.resolve_claim(
                   kConnection, kGeneration, kSession, kExchange, 102).error ==
               CompanionGattAuthorizationError::authority_pending);
        EXPECT(harness.sink.callback_result ==
               CompanionGattAuthorizationError::reentrant_call);
    }
    {
        Harness harness({5, 30});
        harness.open_claim_path();
        const auto pending = harness.start_claim();
        EXPECT(pending.pending());
        harness.sink.callback = FakeSink::Callback::abandon;
        EXPECT(harness.lifecycle.service_timeout(
                   kConnection, kGeneration, kSession, kExchange,
                   pending.delivery_token, 106) ==
               CompanionGattAuthorizationError::indication_timeout);
        EXPECT(harness.sink.callback_result ==
               CompanionGattAuthorizationError::reentrant_call);
    }
}

void test_reentry_and_reservation_failures_are_contained() {
    {
        Harness harness;
        harness.open_claim_path();
        harness.sink.callback = FakeSink::Callback::reserve;
        EXPECT(harness.start_claim().pending());
        EXPECT(harness.sink.callback_result ==
               CompanionGattAuthorizationError::reentrant_call);
    }
    {
        Harness harness;
        harness.open_claim_path();
        harness.sink.reserve_result = CompanionGattSinkError::failed;
        EXPECT(harness.start_claim().error ==
               CompanionGattAuthorizationError::response_path_unavailable);
        EXPECT(harness.sink.cancel_calls == 0);
        EXPECT(harness.issuer.calls == 0);
    }
}

void test_invalid_policy_and_redacted_fixed_memory_status() {
    Harness zero_timeout({0, 1});
    EXPECT(zero_timeout.lifecycle.register_handles(kHandles) ==
           CompanionGattAuthorizationError::internal_failure);
    Harness long_claim({1, 30001});
    EXPECT(long_claim.lifecycle.register_handles(kHandles) ==
           CompanionGattAuthorizationError::internal_failure);
    Harness harness;
    harness.register_and_connect();
    const auto status = harness.lifecycle.status();
    EXPECT(status.encrypted);
    EXPECT(status.authenticated_bond);
    EXPECT(status.session_nonce == kSession);
    static_assert(std::is_trivially_copyable_v<
                  CompanionGattAuthorizationStatus>);
    static_assert(sizeof(CompanionGattAuthorizationStatus) <= 40);
    static_assert(sizeof(CompanionGattAuthorizationLifecycle) <= 768);
}

}  // namespace

int main() {
    test_protocol_info_exact_vector_and_strict_decode();
    test_secure_protocol_read_and_exact_handle_gate();
    test_mtu_50_rejects_and_51_accepts_before_mutation();
    test_pending_exact_vector_and_no_early_authority();
    test_not_ready_then_exact_accepted_terminal_and_promotion();
    test_authority_failure_is_local_unknown_and_tombstones();
    test_mtu_51_promotes_but_normal_waits_for_151();
    test_promoted_normal_timeout_reports_and_contains_exact_pending();
    test_pre_auth_normal_commands_and_wrong_context_never_mutate();
    test_security_loss_and_unsubscribe_tombstone();
    test_submit_failures_and_negative_completion_never_promote();
    test_denied_terminal_blocks_until_exact_disconnect();
    test_exact_bound_timeout_and_stale_callback_are_noops();
    test_claim_and_terminal_timeouts_never_promote();
    test_replace_requires_replace_outcome_and_promotes();
    test_changed_private_claim_evidence_contains();
    test_fresh_lower_nonce_reconnects_but_exact_reuse_fails();
    test_stale_resolve_and_timeout_cannot_touch_reopened_generation();
    test_all_injected_callbacks_are_reentry_closed();
    test_reentry_and_reservation_failures_are_contained();
    test_invalid_policy_and_redacted_fixed_memory_status();

    if (failures != 0) {
        std::cerr << failures
                  << " companion GATT authorization assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout <<
        "PASS: 20 companion GATT authorization scenario groups\n";
    return EXIT_SUCCESS;
}
