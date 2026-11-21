#include "opentrail/companion_group_owner.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace opentrail::companion;
namespace roster = opentrail::identity::roster;
using opentrail::persistence::PersistentKvBackend;
using KvError = opentrail::persistence::PersistentKvBackendError;
using Operation = GroupConfigurationOperation;
using Status = GroupConfigurationStatus;
using ReadState = GroupConfigurationRosterState;
using Bytes = std::vector<std::uint8_t>;

void check(bool value, const char* expression, int line) {
    if (value) return;
    std::cerr << "FAIL " << line << ": " << expression << '\n';
    std::exit(EXIT_FAILURE);
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

class MemoryStore final : public PersistentKvBackend {
public:
    enum class Cut { none, before_commit, after_commit };

    explicit MemoryStore(const char* name) : name_(name) {}

    KvError read_blob(const char* partition, const char* name, const char* key,
        std::uint8_t* bytes, std::size_t capacity, std::size_t& size) override {
        CHECK(std::string(partition) == "ot_state" && name_ == name);
        const auto stored = records.find(key);
        if (stored == records.end()) return KvError::not_found;
        size = stored->second.size();
        if (size > capacity) return KvError::invalid_argument;
        std::copy(stored->second.begin(), stored->second.end(), bytes);
        return KvError::none;
    }

    KvError write_blob(const char* partition, const char* name, const char* key,
        const std::uint8_t* bytes, std::size_t size) override {
        CHECK(std::string(partition) == "ot_state" && name_ == name && pending_.empty());
        key_ = key;
        pending_.assign(bytes, bytes + size);
        return KvError::none;
    }

    KvError erase_key(const char*, const char*, const char*) override {
        CHECK(false);
        return KvError::io_failure;
    }

    KvError commit(const char* partition, const char* name) override {
        CHECK(std::string(partition) == "ot_state" && name_ == name && !pending_.empty());
        ++commit_attempts;
        const auto fault = commit_attempts == cut_at ? cut : Cut::none;
        if (fault == Cut::before_commit) return KvError::io_failure;
        records[key_] = std::move(pending_);
        pending_.clear();
        if (on_commit) on_commit();
        return fault == Cut::after_commit ? KvError::io_failure : KvError::none;
    }

    void reboot() {
        pending_.clear();
        cut = Cut::none;
        on_commit = {};
    }

    std::map<std::string, Bytes> records;
    std::size_t commit_attempts{};
    std::size_t cut_at{1};
    Cut cut{Cut::none};
    std::function<void()> on_commit;

private:
    const std::string name_;
    std::string key_;
    Bytes pending_;
};

// Deterministic stand-in. The real roster parser and signature/digest contract
// are exercised here; Ed25519 is covered by the focused signer suite.
roster::Signature test_signature(const roster::PublicKey& key,
    const std::uint8_t* bytes, std::size_t size) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto byte : key) hash = (hash ^ byte) * 1099511628211ULL;
    for (std::size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 1099511628211ULL;
    roster::Signature result{};
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<std::uint8_t>(hash >> (8 * (i % sizeof(hash))));
    return result;
}

class TestCrypto final : public roster::RosterCrypto {
public:
    bool verify(const roster::Signature& signature, const std::uint8_t* bytes,
        std::size_t size, const roster::PublicKey& sender) const override {
        return signature == test_signature(sender, bytes, size);
    }

    bool digest(const std::uint8_t* bytes, std::size_t size, roster::Digest& out) const override {
        const auto hash = test_signature({}, bytes, size);
        std::copy_n(hash.begin(), out.size(), out.begin());
        return true;
    }

    bool fingerprint(const roster::PublicKey& key, roster::Fingerprint& out) const override {
        out = key;
        return true;
    }
};

class TestSigner final : public roster::RosterSigner {
public:
    const roster::PublicKey& public_key() const override { return key; }

    bool sign(const std::uint8_t* bytes, std::size_t size, roster::Signature& out) const override {
        if (on_sign) on_sign();
        out = test_signature(key, bytes, size);
        return true;
    }

    roster::PublicKey key{1, 2, 3};
    std::function<void()> on_sign;
};

class TestPlatform final : public GroupConfigurationPlatform {
public:
    const roster::RosterSigner* prepare() noexcept override { return ready ? &signer : nullptr; }

