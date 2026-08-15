#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "opentrail/companion_gatt_session.hpp"

namespace {

using namespace opentrail::companion;
using opentrail::protocol::QuickStatusKind;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

constexpr CompanionGattHandles kHandles{11, 13, 14};
constexpr std::uint16_t kConnection = 7;
constexpr std::uint64_t kBinding = 0xA5;

struct EncodedRequest {
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> bytes{};
    std::size_t size{0};
};

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

EncodedRequest snapshot_request(std::uint32_t session,
                                std::uint32_t exchange) {
    std::array<std::uint8_t, kCompanionSnapshotRequestBytes> payload{};
    EXPECT(encode_companion_snapshot_request(
               {}, {payload.data(), payload.size()}).encoded());
    return envelope(CompanionFrameKind::snapshot_request,
                    session, exchange, payload.data(), payload.size());
}

EncodedRequest action_request(std::uint32_t session,
                              std::uint32_t exchange) {
    std::array<std::uint8_t, kCompanionActionRequestBytes> payload{};
    const CompanionActionRequest action{
        CompanionActionKind::quick_status,
        QuickStatusKind::available_to_help,
        0,
    };
    EXPECT(encode_companion_action_request(
               action, {payload.data(), payload.size()}).encoded());
    return envelope(CompanionFrameKind::action_request,
                    session, exchange, payload.data(), payload.size());
}

class FakeIndicationSink final : public CompanionGattIndicationSink {
public:
    enum class ReenterAt : std::uint8_t {
        none = 0,
        reserve,
        submit,
        cancel,
        abandon,
    };

    CompanionGattSinkError next_reserve{CompanionGattSinkError::none};
    CompanionGattSinkError next_submit{CompanionGattSinkError::none};
    std::uint32_t reserve_calls{0};
    std::uint32_t submit_calls{0};
    std::uint32_t cancel_calls{0};
    std::uint32_t abandon_calls{0};
    bool reserved{false};
    bool pending{false};
    std::uint16_t connection{0};
    std::uint32_t session{0};
    std::uint16_t stream_handle{0};
    std::uint64_t token{0};
    std::size_t reserved_capacity{0};
    std::size_t response_bytes{0};
    std::array<std::uint8_t, kCompanionMaxResponseRecordBytes> response{};
    CompanionGattSessionLifecycle* lifecycle{nullptr};
    ReenterAt reenter_at{ReenterAt::none};
    CompanionGattLifecycleError reentrant_result{
        CompanionGattLifecycleError::none};

