#include "opentrail/group_pair_index.hpp"

#include <algorithm>
#include <array>
#include <limits>

#include "opentrail/configuration_store.hpp"

namespace opentrail::persistence {
namespace {

namespace roster = identity::roster;
using Error = GroupPairIndexError;
using KvError = PersistentKvBackendError;

constexpr char kNamespace[] = "ot_group_idx";
constexpr char kKey[] = "catalog";
static_assert(sizeof(kNamespace) - 1 <= 15 && sizeof(kKey) - 1 <= 15);
constexpr std::array<std::uint8_t, 5> kHeader{'O', 'T', 'P', 'I', 1};

template <typename T>
void write_be(std::uint8_t*& out, T value) {
    for (std::size_t i = sizeof(T); i > 0; --i) {
        *out++ = static_cast<std::uint8_t>(value >> (8 * (i - 1)));
    }
}

template <typename T>
T read_be(const std::uint8_t*& in) {
    T value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value = (value << 8U) | *in++;
    }
    return value;
}

void write_fingerprint(std::uint8_t*& out, const roster::Fingerprint& identity) {
    out = std::copy(identity.begin(), identity.end(), out);
}

roster::Fingerprint read_fingerprint(const std::uint8_t*& in) {
    roster::Fingerprint identity{};
    std::copy_n(in, identity.size(), identity.begin());
    in += identity.size();
    return identity;
}

bool same_group(const roster::GroupName& a, const roster::GroupName& b) {
    return a.group_id == b.group_id && a.admin == b.admin;
}

class Busy {
public:
    explicit Busy(bool& busy) : busy_(busy) { busy_ = true; }
    ~Busy() { busy_ = false; }
private:
    bool& busy_;
};

}  // namespace

bool GroupPairIndex::Entry::names(const roster::GroupName& name,
                                 const roster::Fingerprint& fingerprint) const {
    return incarnation != 0 && same_group(group, name) && peer == fingerprint;
}

bool GroupPairIndex::Entry::valid(const roster::Fingerprint& local, std::uint64_t high_water) const {
    if (incarnation == 0) {
        return group.group_id == 0 && group.admin == roster::Fingerprint{} &&
               peer == roster::Fingerprint{} && local_added == 0 &&
               peer_added == 0 && roster_version == 0;
    }
    return incarnation <= high_water && group.group_id != 0 &&
           group.admin != roster::Fingerprint{} && peer != roster::Fingerprint{} &&
           peer != local && local_added != 0 && local_added <= roster_version &&
           peer_added != 0 && peer_added <= roster_version && local_added != peer_added;
}

struct GroupPairIndex::RosterSnapshot {
    roster::GroupName group{};
    std::uint32_t version{0};
    roster::Digest digest{};
    std::array<roster::Fingerprint, roster::kMaximumMembers> fingerprints{};
    std::array<std::uint32_t, roster::kMaximumMembers> memberships{};

    explicit RosterSnapshot(const roster::RosterState& state)
        : group{state.fingerprints()[0], state.roster().group_id},
          version(state.roster().version), digest(state.digest()), fingerprints(state.fingerprints()) {
        for (std::size_t i = 0; i < state.roster().member_count; ++i) {
            memberships[i] = state.roster().members[i].added_in;
        }
    }

    std::uint32_t added_in(const roster::Fingerprint& member) const {
        const auto hit = std::find(fingerprints.begin(), fingerprints.end(), member);
        return hit == fingerprints.end() ? 0 : memberships[hit - fingerprints.begin()];
    }
};

bool GroupPairIndex::GroupWatermark::current(const RosterSnapshot& roster) const {
    return roster.version > version || (roster.version == version && roster.digest == digest);
}

GroupPairIndex::GroupWatermark* GroupPairIndex::Catalog::group_for(const roster::GroupName& name) {
    const auto found = std::find_if(groups.begin(), groups.end(), [&name](const auto& group) {
        return group.version != 0 && same_group(group.group, name);
    });
    return found == groups.end() ? nullptr : &*found;
}

std::size_t GroupPairIndex::Catalog::pair_count(const roster::GroupName& group) const {
    return static_cast<std::size_t>(std::count_if(entries.begin(), entries.end(), [&group](const auto& entry) {
        return entry.incarnation != 0 && same_group(entry.group, group);
    }));
}

