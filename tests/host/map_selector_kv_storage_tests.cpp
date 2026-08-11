#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "opentrail/map_activation_guard.hpp"
#include "opentrail/map_selector_checkpoint.hpp"
#include "opentrail/map_selector_kv_storage.hpp"
#include "opentrail/map_selector_store.hpp"

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

class FakeKvBackend final : public MapSelectorKvBackend {
public:
    MapSelectorKvBackendError read_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        std::uint8_t* output,
        std::size_t capacity,
        std::size_t& actual_size) override {
        ++read_calls;
        if (!binding_matches(partition_label, namespace_name)) {
            return MapSelectorKvBackendError::invalid_argument;
        }
        const int slot = slot_for_key(key);
        if (slot < 0 || output == nullptr) {
            return MapSelectorKvBackendError::invalid_argument;
        }
        last_slot = slot;
        if (fail_read_slot == slot) {
            return MapSelectorKvBackendError::io_failure;
        }
        if (!present[slot]) {
            return MapSelectorKvBackendError::not_found;
        }
        actual_size = sizes[slot];
        if (capacity < actual_size) {
            return MapSelectorKvBackendError::invalid_argument;
        }
        std::copy(
            durable[slot].begin(),
            durable[slot].begin() + actual_size,
            output);
        return MapSelectorKvBackendError::none;
    }

    MapSelectorKvBackendError write_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        const std::uint8_t* data,
        std::size_t size) override {
        ++write_calls;
        if (!binding_matches(partition_label, namespace_name)) {
            return MapSelectorKvBackendError::invalid_argument;
        }
        const int slot = slot_for_key(key);
        if (slot < 0 || data == nullptr ||
            size != kMapSelectorCheckpointBytes) {
            return MapSelectorKvBackendError::invalid_argument;
        }
        last_slot = slot;
        if (fail_write_slot == slot) {
            return MapSelectorKvBackendError::io_failure;
        }
        pending = Pending::write;
        pending_slot = slot;
        std::copy(data, data + size, pending_bytes.begin());
        return MapSelectorKvBackendError::none;
    }

    MapSelectorKvBackendError erase_key(
        const char* partition_label,
        const char* namespace_name,
        const char* key) override {
        ++erase_calls;
        if (!binding_matches(partition_label, namespace_name)) {
            return MapSelectorKvBackendError::invalid_argument;
        }
        const int slot = slot_for_key(key);
        if (slot < 0) {
            return MapSelectorKvBackendError::invalid_argument;
        }
        last_slot = slot;
        if (fail_erase_slot == slot) {
            return MapSelectorKvBackendError::io_failure;
        }
        if (!present[slot]) {
            return MapSelectorKvBackendError::not_found;
        }
        pending = Pending::erase;
        pending_slot = slot;
        return MapSelectorKvBackendError::none;
    }

    MapSelectorKvBackendError commit(
        const char* partition_label,
        const char* namespace_name) override {
        ++commit_calls;
        if (!binding_matches(partition_label, namespace_name)) {
            return MapSelectorKvBackendError::invalid_argument;
        }
        if (fail_commit) {
            if (apply_then_fail) {
                apply_pending();
            } else {
                pending = Pending::none;
            }
            return MapSelectorKvBackendError::io_failure;
        }
        apply_pending();
        return MapSelectorKvBackendError::none;
    }

    void seed(
        int slot,
        const std::array<std::uint8_t, kMapSelectorCheckpointBytes>& bytes,
        std::size_t size = kMapSelectorCheckpointBytes) {
        durable[slot] = bytes;
        sizes[slot] = size;
        present[slot] = true;
    }

    bool binding_matches(
        const char* partition_label,
        const char* namespace_name) {
        const bool exact = partition_label != nullptr &&
                           namespace_name != nullptr &&
                           std::strcmp(
                               partition_label,
                               kMapSelectorPartitionLabel) == 0 &&
                           std::strcmp(
                               namespace_name,
                               kMapSelectorNamespace) == 0;
        binding_was_exact = binding_was_exact && exact;
        return exact;
    }

    int slot_for_key(const char* key) const {
        if (key == nullptr) {
            return -1;
        }
        if (std::strcmp(key, kMapSelectorSlotAKey) == 0) {
            return 0;
        }
        if (std::strcmp(key, kMapSelectorSlotBKey) == 0) {
            return 1;
        }
        return -1;
    }

    enum class Pending : std::uint8_t {
        none = 0,
        write,
        erase,
    };

    void apply_pending() {
        if (pending == Pending::write) {
            durable[pending_slot] = pending_bytes;
            sizes[pending_slot] = kMapSelectorCheckpointBytes;
            present[pending_slot] = true;
        } else if (pending == Pending::erase) {
            durable[pending_slot].fill(0);
            sizes[pending_slot] = 0;
            present[pending_slot] = false;
        }
        pending = Pending::none;
    }

    std::array<std::array<std::uint8_t, kMapSelectorCheckpointBytes>, 2>
        durable{};
    std::array<bool, 2> present{};
    std::array<std::size_t, 2> sizes{};
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> pending_bytes{};
    Pending pending{Pending::none};
    int pending_slot{-1};
    int last_slot{-1};
    int fail_read_slot{-1};
    int fail_write_slot{-1};
    int fail_erase_slot{-1};
    bool fail_commit{false};
    bool apply_then_fail{false};
    bool binding_was_exact{true};
    std::uint32_t read_calls{0};
    std::uint32_t write_calls{0};
    std::uint32_t erase_calls{0};
    std::uint32_t commit_calls{0};
};