    bool random_group_id(std::uint64_t& out) noexcept override {
        if (next_id == ids.size()) return false;
        out = ids[next_id++];
        return true;
    }

    TestSigner signer;
    bool ready{true};
    std::size_t next_id{};
    std::vector<std::uint64_t> ids{11, 22, 33, 44};
};

class TestAuthority final : public DeviceNameAuthoritySource {
public:
    DeviceNameAuthority current() noexcept override { return state; }
    DeviceNameAuthority state{DeviceNamePhase::connected, {1, 2, 3, 4, 5, 6, 7}, 100};
};

struct Fixture {
    TestAuthority authority;
    MemoryStore rosters{"ot_group_rost"};
    MemoryStore catalog{"ot_group_idx"};
    TestCrypto crypto;
    TestPlatform platform;
    DeviceNameContext session{authority.state.context};
    ConfigurationGroupOwner owner{authority, rosters, catalog, crypto, platform};

    GroupConfigurationResult execute(ConfigurationGroupOwner& current,
        const GroupConfigurationCommand& command) {
        return current.execute(session, command, authority.state.now_ms);
    }

    GroupConfigurationResult create(ConfigurationGroupOwner& current) {
        return execute(current,
            {Operation::create, static_cast<std::uint8_t>(roster::LocationRule::required)});
    }
};

GroupConfigurationCommand read_roster(std::uint8_t slot, std::uint16_t offset = 0,
    ReadState state = ReadState::none, const GroupConfigurationScope& scope = {}) {
    GroupConfigurationCommand command{Operation::read_roster, 0, scope};
    command.read = {slot, state, offset};
    return command;
}

Bytes reassemble(Fixture& fixture, ConfigurationGroupOwner& owner,
    std::uint8_t slot, const GroupConfigurationScope& scope, ReadState state) {
    Bytes assembled;
    std::size_t total = 0;
    do {
        const auto offset = static_cast<std::uint16_t>(assembled.size());
        const auto command = offset == 0 ? read_roster(slot) : read_roster(slot, offset, state, scope);
        const auto result = fixture.execute(owner, command);
        CHECK(result.status == Status::ok && result.scope == scope);
        const auto& chunk = result.fragment;
        CHECK(chunk.slot == slot && chunk.state == state && chunk.offset == offset);
        if (total == 0) total = chunk.total_bytes;
        CHECK(chunk.total_bytes == total && total <= kConfigurationGroupRosterBytes);
        CHECK(assembled.size() < total && chunk.size != 0 && chunk.size <= chunk.bytes.size());
        CHECK(chunk.size == std::min(chunk.bytes.size(), total - assembled.size()));
        assembled.insert(assembled.end(), chunk.bytes.begin(), chunk.bytes.begin() + chunk.size);
    } while (assembled.size() < total);
    CHECK(assembled.size() == total);
    return assembled;
}

void discover_and_create_confirmed_signed_roster() {
    Fixture f;
    f.platform.ready = false;
    CHECK(!f.owner.available());
    CHECK(f.create(f.owner).status == Status::unavailable);
    f.platform.ready = true;
    CHECK(f.owner.available());
    CHECK(f.execute(f.owner, {Operation::discover}).status == Status::ok);
    CHECK(f.execute(f.owner, read_roster(0)).status == Status::missing);
    CHECK(f.catalog.records.empty() && f.rosters.records.empty());

    const auto created = f.create(f.owner);
    CHECK(created.status == Status::ok && created.scope.group_id == 11);
    CHECK(created.scope.admin == f.platform.signer.public_key());
    CHECK(f.catalog.records.at("catalog").size() ==
        opentrail::persistence::GroupPairIndex::kMaximumSerializedBytes);
    CHECK(f.rosters.records.at("g0").size() == roster::DurableGroupRoster::kStorageRecordBytes);
    CHECK(f.catalog.commit_attempts == 2 && f.rosters.commit_attempts == 1);

    const auto bytes = reassemble(f, f.owner, 0, created.scope, ReadState::active);
    roster::Roster decoded{};
    CHECK(roster::decode(bytes.data(), bytes.size(), decoded) == roster::FormatError::none);
    CHECK(decoded.version == 1 && decoded.group_id == created.scope.group_id && decoded.member_count == 1);
    CHECK(decoded.location_rule == roster::LocationRule::required);
    CHECK(roster::verify_signature(decoded, f.platform.signer.public_key(), f.crypto));
    roster::Digest digest{};
    CHECK(roster::roster_digest(decoded, f.crypto, digest) && digest == created.scope.roster_digest);
    CHECK(f.catalog.commit_attempts == 2 && f.rosters.commit_attempts == 1);
}