bool GroupPairIndex::Catalog::valid(const roster::Fingerprint& local) {
    for (auto group = groups.begin(); group != groups.end(); ++group) {
        if (group->version == 0) {
            if (group->group.group_id != 0 || group->group.admin != roster::Fingerprint{} ||
                group->digest != roster::Digest{}) return false;
            continue;
        }
        if (group->group.group_id == 0 || group->group.admin == roster::Fingerprint{} ||
            group_for(group->group) != &*group || pair_count(group->group) > kPeersPerGroup) return false;
    }
    for (auto current = entries.begin(); current != entries.end(); ++current) {
        const auto& entry = *current;
        if (!entry.valid(local, high_water)) return false;
        if (entry.incarnation == 0) continue;
        const auto* group = group_for(entry.group);
        if (!group || entry.roster_version > group->version) return false;
        if (std::any_of(entries.begin(), current, [&entry](const Entry& prior) {
                return prior.incarnation == entry.incarnation || prior.names(entry.group, entry.peer);
            })) return false;
    }
    return true;
}

void GroupPairIndex::Catalog::encode(const roster::Fingerprint& local, Blob& blob) const {
    blob.fill(0);
    auto* out = std::copy(kHeader.begin(), kHeader.end(), blob.data());
    write_fingerprint(out, local);
    write_be(out, high_water);
    for (const auto& entry : entries) {
        write_be(out, entry.incarnation);
        write_fingerprint(out, entry.group.admin);
        write_be(out, entry.group.group_id);
        write_fingerprint(out, entry.peer);
        write_be(out, entry.local_added);
        write_be(out, entry.peer_added);
        write_be(out, entry.roster_version);
    }
    for (const auto& group : groups) {
        write_fingerprint(out, group.group.admin);
        write_be(out, group.group.group_id);
        write_be(out, group.version);
        out = std::copy(group.digest.begin(), group.digest.end(), out);
    }
    write_be(out, configuration_crc32({blob.data(), kBodyBytes}));
}

GroupPairIndexError GroupPairIndex::Catalog::decode(const Blob& blob, const roster::Fingerprint& local) {
    const auto* crc = blob.data() + kBodyBytes;
    if (!std::equal(kHeader.begin(), kHeader.end(), blob.begin()) ||
        read_be<std::uint32_t>(crc) != configuration_crc32({blob.data(), kBodyBytes})) {
        return Error::invalid_record;
    }
    const auto* in = blob.data() + kHeader.size();
    if (read_fingerprint(in) != local) return Error::invalid_scope;
    high_water = read_be<std::uint64_t>(in);
    for (auto& entry : entries) {
        entry.incarnation = read_be<std::uint64_t>(in);
        entry.group.admin = read_fingerprint(in);
        entry.group.group_id = read_be<std::uint64_t>(in);
        entry.peer = read_fingerprint(in);
        entry.local_added = read_be<std::uint32_t>(in);
        entry.peer_added = read_be<std::uint32_t>(in);
        entry.roster_version = read_be<std::uint32_t>(in);
    }
    for (auto& group : groups) {
        group.group.admin = read_fingerprint(in);
        group.group.group_id = read_be<std::uint64_t>(in);
        group.version = read_be<std::uint32_t>(in);
        std::copy_n(in, group.digest.size(), group.digest.begin());
        in += group.digest.size();
    }
    return valid(local) ? Error::none : Error::invalid_record;
}

GroupPairIndexError GroupPairIndex::load(Catalog& catalog) {
    if (faulted_) return Error::storage_failure;
    if (local_ == roster::Fingerprint{}) return Error::invalid_scope;
    auto& blob = workspace_.serialized;
    blob.fill(0);
    std::size_t size = 0;
    const auto result = backend_.read_blob(
        kPersistentKvPartitionLabel, kNamespace, kKey, blob.data(), blob.size(), size);
    if (result == KvError::not_found) return Error::missing;
    if (result == KvError::invalid_argument) return Error::invalid_record;
    if (result != KvError::none) return Error::storage_failure;
    return size == blob.size() ? catalog.decode(blob, local_) : Error::invalid_record;
}

GroupPairIndexError GroupPairIndex::save(const Catalog& catalog) {
    auto& blob = workspace_.serialized;
    auto& check = workspace_.readback;
    catalog.encode(local_, blob);
    check.fill(0);
    std::size_t size = 0;
    if (backend_.write_blob(kPersistentKvPartitionLabel, kNamespace, kKey, blob.data(), blob.size()) !=
            KvError::none ||
        backend_.commit(kPersistentKvPartitionLabel, kNamespace) != KvError::none ||
        backend_.read_blob(kPersistentKvPartitionLabel, kNamespace, kKey, check.data(), check.size(), size) !=
            KvError::none || size != check.size() || check != blob) {
        faulted_ = true;
        return Error::storage_failure;
    }
    return Error::none;
}

GroupPairIndexError GroupPairIndex::initialize() {
    if (busy_) return Error::storage_failure;
    const Busy hold(busy_);
    workspace_.catalog = Catalog{};
    auto& catalog = workspace_.catalog;
    const auto loaded = load(catalog);
    return loaded == Error::missing ? save(catalog) : loaded;
}

