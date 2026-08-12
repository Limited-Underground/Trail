#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "opentrail/map_selector_domain_kv_storage.hpp"
#include "opentrail/map_selector_domain_record.hpp"
#include "opentrail/map_selector_domain_store.hpp"

namespace {
using namespace opentrail::maps;
int failures = 0;
void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

class FakeBackend final : public MapSelectorDomainKvBackend {
public:
    MapSelectorDomainKvBackendError read_blob(
        const char* partition, const char* name_space, const char* key,
        std::uint8_t* output, std::size_t capacity,
        std::size_t& actual_size) override {
        ++reads;
        if (!binding(partition, name_space)) {
            return MapSelectorDomainKvBackendError::invalid_argument;
        }
        const int slot = slot_for(key);
        if (slot < 0 || output == nullptr) {
            return MapSelectorDomainKvBackendError::invalid_argument;
        }
        if (failed_read_slot == slot) {
            return MapSelectorDomainKvBackendError::io_failure;
        }
        if (!present[slot]) {
            return MapSelectorDomainKvBackendError::not_found;
        }
        actual_size = sizes[slot];
        if (capacity < actual_size) {
            return MapSelectorDomainKvBackendError::invalid_argument;
        }
        std::copy(durable[slot].begin(), durable[slot].begin() + actual_size,
                  output);
        return MapSelectorDomainKvBackendError::none;
    }

    MapSelectorDomainKvBackendError write_blob(
        const char* partition, const char* name_space, const char* key,
        const std::uint8_t* data, std::size_t size) override {
        ++writes;
        if (!binding(partition, name_space)) {
            return MapSelectorDomainKvBackendError::invalid_argument;
        }
        const int slot = slot_for(key);
        if (slot < 0 || data == nullptr ||
            size != kMapSelectorDomainRecordBytes) {
            return MapSelectorDomainKvBackendError::invalid_argument;
        }
        if (failed_write_call != 0 && writes == failed_write_call) {
            return MapSelectorDomainKvBackendError::io_failure;
        }
        pending = true;
        pending_slot = slot;
        std::copy(data, data + size, pending_bytes.begin());
        return MapSelectorDomainKvBackendError::none;
    }

    MapSelectorDomainKvBackendError commit(
        const char* partition, const char* name_space) override {
        ++commits;
        if (!binding(partition, name_space)) {
            return MapSelectorDomainKvBackendError::invalid_argument;
        }
        if (failed_commit_call != 0 && commits == failed_commit_call) {
            if (apply_then_fail) {
                apply();
            } else {
                pending = false;
            }
            return MapSelectorDomainKvBackendError::io_failure;
        }
        apply();
        return MapSelectorDomainKvBackendError::none;
    }

    void seed(int slot,
              const std::array<std::uint8_t,
                  kMapSelectorDomainRecordBytes>& bytes,
              std::size_t size = kMapSelectorDomainRecordBytes) {
        durable[slot] = bytes;
        sizes[slot] = size;
        present[slot] = true;
    }

    bool binding(const char* partition, const char* name_space) {
        const bool exact = partition != nullptr && name_space != nullptr &&
            std::strcmp(partition, kMapSelectorDomainPartitionLabel) == 0 &&
            std::strcmp(name_space, kMapSelectorDomainNamespace) == 0;
        exact_binding = exact_binding && exact;
        return exact;
    }
    int slot_for(const char* key) const {
        if (key != nullptr &&
            std::strcmp(key, kMapSelectorDomainSlotAKey) == 0) return 0;
        if (key != nullptr &&
            std::strcmp(key, kMapSelectorDomainSlotBKey) == 0) return 1;
        return -1;
    }
    void apply() {
        if (!pending) return;
        durable[pending_slot] = pending_bytes;
        sizes[pending_slot] = kMapSelectorDomainRecordBytes;
        present[pending_slot] = true;
        pending = false;
    }

