#include "opentrail/group_pair_index.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "opentrail/configuration_store.hpp"

namespace {

using namespace opentrail::identity::roster;
using namespace opentrail::persistence;
using Error = GroupPairIndexError;
using KvError = PersistentKvBackendError;

void check(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL " << line << ": " << expression << '\n';
        std::exit(1);
    }
}
#define CHECK(expression) check((expression), #expression, __LINE__)

enum class Fault { none, read, write, unapplied, applied, readback };

class Storage final : public PersistentKvBackend {
public:
    KvError read_blob(const char* partition, const char* space, const char* key,
                      std::uint8_t* out, std::size_t capacity, std::size_t& size) override {
        check_path(partition, space, key);
        if (on_read) {
            auto callback = std::move(on_read);
            on_read = {};
            callback();
        }
        if (fault == Fault::read || fail_readback) return KvError::io_failure;
        if (durable.empty()) return KvError::not_found;
        size = durable.size();
        if (size > capacity) return KvError::invalid_argument;
        std::copy(durable.begin(), durable.end(), out);
        return KvError::none;
    }
    KvError write_blob(const char* partition, const char* space, const char* key,
                       const std::uint8_t* data, std::size_t size) override {
        check_path(partition, space, key);
        ++writes;
        if (fault == Fault::write) return KvError::io_failure;
        pending.assign(data, data + size);
        return KvError::none;
    }
    KvError erase_key(const char*, const char*, const char*) override {
        CHECK(false);
        return KvError::io_failure;
    }
    KvError commit(const char* partition, const char* space) override {
        check_path(partition, space, "catalog");
        CHECK(!pending.empty());
        if (fault == Fault::unapplied) return KvError::io_failure;
        durable.swap(pending);
        pending.clear();
        fail_readback = fault == Fault::readback;
        return fault == Fault::applied ? KvError::io_failure : KvError::none;
    }
    void reboot() {
        pending.clear();
        fault = Fault::none;
        fail_readback = false;
    }

    std::function<void()> on_read;
    std::vector<std::uint8_t> durable;
    Fault fault{Fault::none};
    unsigned writes{0};

private:
    static void check_path(const char* partition, const char* space, const char* key) {
        CHECK(std::string(partition) == "ot_state");
        CHECK(std::string(space) == "ot_group_idx");
        CHECK(std::string(key) == "catalog");
    }
    std::vector<std::uint8_t> pending;
    bool fail_readback{false};
};

// Test-only identity and digest provider. It exercises actual roster parsing and acceptance.
Signature signature(const PublicKey& key) {
    Signature value{};
    std::copy(key.begin(), key.end(), value.begin());
    return value;
}

class TestCrypto final : public RosterCrypto {
public:
    bool verify(const Signature& sig, const std::uint8_t*, std::size_t,
                const PublicKey& key) const override {
        return sig == signature(key);
    }
    bool digest(const std::uint8_t* data, std::size_t size, Digest& out) const override {
        std::uint64_t hash = 1469598103934665603ULL;
        for (std::size_t i = 0; i < size; ++i) hash = (hash ^ data[i]) * 1099511628211ULL;
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i] = static_cast<std::uint8_t>(hash >> (8 * (i % 8)));
        }
        return true;
    }
    bool fingerprint(const PublicKey& key, Fingerprint& out) const override {
        out = key;
        return true;
    }
};

constexpr PublicKey id(std::uint8_t last) {
    PublicKey key{};
    key[0] = 1;
    key.back() = last;
    return key;
}

const TestCrypto crypto{};
constexpr auto self = id(1), peer = id(2), other = id(3);

Outcome offer(RosterState& state, Roster roster) {
    roster.signature = signature(roster.members[0].key);
    std::array<std::uint8_t, kMaximumRosterBytes> bytes{};
    const auto size = encode(roster, bytes.data(), bytes.size());
    CHECK(size != 0);
    return state.offer(bytes.data(), size, crypto);
}

struct Group {
    RosterState state;
    Roster roster{};
    Group(std::uint64_t number, const PublicKey& admin = self) {
        CHECK(state.initialize(admin, number, self));
        CHECK(create_first(number, admin, "Admin", LocationRule::off, roster));
        roster.signature = signature(admin);
        if (admin != self) add(self);
        else CHECK(offer(state, roster) == Outcome::accepted);
    }
    void add(const PublicKey& member) {
        Roster next{};
        CHECK(add_member(roster, crypto, member, std::to_string(member.back()), next));
        apply(next);
    }
    void remove(const PublicKey& member) {
        Roster next{};
        CHECK(remove_member(roster, crypto, member, next));
        apply(next);
    }
    void apply(Roster next) {
        next.signature = signature(next.members[0].key);
        CHECK(offer(state, next) == Outcome::accepted);
        roster = next;
    }
};

