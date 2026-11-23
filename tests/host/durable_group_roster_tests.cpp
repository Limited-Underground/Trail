#include "opentrail/durable_group_roster.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "opentrail/configuration_store.hpp"

namespace {

using namespace opentrail::identity::roster;
using namespace opentrail::persistence;
using Err = DurableRosterError;
using KvError = PersistentKvBackendError;
using Bytes = std::vector<std::uint8_t>;
using Key = std::pair<std::string, std::string>;

void check(bool ok, const char* expression, int line) {
    if (!ok) {
        std::cerr << "FAIL " << line << ": " << expression << '\n';
        std::exit(EXIT_FAILURE);
    }
}
#define CHECK(e) check(static_cast<bool>(e), #e, __LINE__)

enum class Cut { none, write, before_commit, after_commit, readback };
constexpr char kRosterNs[] = "ot_group_rost";
constexpr char kIndexNs[] = "ot_group_idx";

class Kv final : public PersistentKvBackend {
public:
    KvError read_blob(const char* partition, const char* ns, const char* key, std::uint8_t* out,
                      std::size_t cap, std::size_t& size) override {
        CHECK(std::string(partition) == "ot_state");
        if (on_read) {
            auto callback = std::move(on_read);
            on_read = {};
            callback();
        }
        if (fail_read && failure_namespace == ns) return KvError::io_failure;
        const auto it = disk.find({ns, key});
        if (it == disk.end()) return KvError::not_found;
        size = it->second.size();
        if (size > cap) return KvError::invalid_argument;
        std::copy(it->second.begin(), it->second.end(), out);
        return KvError::none;
    }
    KvError write_blob(const char* partition, const char* ns, const char* key,
                       const std::uint8_t* in, std::size_t size) override {
        CHECK(std::string(partition) == "ot_state");
        staged_key = {ns, key};
        if (failure_namespace == ns && cut == Cut::write) return KvError::io_failure;
        staged.assign(in, in + size);
        return KvError::none;
    }
    KvError erase_key(const char*, const char*, const char*) override {
        CHECK(false);
        return KvError::io_failure;
    }
    KvError commit(const char* partition, const char* ns) override {
        CHECK(std::string(partition) == "ot_state" && staged_key.first == ns && !staged.empty());
        const auto failure = failure_namespace == ns ? cut : Cut::none;
        if (failure == Cut::before_commit) return KvError::io_failure;
        committed.emplace_back(ns, staged.size());
        disk[staged_key] = std::move(staged);
        staged.clear();
        if (failure == Cut::after_commit) return KvError::io_failure;
        fail_read = failure == Cut::readback;
        return KvError::none;
    }
    void reboot() {
        staged.clear();
        fail_read = false;
        cut = Cut::none;
        failure_namespace.clear();
    }

