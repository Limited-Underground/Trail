#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/identity_model.hpp"

namespace opentrail::identity {

inline constexpr std::size_t kMaximumGroupMembers = 16;
inline constexpr std::size_t kMaximumInvitations = 8;
inline constexpr std::size_t kMaximumPendingJoins = 4;
inline constexpr std::uint32_t kMaximumInvitationLifetimeMs = 3600000;

enum class GroupRole : std::uint8_t {
    member = 0,
    repeater,
    administrator,
};

enum class GroupMemberState : std::uint8_t {
    active = 0,
    rekey_pending,
    revoked,
};

enum class GroupAccessError : std::uint8_t {
    none = 0,
    invalid_transition,
    invalid_group,
    invalid_epoch,
    invalid_identity,
    invalid_alias,
    alias_collision,
    not_authorized,
    invalid_role,
    invalid_lifetime,
    invalid_invitation,
    capacity_full,
    duplicate_invitation,
    invitation_not_found,
    invitation_unavailable,
    invitation_expired,
    member_exists,
    member_revoked,
    join_already_pending,
    join_not_found,
    authentication_failed,
    confirmation_required,
    member_not_found,
    last_administrator,
    rekey_required,
};

struct GroupAccessResult {
    GroupAccessError error{GroupAccessError::none};

    [[nodiscard]] constexpr bool accepted() const {
        return error == GroupAccessError::none;
    }
};

struct JoinAuthenticationEvidence {
    bool group_authenticated{false};
    bool joining_device_authenticated{false};
    bool transcript_bound_to_invitation{false};
    bool human_code_confirmed{false};

    [[nodiscard]] constexpr bool complete() const {
        return group_authenticated && joining_device_authenticated &&
               transcript_bound_to_invitation && human_code_confirmed;
    }
};

struct AdministratorPromotionConfirmation {
    bool full_fingerprint_verified{false};
    bool target_user_confirmed{false};
    bool recovery_responsibility_acknowledged{false};

    [[nodiscard]] constexpr bool complete() const {
        return full_fingerprint_verified && target_user_confirmed &&
               recovery_responsibility_acknowledged;
    }
};

struct RekeyEvidence {
    bool new_epoch_authenticated{false};
    bool full_fingerprint_matched{false};
    bool key_material_installed{false};

    [[nodiscard]] constexpr bool complete() const {
        return new_epoch_authenticated && full_fingerprint_matched &&
               key_material_installed;
    }
};

struct GroupMemberSnapshot {
    IdentityFingerprint fingerprint{};
    GroupRole role{GroupRole::member};
    GroupMemberState state{GroupMemberState::active};
    std::uint64_t network_alias{0};
    std::uint32_t group_epoch{0};
};

struct GroupMemberLookup {
    bool found{false};
    GroupMemberSnapshot member{};
};

struct GroupAccessStatus {
    bool initialized{false};
    std::uint64_t group_id{0};
    std::uint32_t group_epoch{0};
    std::size_t member_records{0};
    std::size_t active_members{0};
    std::size_t rekey_pending_members{0};
    std::size_t revoked_members{0};
    std::size_t current_administrators{0};
    std::size_t active_invitations{0};
    std::size_t pending_joins{0};
    bool recovery_ready{false};
};

class GroupAccessController {
public:
    GroupAccessResult initialize(
        std::uint64_t group_id,
        std::uint32_t initial_epoch,
        const IdentityFingerprint& owner_fingerprint,
        std::uint64_t owner_alias);

    GroupAccessResult issue_invitation(
        const IdentityFingerprint& administrator,
        std::uint64_t invitation_id,
        GroupRole role,
        std::uint64_t now_ms,
        std::uint32_t lifetime_ms);
    GroupAccessResult cancel_invitation(
        const IdentityFingerprint& administrator,
        std::uint64_t invitation_id,
        std::uint64_t now_ms);
    GroupAccessResult begin_join(
        std::uint64_t invitation_id,
        const IdentityFingerprint& joining_fingerprint,
        std::uint64_t now_ms);
    GroupAccessResult complete_join(
        std::uint64_t invitation_id,
        std::uint64_t network_alias,
        const JoinAuthenticationEvidence& evidence,
        std::uint64_t now_ms);

    GroupAccessResult promote_to_administrator(
        const IdentityFingerprint& administrator,
        const IdentityFingerprint& target,
        const AdministratorPromotionConfirmation& confirmation);
    GroupAccessResult revoke_member(
        const IdentityFingerprint& administrator,
        const IdentityFingerprint& target,
        std::uint32_t new_group_epoch);
    GroupAccessResult apply_rekey(
        const IdentityFingerprint& target,
        std::uint32_t group_epoch,
        std::uint64_t new_network_alias,
        const RekeyEvidence& evidence);

    void service(std::uint64_t now_ms);
    [[nodiscard]] bool member_can_send(
        const IdentityFingerprint& fingerprint) const;
    [[nodiscard]] GroupMemberLookup find_member(
        const IdentityFingerprint& fingerprint) const;
    [[nodiscard]] GroupAccessStatus status(std::uint64_t now_ms) const;

private:
    struct MemberRecord {
        GroupMemberSnapshot member{};
        bool used{false};
    };

    struct InvitationRecord {
        std::uint64_t invitation_id{0};
        std::uint64_t expires_at_ms{0};
        std::uint32_t group_epoch{0};
        GroupRole role{GroupRole::member};
        bool consumed{false};
        bool cancelled{false};
        bool used{false};
    };

    struct PendingJoinRecord {
        std::uint64_t invitation_id{0};
        std::uint64_t expires_at_ms{0};
        IdentityFingerprint fingerprint{};
        GroupRole role{GroupRole::member};
        bool used{false};
    };

    static bool all_zero(const IdentityFingerprint& fingerprint);
    static bool invitable_role(GroupRole role);
    static std::uint64_t saturating_add(
        std::uint64_t value,
        std::uint32_t increment);
    [[nodiscard]] std::size_t find_member_index(
        const IdentityFingerprint& fingerprint) const;
    [[nodiscard]] std::size_t find_invitation_index(
        std::uint64_t invitation_id) const;
    [[nodiscard]] std::size_t find_pending_index(
        std::uint64_t invitation_id) const;
    [[nodiscard]] std::size_t first_free_member() const;
    [[nodiscard]] std::size_t first_free_invitation() const;
    [[nodiscard]] std::size_t first_free_pending() const;
    [[nodiscard]] bool authorized_administrator(
        const IdentityFingerprint& fingerprint) const;
    [[nodiscard]] bool alias_in_use(std::uint64_t alias) const;
    [[nodiscard]] std::size_t current_administrator_count() const;

    bool initialized_{false};
    std::uint64_t group_id_{0};
    std::uint32_t group_epoch_{0};
    std::array<MemberRecord, kMaximumGroupMembers> members_{};
    std::array<InvitationRecord, kMaximumInvitations> invitations_{};
    std::array<PendingJoinRecord, kMaximumPendingJoins> pending_joins_{};
};

}  // namespace opentrail::identity