    std::array<std::array<std::uint8_t,
        kMapSelectorDomainRecordBytes>, 2> durable{};
    std::array<std::uint8_t, kMapSelectorDomainRecordBytes> pending_bytes{};
    std::array<bool, 2> present{};
    std::array<std::size_t, 2> sizes{};
    bool pending{false};
    int pending_slot{-1};
    int failed_read_slot{-1};
    std::uint32_t failed_write_call{0};
    std::uint32_t failed_commit_call{0};
    bool apply_then_fail{false};
    bool exact_binding{true};
    std::uint32_t reads{0};
    std::uint32_t writes{0};
    std::uint32_t commits{0};
};

MapSelectorDomainRecord pending(std::uint64_t generation = 1) {
    MapSelectorDomainId id{};
    for (std::size_t index = 0; index < id.size(); ++index) {
        id[index] = static_cast<std::uint8_t>(index + 1);
    }
    return {kMapSelectorDomainRecordVersion,
            MapSelectorDomainRecordState::pending_first_baseline,
            MapSelectorDomainRecordOrigin::fresh_device_commissioning,
            id, {}, 0, 0, 1, generation};
}
std::array<std::uint8_t, kMapSelectorDomainRecordBytes> prepared() {
    std::array<std::uint8_t, kMapSelectorDomainRecordBytes> bytes{};
    EXPECT(encode_map_selector_domain_record(
        pending(), bytes.data(), bytes.size()).succeeded());
    bytes[kMapSelectorDomainRecordCommitOffset] = 0;
    return bytes;
}

void test_binding_and_argument_validation() {
    EXPECT(std::strcmp(kMapSelectorDomainPartitionLabel, "ot_state") == 0);
    EXPECT(std::strcmp(kMapSelectorDomainNamespace, "ot_map_domain") == 0);
    EXPECT(std::strcmp(kMapSelectorDomainSlotAKey, "otmd_a") == 0);
    EXPECT(std::strcmp(kMapSelectorDomainSlotBKey, "otmd_b") == 0);
    FakeBackend backend{};
    MapSelectorDomainKvStorage storage{backend};
    std::array<std::uint8_t, kMapSelectorDomainRecordBytes> bytes{};
    EXPECT(storage.read_slot(2, bytes.data(), bytes.size()) ==
           MapSelectorDomainStorageError::invalid_argument);
    EXPECT(storage.write_slot(0, bytes.data(), bytes.size() - 1) ==
           MapSelectorDomainStorageError::invalid_argument);
    EXPECT(storage.commit_slot(0, 0,
        kMapSelectorDomainRecordCommitMarker) ==
        MapSelectorDomainStorageError::invalid_argument);
    EXPECT(backend.reads == 0 && backend.writes == 0 && backend.commits == 0);
}

void test_exact_missing_wrong_size_and_failed_reads() {
    FakeBackend backend{};
    MapSelectorDomainKvStorage storage{backend};
    std::array<std::uint8_t, kMapSelectorDomainRecordBytes> output{};
    EXPECT(storage.read_slot(0, output.data(), output.size()) ==
           MapSelectorDomainStorageError::not_found);
    const auto bytes = prepared();
    backend.seed(1, bytes);
    EXPECT(storage.read_slot(1, output.data(), output.size()) ==
           MapSelectorDomainStorageError::none);
    EXPECT(output == bytes && backend.exact_binding);
    backend.seed(0, bytes, 79);
    EXPECT(storage.read_slot(0, output.data(), output.size()) ==
           MapSelectorDomainStorageError::io_failure);
    backend.failed_read_slot = 1;
    EXPECT(storage.read_slot(1, output.data(), output.size()) ==
           MapSelectorDomainStorageError::io_failure);
}

void test_prepared_write_and_failures() {
    const auto bytes = prepared();
    FakeBackend good{};
    MapSelectorDomainKvStorage storage{good};
    EXPECT(storage.write_slot(0, bytes.data(), bytes.size()) ==
           MapSelectorDomainStorageError::none);
    EXPECT(good.durable[0] == bytes && good.commits == 1);
    auto committed = bytes;
    committed[kMapSelectorDomainRecordCommitOffset] =
        kMapSelectorDomainRecordCommitMarker;
    EXPECT(storage.write_slot(1, committed.data(), committed.size()) ==
           MapSelectorDomainStorageError::invalid_argument);
    FakeBackend write_failed{};
    write_failed.failed_write_call = 1;
    MapSelectorDomainKvStorage failed_storage{write_failed};
    EXPECT(failed_storage.write_slot(0, bytes.data(), bytes.size()) ==
           MapSelectorDomainStorageError::io_failure);
    FakeBackend commit_failed{};
    commit_failed.failed_commit_call = 1;
    MapSelectorDomainKvStorage commit_storage{commit_failed};
    EXPECT(commit_storage.write_slot(0, bytes.data(), bytes.size()) ==
           MapSelectorDomainStorageError::io_failure);
}

void test_marker_full_blob_rewrite_and_refusal() {
    FakeBackend backend{};
    auto bytes = prepared();
    backend.seed(0, bytes);
    MapSelectorDomainKvStorage storage{backend};
    EXPECT(storage.commit_slot(0, kMapSelectorDomainRecordCommitOffset,
        kMapSelectorDomainRecordCommitMarker) ==
        MapSelectorDomainStorageError::none);
    bytes[kMapSelectorDomainRecordCommitOffset] =
        kMapSelectorDomainRecordCommitMarker;
    EXPECT(backend.durable[0] == bytes && backend.reads == 1 &&
           backend.writes == 1 && backend.commits == 1);
    EXPECT(storage.commit_slot(0, kMapSelectorDomainRecordCommitOffset,
        kMapSelectorDomainRecordCommitMarker) ==
        MapSelectorDomainStorageError::io_failure);
    FakeBackend missing{};
    MapSelectorDomainKvStorage missing_storage{missing};
    EXPECT(missing_storage.commit_slot(0,
        kMapSelectorDomainRecordCommitOffset,
        kMapSelectorDomainRecordCommitMarker) ==
        MapSelectorDomainStorageError::not_found);
}

void test_store_rotation_and_restart_recovery() {
    FakeBackend backend{};
    MapSelectorDomainKvStorage storage{backend};
    MapSelectorDomainStore store{storage};
    EXPECT(store.save(pending()).saved());
    auto active = pending(2);
    active.state = MapSelectorDomainRecordState::active;
    active.accepted_selector_generation = 5;
    EXPECT(store.save(active).saved());
    EXPECT(store.inspect().record.record_generation == 2);

    FakeBackend uncertain{};
    uncertain.failed_commit_call = 2;
    uncertain.apply_then_fail = true;
    MapSelectorDomainKvStorage uncertain_storage{uncertain};
    MapSelectorDomainStore uncertain_store{uncertain_storage};
    const auto saved = uncertain_store.save(pending());
    EXPECT(saved.error == MapSelectorDomainStoreError::storage_failure);
    EXPECT(saved.commit_uncertain);
    uncertain.failed_commit_call = 0;
    MapSelectorDomainKvStorage restarted_storage{uncertain};
    MapSelectorDomainStore restarted{restarted_storage};
    const auto inspected = restarted.inspect();
    EXPECT(inspected.record_available &&
           inspected.record.record_generation == 1);
}
}  // namespace

int main() {
    test_binding_and_argument_validation();
    test_exact_missing_wrong_size_and_failed_reads();
    test_prepared_write_and_failures();
    test_marker_full_blob_rewrite_and_refusal();
    test_store_rotation_and_restart_recovery();
    if (failures != 0) {
        std::cerr << failures << " map domain storage assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 5 map domain key/value storage scenario groups\n";
    return EXIT_SUCCESS;
}
