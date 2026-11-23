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

    bool random_invite_id(roster::qr::Nonce& out) noexcept override {
        if (!invites_ready) return false;
        out = {};
        out[0] = 0x5a;
        out[1] = static_cast<std::uint8_t>(++next_invite);
        out[15] = static_cast<std::uint8_t>(signer.key[0]);
        return true;
    }

    const roster::RosterSigner* invite_signer(const roster::qr::Descriptor& d) noexcept override {
        last_descriptor = d;
        return invites_ready ? &signer : nullptr;
    }

    TestSigner signer;
    bool ready{true};
    std::size_t next_id{};
    std::vector<std::uint64_t> ids{11, 22, 33, 44};
    bool invites_ready{true};
    unsigned next_invite{};
    roster::qr::Descriptor last_descriptor{};
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
    MemoryStore joins{"ot_group_join"};
    TestCrypto crypto;
    TestPlatform platform;
    DeviceNameContext session{authority.state.context};
    ConfigurationGroupOwner owner{authority, rosters, catalog, joins, crypto, platform};

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

// ---- F8 live Invite and Join (QR route) ----

GroupConfigurationLabel label_of(const std::string& text) {
    GroupConfigurationLabel label{};
    label.size = static_cast<std::uint8_t>(text.size());
    std::copy(text.begin(), text.end(), label.bytes.begin());
    return label;
}

GroupConfigurationCommand invite_command(const GroupConfigurationScope& scope, const std::string& label = "Trail crew") {
    GroupConfigurationCommand command{Operation::invite, 0, scope};
    command.label = label_of(label);
    return command;
}

GroupConfigurationCommand read_invite_command(const std::array<std::uint8_t, 16>& id, std::uint16_t offset) {
    GroupConfigurationCommand command{Operation::read_invite};
    command.invite.invite_id = id;
    command.invite.offset = offset;
    return command;
}

GroupConfigurationCommand join_command(const Bytes& wire, std::size_t offset) {
    GroupConfigurationCommand command{Operation::join};
    command.invite.offset = static_cast<std::uint16_t>(offset);
    command.invite.total_bytes = static_cast<std::uint16_t>(wire.size());
    command.invite.size = static_cast<std::uint8_t>(
        std::min(kConfigurationInviteFragmentBytes, wire.size() - offset));
    std::copy_n(wire.begin() + offset, command.invite.size, command.invite.bytes.begin());
    return command;
}

Bytes read_invite(Fixture& f, const GroupConfigurationResult& issued) {
    Bytes wire;
    while (wire.size() < issued.invite.total_bytes) {
        const auto page = f.execute(f.owner, read_invite_command(issued.invite.invite_id,
            static_cast<std::uint16_t>(wire.size())));
        CHECK(page.status == Status::ok && page.invite.invite_id == issued.invite.invite_id);
        CHECK(page.invite.offset == wire.size() && page.invite.total_bytes == issued.invite.total_bytes);
        CHECK(page.invite.size == std::min(kConfigurationInviteFragmentBytes, issued.invite.total_bytes - wire.size()));
        wire.insert(wire.end(), page.invite.bytes.begin(), page.invite.bytes.begin() + page.invite.size);
    }
    return wire;
}

GroupConfigurationResult upload(Fixture& f, ConfigurationGroupOwner& owner, const Bytes& wire) {
    GroupConfigurationResult last{};
    for (std::size_t offset = 0; offset < wire.size(); offset += kConfigurationInviteFragmentBytes) {
        last = f.execute(owner, join_command(wire, offset));
        if (last.status != Status::ok) return last;
        CHECK(last.invite.offset == offset && last.invite.total_bytes == wire.size());
    }
    return last;
}

GroupConfigurationScope current_scope(Fixture& f, std::uint8_t slot = 0) {
    const auto first = f.execute(f.owner, read_roster(slot));
    CHECK(first.status == Status::ok);
    return first.scope;
}

