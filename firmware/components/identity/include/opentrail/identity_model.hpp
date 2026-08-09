#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace opentrail::identity {

inline constexpr std::size_t kIdentityFingerprintBytes = 32;
inline constexpr std::size_t kMaximumDisplayNameBytes = 32;

using IdentityFingerprint =
    std::array<std::uint8_t, kIdentityFingerprintBytes>;

enum class MembershipState : std::uint8_t {
    unprovisioned = 0,
    identity_ready,
    join_pending,
    active,
    revoked,
    left,
};

enum class IdentityError : std::uint8_t {
    none = 0,
    invalid_transition,
    invalid_identity,
    invalid_group,
    invalid_alias,
    invalid_epoch,
    invalid_display_name,
};

enum class ResetMode : std::uint8_t {
    configuration_only = 0,
    factory,
};

enum class AliasComparison : std::uint8_t {
    distinct = 0,
    same_identity,
    collision,
};

struct IdentityResult {
    IdentityError error{IdentityError::none};

    [[nodiscard]] constexpr bool accepted() const {
        return error == IdentityError::none;
    }
};

struct MembershipSnapshot {
    MembershipState state{MembershipState::unprovisioned};
    IdentityFingerprint fingerprint{};
    std::uint64_t group_id{0};
    std::uint64_t network_alias{0};
    std::uint32_t group_epoch{0};
    std::array<char, kMaximumDisplayNameBytes + 1> display_name{};
    std::size_t display_name_bytes{0};
};

class IdentityModel {
public:
    [[nodiscard]] const MembershipSnapshot& snapshot() const;
    [[nodiscard]] bool can_send_group_traffic() const;

    IdentityResult provision(const IdentityFingerprint& fingerprint);
    IdentityResult rename(std::string_view display_name);
    IdentityResult begin_join(std::uint64_t group_id, std::uint32_t group_epoch);
    IdentityResult activate(
        std::uint64_t group_id,
        std::uint32_t group_epoch,
        std::uint64_t network_alias);
    IdentityResult revoke(std::uint64_t group_id, std::uint32_t new_group_epoch);
    IdentityResult leave();
    IdentityResult reset(ResetMode mode);

    [[nodiscard]] AliasComparison compare_alias(
        std::uint64_t observed_alias,
        const IdentityFingerprint& observed_fingerprint) const;

private:
    static bool all_zero(const IdentityFingerprint& fingerprint);
    void clear_membership(MembershipState next_state);

    MembershipSnapshot snapshot_{};
};

}  // namespace opentrail::identity