MapActivationPolicy policy() {
    return {8U * 1024U * 1024U, 500, 3, 3};
}

MapPackageEvidence package(
    MapSlot slot = MapSlot::slot_a,
    std::uint64_t generation = 10) {
    return {
        slot,
        generation,
        1024U * 1024U,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true};
}

MapActivationGuard active_guard(
    MapSlot slot = MapSlot::slot_a,
    std::uint64_t generation = 10) {
    MapActivationGuard guard{};
    EXPECT(guard.start(
               policy(),
               {MapSelectorState::valid, package(slot, generation)}) ==
           MapActivationError::none);
    return guard;
}

std::array<std::uint8_t, kMapSelectorCheckpointBytes> encoded(
    const MapActivationGuard& guard,
    std::uint64_t record_generation) {
    MapSelectorCheckpoint checkpoint{};
    EXPECT(guard.export_checkpoint(record_generation, checkpoint) ==
           MapActivationError::none);
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> bytes{};
    EXPECT(encode_map_selector_checkpoint(
               checkpoint, bytes.data(), bytes.size()).succeeded());
    return bytes;
}

void test_fixed_binding_names_are_bounded_and_distinct() {
    EXPECT(std::strcmp(kMapSelectorPartitionLabel, "ot_state") == 0);
    EXPECT(std::strcmp(kMapSelectorNamespace, "ot_maps") == 0);
    EXPECT(std::strcmp(kMapSelectorSlotAKey, "otm_sel_a") == 0);
    EXPECT(std::strcmp(kMapSelectorSlotBKey, "otm_sel_b") == 0);
    EXPECT(std::strcmp(kMapSelectorSlotAKey, kMapSelectorSlotBKey) != 0);
    EXPECT(std::strlen(kMapSelectorPartitionLabel) <= 15);
    EXPECT(std::strlen(kMapSelectorNamespace) <= 15);
}

void test_public_argument_validation_precedes_backend_io() {
    FakeKvBackend backend{};
    MapSelectorKvStorage storage{backend};
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> bytes{};
    EXPECT(storage.read_slot(2, bytes.data(), bytes.size()) ==
           MapSelectorStorageError::invalid_argument);
    EXPECT(storage.read_slot(0, nullptr, bytes.size()) ==
           MapSelectorStorageError::invalid_argument);
    EXPECT(storage.write_slot(0, bytes.data(), bytes.size() - 1) ==
           MapSelectorStorageError::invalid_argument);
    bytes[kMapSelectorCommitOffset] = kMapSelectorCommitMarker;
    EXPECT(storage.write_slot(0, bytes.data(), bytes.size()) ==
           MapSelectorStorageError::invalid_argument);
    EXPECT(storage.commit_slot(0, 0, kMapSelectorCommitMarker) ==
           MapSelectorStorageError::invalid_argument);
    EXPECT(storage.erase_slot(2) ==
           MapSelectorStorageError::invalid_argument);
    EXPECT(backend.read_calls == 0);
    EXPECT(backend.write_calls == 0);
    EXPECT(backend.erase_calls == 0);
    EXPECT(backend.commit_calls == 0);
}