// Grow the admin's group through the durable roster owner, as accepted
// rosters would after real joins. Returns the new current scope.
GroupConfigurationScope grow_group(Fixture& f, std::size_t members) {
    const auto start = current_scope(f);
    opentrail::persistence::GroupPairIndex index(f.catalog, f.platform.signer.key);
    CHECK(index.reopen() == opentrail::persistence::GroupPairIndexError::none);
    roster::DurableGroupRoster owner(f.rosters, index, f.crypto, f.platform.signer.key, start.group_id,
        f.platform.signer.key);
    CHECK(owner.reopen() == roster::DurableRosterError::none);
    std::array<std::uint8_t, roster::kMaximumRosterBytes> bytes{};
    roster::Roster current{};
    roster::DurableRosterSnapshot snapshot{};
    CHECK(owner.snapshot(snapshot, bytes.data(), bytes.size()) == roster::DurableRosterError::none);
    CHECK(roster::decode(bytes.data(), snapshot.bytes, current) == roster::FormatError::none);
    while (current.member_count < members) {
        roster::Roster next{};
        roster::PublicKey key{0x70, static_cast<std::uint8_t>(current.member_count)};
        const std::string alias = "Member " + std::to_string(current.member_count);
        CHECK(roster::add_member(current, f.crypto, key, alias, next) && roster::sign(next, f.platform.signer));
        const auto size = roster::encode(next, bytes.data(), bytes.size());
        CHECK(size != 0);
        const auto offered = owner.offer(bytes.data(), size);
        CHECK(offered.error == roster::DurableRosterError::none && offered.outcome == roster::Outcome::accepted);
        current = next;
    }
    return current_scope(f);
}

void invite_issues_signed_descriptor_for_current_roster() {
    Fixture f;
    const auto created = f.create(f.owner);
    CHECK(created.status == Status::ok);
    const auto rosters = f.rosters.commit_attempts;
    const auto issued = f.execute(f.owner, invite_command(created.scope));
    CHECK(issued.status == Status::ok && issued.scope == created.scope);
    CHECK(issued.invite.total_bytes == kConfigurationInviteMinimumBytes + 10);
    // Issuing an invite writes nothing durable on the admin board.
    CHECK(f.rosters.commit_attempts == rosters && f.joins.records.empty());

    const auto wire = read_invite(f, issued);
    roster::qr::DescriptorBytes encoded{};
    std::copy(wire.begin(), wire.end(), encoded.bytes.begin());
    encoded.size = wire.size();
    roster::qr::Descriptor descriptor{};
    CHECK(roster::qr::verify_descriptor(encoded, f.crypto, descriptor));
    CHECK(descriptor.admin == f.platform.signer.key && descriptor.group == created.scope.group_id);
    CHECK(descriptor.roster_version == 1 && descriptor.roster_digest == created.scope.roster_digest);
    CHECK(descriptor.rule == roster::LocationRule::required && descriptor.invite_id == issued.invite.invite_id);
    CHECK(std::string(descriptor.label.begin(), descriptor.label.begin() + descriptor.label_bytes) == "Trail crew");

    // The same Invite again returns the same live invite.
    const auto again = f.execute(f.owner, invite_command(created.scope));
    CHECK(again.status == Status::ok && again.invite.invite_id == issued.invite.invite_id);
    CHECK(f.platform.next_invite == 1);

    // Unknown ID, past-the-end offset, and expiry after 180 s are stale.
    auto unknown = issued.invite.invite_id;
    unknown[3] ^= 1;
    CHECK(f.execute(f.owner, read_invite_command(unknown, 0)).status == Status::stale);
    CHECK(f.execute(f.owner, read_invite_command(issued.invite.invite_id, 288)).status == Status::stale);
    f.authority.state.now_ms += roster::qr::kInviteMs;
    f.session = f.authority.state.context;
    CHECK(f.execute(f.owner, read_invite_command(issued.invite.invite_id, 0)).status == Status::stale);
    const auto renewed = f.execute(f.owner, invite_command(created.scope));
    CHECK(renewed.status == Status::ok && renewed.invite.invite_id != issued.invite.invite_id);

    // A restart forgets every outstanding invite.
    ConfigurationGroupOwner rebooted{f.authority, f.rosters, f.catalog, f.joins, f.crypto, f.platform};
    CHECK(f.execute(rebooted, read_invite_command(renewed.invite.invite_id, 0)).status == Status::stale);
}

