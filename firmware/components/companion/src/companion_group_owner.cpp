#include "opentrail/companion_group_owner.hpp"

#include <algorithm>

namespace opentrail::companion {
namespace {
namespace roster = identity::roster;
using IndexError = persistence::GroupPairIndexError;
using Error = roster::DurableRosterError;
using Operation = GroupConfigurationOperation;
using Status = GroupConfigurationStatus;
using State = GroupConfigurationRosterState;

static_assert(kConfigurationGroupSlots == persistence::GroupPairIndex::kMaximumGroups);
static_assert(kConfigurationGroupRosterBytes == roster::kMaximumRosterBytes);

GroupConfigurationScope scope_of(const roster::DurableRosterSnapshot& snapshot) {
    return {snapshot.group.admin, snapshot.group.group_id, snapshot.digest};
}

GroupConfigurationRosterState state_of(const roster::DurableRosterSnapshot& snapshot) {
    return snapshot.state == roster::State::distrusted ? State::distrusted : State::active;
}

Status mutation_status(Error error) {
    switch (error) {
    case Error::none: return Status::ok;
    case Error::capacity:
    case Error::refused:
    case Error::invalid_scope: return Status::refused;
    default: return Status::uncertain;
    }
}
} // namespace

bool ConfigurationGroupOwner::available() const noexcept {
    return platform_.prepare() != nullptr;
}

bool ConfigurationGroupOwner::current(const DeviceNameContext& context, std::uint64_t admitted_ms) const noexcept {
    const auto observed = authority_.current();
    return context.session_nonce != 0 && observed.context == context &&
        observed.now_ms >= admitted_ms && observed.now_ms - admitted_ms < DeviceNameOwner::admission_lifetime_ms &&
        (observed.phase == DeviceNamePhase::connected || observed.phase == DeviceNamePhase::ready);
}

bool ConfigurationGroupOwner::inspect(const roster::PublicKey& local, Slots& slots) {
    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
        const auto status = roster::DurableGroupRoster::inspect_slot(rosters_, slot, slots[slot]);
        if (status == Error::missing) continue;
        if (status != Error::none || slots[slot].local != local) return false;
    }
    return true;
}

Error ConfigurationGroupOwner::load_snapshot(const Slot& slot, roster::DurableRosterSnapshot& snapshot) {
    auto& owner = roster_.emplace(rosters_, *index_, crypto_, slot.admin, slot.group_id,
        slot.local, slot.minimum_version);
    const auto opened = owner.reopen();
    return opened == Error::none ?
        owner.snapshot(snapshot, roster_bytes_.data(), roster_bytes_.size()) : opened;
}

GroupConfigurationResult ConfigurationGroupOwner::execute(
    const DeviceNameContext& context, const GroupConfigurationCommand& request, std::uint64_t admitted_ms) noexcept {
    if (!current(context, admitted_ms)) return {request.operation, Status::refused};
    const auto* signer = platform_.prepare();
    roster::Fingerprint fingerprint{};
    if (!signer || !crypto_.fingerprint(signer->public_key(), fingerprint) || fingerprint == roster::Fingerprint{})
        return {request.operation, Status::unavailable};
    if (request.operation == Operation::discover) return {request.operation, Status::ok};

    roster_.reset();
    index_.emplace(catalog_, fingerprint);
    const auto catalog = index_->reopen();
    auto& slots = slots_;
    slots = {};
    if (!inspect(signer->public_key(), slots) || (catalog != IndexError::none && catalog != IndexError::missing))
        return {request.operation, Status::uncertain};
    const bool blank = std::none_of(slots.begin(), slots.end(), [](const Slot& slot) { return slot.group_id != 0; });
    if (catalog == IndexError::missing && !blank) return {request.operation, Status::uncertain};
    if (!current(context, admitted_ms)) return {request.operation, Status::refused};

    switch (request.operation) {
    case Operation::create:
        return create(context, admitted_ms, request, *signer, slots, catalog == IndexError::missing);
    case Operation::read_roster:
        return read(request, slots);
    case Operation::stop_trust:
        return stop_trust(context, admitted_ms, request, slots);
    default:
        return {request.operation, Status::refused};
    }
}