void test_read_uses_exact_binding_and_size() {
    FakeKvBackend backend{};
    auto bytes = encoded(active_guard(), 4);
    bytes[kMapSelectorCommitOffset] = 0;
    backend.seed(1, bytes);
    MapSelectorKvStorage storage{backend};
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> output{};
    EXPECT(storage.read_slot(0, output.data(), output.size()) ==
           MapSelectorStorageError::not_found);
    EXPECT(storage.read_slot(1, output.data(), output.size()) ==
           MapSelectorStorageError::none);
    EXPECT(output == bytes);
    EXPECT(backend.last_slot == 1);
    EXPECT(backend.binding_was_exact);
}

void test_read_rejects_wrong_blob_length_and_io_failure() {
    FakeKvBackend wrong_size{};
    wrong_size.seed(0, encoded(active_guard(), 4), 63);
    MapSelectorKvStorage wrong_size_storage{wrong_size};
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> output{};
    EXPECT(wrong_size_storage.read_slot(
               0, output.data(), output.size()) ==
           MapSelectorStorageError::io_failure);

    FakeKvBackend failed{};
    failed.fail_read_slot = 0;
    MapSelectorKvStorage failed_storage{failed};
    EXPECT(failed_storage.read_slot(0, output.data(), output.size()) ==
           MapSelectorStorageError::io_failure);
}

void test_prepared_write_requires_backend_commit() {
    FakeKvBackend backend{};
    MapSelectorKvStorage storage{backend};
    auto bytes = encoded(active_guard(), 4);
    bytes[kMapSelectorCommitOffset] = 0;
    EXPECT(storage.write_slot(0, bytes.data(), bytes.size()) ==
           MapSelectorStorageError::none);
    EXPECT(backend.write_calls == 1);
    EXPECT(backend.commit_calls == 1);
    EXPECT(backend.present[0]);
    EXPECT(backend.durable[0] == bytes);
    EXPECT(backend.binding_was_exact);
}

void test_write_and_commit_failures_are_visible() {
    auto bytes = encoded(active_guard(), 4);
    bytes[kMapSelectorCommitOffset] = 0;
    FakeKvBackend write_failed{};
    write_failed.fail_write_slot = 0;
    MapSelectorKvStorage write_storage{write_failed};
    EXPECT(write_storage.write_slot(0, bytes.data(), bytes.size()) ==
           MapSelectorStorageError::io_failure);
    EXPECT(write_failed.commit_calls == 0);
    EXPECT(!write_failed.present[0]);

    FakeKvBackend commit_failed{};
    commit_failed.fail_commit = true;
    MapSelectorKvStorage commit_storage{commit_failed};
    EXPECT(commit_storage.write_slot(0, bytes.data(), bytes.size()) ==
           MapSelectorStorageError::io_failure);
    EXPECT(commit_failed.commit_calls == 1);
    EXPECT(!commit_failed.present[0]);

    FakeKvBackend uncertain{};
    uncertain.seed(0, bytes);
    uncertain.fail_commit = true;
    uncertain.apply_then_fail = true;
    MapSelectorKvStorage uncertain_storage{uncertain};
    EXPECT(uncertain_storage.commit_slot(
               0,
               kMapSelectorCommitOffset,
               kMapSelectorCommitMarker) ==
           MapSelectorStorageError::io_failure);
    EXPECT(uncertain.present[0]);
    EXPECT(uncertain.durable[0][kMapSelectorCommitOffset] ==
           kMapSelectorCommitMarker);
}