void invite_refusals() {
    Fixture f;
    const auto created = f.create(f.owner);
    CHECK(created.status == Status::ok);
    auto wrong = created.scope;
    wrong.roster_digest.back() ^= 1;
    CHECK(f.execute(f.owner, invite_command(wrong)).status == Status::stale);
    wrong = created.scope;
    wrong.group_id ^= 1;
    CHECK(f.execute(f.owner, invite_command(wrong)).status == Status::stale);
    // Label must be valid UTF-8 without edge spaces.
    CHECK(f.execute(f.owner, invite_command(created.scope, " padded")).status == Status::refused);
    CHECK(f.execute(f.owner, invite_command(created.scope, std::string("\xff\xfe", 2))).status == Status::refused);
    f.platform.invites_ready = false;
    CHECK(f.execute(f.owner, invite_command(created.scope)).status == Status::unavailable);
    f.platform.invites_ready = true;
    CHECK(f.joins.records.empty() && f.rosters.commit_attempts == 1);

    const auto four = grow_group(f, 4);
    CHECK(f.execute(f.owner, invite_command(created.scope)).status == Status::stale);
    CHECK(f.execute(f.owner, invite_command(four)).status == Status::ok);
    // The roster moved while that invite is live: no second invite until it expires.
    const auto five = grow_group(f, 5);
    CHECK(f.execute(f.owner, invite_command(five)).status == Status::refused);
    f.authority.state.now_ms += roster::qr::kInviteMs;
    CHECK(f.execute(f.owner, invite_command(five)).status == Status::ok);
    // D-057: six members is full, so a seventh is never invited.
    const auto six = grow_group(f, 6);
    f.authority.state.now_ms += roster::qr::kInviteMs;
    CHECK(f.execute(f.owner, invite_command(six)).status == Status::refused);

    // Distrusted groups do not invite.
    Fixture g;
    const auto other = g.create(g.owner);
    CHECK(g.execute(g.owner, {Operation::stop_trust, 0, other.scope}).status == Status::ok);
    CHECK(g.execute(g.owner, invite_command(other.scope)).status == Status::refused);
}

struct JoinPair {
    Fixture admin, joiner;
    GroupConfigurationScope scope{};
    Bytes wire;
    JoinPair() {
        joiner.platform.signer.key = {9, 9, 9};
        scope = admin.create(admin.owner).scope;
        const auto issued = admin.execute(admin.owner, invite_command(scope));
        CHECK(issued.status == Status::ok);
        wire = read_invite(admin, issued);
    }
};

void join_verifies_and_claims_once() {
    JoinPair p;
    auto& j = p.joiner;
    const auto joined = upload(j, j.owner, p.wire);
    CHECK(joined.status == Status::ok && joined.scope == p.scope);
    CHECK(j.joins.records.at("claims").size() == opentrail::persistence::GroupJoinClaims::kRecordBytes);
    CHECK(j.joins.commit_attempts == 1 && j.rosters.records.empty() && j.catalog.records.empty());
    const auto* pending = j.owner.pending_join(j.authority.state.now_ms);
    CHECK(pending && pending->descriptor.group == p.scope.group_id && pending->wire.size == p.wire.size());
    CHECK(!j.owner.pending_join(j.authority.state.now_ms + roster::qr::kRequestMs));

    // The same invite again is a replay, now and after a restart.
    CHECK(upload(j, j.owner, p.wire).status == Status::stale);
    ConfigurationGroupOwner rebooted{j.authority, j.rosters, j.catalog, j.joins, j.crypto, j.platform};
    CHECK(!rebooted.pending_join(j.authority.state.now_ms));
    CHECK(upload(j, rebooted, p.wire).status == Status::stale);
    CHECK(j.joins.commit_attempts == 1);

    // A fresh invite from the same admin is accepted and replaces the pending join.
    p.admin.authority.state.now_ms += roster::qr::kInviteMs;
    p.admin.session = p.admin.authority.state.context;
    const auto second = p.admin.execute(p.admin.owner, invite_command(p.scope));
    CHECK(second.status == Status::ok);
    const auto second_wire = read_invite(p.admin, second);
    CHECK(upload(j, rebooted, second_wire).status == Status::ok && j.joins.commit_attempts == 2);
    const auto* replaced = rebooted.pending_join(j.authority.state.now_ms);
    CHECK(replaced && replaced->descriptor.invite_id == second.invite.invite_id);
}

