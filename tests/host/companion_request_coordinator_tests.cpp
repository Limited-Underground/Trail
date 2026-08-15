#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "opentrail/companion_request_coordinator.hpp"

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

struct EncodedRequest {
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> bytes{};
    std::size_t size{0};
};

class FakeSnapshotAuthority final : public CompanionSnapshotAuthority {
public:
    CompanionSnapshotAuthorityResult next{
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
    std::uint32_t calls{0};

    CompanionSnapshotAuthorityResult read_snapshot() override {
        ++calls;
        return next;
    }
};

class FakeActionAuthority final : public CompanionActionAuthority {
public:
    CompanionAuthorityError next_error{CompanionAuthorityError::none};
    CompanionActionDisposition next_disposition{
        CompanionActionDisposition::queued};
    CompanionActionRejectReason next_reason{
        CompanionActionRejectReason::none};
    CompanionAuthorityError next_commit_error{CompanionAuthorityError::none};
    std::uint32_t next_token{1};
    std::uint32_t calls{0};
    std::uint32_t commit_calls{0};
    std::uint32_t applied_calls{0};
    CompanionActionRequest last{};

    CompanionActionAuthorityResult prepare_action(
        const CompanionActionRequest& request) override {
        ++calls;
        last = request;
        return {next_error, next_disposition, next_reason, next_token};
    }

