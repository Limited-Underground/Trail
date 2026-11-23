#pragma once

#include <array>
#include <optional>

#include "opentrail/companion_configuration_dispatcher.hpp"
#include "opentrail/v1_location_sharing.hpp"

namespace opentrail::companion {

// The board supplies the rule only for the exact verified active scope. The
// app provides a scope reference, never the roster rule or direct authority.
class ConfigurationLocationAudienceSource {
public:
    virtual ~ConfigurationLocationAudienceSource() = default;
    [[nodiscard]] virtual std::optional<location::v1::GroupRule> verified_rule(
        const ConfigurationLocationScope& scope) noexcept = 0;
};

// Serialized app-task owner. Consent, lease identity and position use bounded
// RAM and never go to the catalog, event log or retained inbox.
class ConfigurationLocationOwner final : public ConfigurationLocationBackend {
public:
    ConfigurationLocationOwner(DeviceNameAuthoritySource& authority,
                               ConfigurationLocationAudienceSource& audiences)
        : authority_(authority), audiences_(audiences) {}

    [[nodiscard]] ConfigurationLocationResult execute(
        const DeviceNameContext& context, const ConfigurationLocationCommand& command,
        std::uint64_t admitted_ms) noexcept override;
    void observe(const DeviceNameContext* ready_context, std::uint64_t now_ms) noexcept override;

private:
    struct Entry {
        ConfigurationLocationScope scope{};
        location::v1::Audience audience{};
        location::v1::SharingLease sharing;
        std::uint64_t wire_lease{};
        // A stopped or expired lease cannot admit the same phone sample as fresh.
        std::uint64_t newest_sample{};
    };

    static constexpr std::size_t kDirectSlots = 8;

    [[nodiscard]] Entry* entry_for(const ConfigurationLocationScope& scope, bool allow_new);
    void clear_attachment();

    DeviceNameAuthoritySource& authority_;
    ConfigurationLocationAudienceSource& audiences_;
    std::array<Entry, kConfigurationGroupSlots> groups_{};
    std::array<Entry, kDirectSlots> directs_{};
    std::optional<DeviceNameContext> attachment_;
    std::uint64_t last_now_ms_{};
    std::uint64_t next_lease_{};
};

}  // namespace opentrail::companion
