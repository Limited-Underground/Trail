#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/group_access_controller.hpp"

namespace {

using opentrail::identity::AdministratorPromotionConfirmation;
using opentrail::identity::GroupAccessController;
using opentrail::identity::GroupAccessError;
using opentrail::identity::GroupMemberState;
using opentrail::identity::GroupRole;
using opentrail::identity::IdentityFingerprint;
using opentrail::identity::JoinAuthenticationEvidence;
using opentrail::identity::RekeyEvidence;
using opentrail::identity::kMaximumInvitationLifetimeMs;
using opentrail::identity::kMaximumInvitations;

constexpr JoinAuthenticationEvidence kAuthenticatedJoin{
    true, true, true, true};
constexpr AdministratorPromotionConfirmation kConfirmedPromotion{
    true, true, true};
constexpr RekeyEvidence kAuthenticatedRekey{true, true, true};
int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

IdentityFingerprint fingerprint(std::uint8_t marker) {
    IdentityFingerprint result{};
    result[0] = marker;
    result[31] = static_cast<std::uint8_t>(marker ^ 0xA5U);
    return result;
}

GroupAccessController initialized_group() {
    GroupAccessController controller;
    EXPECT(controller.initialize(0x1001, 1, fingerprint(1), 0x2001).accepted());
    return controller;
}

void join_member(
    GroupAccessController& controller,
    std::uint64_t invitation_id,
    const IdentityFingerprint& member,
    std::uint64_t alias,
    GroupRole role = GroupRole::member) {
    EXPECT(controller.issue_invitation(
               fingerprint(1), invitation_id, role, 0, 60000)
               .accepted());
    EXPECT(controller.begin_join(invitation_id, member, 1).accepted());
    EXPECT(controller.complete_join(
               invitation_id, alias, kAuthenticatedJoin, 2)
               .accepted());
}

void test_initialization_and_owner_authority() {
    GroupAccessController controller;
    EXPECT(controller.initialize(0, 1, fingerprint(1), 1).error ==
           GroupAccessError::invalid_group);
    EXPECT(controller.initialize(1, 0, fingerprint(1), 1).error ==
           GroupAccessError::invalid_epoch);
    EXPECT(controller.initialize(1, 1, {}, 1).error ==
           GroupAccessError::invalid_identity);
    EXPECT(controller.initialize(1, 1, fingerprint(1), 0).error ==
           GroupAccessError::invalid_alias);
    EXPECT(controller.initialize(1, 1, fingerprint(1), 10).accepted());
    EXPECT(controller.member_can_send(fingerprint(1)));
    const auto status = controller.status(0);
    EXPECT(status.current_administrators == 1);
    EXPECT(!status.recovery_ready);
}

void test_invitation_authority_role_lifetime_and_capacity() {
    auto controller = initialized_group();
    EXPECT(controller.issue_invitation(
               fingerprint(9), 1, GroupRole::member, 0, 1000)
               .error == GroupAccessError::not_authorized);
    EXPECT(controller.issue_invitation(
               fingerprint(1), 1, GroupRole::administrator, 0, 1000)
               .error == GroupAccessError::invalid_role);
    EXPECT(controller.issue_invitation(
               fingerprint(1), 1, GroupRole::member, 0, 0)
               .error == GroupAccessError::invalid_lifetime);
    EXPECT(controller.issue_invitation(
               fingerprint(1), 1, GroupRole::member, 0,
               kMaximumInvitationLifetimeMs + 1)
               .error == GroupAccessError::invalid_lifetime);

    for (std::uint64_t id = 1; id <= kMaximumInvitations; ++id) {
        EXPECT(controller.issue_invitation(
                   fingerprint(1), id, GroupRole::member, 0, 1000)
                   .accepted());
    }
    EXPECT(controller.issue_invitation(
               fingerprint(1), 99, GroupRole::member, 0, 1000)
               .error == GroupAccessError::capacity_full);
    EXPECT(controller.issue_invitation(
               fingerprint(1), 1, GroupRole::member, 0, 1000)
               .error == GroupAccessError::duplicate_invitation);
    controller.service(1000);
    EXPECT(controller.issue_invitation(
               fingerprint(1), 99, GroupRole::member, 1000, 1000)
               .accepted());
}

void test_single_use_join_requires_complete_authentication() {
    auto controller = initialized_group();
    EXPECT(controller.issue_invitation(
               fingerprint(1), 10, GroupRole::repeater, 0, 1000)
               .accepted());
    EXPECT(controller.begin_join(10, fingerprint(2), 1).accepted());
    EXPECT(controller.begin_join(10, fingerprint(3), 2).error ==
           GroupAccessError::invitation_unavailable);

    auto incomplete = kAuthenticatedJoin;
    incomplete.human_code_confirmed = false;
    EXPECT(controller.complete_join(10, 0x2002, incomplete, 3).error ==
           GroupAccessError::authentication_failed);
    EXPECT(controller.complete_join(10, 0x2002, kAuthenticatedJoin, 4).error ==
           GroupAccessError::join_not_found);
    EXPECT(!controller.find_member(fingerprint(2)).found);
}

void test_identity_cannot_hold_multiple_pending_joins() {
    auto controller = initialized_group();
    EXPECT(controller.issue_invitation(
               fingerprint(1), 15, GroupRole::member, 0, 1000)
               .accepted());
    EXPECT(controller.issue_invitation(
               fingerprint(1), 16, GroupRole::member, 0, 1000)
               .accepted());
    EXPECT(controller.begin_join(15, fingerprint(2), 1).accepted());
    EXPECT(controller.begin_join(16, fingerprint(2), 2).error ==
           GroupAccessError::join_already_pending);
    EXPECT(controller.status(2).pending_joins == 1);
    EXPECT(controller.status(2).active_invitations == 1);
}

void test_pending_joins_cannot_overbook_member_roster() {
    auto controller = initialized_group();
    for (std::uint8_t marker = 2; marker <= 14; ++marker) {
        const auto now_ms = static_cast<std::uint64_t>(marker) * 10U;
        const auto invitation_id = 100U + marker;
        EXPECT(controller.issue_invitation(
                   fingerprint(1),
                   invitation_id,
                   GroupRole::member,
                   now_ms,
                   5)
                   .accepted());
        EXPECT(controller.begin_join(
                   invitation_id, fingerprint(marker), now_ms + 1)
                   .accepted());
        EXPECT(controller.complete_join(
                   invitation_id,
                   0x3000U + marker,
                   kAuthenticatedJoin,
                   now_ms + 2)
                   .accepted());
        controller.service(now_ms + 5);
    }
    EXPECT(controller.status(999).member_records == 14);

    for (std::uint64_t id = 201; id <= 203; ++id) {
        EXPECT(controller.issue_invitation(
                   fingerprint(1), id, GroupRole::member, 1000, 1000)
                   .accepted());
    }
    EXPECT(controller.begin_join(201, fingerprint(15), 1001).accepted());
    EXPECT(controller.begin_join(202, fingerprint(16), 1001).accepted());
    EXPECT(controller.begin_join(203, fingerprint(17), 1001).error ==
           GroupAccessError::capacity_full);
    EXPECT(controller.status(1001).pending_joins == 2);
}

void test_successful_join_and_alias_collision_recovery() {
    auto controller = initialized_group();
    EXPECT(controller.issue_invitation(
               fingerprint(1), 11, GroupRole::member, 0, 1000)
               .accepted());
    EXPECT(controller.begin_join(11, fingerprint(2), 1).accepted());
    EXPECT(controller.complete_join(11, 0x2001, kAuthenticatedJoin, 2).error ==
           GroupAccessError::alias_collision);
    EXPECT(controller.complete_join(11, 0x2002, kAuthenticatedJoin, 3).accepted());
    const auto member = controller.find_member(fingerprint(2));
    EXPECT(member.found);
    EXPECT(member.member.role == GroupRole::member);
    EXPECT(member.member.state == GroupMemberState::active);
    EXPECT(controller.member_can_send(fingerprint(2)));
}

void test_invitation_expiry_and_cancellation() {
    auto controller = initialized_group();
    EXPECT(controller.issue_invitation(
               fingerprint(1), 12, GroupRole::member, 100, 100)
               .accepted());
    EXPECT(controller.begin_join(12, fingerprint(2), 200).error ==
           GroupAccessError::invitation_expired);

    EXPECT(controller.issue_invitation(
               fingerprint(1), 13, GroupRole::member, 200, 100)
               .accepted());
    EXPECT(controller.cancel_invitation(fingerprint(9), 13, 201).error ==
           GroupAccessError::not_authorized);
    EXPECT(controller.cancel_invitation(fingerprint(1), 13, 201).accepted());
    EXPECT(controller.begin_join(13, fingerprint(2), 202).error ==
           GroupAccessError::invitation_unavailable);

    EXPECT(controller.issue_invitation(
               fingerprint(1), 14, GroupRole::member, 300, 100)
               .accepted());
    EXPECT(controller.begin_join(14, fingerprint(2), 301).accepted());
    EXPECT(controller.complete_join(14, 0x2002, kAuthenticatedJoin, 400).error ==
           GroupAccessError::invitation_expired);
}

void test_administrator_promotion_is_separate_and_confirmed() {
    auto controller = initialized_group();
    join_member(controller, 20, fingerprint(2), 0x2002);
    EXPECT(controller.promote_to_administrator(
               fingerprint(9), fingerprint(2), kConfirmedPromotion)
               .error == GroupAccessError::not_authorized);

    auto incomplete = kConfirmedPromotion;
    incomplete.recovery_responsibility_acknowledged = false;
    EXPECT(controller.promote_to_administrator(
               fingerprint(1), fingerprint(2), incomplete)
               .error == GroupAccessError::confirmation_required);
    EXPECT(controller.promote_to_administrator(
               fingerprint(1), fingerprint(2), kConfirmedPromotion)
               .accepted());
    EXPECT(controller.status(3).current_administrators == 2);
    EXPECT(controller.status(3).recovery_ready);
}

void test_revocation_advances_epoch_and_blocks_retained_members() {
    auto controller = initialized_group();
    join_member(controller, 30, fingerprint(2), 0x2002);
    EXPECT(controller.issue_invitation(
               fingerprint(1), 31, GroupRole::member, 10, 1000)
               .accepted());
    EXPECT(controller.issue_invitation(
               fingerprint(1), 32, GroupRole::member, 10, 1000)
               .accepted());
    EXPECT(controller.begin_join(32, fingerprint(3), 11).accepted());

    EXPECT(controller.revoke_member(fingerprint(1), fingerprint(2), 1).error ==
           GroupAccessError::invalid_epoch);
    EXPECT(controller.revoke_member(fingerprint(1), fingerprint(2), 2).accepted());
    EXPECT(controller.status(12).group_epoch == 2);
    EXPECT(controller.status(12).active_invitations == 0);
    EXPECT(controller.status(12).pending_joins == 0);
    EXPECT(!controller.member_can_send(fingerprint(1)));
    EXPECT(!controller.member_can_send(fingerprint(2)));
    EXPECT(controller.find_member(fingerprint(2)).member.state ==
           GroupMemberState::revoked);
    EXPECT(controller.begin_join(31, fingerprint(3), 12).error ==
           GroupAccessError::invitation_unavailable);
}

void test_authenticated_rekey_restores_only_retained_member() {
    auto controller = initialized_group();
    join_member(controller, 40, fingerprint(2), 0x2002);
    EXPECT(controller.revoke_member(fingerprint(1), fingerprint(2), 2).accepted());

    auto incomplete = kAuthenticatedRekey;
    incomplete.key_material_installed = false;
    EXPECT(controller.apply_rekey(fingerprint(1), 2, 0x3001, incomplete).error ==
           GroupAccessError::authentication_failed);
    EXPECT(controller.apply_rekey(
               fingerprint(1), 1, 0x3001, kAuthenticatedRekey)
               .error == GroupAccessError::invalid_epoch);
    EXPECT(controller.apply_rekey(
               fingerprint(1), 2, 0x3001, kAuthenticatedRekey)
               .accepted());
    EXPECT(controller.member_can_send(fingerprint(1)));
    EXPECT(controller.apply_rekey(
               fingerprint(2), 2, 0x3002, kAuthenticatedRekey)
               .error == GroupAccessError::member_revoked);
    EXPECT(controller.issue_invitation(
               fingerprint(1), 41, GroupRole::member, 10, 1000)
               .accepted());
    EXPECT(controller.begin_join(41, fingerprint(2), 11).error ==
           GroupAccessError::member_revoked);
}

void test_last_administrator_protection_and_lost_admin_recovery() {
    auto controller = initialized_group();
    EXPECT(controller.revoke_member(fingerprint(1), fingerprint(1), 2).error ==
           GroupAccessError::last_administrator);

    join_member(controller, 50, fingerprint(2), 0x2002);
    EXPECT(controller.promote_to_administrator(
               fingerprint(1), fingerprint(2), kConfirmedPromotion)
               .accepted());
    EXPECT(controller.revoke_member(fingerprint(2), fingerprint(1), 2).accepted());
    EXPECT(!controller.member_can_send(fingerprint(2)));
    EXPECT(controller.apply_rekey(
               fingerprint(2), 2, 0x3002, kAuthenticatedRekey)
               .accepted());
    EXPECT(controller.member_can_send(fingerprint(2)));
    EXPECT(controller.status(60).current_administrators == 1);
    EXPECT(!controller.status(60).recovery_ready);
}

void test_non_administrator_cannot_invite_or_revoke() {
    auto controller = initialized_group();
    join_member(controller, 60, fingerprint(2), 0x2002);
    EXPECT(controller.issue_invitation(
               fingerprint(2), 61, GroupRole::member, 10, 1000)
               .error == GroupAccessError::not_authorized);
    EXPECT(controller.revoke_member(fingerprint(2), fingerprint(1), 2).error ==
           GroupAccessError::not_authorized);
}

}  // namespace

int main() {
    test_initialization_and_owner_authority();
    test_invitation_authority_role_lifetime_and_capacity();
    test_single_use_join_requires_complete_authentication();
    test_identity_cannot_hold_multiple_pending_joins();
    test_pending_joins_cannot_overbook_member_roster();
    test_successful_join_and_alias_collision_recovery();
    test_invitation_expiry_and_cancellation();
    test_administrator_promotion_is_separate_and_confirmed();
    test_revocation_advances_epoch_and_blocks_retained_members();
    test_authenticated_rekey_restores_only_retained_member();
    test_last_administrator_protection_and_lost_admin_recovery();
    test_non_administrator_cannot_invite_or_revoke();

    if (failures != 0) {
        std::cerr << failures << " group access assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 12 group access lifecycle scenario groups\n";
    return EXIT_SUCCESS;
}