    CompanionAuthorityError commit_action(
        const CompanionActionRequest& request,
        const CompanionActionAuthorityResult& prepared) override {
        ++commit_calls;
        last = request;
        if (next_commit_error == CompanionAuthorityError::none &&
            prepared.disposition != CompanionActionDisposition::rejected) {
            ++applied_calls;
        }
        return next_commit_error;
    }
};

CompanionSessionEvidence evidence(std::uint64_t binding = 0xA5) {
    return {binding, true, true, true};
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

EncodedRequest snapshot_request(std::uint32_t session,
                                std::uint32_t exchange) {
    std::array<std::uint8_t, kCompanionSnapshotRequestBytes> payload{};
    EXPECT(encode_companion_snapshot_request(
               {}, {payload.data(), payload.size()}).encoded());
    return envelope(CompanionFrameKind::snapshot_request,
                    session,
                    exchange,
                    payload.data(),
                    payload.size());
}

EncodedRequest action_request(std::uint32_t session,
                              std::uint32_t exchange,
                              const CompanionActionRequest& action) {
    std::array<std::uint8_t, kCompanionActionRequestBytes> payload{};
    EXPECT(encode_companion_action_request(
               action, {payload.data(), payload.size()}).encoded());
    return envelope(CompanionFrameKind::action_request,
                    session,
                    exchange,
                    payload.data(),
                    payload.size());
}

CompanionActionRequest quick(QuickStatusKind status) {
    return {CompanionActionKind::quick_status, status, 0};
}

CompanionActionRequest alert(std::uint64_t alert_id) {
    return {CompanionActionKind::acknowledge_critical_alert,
            QuickStatusKind::ok,
            alert_id};
}

CompanionActionRequest position(CompanionActionKind kind) {
    return {kind, QuickStatusKind::ok, 0};
}

void test_snapshot_request_produces_exact_correlated_snapshot() {
    FakeSnapshotAuthority snapshots;
    FakeActionAuthority actions;
    CompanionRequestCoordinator coordinator(snapshots, actions);
    EXPECT(coordinator.open_session(evidence(), 3).opened());
    const auto request = snapshot_request(3, 9);
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> response{};
    const auto result = coordinator.service(
        evidence(),
        {request.bytes.data(), request.size},
        {response.data(), response.size()});
    EXPECT(result.responded());
    EXPECT(result.disposition ==
           CompanionCoordinatorDisposition::processed_new);
    EXPECT(result.exchange_id == 9);
    EXPECT(result.response_bytes == 52);
    EXPECT(snapshots.calls == 1);
    EXPECT(actions.calls == 0);

    const std::array<std::uint8_t, 52> expected{
        0x4F, 0x54, 0x43, 0x30, 0x00, 0x00, 0x81, 0x00,
        0x03, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
        0x00, 0x01, 0x20, 0x00,
        0x4F, 0x54, 0x4E, 0x30, 0x00, 0x00, 0x02, 0x03,
        0x02, 0x02, 0x02, 0x00, 0x07, 0x00, 0x00, 0x00,
        0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    for (std::size_t index = 0; index < expected.size(); ++index) {
        EXPECT(response[index] == expected[index]);
    }
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> duplicate{};
    const auto duplicate_result = coordinator.service(
        evidence(), {request.bytes.data(), request.size},
        {duplicate.data(), duplicate.size()});
    EXPECT(duplicate_result.duplicate());
    EXPECT(snapshots.calls == 1);
    for (std::size_t index = 0; index < expected.size(); ++index) {
        EXPECT(duplicate[index] == expected[index]);
    }
}

void test_quick_status_produces_exact_queued_result_not_delivery() {
    FakeSnapshotAuthority snapshots;
    FakeActionAuthority actions;
    CompanionRequestCoordinator coordinator(snapshots, actions);
    EXPECT(coordinator.open_session(evidence(), 4).opened());
    const auto request = action_request(
        4, 0x11223344U, quick(QuickStatusKind::need_assistance));
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> response{};
    const auto result = coordinator.service(
        evidence(), {request.bytes.data(), request.size},
        {response.data(), response.size()});
    EXPECT(result.responded());
    EXPECT(result.response_bytes == 40);
    EXPECT(actions.calls == 1);
    EXPECT(actions.commit_calls == 1);
    EXPECT(actions.applied_calls == 1);
    EXPECT(actions.last.kind == CompanionActionKind::quick_status);
    EXPECT(actions.last.quick_status == QuickStatusKind::need_assistance);

    const std::array<std::uint8_t, 40> expected{
        0x4F, 0x54, 0x43, 0x30, 0x00, 0x00, 0x82, 0x00,
        0x04, 0x00, 0x00, 0x00, 0x44, 0x33, 0x22, 0x11,
        0x00, 0x01, 0x14, 0x00,
        0x4F, 0x54, 0x52, 0x30, 0x00, 0x00, 0x02, 0x00,
        0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };
    for (std::size_t index = 0; index < expected.size(); ++index) {
        EXPECT(response[index] == expected[index]);
    }
}

void test_all_typed_actions_dispatch_without_reinterpretation() {
    FakeSnapshotAuthority snapshots;
    FakeActionAuthority actions;
    CompanionRequestCoordinator coordinator(snapshots, actions);
    EXPECT(coordinator.open_session(evidence(), 5).opened());
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> response{};
    std::uint32_t exchange = 1;
    for (const auto status : {
             QuickStatusKind::ok,
             QuickStatusKind::need_assistance,
             QuickStatusKind::anyone_online,
             QuickStatusKind::available_to_help,
         }) {
        actions.next_disposition = CompanionActionDisposition::queued;
        const auto request = action_request(5, exchange++, quick(status));
        EXPECT(coordinator.service(
                   evidence(), {request.bytes.data(), request.size},
                   {response.data(), response.size()}).responded());
        EXPECT(actions.last.quick_status == status);
    }
    const auto ack = action_request(5, exchange++, alert(0x8877665544332211ULL));
    EXPECT(coordinator.service(
               evidence(), {ack.bytes.data(), ack.size},
               {response.data(), response.size()}).responded());
    EXPECT(actions.last.kind ==
           CompanionActionKind::acknowledge_critical_alert);
    EXPECT(actions.last.critical_alert_id == 0x8877665544332211ULL);

    actions.next_disposition = CompanionActionDisposition::admitted;
    for (const auto kind : {
             CompanionActionKind::start_position_sharing,
             CompanionActionKind::stop_position_sharing,
         }) {
        const auto request = action_request(5, exchange++, position(kind));
        EXPECT(coordinator.service(
                   evidence(), {request.bytes.data(), request.size},
                   {response.data(), response.size()}).responded());
        EXPECT(actions.last.kind == kind);
    }
    EXPECT(actions.calls == 7);
    EXPECT(actions.commit_calls == 7);
    EXPECT(actions.applied_calls == 7);
}

void test_queue_full_and_stale_alert_are_typed_rejections() {
    FakeSnapshotAuthority snapshots;
    FakeActionAuthority actions;
    CompanionRequestCoordinator coordinator(snapshots, actions);
    EXPECT(coordinator.open_session(evidence(), 6).opened());
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> response{};

    actions.next_disposition = CompanionActionDisposition::rejected;
    actions.next_reason = CompanionActionRejectReason::queue_full;
    auto request = action_request(6, 1, quick(QuickStatusKind::ok));
    auto service = coordinator.service(
        evidence(), {request.bytes.data(), request.size},
        {response.data(), response.size()});
    EXPECT(service.responded());
    auto envelope_result = decode_companion_fragment(
        {response.data(), service.response_bytes});
    auto action_result = decode_companion_action_result(
        {envelope_result.fragment.payload.data(),
         envelope_result.fragment.payload_bytes});
    EXPECT(action_result.decoded());
    EXPECT(action_result.value.disposition ==
           CompanionActionDisposition::rejected);
    EXPECT(action_result.value.reject_reason ==
           CompanionActionRejectReason::queue_full);

    actions.next_reason = CompanionActionRejectReason::stale_alert;
    request = action_request(6, 2, alert(0x1111));
    service = coordinator.service(
        evidence(), {request.bytes.data(), request.size},
        {response.data(), response.size()});
    EXPECT(service.responded());
    envelope_result = decode_companion_fragment(
        {response.data(), service.response_bytes});
    action_result = decode_companion_action_result(
        {envelope_result.fragment.payload.data(),
         envelope_result.fragment.payload_bytes});
    EXPECT(action_result.decoded());
    EXPECT(action_result.value.reject_reason ==
           CompanionActionRejectReason::stale_alert);
    EXPECT(action_result.value.critical_alert_id == 0x1111);
    EXPECT(actions.commit_calls == 2);
    EXPECT(actions.applied_calls == 0);
}

void test_malformed_wrong_kind_and_semantic_errors_touch_nothing() {
    FakeSnapshotAuthority snapshots;
    FakeActionAuthority actions;
    CompanionRequestCoordinator coordinator(snapshots, actions);
    EXPECT(coordinator.open_session(evidence(), 7).opened());
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> response{};
    response.fill(0xA5);
    const auto before = response;

    auto request = action_request(7, 1, quick(QuickStatusKind::ok));
    request.bytes[0] = 'X';
    auto result = coordinator.service(
        evidence(), {request.bytes.data(), request.size},
        {response.data(), response.size()});
    EXPECT(result.error == CompanionCoordinatorError::envelope_rejected);
    EXPECT(response == before);

    CompanionStatusSnapshot snapshot{};
    snapshot.revision = 1;
    std::array<std::uint8_t, kCompanionStatusSnapshotBytes> payload{};
    EXPECT(encode_companion_status_snapshot(
               snapshot, {payload.data(), payload.size()}).encoded());
    request = envelope(CompanionFrameKind::snapshot, 7, 1,
                       payload.data(), payload.size());
    result = coordinator.service(
        evidence(), {request.bytes.data(), request.size},
        {response.data(), response.size()});
    EXPECT(result.error == CompanionCoordinatorError::wrong_frame_kind);
    EXPECT(response == before);

    request = action_request(7, 1, quick(QuickStatusKind::ok));
    request.bytes[kCompanionFragmentHeaderBytes + 7] = 9;
    result = coordinator.service(
        evidence(), {request.bytes.data(), request.size},
        {response.data(), response.size()});
    EXPECT(result.error == CompanionCoordinatorError::semantic_rejected);
    EXPECT(response == before);
    EXPECT(actions.calls == 0);
    EXPECT(actions.commit_calls == 0);
    EXPECT(actions.applied_calls == 0);
    EXPECT(snapshots.calls == 0);
    EXPECT(coordinator.session_status().last_request_id == 0);
}

void test_output_capacity_is_checked_before_authority_mutation() {
    FakeSnapshotAuthority snapshots;
    FakeActionAuthority actions;
    CompanionRequestCoordinator coordinator(snapshots, actions);
    EXPECT(coordinator.open_session(evidence(), 8).opened());
    const auto request = action_request(8, 1, quick(QuickStatusKind::ok));
    std::array<std::uint8_t, 39> small{};
    small.fill(0xA5);
    const auto before = small;
    auto result = coordinator.service(
        evidence(), {request.bytes.data(), request.size},
        {small.data(), small.size()});
    EXPECT(result.error == CompanionCoordinatorError::output_too_small);
    EXPECT(small == before);
    EXPECT(actions.calls == 0);
    EXPECT(coordinator.session_status().last_request_id == 0);

    std::array<std::uint8_t, kCompanionMaxFragmentBytes> response{};
    result = coordinator.service(
        evidence(), {request.bytes.data(), request.size},
        {response.data(), response.size()});
    EXPECT(result.responded());
    EXPECT(actions.calls == 1);
    EXPECT(actions.commit_calls == 1);
    EXPECT(actions.applied_calls == 1);
}

void test_security_controller_and_session_are_enforced() {
    FakeSnapshotAuthority snapshots;
    FakeActionAuthority actions;
    CompanionRequestCoordinator coordinator(snapshots, actions);
    EXPECT(coordinator.open_session(evidence(10), 9).opened());
    const auto request = snapshot_request(9, 1);
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> response{};

    auto denied = evidence(10);
    denied.application_authorized = false;
    auto result = coordinator.service(
        denied, {request.bytes.data(), request.size},
        {response.data(), response.size()});
    EXPECT(result.session_error ==
           CompanionSessionError::controller_not_authorized);
    result = coordinator.service(
        evidence(11), {request.bytes.data(), request.size},
        {response.data(), response.size()});
    EXPECT(result.session_error == CompanionSessionError::wrong_controller);
    const auto wrong_session = snapshot_request(10, 1);
    result = coordinator.service(
        evidence(10), {wrong_session.bytes.data(), wrong_session.size},
        {response.data(), response.size()});
    EXPECT(result.session_error == CompanionSessionError::wrong_session);
    EXPECT(snapshots.calls == 0);
}

void test_exact_duplicate_replays_exact_response_without_reapply() {
    FakeSnapshotAuthority snapshots;
    FakeActionAuthority actions;
    CompanionRequestCoordinator coordinator(snapshots, actions);
    EXPECT(coordinator.open_session(evidence(), 10).opened());
    const auto request = action_request(
        10, 7, quick(QuickStatusKind::available_to_help));
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> first{};
    const auto first_result = coordinator.service(
        evidence(), {request.bytes.data(), request.size},
        {first.data(), first.size()});
    EXPECT(first_result.responded());
    EXPECT(actions.calls == 1);
    EXPECT(actions.commit_calls == 1);
    EXPECT(actions.applied_calls == 1);

    actions.next_error = CompanionAuthorityError::failed;
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> duplicate{};
    const auto duplicate_result = coordinator.service(
        evidence(), {request.bytes.data(), request.size},
        {duplicate.data(), duplicate.size()});
    EXPECT(duplicate_result.responded());
    EXPECT(duplicate_result.duplicate());
    EXPECT(duplicate_result.response_bytes == first_result.response_bytes);
    EXPECT(actions.calls == 1);
    EXPECT(actions.commit_calls == 1);
    EXPECT(actions.applied_calls == 1);
    for (std::size_t index = 0; index < first_result.response_bytes; ++index) {
        EXPECT(duplicate[index] == first[index]);
    }
}

void test_same_exchange_with_any_byte_difference_is_conflict() {
    FakeSnapshotAuthority snapshots;
    FakeActionAuthority actions;
    CompanionRequestCoordinator coordinator(snapshots, actions);
    EXPECT(coordinator.open_session(evidence(), 11).opened());
    const auto first = action_request(11, 8, quick(QuickStatusKind::ok));
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> response{};
    EXPECT(coordinator.service(
               evidence(), {first.bytes.data(), first.size},
               {response.data(), response.size()}).responded());
    const auto conflict = action_request(
        11, 8, quick(QuickStatusKind::need_assistance));
    response.fill(0xA5);
    const auto before = response;
    const auto result = coordinator.service(
        evidence(), {conflict.bytes.data(), conflict.size},
        {response.data(), response.size()});
    EXPECT(result.error == CompanionCoordinatorError::duplicate_conflict);
    EXPECT(response == before);
    EXPECT(actions.calls == 1);
}

void test_authority_failure_consumes_id_without_result_or_reapply() {
    FakeSnapshotAuthority snapshots;
    FakeActionAuthority actions;
    actions.next_error = CompanionAuthorityError::failed;
    CompanionRequestCoordinator coordinator(snapshots, actions);
    EXPECT(coordinator.open_session(evidence(), 12).opened());
    const auto failed = action_request(12, 1, quick(QuickStatusKind::ok));
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> response{};
    response.fill(0xA5);
    const auto before = response;
    auto result = coordinator.service(
        evidence(), {failed.bytes.data(), failed.size},
        {response.data(), response.size()});
    EXPECT(result.error == CompanionCoordinatorError::action_authority_failed);
    EXPECT(response == before);
    EXPECT(actions.calls == 1);
    EXPECT(actions.commit_calls == 0);
    EXPECT(actions.applied_calls == 0);
    result = coordinator.service(
        evidence(), {failed.bytes.data(), failed.size},
        {response.data(), response.size()});
    EXPECT(result.error == CompanionCoordinatorError::duplicate_without_result);
    EXPECT(actions.calls == 1);
    EXPECT(actions.commit_calls == 0);
    EXPECT(actions.applied_calls == 0);

    actions.next_error = CompanionAuthorityError::none;
    const auto next = action_request(12, 2, quick(QuickStatusKind::ok));
    EXPECT(coordinator.service(
               evidence(), {next.bytes.data(), next.size},
               {response.data(), response.size()}).responded());
    EXPECT(actions.calls == 2);
    EXPECT(actions.commit_calls == 1);
    EXPECT(actions.applied_calls == 1);
}

void test_commit_failure_is_terminal_and_guarantees_no_mutation() {
    FakeSnapshotAuthority snapshots;
    FakeActionAuthority actions;
    actions.next_commit_error = CompanionAuthorityError::failed;
    CompanionRequestCoordinator coordinator(snapshots, actions);
    EXPECT(coordinator.open_session(evidence(), 121).opened());
    const auto request = action_request(
        121, 1, quick(QuickStatusKind::need_assistance));
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> response{};
    response.fill(0xA5);
    const auto before = response;
    auto result = coordinator.service(
        evidence(), {request.bytes.data(), request.size},
        {response.data(), response.size()});
    EXPECT(result.error == CompanionCoordinatorError::action_authority_failed);
    EXPECT(response == before);
    EXPECT(actions.calls == 1);
    EXPECT(actions.commit_calls == 1);
    EXPECT(actions.applied_calls == 0);

    actions.next_commit_error = CompanionAuthorityError::none;
    result = coordinator.service(
        evidence(), {request.bytes.data(), request.size},
        {response.data(), response.size()});
    EXPECT(result.error == CompanionCoordinatorError::duplicate_without_result);
    EXPECT(actions.calls == 1);
    EXPECT(actions.commit_calls == 1);
    EXPECT(actions.applied_calls == 0);
}

void test_invalid_authority_outputs_fail_closed_atomically() {
    FakeSnapshotAuthority snapshots;
    snapshots.next.snapshot.revision = 0;
    FakeActionAuthority actions;
    CompanionRequestCoordinator coordinator(snapshots, actions);
    EXPECT(coordinator.open_session(evidence(), 13).opened());
    auto request = snapshot_request(13, 1);
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> response{};
    response.fill(0xA5);
    const auto before = response;
    auto result = coordinator.service(
        evidence(), {request.bytes.data(), request.size},
        {response.data(), response.size()});
    EXPECT(result.error == CompanionCoordinatorError::response_rejected);
    EXPECT(result.semantic_error ==
           CompanionSemanticCodecError::invalid_revision);
    EXPECT(response == before);
    EXPECT(snapshots.calls == 1);
    result = coordinator.service(
        evidence(), {request.bytes.data(), request.size},
        {response.data(), response.size()});
    EXPECT(result.error == CompanionCoordinatorError::duplicate_without_result);
    EXPECT(snapshots.calls == 1);

    actions.next_disposition = CompanionActionDisposition::queued;
    request = action_request(
        13, 2, position(CompanionActionKind::start_position_sharing));
    result = coordinator.service(
        evidence(), {request.bytes.data(), request.size},
        {response.data(), response.size()});
    EXPECT(result.error == CompanionCoordinatorError::response_rejected);
    EXPECT(result.semantic_error ==
           CompanionSemanticCodecError::incoherent_result);
    EXPECT(response == before);
    EXPECT(actions.calls == 1);
    EXPECT(actions.commit_calls == 0);
    EXPECT(actions.applied_calls == 0);
    result = coordinator.service(
        evidence(), {request.bytes.data(), request.size},
        {response.data(), response.size()});
    EXPECT(result.error == CompanionCoordinatorError::duplicate_without_result);
    EXPECT(actions.calls == 1);
    EXPECT(actions.commit_calls == 0);
    EXPECT(actions.applied_calls == 0);

    actions.next_disposition = CompanionActionDisposition::queued;
    actions.next_token = 0;
    request = action_request(13, 3, quick(QuickStatusKind::ok));
    result = coordinator.service(
        evidence(), {request.bytes.data(), request.size},
        {response.data(), response.size()});
    EXPECT(result.error == CompanionCoordinatorError::action_authority_failed);
    EXPECT(response == before);
    EXPECT(actions.calls == 2);
    EXPECT(actions.commit_calls == 0);
    EXPECT(actions.applied_calls == 0);
    result = coordinator.service(
        evidence(), {request.bytes.data(), request.size},
        {response.data(), response.size()});
    EXPECT(result.error == CompanionCoordinatorError::duplicate_without_result);
    EXPECT(actions.calls == 2);
    EXPECT(actions.commit_calls == 0);

    snapshots.next.error = CompanionAuthorityError::not_ready;
    request = snapshot_request(13, 4);
    result = coordinator.service(
        evidence(), {request.bytes.data(), request.size},
        {response.data(), response.size()});
    EXPECT(result.error ==
           CompanionCoordinatorError::snapshot_authority_failed);
    EXPECT(response == before);
    EXPECT(snapshots.calls == 2);
    result = coordinator.service(
        evidence(), {request.bytes.data(), request.size},
        {response.data(), response.size()});
    EXPECT(result.error == CompanionCoordinatorError::duplicate_without_result);
    EXPECT(snapshots.calls == 2);
}

void test_stale_and_exhausted_request_ids_fail_closed() {
    FakeSnapshotAuthority first_snapshots;
    FakeActionAuthority first_actions;
    CompanionRequestCoordinator first(first_snapshots, first_actions);
    EXPECT(first.open_session(evidence(), 140).opened());
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> first_response{};
    auto ordinary = snapshot_request(140, 5);
    EXPECT(first.service(
               evidence(), {ordinary.bytes.data(), ordinary.size},
               {first_response.data(), first_response.size()}).responded());
    ordinary = snapshot_request(140, 4);
    auto ordinary_result = first.service(
        evidence(), {ordinary.bytes.data(), ordinary.size},
        {first_response.data(), first_response.size()});
    EXPECT(ordinary_result.session_error ==
           CompanionSessionError::stale_request);
    EXPECT(first_snapshots.calls == 1);
    ordinary = snapshot_request(140, 6);
    EXPECT(first.service(
               evidence(), {ordinary.bytes.data(), ordinary.size},
               {first_response.data(), first_response.size()}).responded());

    FakeSnapshotAuthority snapshots;
    FakeActionAuthority actions;
    CompanionRequestCoordinator coordinator(snapshots, actions);
    EXPECT(coordinator.open_session(evidence(), 14).opened());
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> response{};
    auto request = snapshot_request(14, UINT32_MAX);
    EXPECT(coordinator.service(
               evidence(), {request.bytes.data(), request.size},
               {response.data(), response.size()}).responded());
    EXPECT(snapshots.calls == 1);
    EXPECT(coordinator.service(
               evidence(), {request.bytes.data(), request.size},
               {response.data(), response.size()}).duplicate());
    request = snapshot_request(14, 1);
    const auto exhausted = coordinator.service(
        evidence(), {request.bytes.data(), request.size},
        {response.data(), response.size()});
    EXPECT(exhausted.session_error ==
           CompanionSessionError::request_id_exhausted);
    EXPECT(snapshots.calls == 1);

    auto zero = snapshot_request(14, 2);
    zero.bytes[12] = 0;
    zero.bytes[13] = 0;
    zero.bytes[14] = 0;
    zero.bytes[15] = 0;
    const auto zero_result = coordinator.service(
        evidence(), {zero.bytes.data(), zero.size},
        {response.data(), response.size()});
    EXPECT(zero_result.error ==
           CompanionCoordinatorError::envelope_rejected);
    EXPECT(zero_result.envelope_error ==
           CompanionCodecError::invalid_exchange_id);
    EXPECT(snapshots.calls == 1);
}

void test_close_reopen_clears_cache_and_rejects_old_session_replay() {
    FakeSnapshotAuthority snapshots;
    FakeActionAuthority actions;
    CompanionRequestCoordinator coordinator(snapshots, actions);
    EXPECT(coordinator.open_session(evidence(), 15).opened());
    const auto old_request = action_request(15, 1, quick(QuickStatusKind::ok));
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> response{};
    EXPECT(coordinator.service(
               evidence(), {old_request.bytes.data(), old_request.size},
               {response.data(), response.size()}).responded());
    EXPECT(coordinator.close_session(0xFFFF) ==
           CompanionSessionError::wrong_controller);
    EXPECT(coordinator.open_session(evidence(), 16).error ==
           CompanionSessionError::session_in_use);
    auto result = coordinator.service(
        evidence(), {old_request.bytes.data(), old_request.size},
        {response.data(), response.size()});
    EXPECT(result.duplicate());
    EXPECT(actions.calls == 1);
    EXPECT(coordinator.close_session(evidence().controller_binding) ==
           CompanionSessionError::none);
    EXPECT(coordinator.open_session(evidence(), 16).opened());
    result = coordinator.service(
        evidence(), {old_request.bytes.data(), old_request.size},
        {response.data(), response.size()});
    EXPECT(result.session_error == CompanionSessionError::wrong_session);
    EXPECT(actions.calls == 1);

    const auto new_request = action_request(16, 1, quick(QuickStatusKind::ok));
    result = coordinator.service(
        evidence(), {new_request.bytes.data(), new_request.size},
        {response.data(), response.size()});
    EXPECT(result.responded());
    EXPECT(!result.duplicate());
    EXPECT(actions.calls == 2);
}

void test_in_place_and_partially_overlapping_buffers_preserve_cache() {
    FakeSnapshotAuthority snapshots;
    FakeActionAuthority actions;
    CompanionRequestCoordinator coordinator(snapshots, actions);
    EXPECT(coordinator.open_session(evidence(), 18).opened());
    const auto request = action_request(18, 1, quick(QuickStatusKind::ok));
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> original = request.bytes;
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> shared = request.bytes;
    auto result = coordinator.service(
        evidence(), {shared.data(), request.size},
        {shared.data(), shared.size()});
    EXPECT(result.responded());
    EXPECT(actions.calls == 1);
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> replay{};
    result = coordinator.service(
        evidence(), {original.data(), request.size},
        {replay.data(), replay.size()});
    EXPECT(result.duplicate());
    EXPECT(actions.calls == 1);

    EXPECT(coordinator.close_session(evidence().controller_binding) ==
           CompanionSessionError::none);
    EXPECT(coordinator.open_session(evidence(), 19).opened());
    const auto overlap_request = action_request(
        19, 1, quick(QuickStatusKind::available_to_help));
    std::array<std::uint8_t, 180> overlap{};
    for (std::size_t index = 0; index < overlap_request.size; ++index) {
        overlap[index] = overlap_request.bytes[index];
    }
    result = coordinator.service(
        evidence(), {overlap.data(), overlap_request.size},
        {overlap.data() + 10, overlap.size() - 10});
    EXPECT(result.responded());
    EXPECT(actions.calls == 2);
    result = coordinator.service(
        evidence(),
        {overlap_request.bytes.data(), overlap_request.size},
        {replay.data(), replay.size()});
    EXPECT(result.duplicate());
    EXPECT(actions.calls == 2);
}

void test_component_is_fixed_memory_and_public_status_is_redacted() {
    static_assert(std::is_trivially_copyable_v<CompanionCoordinatorResult>);
    static_assert(sizeof(CompanionCoordinatorResult) <= 32);
    static_assert(kCompanionMaxRequestRecordBytes == 40);
    static_assert(kCompanionMaxResponseRecordBytes == 52);
    static_assert(sizeof(CompanionRequestCoordinator) <= 224);
    FakeSnapshotAuthority snapshots;
    FakeActionAuthority actions;
    CompanionRequestCoordinator coordinator(snapshots, actions);
    EXPECT(coordinator.open_session(
               evidence(0x1122334455667788ULL), 17).opened());
    const auto status = coordinator.session_status();
    EXPECT(status.active);
    EXPECT(status.session_nonce == 17);
    EXPECT(status.last_request_id == 0);
}

}  // namespace

int main() {
    test_snapshot_request_produces_exact_correlated_snapshot();
    test_quick_status_produces_exact_queued_result_not_delivery();
    test_all_typed_actions_dispatch_without_reinterpretation();
    test_queue_full_and_stale_alert_are_typed_rejections();
    test_malformed_wrong_kind_and_semantic_errors_touch_nothing();
    test_output_capacity_is_checked_before_authority_mutation();
    test_security_controller_and_session_are_enforced();
    test_exact_duplicate_replays_exact_response_without_reapply();
    test_same_exchange_with_any_byte_difference_is_conflict();
    test_authority_failure_consumes_id_without_result_or_reapply();
    test_commit_failure_is_terminal_and_guarantees_no_mutation();
    test_invalid_authority_outputs_fail_closed_atomically();
    test_stale_and_exhausted_request_ids_fail_closed();
    test_close_reopen_clears_cache_and_rejects_old_session_replay();
    test_in_place_and_partially_overlapping_buffers_preserve_cache();
    test_component_is_fixed_memory_and_public_status_is_redacted();

    if (failures != 0) {
        std::cerr << failures
                  << " companion request coordinator assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 16 companion request coordinator scenario groups\n";
    return EXIT_SUCCESS;
}
