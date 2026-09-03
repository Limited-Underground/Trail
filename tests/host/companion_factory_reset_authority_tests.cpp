#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <array>

#include "opentrail/companion_factory_reset_authority.hpp"

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
#define EXPECT(value) expect((value), #value, __LINE__)

class Marker final : public DeviceFactoryResetMarkerPort {
public:
    DeviceFactoryResetMarkerState state{
        DeviceFactoryResetMarkerState::absent};
    DeviceFactoryResetPortError commit_error{
        DeviceFactoryResetPortError::none};
    bool apply_on_error{false};
    std::uint64_t receipt{0};
    std::uint32_t commits{0};

    DeviceFactoryResetMarkerSnapshot load() override {
        return {DeviceFactoryResetPortError::none, state, receipt};
    }
    DeviceFactoryResetMarkerSnapshot commit_intent_and_readback(
        std::uint64_t requested_receipt) override {
        ++commits;
        if (commit_error != DeviceFactoryResetPortError::none) {
            if (apply_on_error) {
                state = DeviceFactoryResetMarkerState::intent_committed;
                receipt = requested_receipt;
            }
            return {commit_error, state, receipt};
        }
        state = DeviceFactoryResetMarkerState::intent_committed;
        receipt = requested_receipt;
        return {DeviceFactoryResetPortError::none, state, receipt};
    }
    DeviceFactoryResetMarkerSnapshot complete_cleanup_and_readback() override {
        state = receipt == 0
                    ? DeviceFactoryResetMarkerState::absent
                    : DeviceFactoryResetMarkerState::receipt_pending;
        return {DeviceFactoryResetPortError::none, state, receipt};
    }
    DeviceFactoryResetReceiptConsumeSnapshot
    consume_completion_receipt_and_readback() override {
        const auto consumed = receipt;
        state = DeviceFactoryResetMarkerState::absent;
        receipt = 0;
        return {DeviceFactoryResetPortError::none, consumed,
                consumed != 0};
    }
};

class UserDomain final : public DeviceFactoryResetUserDomainPort {
public:
    bool absent{false};
    std::uint32_t erases{0};
    DeviceFactoryResetAbsenceSnapshot inspect_absence() override {
        return {DeviceFactoryResetPortError::none, absent};
    }
    DeviceFactoryResetAbsenceSnapshot
    erase_all_and_verify_absent() override {
        ++erases;
        absent = true;
        return {DeviceFactoryResetPortError::none, true};
    }
};

class BondDomain final : public DeviceFactoryResetBondDomainPort {
public:
    bool empty{false};
    std::uint32_t erases{0};
    DeviceFactoryResetAbsenceSnapshot inspect_empty() override {
        return {DeviceFactoryResetPortError::none, empty};
    }
    DeviceFactoryResetAbsenceSnapshot
    erase_all_and_verify_empty() override {
        ++erases;
        empty = true;
        return {DeviceFactoryResetPortError::none, true};
    }
};

CompanionActionRequest reset_request() {
    return {CompanionActionKind::factory_reset, QuickStatusKind::ok,
            UINT64_C(0x1122334455667788)};
}

class SnapshotAuthority final : public CompanionSnapshotAuthority {
public:
    CompanionSnapshotAuthorityResult read_snapshot() override {
        return {CompanionAuthorityError::not_ready, {}};
    }
};

struct EncodedResetRequest {
    std::array<std::uint8_t, kCompanionMaxRequestRecordBytes> bytes{};
    std::size_t size{0};
};

EncodedResetRequest encoded_reset(std::uint32_t nonce,
                                  std::uint32_t exchange_id) {
    CompanionFragment fragment{};
    fragment.kind = CompanionFrameKind::action_request;
    fragment.session_nonce = nonce;
    fragment.exchange_id = exchange_id;
    const auto semantic = encode_companion_action_request(
        reset_request(),
        {fragment.payload.data(), fragment.payload.size()});
    fragment.payload_bytes =
        static_cast<std::uint16_t>(semantic.encoded_bytes);
    EncodedResetRequest encoded{};
    const auto envelope = encode_companion_fragment(
        fragment, {encoded.bytes.data(), encoded.bytes.size()});
    encoded.size = envelope.encoded_bytes;
    return encoded;
}

