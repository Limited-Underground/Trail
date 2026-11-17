// Durable duplicate and gap state for MeshTrail V1 pairwise groups.
#include "opentrail/group_receive_state.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

#include "opentrail/configuration_store.hpp"

using namespace opentrail::delivery;
using opentrail::identity::roster::GroupName;
using opentrail::persistence::PersistentKvBackend;
using opentrail::persistence::PersistentKvBackendError;

namespace {

using Error = GroupReceiveStateError;
using KvError = PersistentKvBackendError;

void expect(bool condition, const char* detail) {
    if (!condition) {
        std::cerr << detail << '\n';
        std::exit(1);
    }
}

enum class Fault { none, read, write, before_commit, after_commit, readback, changed_readback };

struct Media {
    std::array<std::vector<std::uint8_t>, GroupReceiveState::kRetainedSenders> records{};
    unsigned reads{0};
    unsigned writes{0};
    unsigned commits{0};
};

class Backend final : public PersistentKvBackend {
public:
    explicit Backend(Media& media) : media_(media) {}

    KvError read_blob(const char* partition, const char* name, const char* key, std::uint8_t* out,
                      std::size_t capacity, std::size_t& size) override {
        check_store(partition, name);
        const auto& record = media_.records[slot(key)];
        size = record.size();
        ++media_.reads;
        if (fault == Fault::read || (committed_ && fault == Fault::readback)) return KvError::io_failure;
        if (record.empty()) return KvError::not_found;
        if (record.size() > capacity) return KvError::invalid_argument;
        std::copy(record.begin(), record.end(), out);
        if (committed_ && fault == Fault::changed_readback) out[record.size() - 1] ^= 1U;
        return KvError::none;
    }

    KvError write_blob(const char* partition, const char* name, const char* key,
                       const std::uint8_t* input, std::size_t size) override {
        check_store(partition, name);
        ++media_.writes;
        if (fault == Fault::write) return KvError::io_failure;
        target_ = slot(key);
        staged_.assign(input, input + size);
        return KvError::none;
    }

    KvError erase_key(const char*, const char*, const char*) override {
        expect(false, "receiving must not erase retained history");
        return KvError::io_failure;
    }

    KvError commit(const char* partition, const char* name) override {
        check_store(partition, name);
        if (fault == Fault::before_commit) return KvError::io_failure;
        media_.records[target_] = staged_;
        ++media_.commits;
        committed_ = true;
        return fault == Fault::after_commit ? KvError::io_failure : KvError::none;
    }

    Fault fault{Fault::none};

private:
    static void check_store(const char* partition, const char* name) {
        expect(std::strcmp(partition, "ot_state") == 0 && std::strcmp(name, "ot_group_rx") == 0,
               "receive history uses only its isolated namespace");
    }

    static std::size_t slot(const char* key) {
        expect(std::strlen(key) == 3 && key[0] == 'r' && key[1] >= '0' && key[1] <= '1' &&
                   key[2] >= '0' && key[2] <= '9', "receive history uses only its bounded slots");
        return static_cast<std::size_t>((key[1] - '0') * 10 + key[2] - '0');
    }

