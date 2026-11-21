#include "companion_group_runtime.hpp"

#include <optional>

#include "heltec_enrollment_identity_owner.hpp"
#include "heltec_group_roster_identity.hpp"
#include "opentrail/companion_group_owner.hpp"
#if OPENTRAIL_LORA_LINK_TEST
#include "board_messaging_runtime.hpp"
#include "board_pairing_runtime.hpp"
#endif
#include "pairing_nvs_storage.hpp"

namespace opentrail::target::heltec_v4_bench {
namespace {

class BoardGroupPlatform final : public companion::GroupConfigurationPlatform {
public:
    explicit BoardGroupPlatform(CompanionGroupInputs inputs) : inputs_(inputs) {}

    const identity::roster::RosterSigner* prepare() noexcept override {
        auto& signer = signer_.emplace(HeltecGroupRosterSigner::Source{&inputs_, read_key, sign_roster});
        return signer.ready() && rosters.ready() && catalog.ready() ? &signer : nullptr;
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

private:
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
};

}  // namespace

companion::ConfigurationGroupBackend& companion_group_backend(
    companion::DeviceNameAuthoritySource& authority) {
    static BoardGroupPlatform platform{companion_group_inputs()};
    static HeltecGroupRosterCrypto crypto;
    static companion::ConfigurationGroupOwner owner{
        authority, platform.rosters, platform.catalog, crypto, platform};
    return owner;
}

}  // namespace opentrail::target::heltec_v4_bench