void test_admission_commits_only_durable_intent() {
    Marker marker;
    UserDomain user;
    BondDomain bonds;
    DeviceFactoryResetExecutor executor{marker, user, bonds};
    EXPECT(executor.restore().accepted());
    CompanionFactoryResetActionAuthority authority{executor};

    const auto prepared = authority.prepare_action(reset_request());
    EXPECT(prepared.ready());
    EXPECT(prepared.disposition == CompanionActionDisposition::admitted);
    EXPECT(prepared.reject_reason == CompanionActionRejectReason::none);
    EXPECT(prepared.operation_token != 0);
    EXPECT(marker.commits == 0);
    EXPECT(authority.commit_action(reset_request(), prepared) ==
           CompanionAuthorityError::none);
    EXPECT(marker.commits == 1);
    EXPECT(marker.receipt == UINT64_C(0x1122334455667788));
    EXPECT(user.erases == 0);
    EXPECT(bonds.erases == 0);
    EXPECT(executor.status().phase ==
           DeviceFactoryResetPhase::cleanup_required);
    const auto status = authority.status();
    EXPECT(status.phase ==
           CompanionFactoryResetAuthorityPhase::intent_committed);
    EXPECT(status.reset_intent_committed);
    EXPECT(status.protected_operations_blocked);
    EXPECT(authority.prepare_action(reset_request()).error ==
           CompanionAuthorityError::not_ready);
}

void test_other_actions_are_rejected_without_mutation() {
    Marker marker;
    UserDomain user;
    BondDomain bonds;
    DeviceFactoryResetExecutor executor{marker, user, bonds};
    EXPECT(executor.restore().accepted());
    CompanionFactoryResetActionAuthority authority{executor};
    const CompanionActionRequest quick{
        CompanionActionKind::quick_status, QuickStatusKind::ok, 0};
    const auto prepared = authority.prepare_action(quick);
    EXPECT(prepared.ready());
    EXPECT(prepared.disposition == CompanionActionDisposition::rejected);
    EXPECT(prepared.reject_reason ==
           CompanionActionRejectReason::unsupported_action);
    EXPECT(authority.commit_action(quick, prepared) ==
           CompanionAuthorityError::none);
    EXPECT(marker.commits == 0);
    EXPECT(executor.status().phase ==
           DeviceFactoryResetPhase::idle_old_state);
    EXPECT(authority.status().phase ==
           CompanionFactoryResetAuthorityPhase::idle);
}

void test_known_no_change_does_not_claim_admission() {
    Marker marker;
    marker.commit_error = DeviceFactoryResetPortError::known_no_change;
    UserDomain user;
    BondDomain bonds;
    DeviceFactoryResetExecutor executor{marker, user, bonds};
    EXPECT(executor.restore().accepted());
    CompanionFactoryResetActionAuthority authority{executor};
    const auto prepared = authority.prepare_action(reset_request());
    EXPECT(authority.commit_action(reset_request(), prepared) ==
           CompanionAuthorityError::failed);
    EXPECT(marker.state == DeviceFactoryResetMarkerState::absent);
    EXPECT(executor.status().phase ==
           DeviceFactoryResetPhase::idle_old_state);
    EXPECT(authority.status().phase ==
           CompanionFactoryResetAuthorityPhase::idle);
}

void test_uncertain_commit_blocks_and_emits_no_success() {
    Marker marker;
    marker.commit_error = DeviceFactoryResetPortError::uncertain;
    marker.apply_on_error = true;
    UserDomain user;
    BondDomain bonds;
    DeviceFactoryResetExecutor executor{marker, user, bonds};
    EXPECT(executor.restore().accepted());
    CompanionFactoryResetActionAuthority authority{executor};
    const auto prepared = authority.prepare_action(reset_request());
    EXPECT(authority.commit_action(reset_request(), prepared) ==
           CompanionAuthorityError::outcome_unknown);
    EXPECT(marker.state ==
           DeviceFactoryResetMarkerState::intent_committed);
    EXPECT(executor.status().phase ==
           DeviceFactoryResetPhase::reconciliation_required);
    EXPECT(authority.status().phase ==
           CompanionFactoryResetAuthorityPhase::outcome_unknown);
    EXPECT(authority.status().protected_operations_blocked);
    EXPECT(!authority.status().reset_intent_committed);
}