    Media& media_;
    std::vector<std::uint8_t> staged_;
    std::size_t target_{0};
    bool committed_{false};
};

GroupMemberId member(std::uint8_t tag) {
    GroupMemberId id{};
    id.back() = tag;  // all test identities share their first 31 bytes
    return id;
}

const GroupName kGroup{member(1), 0x1020304050607080ULL};
const GroupMemberId kLocal = member(2);
const GroupMemberId kPeer = member(3);
const GroupView kView{3, {1, 2, 3, 4}, true};
const SenderStanding kCurrent{true, true};

DurableGroupReceiveResult incoming(GroupReceiveState& state, std::uint32_t number, std::string_view body,
                                   SenderStanding standing = kCurrent, GroupView view = kView,
                                   GroupContent content = GroupContent::text,
                                   std::uint32_t version = 3, GroupDigestPrefix prefix = kView.digest_prefix) {
    std::array<std::uint8_t, kGroupPlaintextBytes> data{};
    GroupFrameError error{};
    const auto size = encode_group_frame(content, {version, prefix, number},
                                          reinterpret_cast<const std::uint8_t*>(body.data()), body.size(),
                                          data.data(), data.size(), error);
    expect(size != 0 && error == GroupFrameError::none, "test frame encodes");
    return state.receive(standing, view, content, data.data(), size);
}

GroupReceiveResult disposition(const DurableGroupReceiveResult& result, GroupReceive wanted) {
    expect(result.error == Error::none && result.disposition && result.disposition->outcome == wanted,
           "expected receive disposition");
    return *result.disposition;
}

void refuse(const DurableGroupReceiveResult& result, Error error) {
    expect(result.error == error && !result.disposition, "storage/parse failure returns no receive disposition");
}

void gaps(GroupReceiveState& state, std::uint32_t wanted) {
    const auto result = state.missing();
    expect(result.error == Error::none && result.count == wanted, "expected durable gap count");
}

void window_survives_reopen() {
    Media media;
    {
        Backend backend(media);
        GroupReceiveState state(backend, kGroup, kLocal, kPeer);
        refuse(incoming(state, 100, "a"), Error::missing);
        expect(media.commits == 0, "normal receive never provisions a missing record");
        expect(state.initialize() == Error::none, "explicit new member provisioning");
        expect(state.initialize() == Error::none && media.commits == 1, "repeat provisioning preserves the record");
        const auto reads = media.reads;
        disposition(incoming(state, 100, "a"), GroupReceive::shown);
        expect(media.reads == reads + 2, "matching live scope reads one record and verifies its update");
        const auto advanced = disposition(incoming(state, 103, "d"), GroupReceive::shown);
        expect(advanced.missing_before == 2, "advancing tracks gaps");
        gaps(state, 2);
    }
    {
        Backend backend(media);
        GroupReceiveState state(backend, kGroup, kLocal, kPeer);
        const auto commits = media.commits;
        gaps(state, 2);
        disposition(incoming(state, 100, "a"), GroupReceive::duplicate);
        disposition(incoming(state, 100, "same number, different body"), GroupReceive::conflict);
        disposition(incoming(state, 100, "a", kCurrent, kView, GroupContent::quick_status), GroupReceive::conflict);
        expect(media.commits == commits, "duplicates and conflicts do not rewrite the window");
        disposition(incoming(state, 101, "late"), GroupReceive::shown);
        gaps(state, 1);
        const auto limit = std::numeric_limits<std::uint32_t>::max();
        disposition(incoming(state, limit, "last"), GroupReceive::shown);
        gaps(state, 31);
        disposition(incoming(state, limit - 31, "edge"), GroupReceive::shown);
        disposition(incoming(state, limit - 32, "past edge"), GroupReceive::too_old);
        gaps(state, 30);
    }
    Backend backend(media);
    GroupReceiveState state(backend, kGroup, kLocal, kPeer);
    disposition(incoming(state, std::numeric_limits<std::uint32_t>::max(), "last"), GroupReceive::duplicate);
    gaps(state, 30);
}

void admission_before_storage() {
    Media media;
    Backend backend(media);
    GroupReceiveState state(backend, kGroup, kLocal, kPeer);
    disposition(incoming(state, 1, "x", {false, true}), GroupReceive::not_member);
    disposition(incoming(state, 1, "x", {true, false}), GroupReceive::no_pair);
    disposition(incoming(state, 1, "x", kCurrent, kView, GroupContent::text, 4), GroupReceive::roster_newer);
    disposition(incoming(state, 1, "x", kCurrent, kView, GroupContent::text, 3, {9}), GroupReceive::roster_differs);
    auto off = kView;
    off.location_allowed = false;
    disposition(incoming(state, 1, "p", kCurrent, off, GroupContent::position), GroupReceive::position_refused);
    refuse(state.receive(kCurrent, kView, GroupContent::text, nullptr, 0), Error::invalid_frame);
    expect(media.reads == 0 && media.writes == 0, "admission refusals never touch receive storage");

    expect(state.initialize() == Error::none, "initialize admitted receive fixture");
    auto larger = kView;
    ++larger.roster_version;
    const auto result = disposition(incoming(state, 1, "old roster", kCurrent, larger), GroupReceive::shown);
    expect(result.sender_roster_older, "older roster signal survives the durable receive path");
    disposition(incoming(state, 1, "old roster", kCurrent, larger), GroupReceive::duplicate);
}

void identity_capacity_and_duplicates() {
    Media media;
    Backend backend(media);
    GroupReceiveState state(backend, kGroup, kLocal, kPeer);
    expect(state.initialize() == Error::none, "seed first sender");
    disposition(incoming(state, 1, "a"), GroupReceive::shown);
    const GroupName other_admin{member(9), kGroup.group_id};
    const GroupName other_group{kGroup.admin, kGroup.group_id ^ 0x8000000000000000ULL};
    GroupReceiveState wrong_admin(backend, other_admin, kLocal, kPeer);
    GroupReceiveState wrong_group(backend, other_group, kLocal, kPeer);
    GroupReceiveState wrong_local(backend, kGroup, member(4), kPeer);
    GroupReceiveState wrong_peer(backend, kGroup, kLocal, member(4));
    for (auto* unrelated : {&wrong_admin, &wrong_group, &wrong_local, &wrong_peer}) {
        refuse(incoming(*unrelated, 1, "a"), Error::missing);
    }
    GroupReceiveState bad_scope(backend, kGroup, kLocal, kLocal);
    expect(bad_scope.initialize() == Error::invalid_scope, "self sender refused");

    for (std::size_t i = 1; i < GroupReceiveState::kRetainedSenders; ++i) {
        const auto index = static_cast<std::uint64_t>(i);
        const GroupName group{kGroup.admin, kGroup.group_id + index / kGroupOtherMembers};
        GroupReceiveState next(backend, group, kLocal, member(static_cast<std::uint8_t>(3 + i % kGroupOtherMembers)));
        expect(next.initialize() == Error::none, "each retained sender uses a separate slot");
    }
    expect(wrong_admin.initialize() == Error::capacity, "full catalog refuses a new sender without evicting");
    disposition(incoming(state, 1, "a"), GroupReceive::duplicate);

    media.records.back() = media.records.front();
    Backend restarted(media);
    GroupReceiveState ambiguous(restarted, kGroup, kLocal, kPeer);
    expect(ambiguous.initialize() == Error::invalid_record, "two matching records cannot be opened");
    refuse(incoming(ambiguous, 2, "b"), Error::invalid_record);
}

void interrupted_updates() {
    for (const auto fault : {Fault::read, Fault::write, Fault::before_commit, Fault::after_commit,
                             Fault::readback, Fault::changed_readback}) {
        Media media;
        {
            Backend backend(media);
            GroupReceiveState state(backend, kGroup, kLocal, kPeer);
            expect(state.initialize() == Error::none, "initialize interruption fixture");
            disposition(incoming(state, 1, "already shown"), GroupReceive::shown);
        }
        {
            Backend backend(media);
            GroupReceiveState state(backend, kGroup, kLocal, kPeer);
            backend.fault = fault;
            refuse(incoming(state, 2, "uncertain"), Error::storage_failure);
            backend.fault = Fault::none;
            if (fault != Fault::read) {
                const auto old_writes = media.writes;
                refuse(incoming(state, 3, "must wait for a new handle"), Error::storage_failure);
                expect(media.writes == old_writes, "an uncertain handle cannot write again");
            }
        }
        Backend restarted(media);
        GroupReceiveState state(restarted, kGroup, kLocal, kPeer);
        disposition(incoming(state, 1, "already shown"), GroupReceive::duplicate);
        const bool applied = fault == Fault::after_commit || fault == Fault::readback ||
                             fault == Fault::changed_readback;
        disposition(incoming(state, 2, "uncertain"),
                    applied ? GroupReceive::duplicate : GroupReceive::shown);
    }
}

void invalid_history_stays_unavailable() {
    Media media;
    Backend backend(media);
    GroupReceiveState known(backend, kGroup, kLocal, kPeer);
    expect(known.initialize() == Error::none, "initialize damage fixture");
    disposition(incoming(known, 1, "a"), GroupReceive::shown);
    auto& record = media.records.front();
    const auto valid = record;

    record.back() ^= 1U;
    refuse(incoming(known, 2, "b"), Error::invalid_record);
    record = valid;
    record.pop_back();
    expect(known.initialize() == Error::invalid_record, "truncated history cannot be reinitialized");
    record = valid;
    const auto crc_offset = record.size() - sizeof(std::uint32_t);
    const auto seen_last = crc_offset - kGroupInboxWindow * sizeof(std::uint64_t) - 1;
    record[seen_last] &= 0xfeU;  // highest message missing from its own bitmap
    const auto crc = opentrail::persistence::configuration_crc32({record.data(), crc_offset});
    for (std::size_t i = 0; i < sizeof(crc); ++i) {
        record[record.size() - 1 - i] = static_cast<std::uint8_t>(crc >> (8U * i));
    }
    refuse(incoming(known, 2, "b"), Error::invalid_record);

    record.clear();
    refuse(incoming(known, 1, "a"), Error::missing);
    expect(known.initialize() == Error::missing, "known lost history cannot be replaced by provisioning");
    expect(media.commits == 2, "damage handling made no writes to durable state");
}

}  // namespace

int main() {
    window_survives_reopen();
    admission_before_storage();
    identity_capacity_and_duplicates();
    interrupted_updates();
    invalid_history_stays_unavailable();
    std::cout << "durable group receive state: 5 scenario groups passed\n";
}
