// F8 step 2d-3 (D-179): the QR-joined pair keys kept on the board.
#include "opentrail/group_qr_pair_keys.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {
namespace p = opentrail::persistence;
namespace r = opentrail::identity::roster;
using Error = p::GroupQrPairKeyError;
using KvError = p::PersistentKvBackendError;
using Keys = p::GroupQrPairKeys;

void check(bool value, const char* expression, int line) {
    if (value) return;
    std::cerr << "FAIL " << line << ": " << expression << '\n';
    std::exit(EXIT_FAILURE);
}
#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

class MemoryKv final : public p::PersistentKvBackend {
public:
    enum class Cut { none, before_commit, after_commit, bad_readback };
    Cut cut{Cut::none};
    unsigned commits{};
    std::map<std::string, std::vector<std::uint8_t>> records;
    KvError read_blob(const char* partition, const char* ns, const char* key, std::uint8_t* out,
                      std::size_t capacity, std::size_t& size) override {
        CHECK(std::string(partition) == "ot_state" && std::string(ns) == "ot_group_pair");
        const auto found = records.find(key);
        if (found == records.end()) return KvError::not_found;
        size = found->second.size();
        if (size > capacity) return KvError::invalid_argument;
        std::copy(found->second.begin(), found->second.end(), out);
        if (corrupt_next_read) { out[size - 1] ^= 1; corrupt_next_read = false; }
        return KvError::none;
    }
    KvError write_blob(const char*, const char*, const char* key, const std::uint8_t* bytes,
                       std::size_t size) override {
        staged_key_ = key;
        staged_.assign(bytes, bytes + size);
        return KvError::none;
    }
    KvError erase_key(const char*, const char*, const char*) override { return KvError::invalid_argument; }
    KvError commit(const char*, const char*) override {
        ++commits;
        const auto fault = cut;
        cut = Cut::none;
        if (fault == Cut::before_commit) { staged_.clear(); return KvError::io_failure; }
        records[staged_key_] = staged_;
        staged_.clear();
        if (fault == Cut::bad_readback) corrupt_next_read = true;
        return fault == Cut::after_commit ? KvError::io_failure : KvError::none;
    }
    bool corrupt_next_read{false};
private:
    std::string staged_key_;
    std::vector<std::uint8_t> staged_;
};

r::PublicKey key(std::uint8_t b) { r::PublicKey k{}; k.fill(b); return k; }
const auto kLocal = key(0xa1);

Keys::Entry entry(std::uint8_t peer, std::uint8_t salt = 0) {
    Keys::Entry e{};
    e.peer = key(peer);
    e.epoch = 1;
    e.transaction.fill(static_cast<std::uint8_t>(0x70 + peer));
    e.transmit.fill(static_cast<std::uint8_t>(0x10 + peer + salt));
    e.receive.fill(static_cast<std::uint8_t>(0x20 + peer + salt));
    return e;
}

bool same(const Keys::Entry& a, const Keys::Entry& b) {
    return a.peer == b.peer && a.epoch == b.epoch && a.transaction == b.transaction && a.transmit == b.transmit &&
           a.receive == b.receive;
}

void store_find_reopen() {
    MemoryKv kv;
    Keys keys{kv, kLocal};
    CHECK(keys.store(7, entry(2)) == Error::storage_failure);  // fails closed until opened
    CHECK(keys.reopen() == Error::missing);
    CHECK(keys.store(7, entry(2)) == Error::none && kv.commits == 1);
    CHECK(keys.store(7, entry(2)) == Error::none && kv.commits == 1);  // same entry: no write
    CHECK(keys.store(7, entry(2, 5)) == Error::refused);               // other keys for that pair
    CHECK(keys.store(7, entry(3)) == Error::none && keys.count(7) == 2);
    CHECK(keys.store(9, entry(2)) == Error::none && keys.count(9) == 1);  // same peer, other group
    Keys::Entry found{};
    CHECK(keys.find(7, key(3), found) && same(found, entry(3)));
    CHECK(!keys.find(7, key(4), found) && !keys.find(8, key(2), found));

    Keys reopened{kv, kLocal};
    CHECK(reopened.reopen() == Error::none);
    CHECK(reopened.find(7, key(2), found) && same(found, entry(2)) && reopened.count(9) == 1);
    // The record never holds another board's keys.
    Keys foreign{kv, key(0xb2)};
    CHECK(foreign.reopen() == Error::invalid_record && !foreign.find(7, key(2), found));
}