void test_token_mismatch_fails_without_mutation() {
    Marker marker;
    UserDomain user;
    BondDomain bonds;
    DeviceFactoryResetExecutor executor{marker, user, bonds};
    EXPECT(executor.restore().accepted());
    CompanionFactoryResetActionAuthority authority{executor};
    auto prepared = authority.prepare_action(reset_request());
    ++prepared.operation_token;
    EXPECT(authority.commit_action(reset_request(), prepared) ==
           CompanionAuthorityError::failed);
    EXPECT(marker.commits == 0);
    EXPECT(executor.status().phase ==
           DeviceFactoryResetPhase::idle_old_state);
    EXPECT(authority.status().phase ==
           CompanionFactoryResetAuthorityPhase::idle);
}

void test_zero_or_changed_receipt_never_commits() {
    Marker zero_marker;
    UserDomain zero_user;
    BondDomain zero_bonds;
    DeviceFactoryResetExecutor zero_executor{
        zero_marker, zero_user, zero_bonds};
    EXPECT(zero_executor.restore().accepted());
    CompanionFactoryResetActionAuthority zero_authority{zero_executor};
    const CompanionActionRequest zero{
        CompanionActionKind::factory_reset, QuickStatusKind::ok, 0};
    const auto denied = zero_authority.prepare_action(zero);
    EXPECT(denied.ready());
    EXPECT(denied.disposition == CompanionActionDisposition::rejected);
    EXPECT(zero_authority.commit_action(zero, denied) ==
           CompanionAuthorityError::none);
    EXPECT(zero_marker.commits == 0);

    Marker changed_marker;
    UserDomain changed_user;
    BondDomain changed_bonds;
    DeviceFactoryResetExecutor changed_executor{
        changed_marker, changed_user, changed_bonds};
    EXPECT(changed_executor.restore().accepted());
    CompanionFactoryResetActionAuthority changed_authority{changed_executor};
    const auto prepared = changed_authority.prepare_action(reset_request());
    auto changed = reset_request();
    ++changed.critical_alert_id;
    EXPECT(changed_authority.commit_action(changed, prepared) ==
           CompanionAuthorityError::failed);
    EXPECT(changed_marker.commits == 0);
}

void test_protected_coordinator_admits_once_and_replays_only_acceptance() {
    Marker marker;
    UserDomain user;
    BondDomain bonds;
    DeviceFactoryResetExecutor executor{marker, user, bonds};
    EXPECT(executor.restore().accepted());
    CompanionFactoryResetActionAuthority authority{executor};
    SnapshotAuthority snapshots;
    CompanionRequestCoordinator coordinator{snapshots, authority};

    EXPECT(coordinator.open_session(
               {7, true, true, false}, 91).error ==
           CompanionSessionError::controller_not_authorized);
    EXPECT(coordinator.open_session(
               {7, true, true, true}, 91).opened());

    const auto request = encoded_reset(91, 1);
    std::array<std::uint8_t, kCompanionMaxResponseRecordBytes> response{};
    auto result = coordinator.service(
        {7, true, true, true},
        {request.bytes.data(), request.size},
        {response.data(), response.size()});
    EXPECT(result.responded());
    EXPECT(!result.duplicate());
    EXPECT(marker.commits == 1);
    EXPECT(user.erases == 0);
    EXPECT(bonds.erases == 0);
    const auto envelope = decode_companion_fragment(
        {response.data(), result.response_bytes});
    EXPECT(envelope.decoded());
    EXPECT(envelope.fragment.kind == CompanionFrameKind::action_result);
    const auto action = decode_companion_action_result(
        {envelope.fragment.payload.data(),
         envelope.fragment.payload_bytes});
    EXPECT(action.decoded());
    EXPECT(action.value.kind == CompanionActionKind::factory_reset);
    EXPECT(action.value.disposition ==
           CompanionActionDisposition::admitted);

    result = coordinator.service(
        {7, true, true, true},
        {request.bytes.data(), request.size},
        {response.data(), response.size()});
    EXPECT(result.responded());
    EXPECT(result.duplicate());
    EXPECT(marker.commits == 1);
}

}  // namespace

int main() {
    test_admission_commits_only_durable_intent();
    test_other_actions_are_rejected_without_mutation();
    test_known_no_change_does_not_claim_admission();
    test_uncertain_commit_blocks_and_emits_no_success();
    test_token_mismatch_fails_without_mutation();
    test_zero_or_changed_receipt_never_commits();
    test_protected_coordinator_admits_once_and_replays_only_acceptance();

    if (failures != 0) {
        std::cerr << failures
                  << " factory-reset authority assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 7 protected factory-reset authority groups\n";
    return EXIT_SUCCESS;
}
