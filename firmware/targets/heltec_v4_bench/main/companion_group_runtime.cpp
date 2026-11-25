#include "companion_group_runtime.hpp"

#include <optional>

#include "heltec_enrollment_identity_owner.hpp"
#include "heltec_group_roster_identity.hpp"
#include "opentrail/companion_group_owner.hpp"
#if OPENTRAIL_LORA_LINK_TEST
#include "board_messaging_runtime.hpp"
#include "board_pairing_runtime.hpp"
#include "opentrail/companion_location_owner.hpp"
#endif
#include "pairing_nvs_storage.hpp"

namespace opentrail::target::heltec_v4_bench {
namespace {

class BoardGroupPlatform final : public companion::GroupConfigurationPlatform {
public:
    explicit BoardGroupPlatform(CompanionGroupInputs inputs) : inputs_(inputs) {}

    const identity::roster::RosterSigner* prepare() noexcept override {
        auto& signer = signer_.emplace(HeltecGroupRosterSigner::Source{&inputs_, read_key, sign_roster});
        return signer.ready() && rosters.ready() && catalog.ready() && joins.ready() ? &signer : nullptr;
    }

    bool random_invite_id(identity::roster::qr::Nonce& out) noexcept override {
        identity::roster::qr::Nonce bytes{};
        const auto filled = inputs_.entropy.fill(bytes.data(), bytes.size());
        if (!filled.ok() || filled.bytes_written != bytes.size()) return false;
        out = bytes;
        return true;
    }

    const identity::roster::RosterSigner* invite_signer(
        const identity::roster::qr::Descriptor& descriptor) noexcept override {
#if OPENTRAIL_LORA_LINK_TEST
        if (inputs_.pairing && inputs_.pairing->owns_enrollment_identity()) return nullptr;
#endif
        auto& signer = invite_signer_.emplace(
            HeltecInviteSigner::Source{&inputs_, read_key, sign_descriptor}, descriptor);
        return signer.ready() ? &signer : nullptr;
    }

    const identity::roster::RosterSigner* successor_signer(const identity::roster::Roster& previous,
                                                           const identity::roster::Roster& next) noexcept override {
#if OPENTRAIL_LORA_LINK_TEST
        if (inputs_.pairing && inputs_.pairing->owns_enrollment_identity()) return nullptr;
        auto& signer = successor_signer_.emplace(
            HeltecSuccessorSigner::Source{&inputs_, read_key, sign_successor}, previous, next);
        return signer.ready() ? &signer : nullptr;
#else
        // Roster N+1 comes only from the QR radio join (Link Test, D-170 b).
        (void)previous;
        (void)next;
        return nullptr;
#endif
    }

    bool random_group_id(std::uint64_t& out) noexcept override {
        std::array<std::uint8_t, sizeof(std::uint64_t)> bytes{};
        const auto filled = inputs_.entropy.fill(bytes.data(), bytes.size());
        if (!filled.ok() || filled.bytes_written != bytes.size()) return false;
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < bytes.size(); ++i)
            value |= static_cast<std::uint64_t>(bytes[i]) << (8 * i);
        out = value;
        return true;
    }

    GroupNvsStorage rosters{GroupNvsStorage::Store::roster};
    GroupNvsStorage catalog{GroupNvsStorage::Store::catalog};
    GroupNvsStorage joins{GroupNvsStorage::Store::join};

private:
    static bool sign_descriptor(void* context, const identity::roster::qr::Descriptor& descriptor,
        identity::roster::Signature& signature) {
#if OPENTRAIL_LORA_LINK_TEST
        // First pairing holds the identity alone. The messaging runtime signs
        // the same exact descriptor with its live copy (I-101).
        const auto& inputs = *static_cast<const CompanionGroupInputs*>(context);
        if (inputs.pairing && inputs.pairing->owns_enrollment_identity()) return false;
        if (inputs.messaging && inputs.messaging->owns_enrollment_identity())
            return inputs.messaging->sign_invite_descriptor(descriptor, signature);
#else
        (void)context;
#endif
        return ::opentrail::targets::heltec_v4_bench::sign_retained_invite_descriptor(descriptor, signature);
    }