GroupPairSlot allocated(GroupPairIndex& index, const RosterState& roster, const Fingerprint& fingerprint) {
    const auto result = index.reserve(roster, fingerprint);
    CHECK(result.error == Error::none && result.location);
    return *result.location;
}

void identity_and_restart() {
    Storage disk;
    Group first(17);
    first.add(peer);
    first.add(other);
    Group lookalike(17, id(4));
    lookalike.add(peer);
    GroupPairIndex index(disk, self);
    CHECK(index.reserve(first.state, peer).error == Error::missing);
    CHECK(disk.writes == 0);
    CHECK(index.initialize() == Error::none);
    const auto primary = allocated(index, first.state, peer);
    CHECK(allocated(index, first.state, other).slot != primary.slot);
    CHECK(allocated(index, lookalike.state, peer).slot != primary.slot);

    GroupPairIndex reopened(disk, self);
    const auto located = reopened.find(first.state, peer);
    CHECK(located.location && located.location->slot == primary.slot);
    CHECK(located.location->incarnation == primary.incarnation);
    const auto writes = disk.writes;
    CHECK(reopened.initialize() == Error::none);
    CHECK(allocated(reopened, first.state, peer).incarnation == primary.incarnation);
    CHECK(reopened.find(first.state, id(5)).error == Error::invalid_scope);
    CHECK(reopened.reserve(first.state, self).error == Error::invalid_scope);
    GroupPairIndex wrong_board(disk, peer);
    CHECK(wrong_board.initialize() == Error::invalid_scope);
    CHECK(writes == disk.writes);
}

void roster_first() {
    Storage disk;
    Group group(17);
    group.add(peer);
    group.add(other);
    GroupPairIndex index(disk, self);
    CHECK(index.initialize() == Error::none);
    const auto old = allocated(index, group.state, peer);
    const auto survivor = allocated(index, group.state, other);

    const auto previous = group.state;
    group.remove(peer);
    CHECK(index.find(group.state, peer).error == Error::invalid_scope);
    GroupPairIndex rebooted(disk, self);
    CHECK(!rebooted.find(group.state, peer).location);
    CHECK(rebooted.prune(group.state) == Error::none);
    CHECK(rebooted.find(previous, peer).error == Error::stale_roster);
    CHECK(rebooted.reserve(previous, peer).error == Error::stale_roster);
    group.add(peer);
    const auto replacement = allocated(rebooted, group.state, peer);
    CHECK(replacement.slot == old.slot && replacement.incarnation > old.incarnation);
    CHECK(rebooted.find(previous, peer).error == Error::stale_roster);
    CHECK(rebooted.prune(previous) == Error::stale_roster);
    const auto kept = rebooted.find(group.state, other);
    CHECK(kept.location && kept.location->incarnation == survivor.incarnation);

    auto fork = group.roster;
    fork.location_rule = LocationRule::required;
    RosterState competing_view;
    CHECK(competing_view.initialize(self, group.roster.group_id, self));
    CHECK(offer(competing_view, fork) == Outcome::accepted);
    CHECK(rebooted.find(competing_view, peer).error == Error::stale_roster);
    CHECK(rebooted.reserve(competing_view, peer).error == Error::stale_roster);
    CHECK(rebooted.prune(competing_view) == Error::stale_roster);
    CHECK(offer(group.state, fork) == Outcome::conflict);
    CHECK(rebooted.find(group.state, peer).error == Error::roster_inactive);
    CHECK(rebooted.prune(group.state) == Error::roster_inactive);
    CHECK(group.state.keep_current());
    CHECK(rebooted.find(group.state, peer).location->incarnation == replacement.incarnation);
}

void capacity() {
    Storage disk;
    GroupPairIndex index(disk, self);
    CHECK(index.initialize() == Error::none);
    Group extra(10);
    extra.add(peer);
    for (std::uint64_t group_id = 1; group_id <= 4; ++group_id) {
        Group group(group_id);
        for (std::uint8_t member = 2; member <= 6; ++member) group.add(id(member));
        for (std::uint8_t member = 2; member <= 6; ++member) {
            CHECK(allocated(index, group.state, id(member)).slot < GroupPairIndex::kMaximumPairs);
        }
        if (group_id == 1) {
            group.remove(peer);
            group.add(id(7));
            CHECK(index.reserve(group.state, id(7)).error == Error::capacity);
        }
        if (group_id == 4) CHECK(index.reserve(extra.state, peer).error == Error::capacity);
    }
}

