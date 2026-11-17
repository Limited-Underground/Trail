#include "opentrail/durable_group_roster.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

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

    Rosters() {
        Roster first{}, second{};
        CHECK(create_first(group, admin, "Admin", LocationRule::off, first));
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

}  // namespace

int main() {
    publish_and_reopen();
    conflict_history();
    removal_and_readdition();
    interrupted_transition();
    missing_or_bad_state();
    self_removal();
    std::cout << "PASS: 6 durable group roster scenario groups\n";
}
