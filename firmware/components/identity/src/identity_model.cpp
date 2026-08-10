#include "opentrail/identity_model.hpp"

#include <algorithm>

namespace opentrail::identity {

const MembershipSnapshot& IdentityModel::snapshot() const {
    return snapshot_;
}

bool IdentityModel::can_send_group_traffic() const {
    return snapshot_.state == MembershipState::active;
}

bool IdentityModel::all_zero(const IdentityFingerprint& fingerprint) {
    return std::all_of(
        fingerprint.begin(), fingerprint.end(), [](std::uint8_t byte) {
            return byte == 0;
        });
}

IdentityResult IdentityModel::provision(
    const IdentityFingerprint& fingerprint) {
    if (snapshot_.state != MembershipState::unprovisioned) {
        return {IdentityError::invalid_transition};
    }
    if (all_zero(fingerprint)) {
        return {IdentityError::invalid_identity};
    }
    snapshot_.fingerprint = fingerprint;
    snapshot_.state = MembershipState::identity_ready;
    return {};
}

IdentityResult IdentityModel::rename(std::string_view display_name) {
    if (snapshot_.state == MembershipState::unprovisioned) {
        return {IdentityError::invalid_transition};
    }
    if (display_name.empty() ||
        display_name.size() > kMaximumDisplayNameBytes) {
        return {IdentityError::invalid_display_name};
    }
    for (const auto character : display_name) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20U || byte > 0x7EU) {
            return {IdentityError::invalid_display_name};
        }
    }

    snapshot_.display_name.fill('\0');
    std::copy(
        display_name.begin(),
        display_name.end(),
        snapshot_.display_name.begin());
    snapshot_.display_name_bytes = display_name.size();
    return {};
}

IdentityResult IdentityModel::begin_join(
    std::uint64_t group_id,
    std::uint32_t group_epoch) {
    if (snapshot_.state != MembershipState::identity_ready &&
        snapshot_.state != MembershipState::left) {
        return {IdentityError::invalid_transition};
    }
    if (group_id == 0) {
        return {IdentityError::invalid_group};
    }
    if (group_epoch == 0) {
        return {IdentityError::invalid_epoch};
    }

    snapshot_.group_id = group_id;
    snapshot_.group_epoch = group_epoch;
    snapshot_.network_alias = 0;
    snapshot_.state = MembershipState::join_pending;
    return {};
}

IdentityResult IdentityModel::activate(
    std::uint64_t group_id,
    std::uint32_t group_epoch,
    std::uint64_t network_alias) {
    if (snapshot_.state != MembershipState::join_pending) {
        return {IdentityError::invalid_transition};
    }
    if (group_id == 0 || group_id != snapshot_.group_id) {
        return {IdentityError::invalid_group};
    }
    if (group_epoch == 0 || group_epoch != snapshot_.group_epoch) {
        return {IdentityError::invalid_epoch};
    }
    if (network_alias == 0) {
        return {IdentityError::invalid_alias};
    }

    snapshot_.network_alias = network_alias;
    snapshot_.state = MembershipState::active;
    return {};
}

IdentityResult IdentityModel::revoke(
    std::uint64_t group_id,
    std::uint32_t new_group_epoch) {
    if (snapshot_.state != MembershipState::active) {
        return {IdentityError::invalid_transition};
    }
    if (group_id == 0 || group_id != snapshot_.group_id) {
        return {IdentityError::invalid_group};
    }
    if (new_group_epoch <= snapshot_.group_epoch) {
        return {IdentityError::invalid_epoch};
    }

    snapshot_.group_epoch = new_group_epoch;
    snapshot_.network_alias = 0;
    snapshot_.state = MembershipState::revoked;
    return {};
}

void IdentityModel::clear_membership(MembershipState next_state) {
    snapshot_.group_id = 0;
    snapshot_.network_alias = 0;
    snapshot_.group_epoch = 0;
    snapshot_.state = next_state;
}

IdentityResult IdentityModel::leave() {
    if (snapshot_.state != MembershipState::active &&
        snapshot_.state != MembershipState::join_pending &&
        snapshot_.state != MembershipState::revoked) {
        return {IdentityError::invalid_transition};
    }
    clear_membership(MembershipState::left);
    return {};
}

IdentityResult IdentityModel::reset(ResetMode mode) {
    if (mode == ResetMode::factory) {
        snapshot_ = {};
        return {};
    }
    if (snapshot_.state == MembershipState::unprovisioned) {
        return {IdentityError::invalid_transition};
    }

    clear_membership(MembershipState::identity_ready);
    snapshot_.display_name.fill('\0');
    snapshot_.display_name_bytes = 0;
    return {};
}

AliasComparison IdentityModel::compare_alias(
    std::uint64_t observed_alias,
    const IdentityFingerprint& observed_fingerprint) const {
    if (snapshot_.network_alias == 0 || observed_alias == 0 ||
        observed_alias != snapshot_.network_alias) {
        return AliasComparison::distinct;
    }
    return observed_fingerprint == snapshot_.fingerprint
        ? AliasComparison::same_identity
        : AliasComparison::collision;
}

}  // namespace opentrail::identity
