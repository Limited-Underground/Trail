#include "opentrail/companion_group_owner.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

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

std::optional<roster::LocationRule> ConfigurationGroupOwner::verified_location_rule(
    const ConfigurationLocationScope& scope) noexcept {
    if (scope.kind != ConfigurationLocationKind::group || scope.slot >= kConfigurationGroupSlots ||
        scope.generation == 0 || scope.generation > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }

    const auto* signer = platform_.prepare();
    roster::DurableRosterSlot slot{};
    roster::Fingerprint local{};
    if (!signer || !crypto_.fingerprint(signer->public_key(), local) || local == roster::Fingerprint{} ||
        roster::DurableGroupRoster::inspect_slot(rosters_, scope.slot, slot) != Error::none ||
        slot.group_id != scope.id || slot.local != signer->public_key()) return std::nullopt;

    // Build the existing owner's read scratch without reopening either store.
    roster_.reset();
    index_.emplace(catalog_, local);
    auto& owner = roster_.emplace(rosters_, *index_, crypto_, slot.admin, slot.group_id,
        slot.local, slot.minimum_version);
    return owner.read_active_location_rule(scope.slot, static_cast<std::uint32_t>(scope.generation),
                                           scope.admin, scope.digest);
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
    case Operation::invite:
        return invite(context, admitted_ms, request, *signer, slots);
    case Operation::read_invite:
        return read_invite(request);
    case Operation::join:
        return join(context, admitted_ms, request, *signer, slots);
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

GroupConfigurationResult ConfigurationGroupOwner::invite(const DeviceNameContext& context, std::uint64_t admitted_ms,
    const GroupConfigurationCommand& request, const roster::RosterSigner& signer, const Slots& slots) {
    constexpr auto operation = Operation::invite;
    const auto selected = std::find_if(slots.begin(), slots.end(), [&](const Slot& slot) {
        roster::Fingerprint admin{};
        return slot.group_id != 0 && slot.group_id == request.scope.group_id &&
            crypto_.fingerprint(slot.admin, admin) && admin == request.scope.admin;
    });
    if (selected == slots.end()) return {operation, Status::stale};
    // Only the group's admin board issues invites for it.
    if (selected->admin != signer.public_key()) return {operation, Status::refused};

    roster::DurableRosterSnapshot snapshot{};
    const auto opened = load_snapshot(*selected, snapshot);
    if (opened != Error::none) return {operation, opened == Error::refused ? Status::refused : Status::uncertain};
    if (scope_of(snapshot) != request.scope) return {operation, Status::stale};
    if (snapshot.state != roster::State::active) return {operation, Status::refused};
    auto& current_roster = candidate_;
    current_roster = {};
    if (roster::decode(roster_bytes_.data(), snapshot.bytes, current_roster) != roster::FormatError::none ||
        current_roster.version != snapshot.version) return {operation, Status::uncertain};
    // D-057: at most six members. A full group cannot invite a seventh.
    if (current_roster.member_count >= roster::kMaximumMembers) return {operation, Status::refused};

    const auto at = now();
    // One live invite per group: repeat the same Invite to get the same ID.
    for (const auto& issued : issued_.view()) {
        std::uint64_t end = 0;
        if (!issued.issued || !issued.timing.deadline(end) || at >= end) continue;
        roster::qr::Descriptor live{};
        if (!roster::qr::verify_descriptor(*issued.issued, crypto_, live)) continue;
        if (live.group != snapshot.group.group_id || live.admin != signer.public_key()) continue;
        if (live.roster_version != snapshot.version || live.roster_digest != snapshot.digest)
            return {operation, Status::refused};  // Roster changed; wait for this invite to expire.
        GroupConfigurationResult result{operation, Status::ok, request.scope};
        result.invite.invite_id = live.invite_id;
        result.invite.total_bytes = static_cast<std::uint16_t>(issued.issued->size);
        return result;
    }

    roster::qr::Descriptor descriptor{};
    descriptor.admin = signer.public_key();
    descriptor.group = snapshot.group.group_id;
    descriptor.roster_version = snapshot.version;
    descriptor.roster_digest = snapshot.digest;
    descriptor.rule = current_roster.location_rule;
    descriptor.label_bytes = request.label.size;
    std::copy_n(request.label.bytes.begin(), request.label.size, descriptor.label.begin());
    if (!platform_.random_invite_id(descriptor.invite_id) ||
        std::all_of(descriptor.invite_id.begin(), descriptor.invite_id.end(), [](std::uint8_t b) { return b == 0; }))
        return {operation, Status::unavailable};
    const auto* invite_signer = platform_.invite_signer(descriptor);
    if (!invite_signer || invite_signer->public_key() != signer.public_key()) return {operation, Status::unavailable};
    if (!current(context, admitted_ms)) return {operation, Status::refused};

    roster::qr::DescriptorBytes wire{};
    if (!issued_.issue(descriptor, {at, at}, at, crypto_, *invite_signer, wire))
        return {operation, Status::refused};  // Bad label, four live invites, or clock went back.
    GroupConfigurationResult result{operation, Status::ok, request.scope};
    result.invite.invite_id = descriptor.invite_id;
    result.invite.total_bytes = static_cast<std::uint16_t>(wire.size);
    return result;
}

GroupConfigurationResult ConfigurationGroupOwner::read_invite(const GroupConfigurationCommand& request) {
    constexpr auto operation = Operation::read_invite;
    const auto at = now();
    for (const auto& issued : issued_.view()) {
        std::uint64_t end = 0;
        if (!issued.issued || !issued.timing.deadline(end) || at >= end) continue;
        const auto& wire = *issued.issued;
        // The invite ID sits at a fixed offset in the signed descriptor.
        constexpr std::size_t kInviteIdOffset = 84;
        if (!std::equal(request.invite.invite_id.begin(), request.invite.invite_id.end(),
                wire.bytes.begin() + kInviteIdOffset)) continue;
        if (request.invite.offset >= wire.size) return {operation, Status::stale};
        GroupConfigurationResult result{operation, Status::ok};
        auto& page = result.invite;
        page.invite_id = request.invite.invite_id;
        page.offset = request.invite.offset;
        page.total_bytes = static_cast<std::uint16_t>(wire.size);
        page.size = static_cast<std::uint8_t>(
            std::min(kConfigurationInviteFragmentBytes, wire.size - request.invite.offset));
        std::copy_n(wire.bytes.begin() + page.offset, page.size, page.bytes.begin());
        return result;
    }
    return {operation, Status::stale};  // Unknown, expired, or issued before a restart.
}

GroupConfigurationResult ConfigurationGroupOwner::join(const DeviceNameContext& context, std::uint64_t admitted_ms,
    const GroupConfigurationCommand& request, const roster::RosterSigner& signer, const Slots& slots) {
    constexpr auto operation = Operation::join;
    if (!joins_) return {operation, Status::unavailable};
    const auto& page = request.invite;
    auto& upload = upload_;
    if (page.offset == 0) {
        upload = {};
        upload.context = context;
        upload.total = page.total_bytes;
    } else if (upload.context != context || upload.total != page.total_bytes || upload.received != page.offset) {
        upload = {};
        return {operation, Status::stale};  // Start again from offset zero.
    }
    std::copy_n(page.bytes.begin(), page.size, upload.wire.bytes.begin() + page.offset);
    upload.received = static_cast<std::uint16_t>(page.offset + page.size);

    GroupConfigurationResult result{operation, Status::ok};
    result.invite.offset = page.offset;
    result.invite.total_bytes = page.total_bytes;
    if (upload.received < upload.total) return result;

    upload.wire.size = upload.total;
    const auto wire = upload.wire;
    upload = {};
    roster::qr::Descriptor descriptor{};
    if (!roster::qr::verify_descriptor(wire, crypto_, descriptor)) return {operation, Status::refused};
    // A board does not join a group it administers.
    if (descriptor.admin == signer.public_key()) return {operation, Status::refused};
    roster::Fingerprint admin{}, local{};
    if (!crypto_.fingerprint(descriptor.admin, admin) || !crypto_.fingerprint(signer.public_key(), local))
        return {operation, Status::unavailable};
    // Already holding this group (active or distrusted) makes the invite stale.
    const bool known = std::any_of(slots.begin(), slots.end(), [&](const Slot& slot) {
        return slot.group_id == descriptor.group && slot.admin == descriptor.admin;
    });
    if (known) return {operation, Status::stale};
    // D-059: at most four groups per board.
    if (std::none_of(slots.begin(), slots.end(), [](const Slot& slot) { return slot.group_id == 0; }))
        return {operation, Status::refused};

    persistence::GroupJoinClaims claims(*joins_, local);
    const auto opened = claims.reopen();
    if (opened != persistence::GroupJoinClaimError::none && opened != persistence::GroupJoinClaimError::missing)
        return {operation, Status::uncertain};
    if (claims.claimed(admin, descriptor.group, descriptor.invite_id)) return {operation, Status::stale};
    if (!current(context, admitted_ms)) return {operation, Status::refused};
    const auto claimed = claims.claim(admin, descriptor.group, descriptor.invite_id);
    if (claimed == persistence::GroupJoinClaimError::replayed) return {operation, Status::stale};
    if (claimed != persistence::GroupJoinClaimError::none) {
        // The claim may be durable. The invite is treated as spent.
        pending_.reset();
        return {operation, Status::uncertain};
    }
    pending_ = GroupPendingJoin{wire, descriptor, now() + roster::qr::kRequestMs};

    result.scope = {admin, descriptor.group, descriptor.roster_digest};
    result.invite.invite_id = descriptor.invite_id;
    return result;
}

Error ConfigurationGroupOwner::open_qr_group(const roster::qr::Descriptor& d, roster::DurableRosterSnapshot& snapshot) {
    const auto* signer = platform_.prepare();
    roster::Fingerprint fingerprint{};
    if (!signer || signer->public_key() != d.admin || !crypto_.fingerprint(signer->public_key(), fingerprint) ||
        fingerprint == roster::Fingerprint{}) return Error::refused;
    roster_.reset();
    index_.emplace(catalog_, fingerprint);
    if (index_->reopen() != IndexError::none) return Error::storage_failure;
    auto& slots = slots_;
    slots = {};
    if (!inspect(signer->public_key(), slots)) return Error::storage_failure;
    const auto selected = std::find_if(slots.begin(), slots.end(), [&](const Slot& slot) {
        return slot.group_id != 0 && slot.group_id == d.group && slot.admin == d.admin;
    });
    if (selected == slots.end()) return Error::invalid_scope;
    const auto opened = load_snapshot(*selected, snapshot);
    if (opened != Error::none) return opened;
    if (snapshot.state != roster::State::active || snapshot.group.group_id != d.group) return Error::refused;
    return Error::none;
}

bool ConfigurationGroupOwner::qr_join_roster(const roster::qr::Descriptor& d, roster::Roster& out) noexcept {
    roster::DurableRosterSnapshot snapshot{};
    roster::Roster current{};
    if (open_qr_group(d, snapshot) != Error::none || snapshot.version != d.roster_version ||
        snapshot.digest != d.roster_digest ||
        roster::decode(roster_bytes_.data(), snapshot.bytes, current) != roster::FormatError::none ||
        current.version != snapshot.version) return false;
    out = current;
    return true;
}

bool ConfigurationGroupOwner::qr_join_member_alias(const roster::Roster& current,
    std::array<char, roster::kMaximumAliasBytes>& out, std::size_t& size) noexcept {
    for (unsigned n = 2; n <= roster::kMaximumMembers + 1; ++n) {
        std::array<char, roster::kMaximumAliasBytes + 1> text{};
        const int written = std::snprintf(text.data(), text.size(), "Member %u", n);
        if (written <= 0 || static_cast<std::size_t>(written) > roster::kMaximumAliasBytes) return false;
        const auto length = static_cast<std::size_t>(written);
        const bool used = std::any_of(current.members.begin(), current.members.begin() + current.member_count,
            [&](const roster::Member& m) {
                return m.alias_bytes == length && std::memcmp(m.alias.data(), text.data(), length) == 0;
            });
        if (used) continue;
        std::copy_n(text.begin(), length, out.begin());
        size = length;
        return true;
    }
    return false;
}

GroupConfigurationStatus ConfigurationGroupOwner::add_qr_join_member(const roster::qr::Descriptor& d,
    const roster::PublicKey& joiner, std::string_view alias, std::uint8_t* out, std::size_t capacity,
    std::size_t& size) noexcept {
    size = 0;
    if (out == nullptr) return Status::refused;
    roster::DurableRosterSnapshot snapshot{};
    const auto opened = open_qr_group(d, snapshot);
    if (opened == Error::invalid_scope) return Status::stale;
    if (opened != Error::none) return opened == Error::refused ? Status::refused : Status::uncertain;
    if (snapshot.version != d.roster_version || snapshot.digest != d.roster_digest) return Status::stale;
    auto& current = candidate_;
    current = {};
    if (roster::decode(roster_bytes_.data(), snapshot.bytes, current) != roster::FormatError::none ||
        current.version != snapshot.version) return Status::uncertain;
    // D-057: at most six members; the joiner is not one yet and is not the admin.
    if (current.member_count >= roster::kMaximumMembers || joiner == d.admin ||
        std::any_of(current.members.begin(), current.members.begin() + current.member_count,
                    [&](const roster::Member& m) { return m.key == joiner; })) return Status::refused;
    roster::Roster next{};
    if (!roster::add_member(current, crypto_, joiner, alias, next)) return Status::refused;
    const auto* signer = platform_.successor_signer(current, next);
    if (!signer || signer->public_key() != d.admin) return Status::unavailable;
    if (!roster::sign(next, *signer)) return Status::unavailable;
    std::array<std::uint8_t, roster::kMaximumRosterBytes> wire{};
    const auto bytes = roster::encode(next, wire.data(), wire.size());
    if (bytes == 0 || bytes > capacity) return Status::uncertain;
    auto& owner = *roster_;
    const auto offered = owner.offer(wire.data(), bytes);
    if (offered.error != Error::none) return mutation_status(offered.error);
    if (offered.outcome != roster::Outcome::accepted) return Status::refused;
    std::array<std::uint8_t, roster::kMaximumRosterBytes> readback{};
    const auto kept = owner.copy_signed_roster(readback.data(), readback.size());
    if (kept != bytes || !std::equal(wire.begin(), wire.begin() + bytes, readback.begin())) return Status::uncertain;
    std::copy_n(wire.begin(), bytes, out);
    size = bytes;
    return Status::ok;
}

bool ConfigurationGroupOwner::revoke_qr_join_invite(const roster::qr::Descriptor& d) noexcept {
    for (const auto& issued : issued_.view()) {
        roster::qr::Descriptor live{};
        if (issued.issued && roster::qr::verify_descriptor(*issued.issued, crypto_, live) &&
            live.invite_id == d.invite_id) {
            const auto copy = *issued.issued;
            return issued_.revoke(copy);
        }
    }
    return false;
}

bool ConfigurationGroupOwner::qr_join_successor_readback(const roster::qr::Descriptor& d,
    const std::uint8_t* bytes, std::size_t size) noexcept {
    roster::DurableRosterSnapshot snapshot{};
    if (bytes == nullptr || size == 0 || open_qr_group(d, snapshot) != Error::none) return false;
    std::array<std::uint8_t, roster::kMaximumRosterBytes> readback{};
    const auto kept = roster_->copy_signed_roster(readback.data(), readback.size());
    return kept == size && std::equal(bytes, bytes + size, readback.begin());
}

const GroupPendingJoin* ConfigurationGroupOwner::pending_join(std::uint64_t now_ms) const noexcept {
    return pending_ && now_ms < pending_->deadline_ms ? &*pending_ : nullptr;
}

} // namespace opentrail::companion
