#include "opentrail/group_message_numbers.hpp"
#include "opentrail/configuration_store.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

using opentrail::identity::roster::Fingerprint;
using opentrail::identity::roster::GroupName;
using opentrail::delivery::GroupMessageNumberError;
using opentrail::delivery::GroupMessageNumbers;
using opentrail::persistence::PersistentKvBackend;
using opentrail::persistence::PersistentKvBackendError;

namespace {
using Error = GroupMessageNumberError;
using KvError = PersistentKvBackendError;

void expect(bool condition, const char* description) {
    if (!condition) {
        std::cerr << description << '\n';
        std::exit(1);
    }
}

struct Media {
    std::array<std::vector<std::uint8_t>, 4> records;
    unsigned commits{0};
};

class Backend final : public PersistentKvBackend {
public:
    explicit Backend(Media& media) : media_(media) {}

    KvError read_blob(const char* partition, const char* space, const char* key,
                      std::uint8_t* out, std::size_t capacity, std::size_t& size) override {
        check_address(partition, space, key);
        const auto& record = media_.records[index(key)];
        size = record.size();
        if (fail_read || (fail_readback && committed_)) return KvError::io_failure;
        if (record.empty()) return KvError::not_found;
        if (record.size() > capacity) return KvError::invalid_argument;
        std::copy(record.begin(), record.end(), out);
        return KvError::none;
    }

    KvError write_blob(const char* partition, const char* space, const char* key,
                       const std::uint8_t* data, std::size_t size) override {
        check_address(partition, space, key);
        if (fail_write) return KvError::io_failure;
        target_ = index(key);
        staged_.assign(data, data + size);
        return KvError::none;
    }

    KvError erase_key(const char*, const char*, const char*) override {
        expect(false, "number reservations must never erase their record");
        return KvError::io_failure;
    }

    KvError commit(const char* partition, const char* space) override {
        expect(std::strcmp(partition, "ot_state") == 0 &&
                   std::strcmp(space, "ot_group_num") == 0, "commit addresses the same store");
        if (fail_commit_before_write) return KvError::io_failure;
        media_.records[target_] = staged_;
        ++media_.commits;
        committed_ = true;
        if (wrong_readback) media_.records[target_].back() ^= 1U;
        return fail_commit_after_write ? KvError::io_failure : KvError::none;
    }

    bool fail_read{false};
    bool fail_write{false};
    bool fail_commit_before_write{false};
    bool fail_commit_after_write{false};
    bool fail_readback{false};
    bool wrong_readback{false};

private:
    static void check_address(const char* partition, const char* space, const char* key) {
        expect(std::strcmp(partition, "ot_state") == 0 &&
                   std::strcmp(space, "ot_group_num") == 0 &&
                   std::strlen(key) == 2 && key[0] == 'g' && key[1] >= '0' && key[1] <= '3',
               "checked partition, namespace and bounded group slot");
    }
    static std::size_t index(const char* key) { return static_cast<std::size_t>(key[1] - '0'); }