    static bool sign_successor(void* context, const identity::roster::Roster& previous,
                               const identity::roster::Roster& next, identity::roster::Signature& signature) {
#if OPENTRAIL_LORA_LINK_TEST
        const auto& inputs = *static_cast<const CompanionGroupInputs*>(context);
        if (inputs.pairing && inputs.pairing->owns_enrollment_identity()) return false;
        if (inputs.messaging && inputs.messaging->owns_enrollment_identity())
            return inputs.messaging->sign_successor_group_roster(previous, next, signature);
#else
        (void)context;
#endif
        return ::opentrail::targets::heltec_v4_bench::sign_retained_successor_group_roster(previous, next, signature);
    }

    static bool read_key(void* context, identity::roster::PublicKey& key) {
#if OPENTRAIL_LORA_LINK_TEST
        const auto& inputs = *static_cast<const CompanionGroupInputs*>(context);
        if (inputs.pairing && inputs.pairing->owns_enrollment_identity()) return false;
        if (inputs.messaging && inputs.messaging->owns_enrollment_identity())
            return inputs.messaging->read_group_roster_key(key);
#else
        (void)context;
#endif
        return ::opentrail::targets::heltec_v4_bench::read_retained_group_roster_key(key);
    }

    static bool sign_roster(void* context, const identity::roster::Roster& roster,
        identity::roster::Signature& signature) {
#if OPENTRAIL_LORA_LINK_TEST
        const auto& inputs = *static_cast<const CompanionGroupInputs*>(context);
        if (inputs.pairing && inputs.pairing->owns_enrollment_identity()) return false;
        if (inputs.messaging && inputs.messaging->owns_enrollment_identity())
            return inputs.messaging->sign_initial_group_roster(roster, signature);
#else
        (void)context;
#endif
        return ::opentrail::targets::heltec_v4_bench::sign_retained_initial_group_roster(roster, signature);
    }

    CompanionGroupInputs inputs_;
    std::optional<HeltecGroupRosterSigner> signer_;
    std::optional<HeltecInviteSigner> invite_signer_;
    std::optional<HeltecSuccessorSigner> successor_signer_;
};

companion::ConfigurationGroupOwner* g_started_owner = nullptr;

companion::ConfigurationGroupOwner& group_owner(companion::DeviceNameAuthoritySource& authority) {
    static BoardGroupPlatform platform{companion_group_inputs()};
    static HeltecGroupRosterCrypto crypto;
    static companion::ConfigurationGroupOwner owner{
        authority, platform.rosters, platform.catalog, platform.joins, crypto, platform};
    g_started_owner = &owner;
    return owner;
}

#if OPENTRAIL_LORA_LINK_TEST
class BoardLocationAudiences final : public companion::ConfigurationLocationAudienceSource {
public:
    explicit BoardLocationAudiences(companion::ConfigurationGroupOwner& groups) : groups_(groups) {}

    std::optional<identity::roster::LocationRule> verified_rule(
        const companion::ConfigurationLocationScope& scope) noexcept override {
        // Current firmware has no live authenticated direct-conversation owner.
        return groups_.verified_location_rule(scope);
    }

private:
    companion::ConfigurationGroupOwner& groups_;
};
#endif

}  // namespace

companion::ConfigurationGroupBackend& companion_group_backend(
    companion::DeviceNameAuthoritySource& authority) {
    return group_owner(authority);
}

companion::ConfigurationGroupOwner* companion_group_owner_started() { return g_started_owner; }

#if OPENTRAIL_LORA_LINK_TEST
companion::ConfigurationLocationBackend& companion_location_backend(
    companion::DeviceNameAuthoritySource& authority) {
    static BoardLocationAudiences audiences{group_owner(authority)};
    static companion::ConfigurationLocationOwner owner{authority, audiences};
    return owner;
}
#endif

}  // namespace opentrail::target::heltec_v4_bench