void faults_and_recovery() {
    for (const auto fault : {Fault::write, Fault::unapplied, Fault::applied, Fault::readback}) {
        Storage disk;
        Group group(5);
        group.add(peer);
        GroupPairIndex index(disk, self);
        CHECK(index.initialize() == Error::none);
        disk.fault = fault;
        const auto failed = index.reserve(group.state, peer);
        CHECK(failed.error == Error::storage_failure && !failed.location);
        CHECK(index.find(group.state, peer).error == Error::storage_failure);

        disk.reboot();
        GroupPairIndex reopened(disk, self);
        const auto recovered = reopened.find(group.state, peer);
        CHECK(recovered.location.has_value() == (fault == Fault::applied || fault == Fault::readback));
        CHECK(allocated(reopened, group.state, peer).incarnation == 1);
    }

    Storage disk;
    Group group(5);
    group.add(peer);
    GroupPairIndex index(disk, self);
    CHECK(index.initialize() == Error::none);
    allocated(index, group.state, peer);
    group.remove(peer);
    disk.fault = Fault::unapplied;
    CHECK(index.prune(group.state) == Error::storage_failure);
    disk.reboot();
    GroupPairIndex retry(disk, self);
    CHECK(!retry.find(group.state, peer).location);
    CHECK(retry.prune(group.state) == Error::none);
    group.add(peer);
    CHECK(allocated(retry, group.state, peer).incarnation == 2);
}

void invalid_catalog() {
    Storage disk;
    Group group(7);
    group.add(peer);
    group.add(other);
    GroupPairIndex index(disk, self);
    CHECK(index.initialize() == Error::none);
    allocated(index, group.state, peer);
    const auto original = disk.durable;
    const auto writes = disk.writes;

    disk.fault = Fault::read;
    CHECK(index.find(group.state, peer).error == Error::storage_failure);
    disk.reboot();
    disk.durable.front() ^= 1U;
    CHECK(index.initialize() == Error::invalid_record);
    disk.durable = original;
    disk.durable.pop_back();
    CHECK(index.reserve(group.state, other).error == Error::invalid_record);
    CHECK(disk.writes == writes);

    // Set the encoded catalog generation at its limit with a valid checksum.
    disk.durable = original;
    std::fill_n(disk.durable.begin() + 5 + kFingerprintBytes, 8, 0xff);
    const auto data_size = disk.durable.size() - sizeof(std::uint32_t);
    const auto crc = configuration_crc32({disk.durable.data(), data_size});
    for (std::size_t i = 0; i < 4; ++i) {
        disk.durable[data_size + i] = static_cast<std::uint8_t>(crc >> (8 * (3 - i)));
    }
    CHECK(index.find(group.state, peer).location->incarnation == 1);
    CHECK(index.reserve(group.state, other).error == Error::exhausted);
    CHECK(disk.writes == writes);

    // Simulate the catalog being missing after storage was erased. Normal lookup refuses.
    disk.durable.clear();
    CHECK(index.find(group.state, peer).error == Error::missing);
    CHECK(index.reserve(group.state, peer).error == Error::missing);
    CHECK(disk.writes == writes);
}

void serialized_workspace() {
    Storage storage;
    Group group(23);
    group.add(peer);
    group.add(other);
    GroupPairIndex index(storage, self);
    CHECK(index.initialize() == Error::none);
    const auto first = allocated(index, group.state, peer);
    storage.on_read = [&] {
        CHECK(index.find(group.state, peer).error == Error::storage_failure);
        CHECK(index.reserve(group.state, other).error == Error::storage_failure);
        CHECK(index.prune(group.state) == Error::storage_failure);
        CHECK(index.group_status({self, 23}) == Error::storage_failure);
        CHECK(index.initialize() == Error::storage_failure);
    };
    const auto second = allocated(index, group.state, other);
    CHECK(second.slot != first.slot && second.incarnation > first.incarnation);
    CHECK(index.find(group.state, peer).location->incarnation == first.incarnation);
}

}  // namespace

int main() {
    identity_and_restart();
    roster_first();
    capacity();
    faults_and_recovery();
    invalid_catalog();
    serialized_workspace();
    std::cout << "PASS: 6 group pair index scenario groups\n";
}