    void maybe_reenter(ReenterAt point, std::uint16_t connection_handle) {
        if (reenter_at == point && lifecycle != nullptr) {
            reenter_at = ReenterAt::none;
            reentrant_result = lifecycle->disconnect(connection_handle);
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
        stream_handle = stream_value_handle;
        token = delivery_token;
        reserved_capacity = max_response_bytes;
        maybe_reenter(ReenterAt::reserve, connection_handle);
        if (next_reserve == CompanionGattSinkError::none) {
            reserved = true;
        }
        return next_reserve;
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
        EXPECT(stream_value_handle == stream_handle);
        EXPECT(delivery_token == token);
        maybe_reenter(ReenterAt::submit, connection_handle);
        reserved = false;
        if (next_submit != CompanionGattSinkError::none) {
            return next_submit;
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
        maybe_reenter(ReenterAt::cancel, connection);
        reserved = false;
    }

    void abandon_indication(std::uint64_t delivery_token) override {
        ++abandon_calls;
        EXPECT(delivery_token == token);
        maybe_reenter(ReenterAt::abandon, connection);
        pending = false;
    }

    void observe_completion() {
        EXPECT(pending);
        pending = false;
    }
};

class FakeSnapshotAuthority final : public CompanionSnapshotAuthority {
public:
    std::uint32_t calls{0};

    CompanionSnapshotAuthorityResult read_snapshot() override {
        ++calls;
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
};

class FakeActionAuthority final : public CompanionActionAuthority {
public:
    explicit FakeActionAuthority(FakeIndicationSink& sink) : sink_(sink) {}

    std::uint32_t prepare_calls{0};
    std::uint32_t commit_calls{0};
    std::uint32_t applied_calls{0};
    bool prepare_without_reservation{false};

    CompanionActionAuthorityResult prepare_action(
        const CompanionActionRequest&) override {
        ++prepare_calls;
        if (!sink_.reserved) {
            prepare_without_reservation = true;
        }
        return {
            CompanionAuthorityError::none,
            CompanionActionDisposition::queued,
            CompanionActionRejectReason::none,
            1,
        };
    }

    CompanionAuthorityError commit_action(
        const CompanionActionRequest&,
        const CompanionActionAuthorityResult&) override {
        ++commit_calls;
        if (!sink_.reserved) {
            prepare_without_reservation = true;
        }
        ++applied_calls;
        return CompanionAuthorityError::none;
    }

private:
    FakeIndicationSink& sink_;
};

struct Harness {
    FakeIndicationSink sink{};
    FakeSnapshotAuthority snapshots{};
    FakeActionAuthority actions{sink};
    CompanionRequestCoordinator coordinator{snapshots, actions};
    CompanionGattSessionLifecycle lifecycle;

    explicit Harness(CompanionGattLifecyclePolicy policy = {})
        : lifecycle(coordinator, sink, policy) {
        sink.lifecycle = &lifecycle;
    }

    CompanionGattConnectionEvidence ready_evidence(
        std::uint64_t binding = kBinding) const {
        return {kCompanionMinimumAttMtu, true, true, true, binding};
    }

    void connect_ready(std::uint16_t connection = kConnection) {
        EXPECT(lifecycle.register_handles(kHandles) ==
               CompanionGattLifecycleError::none);
        EXPECT(lifecycle.connect(connection) ==
               CompanionGattLifecycleError::none);
        EXPECT(lifecycle.update_connection_evidence(
                   connection, ready_evidence()) ==
               CompanionGattLifecycleError::none);
        EXPECT(lifecycle.update_indication_subscription(
                   connection, kHandles.stream_cccd, true) ==
               CompanionGattLifecycleError::none);
    }

    void open_ready(std::uint32_t session,
                    std::uint16_t connection = kConnection) {
        connect_ready(connection);
        EXPECT(lifecycle.open_session(connection, session).opened());
    }
};

void test_exact_handle_registration_and_ordered_open_gate() {
    Harness harness;
    EXPECT(harness.lifecycle.connect(kConnection) ==
           CompanionGattLifecycleError::handles_not_registered);
    EXPECT(harness.lifecycle.register_handles({11, 13, 13}) ==
           CompanionGattLifecycleError::invalid_argument);
    EXPECT(harness.lifecycle.register_handles(kHandles) ==
           CompanionGattLifecycleError::none);
    EXPECT(harness.lifecycle.register_handles(kHandles) ==
           CompanionGattLifecycleError::handles_already_registered);
    EXPECT(harness.lifecycle.connect(kConnection) ==
           CompanionGattLifecycleError::none);
    EXPECT(harness.lifecycle.update_indication_subscription(
               kConnection, 15, true) ==
           CompanionGattLifecycleError::wrong_attribute);
    EXPECT(harness.lifecycle.update_indication_subscription(
               kConnection, kHandles.stream_cccd, true) ==
           CompanionGattLifecycleError::mtu_too_small);

    auto partial = harness.ready_evidence();
    partial.att_mtu = kCompanionMinimumAttMtu - 1;
    EXPECT(harness.lifecycle.update_connection_evidence(
               kConnection, partial) == CompanionGattLifecycleError::none);
    EXPECT(harness.lifecycle.open_session(kConnection, 1).error ==
           CompanionGattLifecycleError::mtu_too_small);
    partial.att_mtu = kCompanionMinimumAttMtu;
    partial.authenticated_bond = false;
    EXPECT(harness.lifecycle.update_connection_evidence(
               kConnection, partial) == CompanionGattLifecycleError::none);
    EXPECT(harness.lifecycle.open_session(kConnection, 1).error ==
           CompanionGattLifecycleError::insecure_link);
    partial.authenticated_bond = true;
    partial.application_authorized = false;
    partial.controller_binding = 0;
    EXPECT(harness.lifecycle.update_connection_evidence(
               kConnection, partial) == CompanionGattLifecycleError::none);
    EXPECT(harness.lifecycle.open_session(kConnection, 1).error ==
           CompanionGattLifecycleError::application_unauthorized);
    EXPECT(harness.lifecycle.update_connection_evidence(
               kConnection, harness.ready_evidence()) ==
           CompanionGattLifecycleError::none);
    EXPECT(harness.lifecycle.open_session(kConnection, 1).error ==
           CompanionGattLifecycleError::subscription_required);
    EXPECT(harness.lifecycle.update_indication_subscription(
               kConnection, kHandles.stream_cccd, true) ==
           CompanionGattLifecycleError::none);
    EXPECT(harness.lifecycle.open_session(kConnection, 1).opened());
}

void test_reservation_precedes_action_mutation_and_exact_response() {
    Harness harness;
    harness.open_ready(3);
    const auto request = action_request(3, 9);
    const auto result = harness.lifecycle.service_command(
        kConnection, kHandles.command_value,
        {request.bytes.data(), request.size}, 100);
    EXPECT(result.pending());
    EXPECT(result.delivery_token == 1);
    EXPECT(result.coordinator.exchange_id == 9);
    EXPECT(harness.sink.reserve_calls == 1);
    EXPECT(harness.sink.submit_calls == 1);
    EXPECT(harness.sink.reserved_capacity ==
           kCompanionMaxResponseRecordBytes);
    EXPECT(harness.sink.connection == kConnection);
    EXPECT(harness.sink.session == 3);
    EXPECT(harness.sink.stream_handle == kHandles.stream_value);
    EXPECT(harness.actions.prepare_calls == 1);
    EXPECT(harness.actions.commit_calls == 1);
    EXPECT(harness.actions.applied_calls == 1);
    EXPECT(!harness.actions.prepare_without_reservation);
    EXPECT(harness.sink.response_bytes == 40);
    EXPECT(harness.sink.response[0] == 0x4F);
    EXPECT(harness.sink.response[6] == 0x82);
    EXPECT(harness.lifecycle.status().response_pending);
    harness.sink.observe_completion();
    EXPECT(harness.lifecycle.complete_indication(
               kConnection, 3, kHandles.stream_value,
               result.delivery_token, true) ==
           CompanionGattLifecycleError::none);
    EXPECT(!harness.lifecycle.status().response_pending);
}

void test_snapshot_response_uses_same_reserved_delivery_path() {
    Harness harness;
    harness.open_ready(4);
    const auto request = snapshot_request(4, 10);
    const auto result = harness.lifecycle.service_command(
        kConnection, kHandles.command_value,
        {request.bytes.data(), request.size}, 0);
    EXPECT(result.pending());
    EXPECT(harness.snapshots.calls == 1);
    EXPECT(harness.actions.applied_calls == 0);
    EXPECT(harness.sink.response_bytes == kCompanionMaxResponseRecordBytes);
    EXPECT(harness.sink.response[6] == 0x81);
}

void test_congestion_and_reservation_failure_never_reach_authority() {
    Harness harness;
    harness.open_ready(5);
    const auto request = action_request(5, 1);
    harness.sink.next_reserve = CompanionGattSinkError::busy;
    auto result = harness.lifecycle.service_command(
        kConnection, kHandles.command_value,
        {request.bytes.data(), request.size}, 10);
    EXPECT(result.error == CompanionGattLifecycleError::response_path_busy);
    EXPECT(harness.actions.prepare_calls == 0);
    EXPECT(harness.actions.applied_calls == 0);
    harness.sink.next_reserve = CompanionGattSinkError::failed;
    result = harness.lifecycle.service_command(
        kConnection, kHandles.command_value,
        {request.bytes.data(), request.size}, 11);
    EXPECT(result.error ==
           CompanionGattLifecycleError::response_path_unavailable);
    EXPECT(harness.actions.prepare_calls == 0);
    harness.sink.next_reserve = CompanionGattSinkError::none;
    result = harness.lifecycle.service_command(
        kConnection, kHandles.command_value,
        {request.bytes.data(), request.size}, 12);
    EXPECT(result.pending());
    EXPECT(result.delivery_token == 1);
    EXPECT(harness.actions.applied_calls == 1);
}

void test_wrong_connection_handle_and_session_never_mutate_authority() {
    Harness harness;
    harness.open_ready(6);
    const auto valid = action_request(6, 1);
    auto result = harness.lifecycle.service_command(
        kConnection + 1, kHandles.command_value,
        {valid.bytes.data(), valid.size}, 0);
    EXPECT(result.error == CompanionGattLifecycleError::wrong_connection);
    result = harness.lifecycle.service_command(
        kConnection, kHandles.command_value + 1,
        {valid.bytes.data(), valid.size}, 0);
    EXPECT(result.error == CompanionGattLifecycleError::wrong_attribute);
    const auto wrong_session = action_request(7, 1);
    result = harness.lifecycle.service_command(
        kConnection, kHandles.command_value,
        {wrong_session.bytes.data(), wrong_session.size}, 0);
    EXPECT(result.error == CompanionGattLifecycleError::coordinator_rejected);
    EXPECT(result.coordinator.session_error ==
           CompanionSessionError::wrong_session);
    EXPECT(harness.sink.reserve_calls == 1);
    EXPECT(harness.sink.cancel_calls == 1);
    EXPECT(harness.actions.prepare_calls == 0);
    EXPECT(harness.actions.applied_calls == 0);
}

void test_only_one_indication_can_be_outstanding() {
    Harness harness;
    harness.open_ready(8);
    auto request = action_request(8, 1);
    const auto first = harness.lifecycle.service_command(
        kConnection, kHandles.command_value,
        {request.bytes.data(), request.size}, 0);
    EXPECT(first.pending());
    request = action_request(8, 2);
    const auto second = harness.lifecycle.service_command(
        kConnection, kHandles.command_value,
        {request.bytes.data(), request.size}, 1);
    EXPECT(second.error == CompanionGattLifecycleError::response_path_busy);
    EXPECT(harness.sink.reserve_calls == 1);
    EXPECT(harness.actions.applied_calls == 1);
}

void test_unsubscribe_abandons_response_and_requires_exact_disconnect() {
    Harness harness;
    harness.open_ready(9);
    const auto request = action_request(9, 1);
    const auto pending = harness.lifecycle.service_command(
        kConnection, kHandles.command_value,
        {request.bytes.data(), request.size}, 0);
    EXPECT(pending.pending());
    EXPECT(harness.lifecycle.update_indication_subscription(
               kConnection, kHandles.stream_cccd, false) ==
           CompanionGattLifecycleError::none);
    EXPECT(harness.sink.abandon_calls == 1);
    EXPECT(harness.actions.applied_calls == 1);
    EXPECT(!harness.coordinator.session_status().active);
    EXPECT(harness.lifecycle.status().blocked_until_disconnect);
    EXPECT(harness.lifecycle.disconnect(kConnection + 1) ==
           CompanionGattLifecycleError::wrong_connection);
    EXPECT(harness.lifecycle.status().blocked_until_disconnect);
    EXPECT(harness.lifecycle.disconnect(kConnection) ==
           CompanionGattLifecycleError::none);
    EXPECT(!harness.lifecycle.status().connected);
}

void test_security_loss_closes_and_blocks_active_session() {
    Harness harness;
    harness.open_ready(10);
    auto lost = harness.ready_evidence();
    lost.encrypted = false;
    EXPECT(harness.lifecycle.update_connection_evidence(
               kConnection, lost) == CompanionGattLifecycleError::none);
    EXPECT(!harness.coordinator.session_status().active);
    EXPECT(harness.lifecycle.status().blocked_until_disconnect);
    const auto request = action_request(10, 1);
    EXPECT(harness.lifecycle.service_command(
               kConnection, kHandles.command_value,
               {request.bytes.data(), request.size}, 0).error ==
           CompanionGattLifecycleError::blocked_until_disconnect);
    EXPECT(harness.actions.applied_calls == 0);
}

void test_submit_failure_after_commit_is_contained_without_retry() {
    Harness harness;
    harness.open_ready(11);
    harness.sink.next_submit = CompanionGattSinkError::failed;
    const auto request = action_request(11, 1);
    auto result = harness.lifecycle.service_command(
        kConnection, kHandles.command_value,
        {request.bytes.data(), request.size}, 0);
    EXPECT(result.error ==
           CompanionGattLifecycleError::indication_submit_failed);
    EXPECT(result.coordinator.responded());
    EXPECT(harness.actions.applied_calls == 1);
    EXPECT(!harness.coordinator.session_status().active);
    EXPECT(harness.lifecycle.status().blocked_until_disconnect);
    result = harness.lifecycle.service_command(
        kConnection, kHandles.command_value,
        {request.bytes.data(), request.size}, 1);
    EXPECT(result.error ==
           CompanionGattLifecycleError::blocked_until_disconnect);
    EXPECT(harness.actions.applied_calls == 1);

    EXPECT(harness.lifecycle.disconnect(kConnection) ==
           CompanionGattLifecycleError::none);
    EXPECT(harness.lifecycle.connect(kConnection) ==
           CompanionGattLifecycleError::none);
    EXPECT(harness.lifecycle.update_connection_evidence(
               kConnection, harness.ready_evidence()) ==
           CompanionGattLifecycleError::none);
    EXPECT(harness.lifecycle.update_indication_subscription(
               kConnection, kHandles.stream_cccd, true) ==
           CompanionGattLifecycleError::none);
    EXPECT(harness.lifecycle.open_session(kConnection, 12).opened());
    harness.sink.next_submit = CompanionGattSinkError::none;
    result = harness.lifecycle.service_command(
        kConnection, kHandles.command_value,
        {request.bytes.data(), request.size}, 2);
    EXPECT(result.error == CompanionGattLifecycleError::coordinator_rejected);
    EXPECT(result.coordinator.session_error ==
           CompanionSessionError::wrong_session);
    EXPECT(harness.actions.applied_calls == 1);
}

void test_failed_completion_and_timeout_close_session() {
    {
        Harness harness;
        harness.open_ready(13);
        const auto request = action_request(13, 1);
        const auto pending = harness.lifecycle.service_command(
            kConnection, kHandles.command_value,
            {request.bytes.data(), request.size}, 100);
        EXPECT(pending.pending());
        harness.sink.observe_completion();
        EXPECT(harness.lifecycle.complete_indication(
                   kConnection, 13, kHandles.stream_value,
                   pending.delivery_token, false) ==
               CompanionGattLifecycleError::indication_failed);
        EXPECT(!harness.coordinator.session_status().active);
        EXPECT(harness.lifecycle.status().blocked_until_disconnect);
    }
    {
        Harness harness({50, 1, UINT64_MAX});
        harness.open_ready(14);
        const auto request = action_request(14, 1);
        EXPECT(harness.lifecycle.service_command(
                   kConnection, kHandles.command_value,
                   {request.bytes.data(), request.size}, 100).pending());
        EXPECT(harness.lifecycle.service_timeout(149) ==
               CompanionGattLifecycleError::none);
        EXPECT(harness.lifecycle.service_timeout(150) ==
               CompanionGattLifecycleError::indication_timeout);
        EXPECT(harness.sink.abandon_calls == 1);
        EXPECT(!harness.coordinator.session_status().active);
        EXPECT(harness.lifecycle.status().blocked_until_disconnect);
    }
}

void test_stale_completion_cannot_clear_reused_connection() {
    Harness harness;
    harness.open_ready(15);
    auto request = action_request(15, 1);
    const auto old_pending = harness.lifecycle.service_command(
        kConnection, kHandles.command_value,
        {request.bytes.data(), request.size}, 0);
    EXPECT(old_pending.pending());
    EXPECT(harness.lifecycle.disconnect(kConnection) ==
           CompanionGattLifecycleError::none);
    EXPECT(harness.lifecycle.connect(kConnection) ==
           CompanionGattLifecycleError::none);
    EXPECT(harness.lifecycle.update_connection_evidence(
               kConnection, harness.ready_evidence()) ==
           CompanionGattLifecycleError::none);
    EXPECT(harness.lifecycle.update_indication_subscription(
               kConnection, kHandles.stream_cccd, true) ==
           CompanionGattLifecycleError::none);
    EXPECT(harness.lifecycle.open_session(kConnection, 16).opened());
    request = action_request(16, 1);
    const auto current = harness.lifecycle.service_command(
        kConnection, kHandles.command_value,
        {request.bytes.data(), request.size}, 1);
    EXPECT(current.pending());
    EXPECT(current.delivery_token != old_pending.delivery_token);
    EXPECT(harness.lifecycle.complete_indication(
               kConnection, 15, kHandles.stream_value,
               old_pending.delivery_token, true) ==
           CompanionGattLifecycleError::indication_mismatch);
    EXPECT(harness.lifecycle.status().response_pending);
    EXPECT(harness.actions.applied_calls == 2);
}

void test_wrong_completion_is_a_noop_until_exact_confirmation() {
    Harness harness;
    harness.open_ready(17);
    const auto request = action_request(17, 1);
    const auto pending = harness.lifecycle.service_command(
        kConnection, kHandles.command_value,
        {request.bytes.data(), request.size}, 0);
    EXPECT(pending.pending());
    EXPECT(harness.lifecycle.complete_indication(
               kConnection + 1, 17, kHandles.stream_value,
               pending.delivery_token, true) ==
           CompanionGattLifecycleError::wrong_connection);
    EXPECT(harness.lifecycle.complete_indication(
               kConnection, 17, kHandles.command_value,
               pending.delivery_token, true) ==
           CompanionGattLifecycleError::indication_mismatch);
    EXPECT(harness.lifecycle.complete_indication(
               kConnection, 17, kHandles.stream_value,
               pending.delivery_token + 1, true) ==
           CompanionGattLifecycleError::indication_mismatch);
    EXPECT(harness.lifecycle.status().response_pending);
    harness.sink.observe_completion();
    EXPECT(harness.lifecycle.complete_indication(
               kConnection, 17, kHandles.stream_value,
               pending.delivery_token, true) ==
           CompanionGattLifecycleError::none);
    EXPECT(harness.lifecycle.complete_indication(
               kConnection, 17, kHandles.stream_value,
               pending.delivery_token, true) ==
           CompanionGattLifecycleError::no_outstanding_indication);
}

void test_clock_rollback_and_delivery_token_exhaustion_fail_closed() {
    {
        Harness harness;
        harness.open_ready(18);
        EXPECT(harness.lifecycle.service_timeout(100) ==
               CompanionGattLifecycleError::none);
        EXPECT(harness.lifecycle.service_timeout(99) ==
               CompanionGattLifecycleError::clock_rollback);
        EXPECT(harness.lifecycle.status().blocked_until_disconnect);
        EXPECT(!harness.coordinator.session_status().active);
    }
    {
        Harness harness({50, 7, 7});
        harness.open_ready(19);
        auto request = action_request(19, 1);
        const auto pending = harness.lifecycle.service_command(
            kConnection, kHandles.command_value,
            {request.bytes.data(), request.size}, 0);
        EXPECT(pending.pending());
        EXPECT(pending.delivery_token == 7);
        harness.sink.observe_completion();
        EXPECT(harness.lifecycle.complete_indication(
                   kConnection, 19, kHandles.stream_value,
                   pending.delivery_token, true) ==
               CompanionGattLifecycleError::none);
        request = action_request(19, 2);
        EXPECT(harness.lifecycle.service_command(
                   kConnection, kHandles.command_value,
                   {request.bytes.data(), request.size}, 1).error ==
               CompanionGattLifecycleError::delivery_token_exhausted);
        EXPECT(harness.actions.applied_calls == 1);
    }
}

void test_reentrant_sink_callbacks_cannot_advance_lifecycle() {
    {
        Harness harness;
        harness.open_ready(21);
        harness.sink.reenter_at = FakeIndicationSink::ReenterAt::reserve;
        const auto request = action_request(21, 1);
        const auto pending = harness.lifecycle.service_command(
            kConnection, kHandles.command_value,
            {request.bytes.data(), request.size}, 0);
        EXPECT(pending.pending());
        EXPECT(harness.sink.reentrant_result ==
               CompanionGattLifecycleError::reentrant_call);
        EXPECT(harness.lifecycle.status().connected);
        EXPECT(harness.actions.applied_calls == 1);
    }
    {
        Harness harness;
        harness.open_ready(22);
        harness.sink.reenter_at = FakeIndicationSink::ReenterAt::submit;
        const auto request = action_request(22, 1);
        EXPECT(harness.lifecycle.service_command(
                   kConnection, kHandles.command_value,
                   {request.bytes.data(), request.size}, 0).pending());
        EXPECT(harness.sink.reentrant_result ==
               CompanionGattLifecycleError::reentrant_call);
        EXPECT(harness.lifecycle.status().response_pending);
    }
    {
        Harness harness;
        harness.open_ready(23);
        harness.sink.reenter_at = FakeIndicationSink::ReenterAt::cancel;
        const auto request = action_request(24, 1);
        EXPECT(harness.lifecycle.service_command(
                   kConnection, kHandles.command_value,
                   {request.bytes.data(), request.size}, 0).error ==
               CompanionGattLifecycleError::coordinator_rejected);
        EXPECT(harness.sink.reentrant_result ==
               CompanionGattLifecycleError::reentrant_call);
        EXPECT(harness.lifecycle.status().session_active);
        EXPECT(harness.actions.applied_calls == 0);
    }
    {
        Harness harness;
        harness.open_ready(25);
        const auto request = action_request(25, 1);
        EXPECT(harness.lifecycle.service_command(
                   kConnection, kHandles.command_value,
                   {request.bytes.data(), request.size}, 0).pending());
        harness.sink.reenter_at = FakeIndicationSink::ReenterAt::abandon;
        EXPECT(harness.lifecycle.update_indication_subscription(
                   kConnection, kHandles.stream_cccd, false) ==
               CompanionGattLifecycleError::none);
        EXPECT(harness.sink.reentrant_result ==
               CompanionGattLifecycleError::reentrant_call);
        EXPECT(harness.lifecycle.status().blocked_until_disconnect);
        EXPECT(!harness.coordinator.session_status().active);
    }
}

void test_invalid_policy_and_redacted_fixed_memory_status() {
    Harness invalid({0, 1, 1});
    EXPECT(invalid.lifecycle.register_handles(kHandles) ==
           CompanionGattLifecycleError::internal_failure);

    Harness harness;
    harness.open_ready(20);
    const auto status = harness.lifecycle.status();
    EXPECT(status.handles_registered);
    EXPECT(status.connected);
    EXPECT(status.encrypted);
    EXPECT(status.authenticated_bond);
    EXPECT(status.application_authorized);
    EXPECT(status.indication_subscribed);
    EXPECT(status.session_active);
    EXPECT(status.att_mtu == kCompanionMinimumAttMtu);
    EXPECT(status.session_nonce == 20);
    EXPECT(status.pending_exchange_id == 0);
    static_assert(std::is_trivially_copyable_v<CompanionGattLifecycleStatus>);
    static_assert(std::is_trivially_copyable_v<CompanionGattRequestResult>);
    static_assert(sizeof(CompanionGattLifecycleStatus) <= 32);
    static_assert(sizeof(CompanionGattRequestResult) <= 64);
    static_assert(sizeof(CompanionGattSessionLifecycle) <= 256);
}

}  // namespace

int main() {
    test_exact_handle_registration_and_ordered_open_gate();
    test_reservation_precedes_action_mutation_and_exact_response();
    test_snapshot_response_uses_same_reserved_delivery_path();
    test_congestion_and_reservation_failure_never_reach_authority();
    test_wrong_connection_handle_and_session_never_mutate_authority();
    test_only_one_indication_can_be_outstanding();
    test_unsubscribe_abandons_response_and_requires_exact_disconnect();
    test_security_loss_closes_and_blocks_active_session();
    test_submit_failure_after_commit_is_contained_without_retry();
    test_failed_completion_and_timeout_close_session();
    test_stale_completion_cannot_clear_reused_connection();
    test_wrong_completion_is_a_noop_until_exact_confirmation();
    test_clock_rollback_and_delivery_token_exhaustion_fail_closed();
    test_reentrant_sink_callbacks_cannot_advance_lifecycle();
    test_invalid_policy_and_redacted_fixed_memory_status();

    if (failures != 0) {
        std::cerr << failures
                  << " companion GATT session assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 15 companion GATT session scenario groups\n";
    return EXIT_SUCCESS;
}