void bounded_randomness_and_collision() {
    Fixture f;
    f.platform.ids = {0, 0, 0, 0, 5};
    CHECK(f.create(f.owner).status == Status::refused && f.platform.next_id == 4);
    CHECK(f.catalog.records.empty() && f.rosters.records.empty());

    const auto first = f.create(f.owner);
    CHECK(first.status == Status::ok && first.scope.group_id == 5);
    CHECK(f.create(f.owner).status == Status::unavailable);
    f.platform.ids.insert(f.platform.ids.end(), {5, 0, 6});
    const auto second = f.create(f.owner);
    CHECK(second.status == Status::ok && second.scope.group_id == 6);
    CHECK(f.rosters.records.size() == 2 && f.rosters.commit_attempts == 2);
}

void distrust_rejects_old_scope_and_keeps_other_group() {
    Fixture f;
    const auto first = f.create(f.owner);
    const auto second = f.create(f.owner);
    CHECK(first.status == Status::ok && second.status == Status::ok);
    const auto first_bytes = reassemble(f, f.owner, 0, first.scope, ReadState::active);
    const auto second_bytes = reassemble(f, f.owner, 1, second.scope, ReadState::active);

    auto wrong_scope = first.scope;
    wrong_scope.roster_digest.front() ^= 1;
    CHECK(f.execute(f.owner, read_roster(0, 42, ReadState::active, wrong_scope)).status == Status::stale);
    CHECK(f.execute(f.owner, {Operation::stop_trust, 0, wrong_scope}).status == Status::stale);
    wrong_scope = first.scope;
    wrong_scope.admin.front() ^= 1;
    CHECK(f.execute(f.owner, {Operation::stop_trust, 0, wrong_scope}).status == Status::stale);
    CHECK(f.rosters.commit_attempts == 2);

    const auto stopped = f.execute(f.owner, {Operation::stop_trust, 0, first.scope});
    CHECK(stopped.status == Status::ok && stopped.scope == first.scope);
    CHECK(f.execute(f.owner, read_roster(0, 42, ReadState::active, first.scope)).status == Status::stale);
    CHECK(f.execute(f.owner, {Operation::stop_trust, 0, first.scope}).status == Status::ok);
    CHECK(f.rosters.commit_attempts == 3);

    ConfigurationGroupOwner reopened{f.authority, f.rosters, f.catalog, f.crypto, f.platform};
    CHECK(reassemble(f, reopened, 0, first.scope, ReadState::distrusted) == first_bytes);
    CHECK(reassemble(f, reopened, 1, second.scope, ReadState::active) == second_bytes);
    CHECK(f.rosters.commit_attempts == 3);
}

void roster_commit_interruption_requires_readback() {
    for (const auto cut : {MemoryStore::Cut::before_commit, MemoryStore::Cut::after_commit}) {
        Fixture f;
        f.rosters.cut = cut;
        const auto created = f.create(f.owner);
        CHECK(created.status == Status::uncertain && created.scope == GroupConfigurationScope{});
        f.rosters.reboot();
        f.catalog.reboot();

        ConfigurationGroupOwner reopened{f.authority, f.rosters, f.catalog, f.crypto, f.platform};
        const auto retained = f.execute(reopened, read_roster(0));
        if (cut == MemoryStore::Cut::before_commit) {
            CHECK(retained.status == Status::missing && f.rosters.records.empty());
        } else {
            CHECK(retained.status == Status::ok && retained.scope.group_id == 11);
            CHECK(retained.fragment.state == ReadState::active && f.catalog.commit_attempts == 2);
        }
    }
}