void join_refusals_write_nothing() {
    JoinPair p;
    auto& j = p.joiner;
    auto tampered = p.wire;
    tampered[110] ^= 1;  // label byte under the admin signature
    CHECK(upload(j, j.owner, tampered).status == Status::refused);
    tampered = p.wire;
    tampered[60] ^= 1;   // roster digest
    CHECK(upload(j, j.owner, tampered).status == Status::refused);
    // The admin's own invite is refused on the admin board.
    CHECK(upload(p.admin, p.admin.owner, p.wire).status == Status::refused);
    // Pages out of order, a changed total, or another attachment restart the upload.
    CHECK(j.execute(j.owner, join_command(p.wire, 96)).status == Status::stale);
    CHECK(j.execute(j.owner, join_command(p.wire, 0)).status == Status::ok);
    auto other_total = join_command(p.wire, 96);
    other_total.invite.total_bytes = static_cast<std::uint16_t>(p.wire.size() + 1);
    other_total.invite.size = static_cast<std::uint8_t>(std::min<std::size_t>(96, p.wire.size() + 1 - 96));
    CHECK(j.execute(j.owner, other_total).status == Status::stale);
    CHECK(j.execute(j.owner, join_command(p.wire, 0)).status == Status::ok);
    auto other_session = j.session;
    ++other_session.session_nonce;
    j.authority.state.context = other_session;
    CHECK(j.owner.execute(other_session, join_command(p.wire, 96), j.authority.state.now_ms).status == Status::stale);
    j.authority.state.context = j.session;
    CHECK(j.joins.commit_attempts == 0 && j.joins.records.empty());

    // D-059: a board holding four groups refuses a fifth.
    for (int i = 0; i < 4; ++i) CHECK(j.create(j.owner).status == Status::ok);
    CHECK(upload(j, j.owner, p.wire).status == Status::refused);
    CHECK(j.joins.commit_attempts == 0);

    // Expired attachment admission refuses before the claim is written.
    JoinPair q;
    auto& k = q.joiner;
    for (std::size_t offset = 0; offset + kConfigurationInviteFragmentBytes < q.wire.size();
         offset += kConfigurationInviteFragmentBytes)
        CHECK(k.execute(k.owner, join_command(q.wire, offset)).status == Status::ok);
    const auto last = (q.wire.size() - 1) / kConfigurationInviteFragmentBytes * kConfigurationInviteFragmentBytes;
    CHECK(k.owner.execute(k.session, join_command(q.wire, last), k.authority.state.now_ms - 5001).status ==
          Status::refused);
    CHECK(k.joins.commit_attempts == 0);
}

void interrupted_claim_spends_invite_only_if_applied() {
    for (const auto cut : {MemoryStore::Cut::before_commit, MemoryStore::Cut::after_commit}) {
        JoinPair p;
        auto& j = p.joiner;
        // A write refused before it applies stands for the D-163 storage guard
        // refusing the record: nothing is claimed and the invite stays usable.
        j.joins.cut = cut;
        CHECK(upload(j, j.owner, p.wire).status == Status::uncertain);
        CHECK(!j.owner.pending_join(j.authority.state.now_ms));
        j.joins.reboot();
        ConfigurationGroupOwner rebooted{j.authority, j.rosters, j.catalog, j.joins, j.crypto, j.platform};
        const auto retry = upload(j, rebooted, p.wire);
        if (cut == MemoryStore::Cut::before_commit) {
            CHECK(retry.status == Status::ok && rebooted.pending_join(j.authority.state.now_ms));
        } else {
            CHECK(retry.status == Status::stale && !rebooted.pending_join(j.authority.state.now_ms));
        }
    }
}

void claim_ring_keeps_newest_eight_and_rejects_foreign_record() {
    namespace persistence = opentrail::persistence;
    MemoryStore store{"ot_group_join"};
    const roster::Fingerprint local{4, 4, 4}, admin{1, 2, 3};
    using Id = persistence::GroupJoinClaims::InviteId;
    const auto id = [](std::uint8_t n) { Id value{}; value[0] = n; value[15] = 1; return value; };
    {
        persistence::GroupJoinClaims claims(store, local);
        CHECK(claims.claimed(admin, 1, id(1)));  // unloaded fails closed
        CHECK(claims.reopen() == persistence::GroupJoinClaimError::missing);
        CHECK(claims.claim(admin, 0, id(1)) == persistence::GroupJoinClaimError::invalid);
        CHECK(claims.claim(admin, 1, Id{}) == persistence::GroupJoinClaimError::invalid);
        for (std::uint8_t n = 1; n <= 9; ++n) CHECK(claims.claim(admin, 1, id(n)) == persistence::GroupJoinClaimError::none);
        CHECK(claims.claim(admin, 1, id(9)) == persistence::GroupJoinClaimError::replayed);
    }
    persistence::GroupJoinClaims reopened(store, local);
    CHECK(reopened.reopen() == persistence::GroupJoinClaimError::none);
    CHECK(!reopened.claimed(admin, 1, id(1)));  // the oldest of nine left the ring
    for (std::uint8_t n = 2; n <= 9; ++n) CHECK(reopened.claimed(admin, 1, id(n)));
    // Any signer presenting a consumed ID is refused.
    CHECK(reopened.claimed(roster::Fingerprint{7}, 2, id(5)));

    persistence::GroupJoinClaims foreign(store, roster::Fingerprint{5, 5, 5});
    CHECK(foreign.reopen() == persistence::GroupJoinClaimError::invalid_record);
    auto& bytes = store.records.at("claims");
    bytes[60] ^= 1;
    persistence::GroupJoinClaims corrupt(store, local);
    CHECK(corrupt.reopen() == persistence::GroupJoinClaimError::invalid_record);
    CHECK(corrupt.claimed(admin, 1, id(20)));
}

