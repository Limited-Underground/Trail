#pragma once

#include <optional>

#include "opentrail/companion_configuration_dispatcher.hpp"
#include "opentrail/durable_group_roster.hpp"

namespace opentrail::companion {

class GroupConfigurationPlatform {
public:
    virtual ~GroupConfigurationPlatform() = default;
    // Prepare the current serialized app owner's storage and signer. May read
    // retained records, but must not commission either group namespace.
    [[nodiscard]] virtual const identity::roster::RosterSigner* prepare() noexcept = 0;
    [[nodiscard]] virtual bool random_group_id(std::uint64_t& out) noexcept = 0;
};

class ConfigurationGroupOwner final : public ConfigurationGroupBackend {
public:
    ConfigurationGroupOwner(DeviceNameAuthoritySource& authority,
        persistence::PersistentKvBackend& rosters, persistence::PersistentKvBackend& catalog,
        const identity::roster::RosterCrypto& crypto, GroupConfigurationPlatform& platform)
        : authority_(authority), rosters_(rosters), catalog_(catalog), crypto_(crypto), platform_(platform) {}

    [[nodiscard]] bool available() const noexcept override;
    [[nodiscard]] GroupConfigurationResult execute(
        const DeviceNameContext&, const GroupConfigurationCommand&, std::uint64_t admitted_ms) noexcept override;
    [[nodiscard]] std::optional<identity::roster::LocationRule> verified_location_rule(
        const ConfigurationLocationScope& scope) noexcept;

private:
    using Slot = identity::roster::DurableRosterSlot;
    using Slots = std::array<Slot, kConfigurationGroupSlots>;

    [[nodiscard]] bool current(const DeviceNameContext&, std::uint64_t admitted_ms) const noexcept;
    [[nodiscard]] bool inspect(const identity::roster::PublicKey&, Slots&);
    [[nodiscard]] identity::roster::DurableRosterError load_snapshot(
        const Slot&, identity::roster::DurableRosterSnapshot&);
    [[nodiscard]] GroupConfigurationResult create(const DeviceNameContext&, std::uint64_t admitted_ms,
        const GroupConfigurationCommand&, const identity::roster::RosterSigner&, const Slots&, bool commission);
    [[nodiscard]] GroupConfigurationResult read(const GroupConfigurationCommand&, const Slots&);
    [[nodiscard]] GroupConfigurationResult stop_trust(
        const DeviceNameContext&, std::uint64_t admitted_ms, const GroupConfigurationCommand&, const Slots&);

    DeviceNameAuthoritySource& authority_;
    persistence::PersistentKvBackend& rosters_;
    persistence::PersistentKvBackend& catalog_;
    const identity::roster::RosterCrypto& crypto_;
    GroupConfigurationPlatform& platform_;
    // Fixed scratch on the serialized app owner, outside the main-task stack.
    Slots slots_{};
    identity::roster::Roster candidate_{};
    std::optional<persistence::GroupPairIndex> index_;
    std::optional<identity::roster::DurableGroupRoster> roster_;
    std::array<std::uint8_t, identity::roster::kMaximumRosterBytes> roster_bytes_{};
};

} // namespace opentrail::companion