GroupPairIndexError GroupPairIndex::group_status(const roster::GroupName& group) {
    if (busy_) return Error::storage_failure;
    const Busy hold(busy_);
    auto& catalog = workspace_.catalog;
    const auto loaded = load(catalog);
    if (loaded != Error::none) return loaded;
    return catalog.group_for(group) ? Error::none : Error::not_indexed;
}

GroupPairIndexResult GroupPairIndex::find(const roster::RosterState& state,
                                          const roster::Fingerprint& peer) {
    return find_or_reserve(state, peer, false);
}

GroupPairIndexResult GroupPairIndex::reserve(const roster::RosterState& state,
                                             const roster::Fingerprint& peer) {
    return find_or_reserve(state, peer, true);
}

GroupPairIndexResult GroupPairIndex::find_or_reserve(const roster::RosterState& state,
                                                     const roster::Fingerprint& peer, bool reserve) {
    if (busy_) return {Error::storage_failure};
    const Busy hold(busy_);
    if (state.state() != roster::State::active) return {Error::roster_inactive};
    const RosterSnapshot current(state);
    const auto local_added = current.added_in(local_);
    const auto peer_added = current.added_in(peer);
    if (local_added == 0 || peer_added == 0 || local_ == peer) return {Error::invalid_scope};

    auto& catalog = workspace_.catalog;
    const auto loaded = load(catalog);
    if (loaded != Error::none) return {loaded};
    auto* group = catalog.group_for(current.group);
    if (group && !group->current(current)) return {Error::stale_roster};
    auto vacant = catalog.entries.end();
    for (auto it = catalog.entries.begin(); it != catalog.entries.end(); ++it) {
        if (it->incarnation == 0) {
            if (vacant == catalog.entries.end()) vacant = it;
            continue;
        }
        if (it->names(current.group, peer)) {
            if (current.version < it->roster_version) return {Error::stale_roster};
            if (local_added != it->local_added || peer_added != it->peer_added) {
                return {Error::stale_membership};
            }
            if (reserve && current.version > group->version) {
                *group = {current.group, current.version, current.digest};
                const auto saved = save(catalog);
                if (saved != Error::none) return {saved};
            }
            return {Error::none, GroupPairSlot{static_cast<std::size_t>(it - catalog.entries.begin()),
                                               it->incarnation}};
        }
    }
    if (!reserve) return {Error::not_indexed};
    if (vacant == catalog.entries.end() || catalog.pair_count(current.group) >= kPeersPerGroup) {
        return {Error::capacity};
    }
    if (!group) {
        const auto empty = std::find_if(catalog.groups.begin(), catalog.groups.end(), [](const auto& slot) {
            return slot.version == 0;
        });
        if (empty == catalog.groups.end()) return {Error::capacity};
        group = &*empty;
    }
    if (catalog.high_water == std::numeric_limits<std::uint64_t>::max()) return {Error::exhausted};
    *group = {current.group, current.version, current.digest};
    *vacant = {++catalog.high_water, current.group, peer, local_added, peer_added, current.version};
    const auto saved = save(catalog);
    if (saved != Error::none) return {saved};
    return {Error::none, GroupPairSlot{static_cast<std::size_t>(vacant - catalog.entries.begin()),
                                       vacant->incarnation}};
}

GroupPairIndexError GroupPairIndex::prune(const roster::RosterState& state) {
    if (busy_) return Error::storage_failure;
    const Busy hold(busy_);
    const bool left = state.state() == roster::State::left;
    if (state.state() != roster::State::active && !left) return Error::roster_inactive;
    const RosterSnapshot current(state);
    const auto local_added = current.added_in(local_);
    if ((local_added == 0) != left) return Error::invalid_scope;

    auto& catalog = workspace_.catalog;
    const auto loaded = load(catalog);
    if (loaded != Error::none) return loaded;
    auto* group = catalog.group_for(current.group);
    if (!group) {
        if (left) return Error::not_indexed;
        const auto vacant = std::find_if(catalog.groups.begin(), catalog.groups.end(), [](const auto& slot) {
            return slot.version == 0;
        });
        if (vacant == catalog.groups.end()) return Error::capacity;
        *vacant = {current.group, current.version, current.digest};
        return save(catalog);
    }
    if (!group->current(current)) return Error::stale_roster;
    bool changed = current.version > group->version;
    for (auto& entry : catalog.entries) {
        if (entry.incarnation == 0 || !same_group(entry.group, current.group)) continue;
        if (current.version < entry.roster_version) return Error::stale_roster;
        if (entry.local_added == local_added && entry.peer_added == current.added_in(entry.peer)) continue;
        if (current.version == entry.roster_version) return Error::stale_roster;
        entry = Entry{};
        changed = true;
    }
    if (!changed) return Error::none;
    *group = {current.group, current.version, current.digest};
    return save(catalog);
}

}  // namespace opentrail::persistence