    Media& media_;
    std::vector<std::uint8_t> staged_;
    std::size_t target_{0};
    bool committed_{false};
};

const GroupName kGroup = [] {
    GroupName group{};
    group.admin[31] = 1;
    group.group_id = 0x1020304050607080ULL;
    return group;
}();
const Fingerprint kSender = [] {
    Fingerprint sender{};
    sender[31] = 2;
    return sender;
}();

void take(GroupMessageNumbers& numbers, std::uint32_t expected) {
    const auto result = numbers.next();
    expect(result.error == Error::none && result.number == expected, "expected next number");
}

void refuse(GroupMessageNumbers& numbers, Error error) {
    const auto result = numbers.next();
    expect(result.error == error && result.number == 0, "failure must return no number");
}

void batch_and_restart() {
    Media media;
    {
        Backend backend(media);
        GroupMessageNumbers numbers(backend, kGroup, kSender);
        refuse(numbers, Error::missing);
        expect(numbers.initialize() == Error::none, "new group initializes");
        expect(numbers.initialize() == Error::none && media.commits == 1, "init is idempotent");
        for (std::uint32_t i = 1; i <= GroupMessageNumbers::kLeaseSize; ++i) take(numbers, i);
        expect(media.commits == 2, "one durable reservation per batch");
        take(numbers, GroupMessageNumbers::kLeaseSize + 1);
        expect(media.commits == 3, "rollover durably reserves another batch");
    }
    {
        Backend backend(media);
        GroupMessageNumbers numbers(backend, kGroup, kSender);
        expect(numbers.initialize() == Error::none && media.commits == 3,
               "initialization cannot overwrite the high-water mark");
        take(numbers, 2 * GroupMessageNumbers::kLeaseSize + 1);
    }
}

void identities_and_bounds() {
    Media media;
    Backend first_backend(media);
    GroupMessageNumbers first(first_backend, kGroup, kSender);
    expect(first.initialize() == Error::none, "initialize first group");
    take(first, 1);

    auto other_group = kGroup;
    other_group.admin[31] ^= 0x80;
    auto other_sender = kSender;
    other_sender[31] ^= 0x80;
    auto other_id = kGroup;
    other_id.group_id ^= 0x8000000000000000ULL;
    Backend backend(media);
    for (const auto& group : {other_group, other_id}) {
        GroupMessageNumbers unknown(backend, group, kSender);
        refuse(unknown, Error::missing);
    }
    GroupMessageNumbers wrong_sender(backend, kGroup, other_sender);
    refuse(wrong_sender, Error::scope_mismatch);

    GroupMessageNumbers second(backend, other_group, kSender);
    expect(second.initialize() == Error::none, "second group owns another slot");
    take(second, 1);
    take(first, 2);

    GroupMessageNumbers third(backend, other_id, kSender);
    expect(third.initialize() == Error::none, "group ID has independent state");
    auto fourth_group = other_id;
    ++fourth_group.group_id;
    GroupMessageNumbers fourth(backend, fourth_group, kSender);
    expect(fourth.initialize() == Error::none, "fourth group fits");
    ++fourth_group.group_id;
    GroupMessageNumbers fifth(backend, fourth_group, kSender);
    expect(fifth.initialize() == Error::capacity, "fifth group cannot replace an existing one");

    media.records[3] = media.records[0];
    Backend restart(media);
    GroupMessageNumbers duplicate(restart, kGroup, kSender);
    refuse(duplicate, Error::invalid_record);

    GroupMessageNumbers invalid_group(backend, GroupName{}, kSender);
    GroupMessageNumbers invalid_sender(backend, kGroup, Fingerprint{});
    refuse(invalid_group, Error::invalid_scope);
    refuse(invalid_sender, Error::invalid_scope);
}

void faults() {
    for (int stage = 0; stage < 6; ++stage) {
        Media media;
        Backend setup(media);
        GroupMessageNumbers initial(setup, kGroup, kSender);
        expect(initial.initialize() == Error::none, "initialize interruption fixture");
        take(initial, 1);
        {
            Backend backend(media);
            backend.fail_read = stage == 0;
            backend.fail_write = stage == 1;
            backend.fail_commit_before_write = stage == 2;
            backend.fail_commit_after_write = stage == 3;
            backend.fail_readback = stage == 4;
            backend.wrong_readback = stage == 5;
            GroupMessageNumbers interrupted(backend, kGroup, kSender);
            refuse(interrupted, Error::storage_failure);
            if (stage != 0) refuse(interrupted, Error::storage_failure);
        }
        Backend backend(media);
        GroupMessageNumbers rebooted(backend, kGroup, kSender);
        if (stage == 5) {
            refuse(rebooted, Error::invalid_record);
        } else {
            const auto batches = stage >= 3 ? 2U : 1U;
            take(rebooted, batches * GroupMessageNumbers::kLeaseSize + 1);
        }
    }
}

void exhaustion_and_corruption() {
    Media media;
    Backend setup(media);
    GroupMessageNumbers initial(setup, kGroup, kSender);
    expect(initial.initialize() == Error::none, "initialize end-of-range fixture");

    // Seed a valid record two numbers below exhaustion. The last eight
    // bytes hold the big-endian high-water mark and CRC32.
    auto& record = media.records[0];
    const auto end = record.size();
    std::fill(record.end() - 8, record.end() - 4, 0xff);
    record[end - 5] = 0xfd;
    const auto crc = opentrail::persistence::configuration_crc32({record.data(), end - 4});
    for (std::size_t i = 0; i < 4; ++i) {
        record[end - 1 - i] = static_cast<std::uint8_t>(crc >> (8 * i));
    }
    Backend backend(media);
    GroupMessageNumbers numbers(backend, kGroup, kSender);
    const auto limit = std::numeric_limits<std::uint32_t>::max();
    take(numbers, limit - 1);
    take(numbers, limit);
    refuse(numbers, Error::exhausted);
    const auto commits = media.commits;
    Backend reboot(media);
    GroupMessageNumbers rebooted(reboot, kGroup, kSender);
    refuse(rebooted, Error::exhausted);
    expect(media.commits == commits, "no commit after exhaustion");

    record.back() ^= 1U;
    refuse(rebooted, Error::invalid_record);
    record.pop_back();
    refuse(rebooted, Error::invalid_record);
    record.clear();
    refuse(rebooted, Error::missing);
}
}  // namespace

int main() {
    batch_and_restart();
    identities_and_bounds();
    faults();
    exhaustion_and_corruption();
    std::cout << "group message numbers: 4 scenario groups passed\n";
}