GroupConfigurationResult ConfigurationGroupOwner::create(const DeviceNameContext& context, std::uint64_t admitted_ms,
    const GroupConfigurationCommand& request, const roster::RosterSigner& signer, const Slots& slots,
    bool commission) {
    constexpr auto operation = Operation::create;
    const auto occupied = [&](std::uint64_t id) {
        return std::any_of(slots.begin(), slots.end(), [id](const Slot& slot) { return slot.group_id == id; });
    };
    if (!occupied(0) || request.location_rule > static_cast<std::uint8_t>(roster::LocationRule::required))
        return {operation, Status::refused};

    std::uint64_t group_id = 0;
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        if (!platform_.random_group_id(group_id)) return {operation, Status::unavailable};
        if (group_id != 0 && !occupied(group_id)) break;
        group_id = 0;
    }
    if (group_id == 0) return {operation, Status::refused};

    auto& candidate = candidate_;
    candidate = {};
    if (!roster::create_first(group_id, signer.public_key(), "Admin",
            static_cast<roster::LocationRule>(request.location_rule), candidate) ||
        !roster::sign(candidate, signer)) return {operation, Status::unavailable};
    const auto bytes = roster::encode(candidate, roster_bytes_.data(), roster_bytes_.size());
    if (bytes == 0) return {operation, Status::uncertain};
    if (!current(context, admitted_ms)) return {operation, Status::refused};

    if (commission && index_->initialize() != IndexError::none) return {operation, Status::uncertain};
    if (!current(context, admitted_ms)) return {operation, Status::refused};
    auto& owner = roster_.emplace(rosters_, *index_, crypto_, signer.public_key(), group_id, signer.public_key());
    const auto created = owner.create_owned(roster_bytes_.data(), bytes);
    if (created != Error::none) return {operation, mutation_status(created)};

    roster::DurableRosterSnapshot snapshot{};
    if (owner.snapshot(snapshot, roster_bytes_.data(), roster_bytes_.size()) != Error::none ||
        snapshot.state != roster::State::active || snapshot.version != 1)
        return {operation, Status::uncertain};
    return {operation, Status::ok, scope_of(snapshot)};
}

GroupConfigurationResult ConfigurationGroupOwner::read(const GroupConfigurationCommand& request, const Slots& slots) {
    constexpr auto operation = Operation::read_roster;
    if (request.read.slot >= slots.size() || request.read.offset % kConfigurationRosterFragmentBytes != 0)
        return {operation, Status::refused};
    const auto& slot = slots[request.read.slot];
    if (slot.group_id == 0)
        return {operation, request.read.offset == 0 ? Status::missing : Status::stale};

    roster::DurableRosterSnapshot snapshot{};
    const auto opened = load_snapshot(slot, snapshot);
    if (opened != Error::none) return {operation, opened == Error::refused ? Status::refused : Status::uncertain};
    const auto scope = scope_of(snapshot);
    const auto state = state_of(snapshot);
    if (request.read.offset >= snapshot.bytes ||
        (request.read.offset != 0 && (request.scope != scope || request.read.state != state)))
        return {operation, Status::stale};

    GroupConfigurationResult result{operation, Status::ok, scope};
    auto& fragment = result.fragment;
    fragment.slot = request.read.slot;
    fragment.offset = request.read.offset;
    fragment.state = state;
    fragment.total_bytes = static_cast<std::uint16_t>(snapshot.bytes);
    const auto size = std::min(kConfigurationRosterFragmentBytes, snapshot.bytes - request.read.offset);
    fragment.size = static_cast<std::uint8_t>(size);
    std::copy_n(roster_bytes_.data() + request.read.offset, size, fragment.bytes.data());
    return result;
}

GroupConfigurationResult ConfigurationGroupOwner::stop_trust(const DeviceNameContext& context, std::uint64_t admitted_ms,
    const GroupConfigurationCommand& request, const Slots& slots) {
    constexpr auto operation = Operation::stop_trust;
    const auto selected = std::find_if(slots.begin(), slots.end(), [&](const Slot& slot) {
        roster::Fingerprint admin{};
        return slot.group_id != 0 && slot.group_id == request.scope.group_id &&
            crypto_.fingerprint(slot.admin, admin) && admin == request.scope.admin;
    });
    if (selected == slots.end()) return {operation, Status::stale};

    roster::DurableRosterSnapshot before{};
    const auto opened = load_snapshot(*selected, before);
    if (opened != Error::none) return {operation, opened == Error::refused ? Status::refused : Status::uncertain};
    if (scope_of(before) != request.scope) return {operation, Status::stale};
    if (!current(context, admitted_ms)) return {operation, Status::refused};

    const auto stopped = roster_->stop_trusting();
    if (stopped != Error::none) return {operation, mutation_status(stopped)};
    roster::DurableRosterSnapshot after{};
    if (roster_->snapshot(after, roster_bytes_.data(), roster_bytes_.size()) != Error::none ||
        after.state != roster::State::distrusted || scope_of(after) != request.scope)
        return {operation, Status::uncertain};
    return {operation, Status::ok, request.scope};
}

} // namespace opentrail::companion