void invite_codec_round_trips_and_rejects_bad_sizes() {
    std::array<std::uint8_t, kConfigurationPayloadBytes> out{};
    GroupConfigurationScope scope{};
    scope.admin = {1};
    scope.group_id = 3;
    scope.roster_digest = {4};
    const auto invite = invite_command(scope, std::string(kConfigurationInviteLabelBytes, 'a'));
    const auto encoded = encode_group_configuration_command(invite, out.data(), out.size());
    CHECK(encoded.encoded() && encoded.encoded_bytes == kConfigurationPayloadBytes);
    const auto decoded = decode_group_configuration_command(out.data(), encoded.encoded_bytes);
    CHECK(decoded.decoded() && decoded.value.scope == scope && decoded.value.label == invite.label);
    CHECK(!decode_group_configuration_command(out.data(), encoded.encoded_bytes - 1).decoded());
    CHECK(!encode_group_configuration_command(invite_command(scope, ""), out.data(), out.size()).encoded());
    CHECK(!encode_group_configuration_command(invite_command({}, "x"), out.data(), out.size()).encoded());

    std::array<std::uint8_t, 16> id{7};
    const auto read = encode_group_configuration_command(read_invite_command(id, 96), out.data(), out.size());
    CHECK(read.encoded() && read.encoded_bytes == 26);
    CHECK(decode_group_configuration_command(out.data(), 26).value.invite.offset == 96);
    CHECK(!encode_group_configuration_command(read_invite_command({}, 0), out.data(), out.size()).encoded());
    CHECK(!encode_group_configuration_command(read_invite_command(id, 42), out.data(), out.size()).encoded());

    Bytes wire(kConfigurationInviteBytes, 0x33);
    const auto last = join_command(wire, 192);
    const auto join = encode_group_configuration_command(last, out.data(), out.size());
    CHECK(join.encoded() && join.encoded_bytes == 12 + 95);
    const auto join_back = decode_group_configuration_command(out.data(), join.encoded_bytes);
    CHECK(join_back.decoded() && join_back.value.invite.size == 95 && join_back.value.invite.bytes[94] == 0x33);
    CHECK(!decode_group_configuration_command(out.data(), join.encoded_bytes + 1).decoded());
    Bytes small(kConfigurationInviteMinimumBytes - 1, 1);
    CHECK(!encode_group_configuration_command(join_command(small, 0), out.data(), out.size()).encoded());

    GroupConfigurationResult final_join{Operation::join, Status::ok, scope};
    final_join.invite.offset = 192;
    final_join.invite.total_bytes = kConfigurationInviteBytes;
    final_join.invite.invite_id = id;
    const auto result = encode_group_configuration_result(final_join, out.data(), out.size());
    CHECK(result.encoded() && result.encoded_bytes == 100);
    const auto result_back = decode_group_configuration_result(out.data(), result.encoded_bytes);
    CHECK(result_back.decoded() && result_back.value.scope == scope && result_back.value.invite.invite_id == id);
    GroupConfigurationResult middle{Operation::join, Status::ok};
    middle.invite.offset = 96;
    middle.invite.total_bytes = kConfigurationInviteBytes;
    CHECK(encode_group_configuration_result(middle, out.data(), out.size()).encoded_bytes == 12);
    middle.invite.invite_id = id;  // an ID before the last page is malformed
    CHECK(!encode_group_configuration_result(middle, out.data(), out.size()).encoded());
    GroupConfigurationResult failed{Operation::invite, Status::stale};
    CHECK(encode_group_configuration_result(failed, out.data(), out.size()).encoded_bytes == 8);
    failed.status = Status::missing;
    CHECK(!encode_group_configuration_result(failed, out.data(), out.size()).encoded());
    failed = {Operation::invite, Status::refused, scope};
    CHECK(!encode_group_configuration_result(failed, out.data(), out.size()).encoded());
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
    invite_issues_signed_descriptor_for_current_roster();
    invite_refusals();
    join_verifies_and_claims_once();
    join_refusals_write_nothing();
    interrupted_claim_spends_invite_only_if_applied();
    claim_ring_keeps_newest_eight_and_rejects_foreign_record();
    invite_codec_round_trips_and_rejects_bad_sizes();
    std::cout << "PASS: 15 concrete companion group owner cases\n";
}