void test_commit_marker_rewrites_exact_complete_blob() {
    FakeKvBackend backend{};
    auto prepared = encoded(active_guard(), 4);
    prepared[kMapSelectorCommitOffset] = 0;
    backend.seed(0, prepared);
    MapSelectorKvStorage storage{backend};
    EXPECT(storage.commit_slot(
               0,
               kMapSelectorCommitOffset,
               kMapSelectorCommitMarker) ==
           MapSelectorStorageError::none);
    auto expected = prepared;
    expected[kMapSelectorCommitOffset] = kMapSelectorCommitMarker;
    EXPECT(backend.durable[0] == expected);
    EXPECT(backend.read_calls == 1);
    EXPECT(backend.write_calls == 1);
    EXPECT(backend.commit_calls == 1);
}

void test_commit_marker_rejects_missing_malformed_or_committed_blob() {
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> bytes{};
    FakeKvBackend missing{};
    MapSelectorKvStorage missing_storage{missing};
    EXPECT(missing_storage.commit_slot(
               0,
               kMapSelectorCommitOffset,
               kMapSelectorCommitMarker) ==
           MapSelectorStorageError::not_found);
    EXPECT(missing.write_calls == 0);

    FakeKvBackend malformed{};
    malformed.seed(0, bytes, 63);
    MapSelectorKvStorage malformed_storage{malformed};
    EXPECT(malformed_storage.commit_slot(
               0,
               kMapSelectorCommitOffset,
               kMapSelectorCommitMarker) ==
           MapSelectorStorageError::io_failure);
    EXPECT(malformed.write_calls == 0);

    FakeKvBackend committed{};
    auto valid = encoded(active_guard(), 4);
    committed.seed(0, valid);
    MapSelectorKvStorage committed_storage{committed};
    EXPECT(committed_storage.commit_slot(
               0,
               kMapSelectorCommitOffset,
               kMapSelectorCommitMarker) ==
           MapSelectorStorageError::io_failure);
    EXPECT(committed.write_calls == 0);
}

void test_erase_is_idempotent_and_commit_bound() {
    FakeKvBackend backend{};
    backend.seed(0, encoded(active_guard(), 4));
    MapSelectorKvStorage storage{backend};
    EXPECT(storage.erase_slot(0) == MapSelectorStorageError::none);
    EXPECT(!backend.present[0]);
    EXPECT(backend.erase_calls == 1);
    EXPECT(backend.commit_calls == 1);
    EXPECT(storage.erase_slot(1) == MapSelectorStorageError::none);
    EXPECT(backend.erase_calls == 2);
    EXPECT(backend.commit_calls == 1);

    backend.seed(1, encoded(active_guard(), 5));
    backend.fail_commit = true;
    EXPECT(storage.erase_slot(1) == MapSelectorStorageError::io_failure);
    EXPECT(backend.present[1]);
}

void test_store_composition_preserves_commit_last_and_verified_clear() {
    FakeKvBackend backend{};
    MapSelectorKvStorage storage{backend};
    MapSelectorStore store{storage};
    const auto guard = active_guard();
    const auto first = store.save(guard);
    EXPECT(first.saved());
    EXPECT(first.generation == 1);
    EXPECT(backend.commit_calls == 2);
    EXPECT(backend.durable[0][kMapSelectorCommitOffset] ==
           kMapSelectorCommitMarker);
    EXPECT(store.inspect().generation == 1);

    const auto second = store.save(guard);
    EXPECT(second.saved());
    EXPECT(second.generation == 2);
    EXPECT(backend.present[0]);
    EXPECT(backend.present[1]);

    const auto cleared = store.reset_and_verify_empty();
    EXPECT(cleared.cleared());
    EXPECT(!backend.present[0]);
    EXPECT(!backend.present[1]);
}

}  // namespace

int main() {
    test_fixed_binding_names_are_bounded_and_distinct();
    test_public_argument_validation_precedes_backend_io();
    test_read_uses_exact_binding_and_size();
    test_read_rejects_wrong_blob_length_and_io_failure();
    test_prepared_write_requires_backend_commit();
    test_write_and_commit_failures_are_visible();
    test_commit_marker_rewrites_exact_complete_blob();
    test_commit_marker_rejects_missing_malformed_or_committed_blob();
    test_erase_is_idempotent_and_commit_bound();
    test_store_composition_preserves_commit_last_and_verified_clear();

    if (failures != 0) {
        std::cerr << failures
                  << " map selector key/value storage assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 map selector key/value storage scenario groups\n";
    return EXIT_SUCCESS;
}