    std::function<void()> on_read;
    std::map<Key, Bytes> disk;
    std::vector<std::pair<std::string, std::size_t>> committed;
    std::string failure_namespace;
    Cut cut{Cut::none};

private:
    Key staged_key;
    Bytes staged;
    bool fail_read{false};
};

// Test stand-in only. RosterState still parses, verifies and calculates the chain.
Signature signature(const PublicKey& key, const std::uint8_t* bytes, std::size_t size) {
    Signature sig{};
    auto hash = 1469598103934665603ULL;
    for (auto value : key) hash = (hash ^ value) * 1099511628211ULL;
    for (std::size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 1099511628211ULL;
    for (std::size_t i = 0; i < sig.size(); ++i) sig[i] = static_cast<std::uint8_t>(hash >> ((i % 8) * 8));
    return sig;
}

class Crypto final : public RosterCrypto {
public:
    bool verify(const Signature& sig, const std::uint8_t* bytes, std::size_t size,
                const PublicKey& key) const override {
        return sig == signature(key, bytes, size);
    }
    bool digest(const std::uint8_t* bytes, std::size_t size, Digest& out) const override {
        const auto hashed = signature({}, bytes, size);
        std::copy_n(hashed.begin(), out.size(), out.begin());
        return true;
    }
    bool fingerprint(const PublicKey& key, Fingerprint& out) const override {
        out = key;
        return true;
    }
};

class Signer final : public RosterSigner {
public:
    explicit Signer(const PublicKey& key) : key_(key) {}
    const PublicKey& public_key() const override { return key_; }
    bool sign(const std::uint8_t* bytes, std::size_t size, Signature& sig) const override {
        sig = signature(key_, bytes, size);
        return true;
    }
private:
    const PublicKey& key_;
};

const Crypto crypto;
constexpr PublicKey admin{1}, local{2}, peer{3}, other{4};
constexpr std::uint64_t group = 0x1122334455667788ULL;

Bytes seal(Roster& roster) {
    CHECK(sign(roster, Signer(admin)));
    Bytes bytes(kMaximumRosterBytes);
    bytes.resize(encode(roster, bytes.data(), bytes.size()));
    CHECK(!bytes.empty());
    return bytes;
}

struct Rosters {
    Roster current{}, removed{}, readded{}, removed_self{};
    Bytes initial_bytes, removed_bytes, readded_bytes, left_bytes;

    explicit Rosters(std::uint64_t group_id = group) {
        Roster first{}, second{};
        CHECK(create_first(group_id, admin, "Admin", LocationRule::off, first));
        seal(first);
        CHECK(add_member(first, crypto, local, "Local", second));
        seal(second);
        CHECK(add_member(second, crypto, peer, "Peer", current));
        initial_bytes = seal(current);
        CHECK(remove_member(current, crypto, peer, removed));
        removed_bytes = seal(removed);
        CHECK(add_member(removed, crypto, peer, "Peer", readded));
        readded_bytes = seal(readded);
        CHECK(remove_member(current, crypto, local, removed_self));
        left_bytes = seal(removed_self);
    }
};

DurableRosterResult offer(DurableGroupRoster& owner, const Bytes& bytes) {
    return owner.offer(bytes.data(), bytes.size());
}

struct Fixture {
    Kv kv;
    GroupPairIndex index{kv, local};
    DurableGroupRoster owner{kv, index, crypto, admin, group, local};
    Rosters rosters;
    DurablePairScope original{};
    Fixture() {
        CHECK(index.initialize() == GroupPairIndexError::none);
        CHECK(owner.reopen() == Err::missing);
        CHECK(owner.initialize() == Err::none);
        CHECK(offer(owner, rosters.initial_bytes).outcome == Outcome::accepted);
        CHECK(!owner.reserve_pair(peer));
        CHECK(owner.accept_member(peer) == Err::none);
        const auto scope = owner.reserve_pair(peer);
        CHECK(scope);
        original = *scope;
        CHECK(!owner.reserve_pair(peer));
        CHECK(owner.find_pair(peer));
    }
};

void publish_and_reopen() {
    Fixture f;
    std::array<std::uint8_t, kMaximumRosterBytes> bytes{};
    const auto n = f.owner.copy_signed_roster(bytes.data(), bytes.size());
    CHECK(n == f.rosters.initial_bytes.size());
    CHECK(std::equal(f.rosters.initial_bytes.begin(), f.rosters.initial_bytes.end(), bytes.begin()));
    CHECK(f.original.local_added == 2 && f.original.peer_added == 3);
    CHECK(f.original.group.admin == admin && f.original.group.group_id == group);
    CHECK(f.original.local == local && f.original.peer == peer);
    CHECK(f.original.roster_version == 3);
    GroupPairIndex index(f.kv, local);
    DurableGroupRoster restored(f.kv, index, crypto, admin, group, local);
    CHECK(restored.reopen() == Err::none);
    CHECK(restored.find_pair(peer)->location.incarnation == f.original.location.incarnation);
    CHECK(!restored.reserve_pair(peer));
    const auto writes = f.kv.committed.size();
    CHECK(offer(restored, f.rosters.initial_bytes).outcome == Outcome::duplicate);
    CHECK(f.kv.committed.size() == writes);
    CHECK(!restored.find_pair(other));
}

void conflict_history() {
    Fixture f;
    Roster fork = f.rosters.current;
    fork.location_rule = LocationRule::preferred;
    const auto fork_bytes = seal(fork);
    CHECK(offer(f.owner, fork_bytes).outcome == Outcome::conflict);
    CHECK(!f.owner.find_pair(peer));
    GroupPairIndex index(f.kv, local);
    DurableGroupRoster restored(f.kv, index, crypto, admin, group, local);
    CHECK(restored.reopen() == Err::none);
    CHECK(!restored.find_pair(peer));
    CHECK(offer(restored, f.rosters.removed_bytes).outcome == Outcome::blocked_by_conflict);
    CHECK(restored.keep_current() == Err::none);
    CHECK(restored.find_pair(peer));

    DurableGroupRoster again(f.kv, index, crypto, admin, group, local);
    CHECK(again.reopen() == Err::none);
    CHECK(offer(again, fork_bytes).outcome == Outcome::resolved_replay);
    CHECK(offer(again, f.rosters.removed_bytes).outcome == Outcome::accepted);
    fork.location_rule = LocationRule::required; // Different fork of an older but remembered version.
    CHECK(offer(again, seal(fork)).outcome == Outcome::conflict);
    CHECK(again.stop_trusting() == Err::none);
    DurableGroupRoster distrusted(f.kv, index, crypto, admin, group, local);
    CHECK(distrusted.reopen() == Err::none);
    CHECK(!distrusted.find_pair(peer));
    CHECK(offer(distrusted, f.rosters.readded_bytes).outcome == Outcome::not_member);
}

void removal_and_readdition() {
    Fixture f;
    f.kv.committed.clear();
    CHECK(offer(f.owner, f.rosters.removed_bytes).outcome == Outcome::accepted);
    CHECK(f.kv.committed.size() == 2);
    CHECK(f.kv.committed[0].first == kRosterNs);
    CHECK(f.kv.committed[1].first == kIndexNs);
    std::cout << "roster change commit bytes: roster=" << f.kv.committed[0].second
              << ", index=" << f.kv.committed[1].second << ", commits=2\n";
    CHECK(!f.owner.find_pair(peer));
    CHECK(offer(f.owner, f.rosters.readded_bytes).outcome == Outcome::accepted);
    CHECK(!f.owner.reserve_pair(peer));
    CHECK(f.owner.accept_member(peer) == Err::none);
    const auto replacement = f.owner.reserve_pair(peer);
    CHECK(replacement && replacement->location.slot == f.original.location.slot);
    CHECK(replacement->location.incarnation > f.original.location.incarnation);
    CHECK(replacement->peer_added == f.rosters.readded.version);
    CHECK(offer(f.owner, f.rosters.initial_bytes).outcome == Outcome::stale);
}

void interrupted_transition() {
    for (const std::string ns : {kRosterNs, kIndexNs}) {
        for (auto cut : {Cut::write, Cut::before_commit, Cut::after_commit, Cut::readback}) {
            Fixture f;
            f.kv.failure_namespace = ns;
            f.kv.cut = cut;
            const auto failed = offer(f.owner, f.rosters.removed_bytes);
            CHECK(failed.error == (ns == kRosterNs ? Err::storage_failure : Err::index_failure));
            CHECK(!f.owner.find_pair(peer));
            CHECK(!f.owner.reserve_pair(peer));

            const bool removal_durable = ns == kIndexNs || cut == Cut::after_commit || cut == Cut::readback;
            f.kv.reboot();
            GroupPairIndex index(f.kv, local);
            DurableGroupRoster restored(f.kv, index, crypto, admin, group, local);
            CHECK(restored.reopen() == Err::none);
            const auto scope = restored.find_pair(peer);
            CHECK(scope.has_value() != removal_durable);
            if (scope) CHECK(scope->location.incarnation == f.original.location.incarnation);
            const auto repeat = offer(restored, f.rosters.removed_bytes);
            CHECK(repeat.error == Err::none);
            CHECK(repeat.outcome == (removal_durable ? Outcome::duplicate : Outcome::accepted));
            CHECK(!restored.find_pair(peer));
        }
    }
}

void missing_or_bad_state() {
    Fixture f;
    // A different board's index, even on valid storage, cannot authorize this roster owner.
    Kv alien_disk;
    GroupPairIndex alien_index(alien_disk, peer);
    CHECK(alien_index.initialize() == GroupPairIndexError::none);
    DurableGroupRoster crossed(f.kv, alien_index, crypto, admin, group, local);
    CHECK(crossed.reopen() == Err::invalid_scope);

    const auto roster = std::find_if(f.kv.disk.begin(), f.kv.disk.end(), [](const auto& entry) {
        return entry.first.first == kRosterNs;
    });
    CHECK(roster != f.kv.disk.end());
    const auto saved = *roster;
    f.kv.disk.erase(roster);
    CHECK(!f.owner.find_pair(peer));
    GroupPairIndex index(f.kv, local);
    DurableGroupRoster restored(f.kv, index, crypto, admin, group, local);
    CHECK(restored.reopen() == Err::missing);
    CHECK(restored.initialize() == Err::missing);
    auto broken = saved.second;
    broken.back() ^= 1;
    f.kv.disk[saved.first] = broken;
    CHECK(restored.reopen() == Err::invalid_record);
    f.kv.disk[saved.first] = saved.second;
    f.kv.disk.erase({kIndexNs, "catalog"});
    CHECK(restored.reopen() == Err::index_failure);
    CHECK(!restored.find_pair(peer));
}

void self_removal() {
    Fixture f;
    CHECK(offer(f.owner, f.rosters.left_bytes).outcome == Outcome::accepted_removed_self);
    CHECK(!f.owner.find_pair(peer));
    GroupPairIndex index(f.kv, local);
    DurableGroupRoster restored(f.kv, index, crypto, admin, group, local);
    CHECK(restored.reopen() == Err::none && !restored.find_pair(peer));
    CHECK(offer(restored, f.rosters.initial_bytes).outcome == Outcome::not_member);
}

void malformed_records() {
    Fixture fixture;
    const auto record = std::find_if(fixture.kv.disk.begin(), fixture.kv.disk.end(), [](const auto& item) {
        return item.first.first == kRosterNs;
    });
    CHECK(record != fixture.kv.disk.end());
    const auto original = record->second;
    const auto committed = fixture.kv.committed.size();
    const auto reject = [&](Bytes malformed, bool update_crc) {
        if (update_crc) {
            const auto length = malformed.size() - sizeof(std::uint32_t);
            const auto crc = configuration_crc32({malformed.data(), length});
            for (std::size_t i = 0; i < sizeof(crc); ++i) {
                malformed[length + i] = static_cast<std::uint8_t>(crc >> (8 * (3 - i)));
            }
        }
        record->second = std::move(malformed);
        GroupPairIndex index(fixture.kv, local);
        DurableGroupRoster owner(fixture.kv, index, crypto, admin, group, local);
        CHECK(owner.reopen() == Err::invalid_record);
        CHECK(!owner.find_pair(peer));
        CHECK(fixture.kv.committed.size() == committed);
    };
    auto malformed = original;
    malformed.pop_back();
    reject(malformed, false);
    malformed = original;
    malformed.push_back(0);
    reject(malformed, false);

    constexpr std::size_t state_offset = 4 + 8 + 2 * kPublicKeyBytes + 4;
    constexpr std::size_t size_offset = state_offset + 5;
    constexpr std::size_t roster_offset = size_offset + 2 + kDigestBytes +
        kKnownDigests * (4 + kDigestBytes) + kResolvedConflicts * kDigestBytes;
    for (const auto offset : {state_offset, state_offset + 1, state_offset + 2, state_offset + 3,
                              state_offset + 4, size_offset, roster_offset}) {
        malformed = original;
        malformed[offset] = 0xff;
        reject(malformed, true);
    }
    malformed = original;
    malformed[original.size() - sizeof(std::uint32_t) - 1] = 1; // nonzero signed-roster padding
    reject(malformed, true);
}

void owned_creation_and_snapshot() {
    Kv kv;
    GroupPairIndex index(kv, admin);
    DurableGroupRoster owner(kv, index, crypto, admin, group, admin);
    CHECK(index.initialize() == GroupPairIndexError::none);
    DurableRosterSlot slot{};
    CHECK(DurableGroupRoster::inspect_slot(kv, 0, slot) == Err::missing);
    CHECK(DurableGroupRoster::inspect_slot(kv, GroupPairIndex::kMaximumGroups, slot) == Err::invalid_scope);

    Roster first{};
    CHECK(create_first(group, admin, "Admin", LocationRule::required, first));
    auto initial = seal(first);
    auto corrupt = initial;
    corrupt.back() ^= 1;
    CHECK(owner.create_owned(corrupt.data(), corrupt.size()) == Err::invalid_record);
    Roster next{};
    CHECK(add_member(first, crypto, peer, "Peer", next));
    const auto successor = seal(next);
    CHECK(owner.create_owned(successor.data(), successor.size()) == Err::invalid_record);
    CHECK(kv.committed.size() == 1);

    CHECK(owner.create_owned(initial.data(), initial.size()) == Err::none);
    CHECK(kv.committed.size() == 3);
    CHECK(kv.committed[1].first == kRosterNs);
    CHECK(kv.committed[1].second == DurableGroupRoster::kStorageRecordBytes);
    CHECK(kv.committed[2].first == kIndexNs);
    CHECK(DurableGroupRoster::inspect_slot(kv, 0, slot) == Err::none);
    CHECK(slot.admin == admin && slot.local == admin && slot.group_id == group);
    CHECK(slot.minimum_version == 1);
    CHECK(DurableGroupRoster::inspect_slot(kv, 1, slot) == Err::missing);
    CHECK(slot.group_id == group);

    GroupPairIndex reopened_index(kv, admin);
    CHECK(reopened_index.reopen() == GroupPairIndexError::none);
    DurableGroupRoster reopened(kv, reopened_index, crypto, slot.admin, slot.group_id,
                                slot.local, slot.minimum_version);
    CHECK(reopened.reopen() == Err::none);
    std::array<std::uint8_t, kMaximumRosterBytes> bytes{};
    DurableRosterSnapshot snapshot{};
    CHECK(reopened.snapshot(snapshot, bytes.data(), initial.size() - 1) == Err::refused);
    CHECK(snapshot.state == State::uninitialized);
    CHECK(reopened.snapshot(snapshot, bytes.data(), bytes.size()) == Err::none);
    CHECK(snapshot.group.admin == admin && snapshot.group.group_id == group);
    CHECK(snapshot.state == State::active && snapshot.version == 1);
    CHECK(snapshot.bytes == initial.size());
    CHECK(std::equal(initial.begin(), initial.end(), bytes.begin()));

    GroupPairIndex unopened_catalog(kv, admin);
    DurableGroupRoster read_only(kv, unopened_catalog, crypto, admin, group, admin);
    CHECK(read_only.read_active_location_rule(0, 1, admin, snapshot.digest) == LocationRule::required);
    CHECK(!read_only.read_active_location_rule(1, 1, admin, snapshot.digest));
    CHECK(!read_only.read_active_location_rule(0, 2, admin, snapshot.digest));
    CHECK(!read_only.read_active_location_rule(0, 1, peer, snapshot.digest));
    auto wrong_digest = snapshot.digest;
    wrong_digest.back() ^= 1;
    CHECK(!read_only.read_active_location_rule(0, 1, admin, wrong_digest));
    CHECK(kv.committed.size() == 3);
    CHECK(owner.create_owned(initial.data(), initial.size()) == Err::refused);
    CHECK(kv.committed.size() == 3);

    // A full catalog reserves every qualified name even when its roster slots
    // are missing. Create refuses before it can leave an unreconcilable record.
    Kv missing_rosters;
    GroupPairIndex full_index(missing_rosters, admin);
    CHECK(full_index.initialize() == GroupPairIndexError::none);
    for (std::size_t i = 1; i <= GroupPairIndex::kMaximumGroups; ++i) {
        Roster occupied{};
        CHECK(create_first(group + i, admin, "Admin", LocationRule::off, occupied));
        const auto signed_roster = seal(occupied);
        RosterState state;
        CHECK(state.initialize(admin, group + i, admin));
        CHECK(state.offer(signed_roster.data(), signed_roster.size(), crypto) == Outcome::accepted);
        CHECK(full_index.prune(state) == GroupPairIndexError::none);
    }
    DurableGroupRoster no_capacity(missing_rosters, full_index, crypto, admin, group, admin);
    CHECK(no_capacity.create_owned(initial.data(), initial.size()) == Err::capacity);
    CHECK(DurableGroupRoster::inspect_slot(missing_rosters, 0, slot) == Err::missing);
    CHECK(missing_rosters.committed.size() == GroupPairIndex::kMaximumGroups + 1);
}

void interrupted_owned_creation() {
    Roster first{};
    CHECK(create_first(group, admin, "Admin", LocationRule::preferred, first));
    const auto bytes = seal(first);
    for (const std::string ns : {kRosterNs, kIndexNs}) {
        for (auto cut : {Cut::write, Cut::before_commit, Cut::after_commit, Cut::readback}) {
            Kv kv;
            GroupPairIndex index(kv, admin);
            CHECK(index.initialize() == GroupPairIndexError::none);
            DurableGroupRoster owner(kv, index, crypto, admin, group, admin);
            kv.failure_namespace = ns;
            kv.cut = cut;
            CHECK(owner.create_owned(bytes.data(), bytes.size()) ==
                  (ns == kRosterNs ? Err::storage_failure : Err::index_failure));
            const bool created = ns == kIndexNs || cut == Cut::after_commit || cut == Cut::readback;
            kv.reboot();
            GroupPairIndex restored_index(kv, admin);
            DurableGroupRoster restored(kv, restored_index, crypto, admin, group, admin);
            CHECK(restored.reopen() == (created ? Err::none : Err::missing));
            if (!created) CHECK(restored.create_owned(bytes.data(), bytes.size()) == Err::none);
            std::array<std::uint8_t, kMaximumRosterBytes> readback{};
            DurableRosterSnapshot snapshot{};
            CHECK(restored.snapshot(snapshot, readback.data(), readback.size()) == Err::none);
            CHECK(snapshot.state == State::active && snapshot.version == 1);
            CHECK(snapshot.bytes == bytes.size());
            CHECK(std::equal(bytes.begin(), bytes.end(), readback.begin()));
        }
    }
}

void terminal_pair_retirement() {
    Fixture f;
    const Rosters secondary(group + 1);
    DurableGroupRoster other_owner(f.kv, f.index, crypto, admin, group + 1, local);
    CHECK(other_owner.initialize() == Err::none);
    CHECK(offer(other_owner, secondary.initial_bytes).outcome == Outcome::accepted);
    CHECK(other_owner.accept_member(peer) == Err::none);
    const auto other_pair = other_owner.reserve_pair(peer);
    CHECK(other_pair);

    DurableGroupRoster stale(f.kv, f.index, crypto, admin, group, local);
    CHECK(stale.reopen() == Err::none);
    CHECK(stale.find_pair(peer));
    RosterState previous;
    CHECK(previous.initialize(admin, group, local));
    CHECK(previous.offer(f.rosters.initial_bytes.data(), f.rosters.initial_bytes.size(), crypto) == Outcome::accepted);

    const auto before = f.kv.committed.size();
    CHECK(f.owner.stop_trusting() == Err::none);
    CHECK(f.kv.committed.size() == before + 2);
    CHECK(f.kv.committed[before].first == kRosterNs);
    CHECK(f.kv.committed[before + 1].first == kIndexNs);
    CHECK(f.index.group_status({admin, group}) == GroupPairIndexError::none);
    CHECK(f.index.find(previous, peer).error == GroupPairIndexError::not_indexed);
    CHECK(!stale.find_pair(peer) && !stale.reserve_pair(peer));
    const auto other_after = other_owner.find_pair(peer);
    CHECK(other_after && other_after->location.incarnation == other_pair->location.incarnation);

    GroupPairIndex restored_index(f.kv, local);
    DurableGroupRoster restored(f.kv, restored_index, crypto, admin, group, local);
    CHECK(restored.reopen() == Err::none);
    CHECK(restored.stop_trusting() == Err::none);
    CHECK(f.kv.committed.size() == before + 2);
    std::array<std::uint8_t, kMaximumRosterBytes> readback{};
    DurableRosterSnapshot snapshot{};
    CHECK(restored.snapshot(snapshot, readback.data(), readback.size()) == Err::none);
    CHECK(snapshot.state == State::distrusted && snapshot.digest == previous.digest());
    CHECK(snapshot.bytes == f.rosters.initial_bytes.size());
    CHECK(std::equal(f.rosters.initial_bytes.begin(), f.rosters.initial_bytes.end(), readback.begin()));
    CHECK(restored.copy_signed_roster(readback.data(), readback.size()) == 0);
    CHECK(!restored.read_active_location_rule(0, snapshot.version, admin, snapshot.digest));
    CHECK(f.kv.committed.size() == before + 2);
    CHECK(!restored.find_pair(peer) && !restored.reserve_pair(peer));
}

void interrupted_distrust_retirement() {
    for (const std::string ns : {kRosterNs, kIndexNs}) {
        for (auto cut : {Cut::write, Cut::before_commit, Cut::after_commit, Cut::readback}) {
            Fixture f;
            f.kv.failure_namespace = ns;
            f.kv.cut = cut;
            CHECK(f.owner.stop_trusting() ==
                  (ns == kRosterNs ? Err::storage_failure : Err::index_failure));
            CHECK(!f.owner.find_pair(peer) && !f.owner.reserve_pair(peer));
            const bool retired = ns == kIndexNs || cut == Cut::after_commit || cut == Cut::readback;
            f.kv.reboot();
            GroupPairIndex restored_index(f.kv, local);
            DurableGroupRoster restored(f.kv, restored_index, crypto, admin, group, local);
            CHECK(restored.reopen() == Err::none);
            CHECK(restored.find_pair(peer).has_value() != retired);
            std::array<std::uint8_t, kMaximumRosterBytes> readback{};
            DurableRosterSnapshot snapshot{};
            CHECK(restored.snapshot(snapshot, readback.data(), readback.size()) == Err::none);
            CHECK(snapshot.state == (retired ? State::distrusted : State::active));
            CHECK(restored.stop_trusting() == Err::none);
            CHECK(!restored.find_pair(peer) && !restored.reserve_pair(peer));
        }
    }
}

void transaction_reentry() {
    Fixture fixture;
    fixture.kv.on_read = [&] {
        CHECK(!fixture.owner.find_pair(peer));
        CHECK(fixture.owner.keep_current() == Err::not_ready);
    };
    CHECK(offer(fixture.owner, fixture.rosters.removed_bytes).outcome == Outcome::accepted);
    CHECK(!fixture.owner.find_pair(peer));
    CHECK(offer(fixture.owner, fixture.rosters.readded_bytes).outcome == Outcome::accepted);
    CHECK(fixture.owner.accept_member(peer) == Err::none);
    const auto replacement = fixture.owner.reserve_pair(peer);
    CHECK(replacement && replacement->location.incarnation > fixture.original.location.incarnation);
}

}  // namespace

int main() {
    publish_and_reopen();
    conflict_history();
    removal_and_readdition();
    interrupted_transition();
    missing_or_bad_state();
    self_removal();
    malformed_records();
    transaction_reentry();
    owned_creation_and_snapshot();
    interrupted_owned_creation();
    terminal_pair_retirement();
    interrupted_distrust_retirement();
    std::cout << "PASS: 12 durable group roster scenario groups\n";
}
