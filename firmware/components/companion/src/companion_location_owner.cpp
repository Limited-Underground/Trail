#include "opentrail/companion_location_owner.hpp"

#include <limits>

namespace opentrail::companion {
namespace {

namespace loc = location::v1;
using Operation = ConfigurationLocationOperation;
using Status = ConfigurationLocationStatus;
using Kind = ConfigurationLocationKind;

static_assert(kConfigurationLocationFixLifetimeMs == loc::kFreshFixMs);

const DeviceNameContext* ready_context(const DeviceNameAuthority& authority) {
    const auto& context = authority.context;
    return authority.phase == DeviceNamePhase::ready && context.transport_generation != 0 &&
           context.session_nonce != 0 ? &context : nullptr;
}

bool current(const DeviceNameAuthority& authority, const DeviceNameContext& context,
             std::uint64_t admitted_ms) {
    const auto* ready = ready_context(authority);
    return ready && *ready == context && authority.now_ms >= admitted_ms &&
           authority.now_ms - admitted_ms < kConfigurationLocationCommandLifetimeMs;
}

bool valid_scope(const ConfigurationLocationScope& scope) {
    if (scope.id == 0 || scope.generation == 0) return false;
    using Digest = std::array<std::uint8_t, 32>;
    switch (scope.kind) {
        case Kind::group:
            return scope.slot < kConfigurationGroupSlots &&
                   scope.generation <= std::numeric_limits<std::uint32_t>::max() &&
                   scope.admin != Digest{} && scope.digest != Digest{};
        case Kind::direct:
            return scope.slot == 0xff && scope.admin == Digest{} && scope.digest == Digest{};
    }
    return false;
}

bool valid_command(const ConfigurationLocationCommand& command) {
    const bool no_fix = command.sample == 0 && command.age_ms == 0 &&
                        command.latitude_e4 == 0 && command.longitude_e4 == 0;
    switch (command.operation) {
        case Operation::consent:
        case Operation::withdraw:
            return command.lease == 0 && no_fix;
        case Operation::start:
            return command.lease == 0 && command.sample != 0;
        case Operation::renew:
            return command.lease != 0 && (command.sample != 0 || no_fix);
        case Operation::stop:
            return command.lease != 0 && no_fix;
    }
    return false;
}

std::optional<loc::Audience> audience_for(const ConfigurationLocationScope& scope, loc::GroupRule rule) {
    switch (scope.kind) {
        case Kind::group:
            if (rule != loc::GroupRule::off && rule != loc::GroupRule::preferred &&
                rule != loc::GroupRule::required) return std::nullopt;
            return loc::Audience{{loc::ScopeKind::group, scope.id,
                static_cast<std::uint32_t>(scope.generation), scope.digest}, scope.generation, rule};
        case Kind::direct:
            if (rule != loc::GroupRule::off) return std::nullopt;
            return loc::Audience{{loc::ScopeKind::direct, scope.id}, scope.generation, rule};
    }
    return std::nullopt;
}

std::optional<loc::PhoneFix> checked_fix(const ConfigurationLocationCommand& command,
                                        std::uint64_t queue_ms) {
    const loc::Coordinates coordinates{command.latitude_e4, command.longitude_e4};
    if (command.sample == 0 || !loc::valid_coordinates(coordinates) ||
        command.age_ms >= loc::kFreshFixMs || queue_ms >= loc::kFreshFixMs - command.age_ms) {
        return std::nullopt;
    }
    return loc::PhoneFix{coordinates, command.sample, command.age_ms + static_cast<std::uint32_t>(queue_ms)};
}

}  // namespace

void ConfigurationLocationOwner::clear_attachment() {
    if (!attachment_) return;
    attachment_.reset();
    groups_ = {};
    directs_ = {};
}

void ConfigurationLocationOwner::observe(const DeviceNameContext* context, std::uint64_t now_ms) noexcept {
    if (now_ms < last_now_ms_) {
        clear_attachment();
        return;
    }
    last_now_ms_ = now_ms;
    if (!context || context->transport_generation == 0 || context->session_nonce == 0) {
        clear_attachment();
        return;
    }
    if (!attachment_ || *attachment_ != *context) {
        clear_attachment();
        attachment_ = *context;
        return;
    }

    const auto expire = [now_ms](auto& entries) {
        for (auto& entry : entries) {
            if (entry.wire_lease != 0 && entry.sharing.active(entry.audience, now_ms).serial == 0) {
                entry.wire_lease = 0;
            }
        }
    };
    expire(groups_);
    expire(directs_);
}

ConfigurationLocationOwner::Entry* ConfigurationLocationOwner::entry_for(
    const ConfigurationLocationScope& scope, bool allow_new) {
    if (scope.kind == Kind::group) {
        auto& slot = groups_[scope.slot];
        return allow_new || slot.scope == scope ? &slot : nullptr;
    }

    Entry* vacant = nullptr;
    for (auto& slot : directs_) {
        if (slot.scope == scope) return &slot;
        if (allow_new && slot.scope.id == scope.id) return &slot;
        if (slot.scope.id == 0 && !vacant) vacant = &slot;
    }
    return allow_new ? vacant : nullptr;
}

ConfigurationLocationResult ConfigurationLocationOwner::execute(
    const DeviceNameContext& context, const ConfigurationLocationCommand& command,
    std::uint64_t admitted_ms) noexcept {
    const auto result = [&](Status status, std::uint64_t lease = 0) {
        return ConfigurationLocationResult{command.operation, status, command.scope.id,
                                           command.scope.generation, lease};
    };
    const auto before = authority_.current();
    const bool clock_current = before.now_ms >= last_now_ms_;
    observe(ready_context(before), before.now_ms);
    if (!clock_current || !current(before, context, admitted_ms) ||
        !valid_scope(command.scope) || !valid_command(command)) return result(Status::denied);

    const auto rule = audiences_.verified_rule(command.scope);
    const auto after = authority_.current();
    observe(ready_context(after), after.now_ms);
    if (after.now_ms < before.now_ms || !current(after, context, admitted_ms)) return result(Status::denied);

    const auto audience = rule ? audience_for(command.scope, *rule) : std::nullopt;
    if (!audience) {
        // An exact previously admitted scope that can no longer be verified
        // loses its consent and any held fix. Unrelated probes cannot clear it.
        if (auto* entry = entry_for(command.scope, false)) *entry = {};
        return result(Status::unavailable);
    }

    const auto now = after.now_ms;
    const auto operation = command.operation;
    auto* entry = entry_for(command.scope, operation == Operation::consent);
    if (!entry) return result(Status::denied);
    if (!(entry->scope == command.scope) || !(entry->audience == *audience)) {
        *entry = {};
        if (operation != Operation::consent) return result(Status::denied);
        entry->scope = command.scope;
        entry->audience = *audience;
        entry->sharing.set_audience(*audience);
    }

    if (operation == Operation::consent) {
        return result(entry->sharing.consent(*audience) ? Status::ok : Status::denied);
    }
    if (operation == Operation::withdraw) {
        *entry = {};
        return result(Status::ok);
    }
    if (operation == Operation::start) {
        const auto fix = checked_fix(command, now - admitted_ms);
        if (!fix || fix->sample <= entry->newest_sample ||
            next_lease_ == std::numeric_limits<std::uint64_t>::max() ||
            !entry->sharing.start(*audience, context.transport_generation, *fix, now)) return result(Status::denied);
        entry->newest_sample = fix->sample;
        entry->wire_lease = ++next_lease_;
        return result(Status::ok, entry->wire_lease);
    }

    if (command.lease != entry->wire_lease) return result(Status::denied);
    const auto inner_lease = entry->sharing.active(*audience, now);
    if (inner_lease.serial == 0 || inner_lease.attachment != context.transport_generation) return result(Status::denied);
    if (operation == Operation::stop) {
        entry->sharing.stop(inner_lease);
        entry->wire_lease = 0;
        return result(Status::ok);
    }
    if (command.sample != 0) {
        const auto fix = checked_fix(command, now - admitted_ms);
        if (!fix || !entry->sharing.update_fix(*audience, inner_lease, *fix, now)) return result(Status::denied);
        entry->newest_sample = fix->sample;
    }
    if (!entry->sharing.renew(*audience, inner_lease, now)) return result(Status::denied);
    return result(Status::ok, entry->wire_lease);
}

}  // namespace opentrail::companion