void limits_and_bad_entries() {
    MemoryKv kv;
    Keys keys{kv, kLocal};
    CHECK(keys.reopen() == Error::missing);
    for (std::uint8_t peer = 2; peer < 2 + Keys::kPeers; ++peer) CHECK(keys.store(1, entry(peer)) == Error::none);
    CHECK(keys.store(1, entry(20)) == Error::refused);  // five peers per group
    for (std::uint64_t g = 2; g <= Keys::kGroups; ++g) CHECK(keys.store(g, entry(2)) == Error::none);
    CHECK(keys.store(99, entry(2)) == Error::refused);  // four groups
    auto bad = entry(30);
    bad.peer = kLocal;
    CHECK(keys.store(2, bad) == Error::invalid);
    bad = entry(30);
    bad.epoch = 0;
    CHECK(keys.store(2, bad) == Error::invalid);
    bad = entry(30);
    bad.receive = bad.transmit;
    CHECK(keys.store(2, bad) == Error::invalid);
    bad = entry(30);
    bad.transmit = {};
    CHECK(keys.store(2, bad) == Error::invalid);
    CHECK(keys.store(0, entry(30)) == Error::invalid);
    CHECK(Keys::kRecordBytes == 717);
}

void stop_trust_overwrites_the_group() {
    MemoryKv kv;
    Keys keys{kv, kLocal};
    CHECK(keys.reopen() == Error::missing);
    CHECK(keys.store(7, entry(2)) == Error::none && keys.store(7, entry(3)) == Error::none);
    CHECK(keys.store(9, entry(4)) == Error::none);
    const auto record = kv.records.at("qk0");
    CHECK(keys.wipe_group(7) == Error::none);
    Keys::Entry found{};
    CHECK(!keys.find(7, key(2), found) && keys.count(7) == 0 && keys.find(9, key(4), found));
    // The bytes on storage no longer hold any of the group's keys.
    const auto& wiped = kv.records.at("qk0");
    CHECK(wiped.size() == record.size() && wiped != record);
    const auto tx = entry(2).transmit;
    CHECK(std::search(wiped.begin(), wiped.end(), tx.begin(), tx.end()) == wiped.end());
    Keys reopened{kv, kLocal};
    CHECK(reopened.reopen() == Error::none && reopened.count(7) == 0 && reopened.count(9) == 1);
    CHECK(keys.wipe_group(7) == Error::none && keys.wipe_group(123) == Error::none);
    // The freed record is reused by the next group.
    CHECK(keys.store(11, entry(5)) == Error::none && keys.count(11) == 1);
}

void interrupted_writes_drop_the_cache() {
    for (const auto cut : {MemoryKv::Cut::before_commit, MemoryKv::Cut::after_commit, MemoryKv::Cut::bad_readback}) {
        MemoryKv kv;
        Keys keys{kv, kLocal};
        CHECK(keys.reopen() == Error::missing);
        CHECK(keys.store(7, entry(2)) == Error::none);
        kv.cut = cut;
        CHECK(keys.store(7, entry(3)) == Error::storage_failure);
        Keys::Entry found{};
        CHECK(!keys.find(7, key(2), found) && keys.store(7, entry(4)) == Error::storage_failure);
        const auto opened = keys.reopen();
        CHECK(opened == Error::none);
        CHECK(keys.find(7, key(2), found));
        CHECK(keys.find(7, key(3), found) == (cut != MemoryKv::Cut::before_commit));
    }
    // A damaged record is refused, never partly used.
    MemoryKv kv;
    Keys keys{kv, kLocal};
    CHECK(keys.reopen() == Error::missing && keys.store(7, entry(2)) == Error::none);
    kv.records.at("qk0")[40] ^= 1;
    Keys::Entry found{};
    CHECK(keys.reopen() == Error::invalid_record && !keys.find(7, key(2), found));
}

}  // namespace

int main() {
    store_find_reopen();
    limits_and_bad_entries();
    stop_trust_overwrites_the_group();
    interrupted_writes_drop_the_cache();
    std::cout << "PASS: 4 QR pair key groups\n";
    return EXIT_SUCCESS;
}
