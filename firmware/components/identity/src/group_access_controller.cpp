#include "opentrail/group_access_controller.hpp"

#include <algorithm>
#include <limits>

namespace opentrail::identity {
namespace {

constexpr std::size_t kNotFound = std::numeric_limits<std::size_t>::max();

}  // namespace

bool GroupAccessController::all_zero(
    const IdentityFingerprint& fingerprint) {
    return std::all_of(
        fingerprint.begin(), fingerprint.end(), [](std::uint8_t byte) {
            return byte == 0;
        });
}

bool GroupAccessController::invitable_role(GroupRole role) {
    return role == GroupRole::member || role == GroupRole::repeater;
}

std::uint64_t GroupAccessController::saturating_add(
    std::uint64_t value,
    std::uint32_t increment) {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    return value > maximum - increment ? maximum : value + increment;
}

std::size_t GroupAccessController::find_member_index(
    const IdentityFingerprint& fingerprint) const {
    for (std::size_t index = 0; index < members_.size(); ++index) {
        if (members_[index].used &&
            members_[index].member.fingerprint == fingerprint) {
            return index;
        }
    }
    return kNotFound;
}

std::size_t GroupAccessController::find_invitation_index(
    std::uint64_t invitation_id) const {
    for (std::size_t index = 0; index < invitations_.size(); ++index) {
        if (invitations_[index].used &&
            invitations_[index].invitation_id == invitation_id) {
            return index;
        }
    }
    return kNotFound;
}

std::size_t GroupAccessController::find_pending_index(
    std::uint64_t invitation_id) const {
    for (std::size_t index = 0; index < pending_joins_.size(); ++index) {
        if (pending_joins_[index].used &&
            pending_joins_[index].invitation_id == invitation_id) {
            return index;
        }
    }
    return kNotFound;
}

std::size_t GroupAccessController::first_free_member() const {
    for (std::size_t index = 0; index < members_.size(); ++index) {
        if (!members_[index].used) {
            return index;
        }
    }
    return kNotFound;
}

std::size_t GroupAccessController::first_free_invitation() const {
    for (std::size_t index = 0; index < invitations_.size(); ++index) {
        if (!invitations_[index].used) {
            return index;
        }
    }
    return kNotFound;
}

std::size_t GroupAccessController::first_free_pending() const {
    for (std::size_t index = 0; index < pending_joins_.size(); ++index) {
        if (!pending_joins_[index].used) {
            return index;
        }
    }
    return kNotFound;
}

bool GroupAccessController::authorized_administrator(
    const IdentityFingerprint& fingerprint) const {
    const auto index = find_member_index(fingerprint);
    if (index == kNotFound) {
        return false;
    }
    const auto& member = members_[index].member;
    return member.role == GroupRole::administrator &&
           member.state == GroupMemberState::active &&
           member.group_epoch == group_epoch_;
}

bool GroupAccessController::alias_in_use(std::uint64_t alias) const {
    if (alias == 0) {
        return false;
    }
    return std::any_of(
        members_.begin(), members_.end(), [alias](const MemberRecord& record) {
            return record.used && record.member.network_alias == alias &&
                   record.member.state != GroupMemberState::revoked;
        });
}

std::size_t GroupAccessController::current_administrator_count() const {
    return static_cast<std::size_t>(std::count_if(
        members_.begin(), members_.end(), [this](const MemberRecord& record) {
            return record.used &&
                   record.member.role == GroupRole::administrator &&
                   record.member.state == GroupMemberState::active &&
                   record.member.group_epoch == group_epoch_;
        }));
}

GroupAccessResult GroupAccessController::initialize(
    std::uint64_t group_id,
    std::uint32_t initial_epoch,
    const IdentityFingerprint& owner_fingerprint,
    std::uint64_t owner_alias) {
    if (initialized_) {
        return {GroupAccessError::invalid_transition};
    }
    if (group_id == 0) {
        return {GroupAccessError::invalid_group};
    }
    if (initial_epoch == 0) {
        return {GroupAccessError::invalid_epoch};
    }
    if (all_zero(owner_fingerprint)) {
        return {GroupAccessError::invalid_identity};
    }
    if (owner_alias == 0) {
        return {GroupAccessError::invalid_alias};
    }

    initialized_ = true;
    group_id_ = group_id;
    group_epoch_ = initial_epoch;
    members_[0].used = true;
    members_[0].member = {
        owner_fingerprint,
        GroupRole::administrator,
        GroupMemberState::active,
        owner_alias,
        initial_epoch,
    };
    return {};
}

GroupAccessResult GroupAccessController::issue_invitation(
    const IdentityFingerprint& administrator,
    std::uint64_t invitation_id,
    GroupRole role,
    std::uint64_t now_ms,
    std::uint32_t lifetime_ms) {
    if (!initialized_) {
        return {GroupAccessError::invalid_transition};
    }
    service(now_ms);
    if (!authorized_administrator(administrator)) {
        return {GroupAccessError::not_authorized};
    }
    if (invitation_id == 0) {
        return {GroupAccessError::invalid_invitation};
    }
    if (!invitable_role(role)) {
        return {GroupAccessError::invalid_role};
    }
    if (lifetime_ms == 0 || lifetime_ms > kMaximumInvitationLifetimeMs) {
        return {GroupAccessError::invalid_lifetime};
    }
    if (find_invitation_index(invitation_id) != kNotFound) {
        return {GroupAccessError::duplicate_invitation};
    }
    const auto slot = first_free_invitation();
    if (slot == kNotFound) {
        return {GroupAccessError::capacity_full};
    }

    invitations_[slot] = {
        invitation_id,
        saturating_add(now_ms, lifetime_ms),
        group_epoch_,
        role,
        false,
        false,
        true,
    };
    return {};
}

GroupAccessResult GroupAccessController::cancel_invitation(
    const IdentityFingerprint& administrator,
    std::uint64_t invitation_id,
    std::uint64_t now_ms) {
    if (!initialized_) {
        return {GroupAccessError::invalid_transition};
    }
    service(now_ms);
    if (!authorized_administrator(administrator)) {
        return {GroupAccessError::not_authorized};
    }
    const auto slot = find_invitation_index(invitation_id);
    if (slot == kNotFound) {
        return {GroupAccessError::invitation_not_found};
    }
    auto& invitation = invitations_[slot];
    if (invitation.cancelled || invitation.consumed) {
        return {GroupAccessError::invitation_unavailable};
    }
    invitation.cancelled = true;
    return {};
}

GroupAccessResult GroupAccessController::begin_join(
    std::uint64_t invitation_id,
    const IdentityFingerprint& joining_fingerprint,
    std::uint64_t now_ms) {
    if (!initialized_) {
        return {GroupAccessError::invalid_transition};
    }
    const auto expired_slot = find_invitation_index(invitation_id);
    if (expired_slot != kNotFound &&
        now_ms >= invitations_[expired_slot].expires_at_ms) {
        invitations_[expired_slot] = {};
        for (auto& pending : pending_joins_) {
            if (pending.used && pending.invitation_id == invitation_id) {
                pending = {};
            }
        }
        return {GroupAccessError::invitation_expired};
    }
    service(now_ms);
    if (all_zero(joining_fingerprint)) {
        return {GroupAccessError::invalid_identity};
    }
    const auto invitation_slot = find_invitation_index(invitation_id);
    if (invitation_slot == kNotFound) {
        return {GroupAccessError::invitation_not_found};
    }
    auto& invitation = invitations_[invitation_slot];
    if (invitation.cancelled || invitation.consumed ||
        invitation.group_epoch != group_epoch_) {
        return {GroupAccessError::invitation_unavailable};
    }

    const auto member_slot = find_member_index(joining_fingerprint);
    if (member_slot != kNotFound) {
        return members_[member_slot].member.state == GroupMemberState::revoked
            ? GroupAccessResult{GroupAccessError::member_revoked}
            : GroupAccessResult{GroupAccessError::member_exists};
    }
    const auto pending_identity = std::any_of(
        pending_joins_.begin(),
        pending_joins_.end(),
        [&joining_fingerprint](const PendingJoinRecord& pending) {
            return pending.used && pending.fingerprint == joining_fingerprint;
        });
    if (pending_identity) {
        return {GroupAccessError::join_already_pending};
    }
    const auto member_count = static_cast<std::size_t>(std::count_if(
        members_.begin(), members_.end(), [](const MemberRecord& record) {
            return record.used;
        }));
    const auto pending_count = static_cast<std::size_t>(std::count_if(
        pending_joins_.begin(),
        pending_joins_.end(),
        [](const PendingJoinRecord& pending) { return pending.used; }));
    if (member_count + pending_count >= kMaximumGroupMembers ||
        first_free_pending() == kNotFound) {
        return {GroupAccessError::capacity_full};
    }

    const auto pending_slot = first_free_pending();
    pending_joins_[pending_slot] = {
        invitation_id,
        invitation.expires_at_ms,
        joining_fingerprint,
        invitation.role,
        true,
    };
    invitation.consumed = true;
    return {};
}

GroupAccessResult GroupAccessController::complete_join(
    std::uint64_t invitation_id,
    std::uint64_t network_alias,
    const JoinAuthenticationEvidence& evidence,
    std::uint64_t now_ms) {
    if (!initialized_) {
        return {GroupAccessError::invalid_transition};
    }
    const auto pending_slot = find_pending_index(invitation_id);
    if (pending_slot == kNotFound) {
        return {GroupAccessError::join_not_found};
    }
    auto& pending = pending_joins_[pending_slot];
    if (now_ms >= pending.expires_at_ms) {
        pending = {};
        service(now_ms);
        return {GroupAccessError::invitation_expired};
    }
    if (!evidence.complete()) {
        pending = {};
        return {GroupAccessError::authentication_failed};
    }
    if (network_alias == 0) {
        return {GroupAccessError::invalid_alias};
    }
    if (alias_in_use(network_alias)) {
        return {GroupAccessError::alias_collision};
    }
    if (find_member_index(pending.fingerprint) != kNotFound) {
        pending = {};
        return {GroupAccessError::member_exists};
    }
    const auto member_slot = first_free_member();
    if (member_slot == kNotFound) {
        return {GroupAccessError::capacity_full};
    }

    members_[member_slot].used = true;
    members_[member_slot].member = {
        pending.fingerprint,
        pending.role,
        GroupMemberState::active,
        network_alias,
        group_epoch_,
    };
    pending = {};
    return {};
}

GroupAccessResult GroupAccessController::promote_to_administrator(
    const IdentityFingerprint& administrator,
    const IdentityFingerprint& target,
    const AdministratorPromotionConfirmation& confirmation) {
    if (!initialized_) {
        return {GroupAccessError::invalid_transition};
    }
    if (!authorized_administrator(administrator)) {
        return {GroupAccessError::not_authorized};
    }
    const auto target_slot = find_member_index(target);
    if (target_slot == kNotFound) {
        return {GroupAccessError::member_not_found};
    }
    auto& member = members_[target_slot].member;
    if (member.state != GroupMemberState::active ||
        member.group_epoch != group_epoch_) {
        return {GroupAccessError::rekey_required};
    }
    if (member.role == GroupRole::administrator) {
        return {GroupAccessError::invalid_transition};
    }
    if (!confirmation.complete()) {
        return {GroupAccessError::confirmation_required};
    }
    member.role = GroupRole::administrator;
    return {};
}

GroupAccessResult GroupAccessController::revoke_member(
    const IdentityFingerprint& administrator,
    const IdentityFingerprint& target,
    std::uint32_t new_group_epoch) {
    if (!initialized_) {
        return {GroupAccessError::invalid_transition};
    }
    if (!authorized_administrator(administrator)) {
        return {GroupAccessError::not_authorized};
    }
    const auto target_slot = find_member_index(target);
    if (target_slot == kNotFound) {
        return {GroupAccessError::member_not_found};
    }
    auto& target_member = members_[target_slot].member;
    if (target_member.state != GroupMemberState::active ||
        target_member.group_epoch != group_epoch_) {
        return {GroupAccessError::invalid_transition};
    }
    if (target_member.role == GroupRole::administrator &&
        current_administrator_count() <= 1) {
        return {GroupAccessError::last_administrator};
    }
    if (group_epoch_ == std::numeric_limits<std::uint32_t>::max() ||
        new_group_epoch != group_epoch_ + 1U) {
        return {GroupAccessError::invalid_epoch};
    }

    group_epoch_ = new_group_epoch;
    for (std::size_t index = 0; index < members_.size(); ++index) {
        if (!members_[index].used) {
            continue;
        }
        auto& member = members_[index].member;
        member.network_alias = 0;
        if (index == target_slot) {
            member.state = GroupMemberState::revoked;
            member.group_epoch = new_group_epoch;
        } else if (member.state == GroupMemberState::active) {
            member.state = GroupMemberState::rekey_pending;
        }
    }
    for (auto& invitation : invitations_) {
        if (invitation.used) {
            invitation.cancelled = true;
        }
    }
    for (auto& pending : pending_joins_) {
        pending = {};
    }
    return {};
}

GroupAccessResult GroupAccessController::apply_rekey(
    const IdentityFingerprint& target,
    std::uint32_t group_epoch,
    std::uint64_t new_network_alias,
    const RekeyEvidence& evidence) {
    if (!initialized_) {
        return {GroupAccessError::invalid_transition};
    }
    const auto target_slot = find_member_index(target);
    if (target_slot == kNotFound) {
        return {GroupAccessError::member_not_found};
    }
    auto& member = members_[target_slot].member;
    if (member.state == GroupMemberState::revoked) {
        return {GroupAccessError::member_revoked};
    }
    if (member.state != GroupMemberState::rekey_pending) {
        return {GroupAccessError::invalid_transition};
    }
    if (group_epoch == 0 || group_epoch != group_epoch_) {
        return {GroupAccessError::invalid_epoch};
    }
    if (!evidence.complete()) {
        return {GroupAccessError::authentication_failed};
    }
    if (new_network_alias == 0) {
        return {GroupAccessError::invalid_alias};
    }
    if (alias_in_use(new_network_alias)) {
        return {GroupAccessError::alias_collision};
    }

    member.group_epoch = group_epoch;
    member.network_alias = new_network_alias;
    member.state = GroupMemberState::active;
    return {};
}

void GroupAccessController::service(std::uint64_t now_ms) {
    for (auto& invitation : invitations_) {
        if (invitation.used && now_ms >= invitation.expires_at_ms) {
            const auto invitation_id = invitation.invitation_id;
            invitation = {};
            for (auto& pending : pending_joins_) {
                if (pending.used && pending.invitation_id == invitation_id) {
                    pending = {};
                }
            }
        }
    }
}

bool GroupAccessController::member_can_send(
    const IdentityFingerprint& fingerprint) const {
    const auto index = find_member_index(fingerprint);
    if (index == kNotFound) {
        return false;
    }
    const auto& member = members_[index].member;
    return member.state == GroupMemberState::active &&
           member.group_epoch == group_epoch_;
}

GroupMemberLookup GroupAccessController::find_member(
    const IdentityFingerprint& fingerprint) const {
    const auto index = find_member_index(fingerprint);
    return index == kNotFound
        ? GroupMemberLookup{}
        : GroupMemberLookup{true, members_[index].member};
}

GroupAccessStatus GroupAccessController::status(std::uint64_t now_ms) const {
    GroupAccessStatus result{};
    result.initialized = initialized_;
    result.group_id = group_id_;
    result.group_epoch = group_epoch_;
    for (const auto& record : members_) {
        if (!record.used) {
            continue;
        }
        ++result.member_records;
        switch (record.member.state) {
            case GroupMemberState::active:
                if (record.member.group_epoch == group_epoch_) {
                    ++result.active_members;
                    if (record.member.role == GroupRole::administrator) {
                        ++result.current_administrators;
                    }
                }
                break;
            case GroupMemberState::rekey_pending:
                ++result.rekey_pending_members;
                break;
            case GroupMemberState::revoked:
                ++result.revoked_members;
                break;
        }
    }
    for (const auto& invitation : invitations_) {
        if (invitation.used && !invitation.cancelled &&
            !invitation.consumed && now_ms < invitation.expires_at_ms &&
            invitation.group_epoch == group_epoch_) {
            ++result.active_invitations;
        }
    }
    for (const auto& pending : pending_joins_) {
        if (pending.used && now_ms < pending.expires_at_ms) {
            ++result.pending_joins;
        }
    }
    result.recovery_ready = result.current_administrators >= 2;
    return result;
}

}  // namespace opentrail::identity