void catalog_commit_interruption_requires_reconciliation() {
    for (const auto cut : {MemoryStore::Cut::before_commit, MemoryStore::Cut::after_commit}) {
        Fixture f;
        // First commit commissions the empty catalog; second records the signed roster watermark.
        f.catalog.cut_at = 2;
        f.catalog.cut = cut;
        const auto created = f.create(f.owner);
        CHECK(created.status == Status::uncertain && created.scope == GroupConfigurationScope{});
        CHECK(f.rosters.records.size() == 1);
        f.rosters.reboot();
        f.catalog.reboot();

        ConfigurationGroupOwner reopened{f.authority, f.rosters, f.catalog, f.crypto, f.platform};
        const auto retained = f.execute(reopened, read_roster(0));
        CHECK(retained.status == Status::ok && retained.scope.group_id == 11);
        CHECK(retained.fragment.state == ReadState::active);
        CHECK(f.catalog.commit_attempts == (cut == MemoryStore::Cut::before_commit ? 3U : 2U));
    }
}

void uncertain_stop_trust_recovers_distrusted_state() {
    Fixture f;
    const auto first = f.create(f.owner);
    const auto second = f.create(f.owner);
    CHECK(first.status == Status::ok && second.status == Status::ok);
    f.rosters.cut_at = 3;
    f.rosters.cut = MemoryStore::Cut::after_commit;

    const auto stopped = f.execute(f.owner, {Operation::stop_trust, 0, first.scope});
    CHECK(stopped.status == Status::uncertain && stopped.scope == GroupConfigurationScope{});
    f.rosters.reboot();
    ConfigurationGroupOwner reopened{f.authority, f.rosters, f.catalog, f.crypto, f.platform};
    const auto distrusted = f.execute(reopened, read_roster(0));
    const auto untouched = f.execute(reopened, read_roster(1));
    CHECK(distrusted.status == Status::ok && distrusted.scope == first.scope);
    CHECK(distrusted.fragment.state == ReadState::distrusted);
    CHECK(untouched.status == Status::ok && untouched.scope == second.scope);
    CHECK(untouched.fragment.state == ReadState::active);
    CHECK(f.execute(reopened, {Operation::stop_trust, 0, first.scope}).status == Status::ok);
    CHECK(f.rosters.commit_attempts == 3);
}

void disconnect_before_mutation_refuses() {
    Fixture f;
    f.platform.signer.on_sign = [&] { f.authority.state.phase = DeviceNamePhase::disconnected; };
    CHECK(f.create(f.owner).status == Status::refused);
    CHECK(f.catalog.commit_attempts == 0 && f.rosters.commit_attempts == 0);

    f.authority.state.phase = DeviceNamePhase::connected;
    f.platform.signer.on_sign = {};
    f.catalog.on_commit = [&] { ++f.authority.state.context.session_nonce; };
    CHECK(f.create(f.owner).status == Status::refused);
    CHECK(f.catalog.commit_attempts == 1 && f.rosters.commit_attempts == 0);
    CHECK(f.create(f.owner).status == Status::refused);
    CHECK(f.catalog.commit_attempts == 1 && f.rosters.commit_attempts == 0);
}
void deadline_expiry_before_roster_write_refuses() {
    Fixture f;
    f.platform.signer.on_sign = [&] { f.authority.state.now_ms += 5001; };
    CHECK(f.create(f.owner).status == Status::refused);
    CHECK(f.catalog.commit_attempts == 0 && f.rosters.commit_attempts == 0);

    f.platform.signer.on_sign = {};
    f.catalog.on_commit = [&] { f.authority.state.now_ms += 5001; };
    CHECK(f.create(f.owner).status == Status::refused);
    CHECK(f.catalog.commit_attempts == 1 && f.rosters.commit_attempts == 0);
    CHECK(f.rosters.records.empty());
}
} // namespace

int main() {
    discover_and_create_confirmed_signed_roster();
    bounded_randomness_and_collision();
    distrust_rejects_old_scope_and_keeps_other_group();
    roster_commit_interruption_requires_readback();
    catalog_commit_interruption_requires_reconciliation();
    uncertain_stop_trust_recovers_distrusted_state();
    disconnect_before_mutation_refuses();
    deadline_expiry_before_roster_write_refuses();
    std::cout << "PASS: 8 concrete companion group owner cases\n";
}
