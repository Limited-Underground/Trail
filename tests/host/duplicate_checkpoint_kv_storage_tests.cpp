#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "opentrail/duplicate_checkpoint_kv_storage.hpp"
#include "opentrail/duplicate_checkpoint_store.hpp"
#include "opentrail/duplicate_window.hpp"

namespace {

using namespace opentrail::delivery;

int failures = 0;
constexpr std::uint64_t kGroupContext = 0x0102030405060708ULL;
constexpr std::uint32_t kGroupEpoch = 0x11223344U;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

class FakeKvBackend final : public DuplicateCheckpointKvBackend {
public:
    DuplicateCheckpointKvBackendError read_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        std::uint8_t* output,
        std::size_t capacity,
        std::size_t& actual_size) override {
        ++read_calls;
        if (!binding_matches(partition_label, namespace_name)) {
            return DuplicateCheckpointKvBackendError::invalid_argument;
        }
        const int slot = slot_for_key(key);
        if (slot < 0 || output == nullptr) {
            return DuplicateCheckpointKvBackendError::invalid_argument;
        }
        last_slot = slot;
        if (fail_read_slot == slot) {
            return DuplicateCheckpointKvBackendError::io_failure;
        }
        if (!present[slot]) {
            return DuplicateCheckpointKvBackendError::not_found;
        }
        actual_size = sizes[slot];
        if (capacity < actual_size) {
            return DuplicateCheckpointKvBackendError::invalid_argument;
        }
        std::copy(
            durable[slot].begin(),
            durable[slot].begin() + actual_size,
            output);
        return DuplicateCheckpointKvBackendError::none;
    }

    DuplicateCheckpointKvBackendError write_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        const std::uint8_t* data,
        std::size_t size) override {
        ++write_calls;
        if (!binding_matches(partition_label, namespace_name)) {
            return DuplicateCheckpointKvBackendError::invalid_argument;
        }
        const int slot = slot_for_key(key);
        if (slot < 0 || data == nullptr ||
            size != kStoredDuplicateCheckpointBytes) {
            return DuplicateCheckpointKvBackendError::invalid_argument;
        }
        last_slot = slot;
        if (fail_write_slot == slot) {
            return DuplicateCheckpointKvBackendError::io_failure;
        }
        pending = Pending::write;
        pending_slot = slot;
        std::copy(data, data + size, pending_bytes.begin());
        return DuplicateCheckpointKvBackendError::none;
    }

    DuplicateCheckpointKvBackendError erase_key(
        const char* partition_label,
        const char* namespace_name,
        const char* key) override {
        ++erase_calls;
        if (!binding_matches(partition_label, namespace_name)) {
            return DuplicateCheckpointKvBackendError::invalid_argument;
        }
        const int slot = slot_for_key(key);
        if (slot < 0) {
            return DuplicateCheckpointKvBackendError::invalid_argument;
        }
        last_slot = slot;
        if (fail_erase_slot == slot) {
            return DuplicateCheckpointKvBackendError::io_failure;
        }
        if (!present[slot]) {
            return DuplicateCheckpointKvBackendError::not_found;
        }
        pending = Pending::erase;
        pending_slot = slot;
        return DuplicateCheckpointKvBackendError::none;
    }

    DuplicateCheckpointKvBackendError commit(
        const char* partition_label,
        const char* namespace_name) override {
        ++commit_calls;
        if (!binding_matches(partition_label, namespace_name)) {
            return DuplicateCheckpointKvBackendError::invalid_argument;
        }
        if (fail_commit) {
            if (apply_then_fail) {
                apply_pending();
            } else {
                pending = Pending::none;
            }
            return DuplicateCheckpointKvBackendError::io_failure;
        }
        apply_pending();
        return DuplicateCheckpointKvBackendError::none;
    }

    void seed(
        int slot,
        const std::array<std::uint8_t, kStoredDuplicateCheckpointBytes>& bytes,
        std::size_t size = kStoredDuplicateCheckpointBytes) {
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
                               kDuplicateCheckpointPartitionLabel) == 0 &&
                           std::strcmp(
                               namespace_name,
                               kDuplicateCheckpointNamespace) == 0;
        binding_was_exact = binding_was_exact && exact;
        return exact;
    }

    int slot_for_key(const char* key) const {
        if (key == nullptr) {
            return -1;
        }
        if (std::strcmp(key, kDuplicateCheckpointSlotAKey) == 0) {
            return 0;
        }
        if (std::strcmp(key, kDuplicateCheckpointSlotBKey) == 0) {
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
            sizes[pending_slot] = kStoredDuplicateCheckpointBytes;
            present[pending_slot] = true;
        } else if (pending == Pending::erase) {
            durable[pending_slot].fill(0);
            sizes[pending_slot] = 0;
            present[pending_slot] = false;
        }
        pending = Pending::none;
    }

    std::array<
        std::array<std::uint8_t, kStoredDuplicateCheckpointBytes>,
        2> durable{};
    std::array<bool, 2> present{};
    std::array<std::size_t, 2> sizes{};
    std::array<std::uint8_t, kStoredDuplicateCheckpointBytes> pending_bytes{};
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

std::array<std::uint8_t, kStoredDuplicateCheckpointBytes> sample_bytes() {
    std::array<std::uint8_t, kStoredDuplicateCheckpointBytes> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(index & 0xFFU);
    }
    return bytes;
}

DuplicateWindow window_with(const DuplicateKey& key) {
    DuplicateWindow window{1000};
    EXPECT(window.observe(key, 100).observation ==
           DuplicateObservation::accepted);
    return window;
}

void test_fixed_binding_names_are_bounded_and_distinct() {
    EXPECT(std::strcmp(kDuplicateCheckpointPartitionLabel, "ot_state") == 0);
    EXPECT(std::strcmp(kDuplicateCheckpointNamespace, "ot_replay") == 0);
    EXPECT(std::strcmp(kDuplicateCheckpointSlotAKey, "ods_dup_a") == 0);
    EXPECT(std::strcmp(kDuplicateCheckpointSlotBKey, "ods_dup_b") == 0);
    EXPECT(std::strcmp(
               kDuplicateCheckpointSlotAKey,
               kDuplicateCheckpointSlotBKey) != 0);
    EXPECT(std::strlen(kDuplicateCheckpointPartitionLabel) <= 15);
    EXPECT(std::strlen(kDuplicateCheckpointNamespace) <= 15);
}

void test_public_argument_validation_precedes_backend_io() {
    FakeKvBackend backend{};
    DuplicateCheckpointKvStorage storage{backend};
    std::array<std::uint8_t, kStoredDuplicateCheckpointBytes> bytes{};
    EXPECT(storage.read_slot(2, bytes.data(), bytes.size()) ==
           DuplicateCheckpointStorageError::invalid_argument);
    EXPECT(storage.read_slot(0, nullptr, bytes.size()) ==
           DuplicateCheckpointStorageError::invalid_argument);
    EXPECT(storage.write_slot(0, bytes.data(), bytes.size() - 1) ==
           DuplicateCheckpointStorageError::invalid_argument);
    EXPECT(storage.write_slot(0, nullptr, bytes.size()) ==
           DuplicateCheckpointStorageError::invalid_argument);
    EXPECT(storage.erase_slot(2) ==
           DuplicateCheckpointStorageError::invalid_argument);
    EXPECT(backend.read_calls == 0);
    EXPECT(backend.write_calls == 0);
    EXPECT(backend.erase_calls == 0);
    EXPECT(backend.commit_calls == 0);
}

void test_read_uses_exact_binding_and_size() {
    FakeKvBackend backend{};
    const auto bytes = sample_bytes();
    backend.seed(1, bytes);
    DuplicateCheckpointKvStorage storage{backend};
    std::array<std::uint8_t, kStoredDuplicateCheckpointBytes> output{};
    EXPECT(storage.read_slot(0, output.data(), output.size()) ==
           DuplicateCheckpointStorageError::not_found);
    EXPECT(storage.read_slot(1, output.data(), output.size()) ==
           DuplicateCheckpointStorageError::none);
    EXPECT(output == bytes);
    EXPECT(backend.last_slot == 1);
    EXPECT(backend.binding_was_exact);
}

void test_read_rejects_wrong_blob_length_and_io_failure() {
    FakeKvBackend wrong_size{};
    wrong_size.seed(0, sample_bytes(), 703);
    DuplicateCheckpointKvStorage wrong_size_storage{wrong_size};
    std::array<std::uint8_t, kStoredDuplicateCheckpointBytes> output{};
    EXPECT(wrong_size_storage.read_slot(
               0, output.data(), output.size()) ==
           DuplicateCheckpointStorageError::io_failure);

    FakeKvBackend failed{};
    failed.fail_read_slot = 0;
    DuplicateCheckpointKvStorage failed_storage{failed};
    EXPECT(failed_storage.read_slot(0, output.data(), output.size()) ==
           DuplicateCheckpointStorageError::io_failure);
}

void test_write_requires_durable_backend_commit() {
    FakeKvBackend backend{};
    DuplicateCheckpointKvStorage storage{backend};
    const auto bytes = sample_bytes();
    EXPECT(storage.write_slot(0, bytes.data(), bytes.size()) ==
           DuplicateCheckpointStorageError::none);
    EXPECT(backend.write_calls == 1);
    EXPECT(backend.commit_calls == 1);
    EXPECT(backend.present[0]);
    EXPECT(backend.durable[0] == bytes);
    EXPECT(backend.binding_was_exact);
}

void test_write_and_commit_failures_are_visible() {
    const auto bytes = sample_bytes();
    FakeKvBackend write_failed{};
    write_failed.fail_write_slot = 0;
    DuplicateCheckpointKvStorage write_storage{write_failed};
    EXPECT(write_storage.write_slot(0, bytes.data(), bytes.size()) ==
           DuplicateCheckpointStorageError::io_failure);
    EXPECT(write_failed.commit_calls == 0);

    FakeKvBackend commit_failed{};
    commit_failed.fail_commit = true;
    DuplicateCheckpointKvStorage commit_storage{commit_failed};
    EXPECT(commit_storage.write_slot(0, bytes.data(), bytes.size()) ==
           DuplicateCheckpointStorageError::io_failure);
    EXPECT(!commit_failed.present[0]);

    FakeKvBackend uncertain{};
    uncertain.fail_commit = true;
    uncertain.apply_then_fail = true;
    DuplicateCheckpointKvStorage uncertain_storage{uncertain};
    EXPECT(uncertain_storage.write_slot(
               0, bytes.data(), bytes.size()) ==
           DuplicateCheckpointStorageError::io_failure);
    EXPECT(uncertain.present[0]);
    EXPECT(uncertain.durable[0] == bytes);
}

void test_erase_is_idempotent_commit_bound_and_fail_visible() {
    FakeKvBackend backend{};
    backend.seed(0, sample_bytes());
    DuplicateCheckpointKvStorage storage{backend};
    EXPECT(storage.erase_slot(0) ==
           DuplicateCheckpointStorageError::none);
    EXPECT(!backend.present[0]);
    EXPECT(backend.commit_calls == 1);
    EXPECT(storage.erase_slot(1) ==
           DuplicateCheckpointStorageError::none);
    EXPECT(backend.commit_calls == 1);

    backend.seed(1, sample_bytes());
    backend.fail_erase_slot = 1;
    EXPECT(storage.erase_slot(1) ==
           DuplicateCheckpointStorageError::io_failure);
    backend.fail_erase_slot = -1;
    backend.fail_commit = true;
    EXPECT(storage.erase_slot(1) ==
           DuplicateCheckpointStorageError::io_failure);
    EXPECT(backend.present[1]);
}

void test_store_composition_preserves_rotation_restore_and_reset() {
    FakeKvBackend backend{};
    DuplicateCheckpointKvStorage storage{backend};
    DuplicateCheckpointStore store{
        storage, kGroupContext, kGroupEpoch};
    const DuplicateKey key{1, kGroupEpoch, 3};
    const auto original = window_with(key);
    const auto first = store.save(original, 400);
    EXPECT(first.saved());
    EXPECT(first.generation == 1);
    const auto second = store.save(original, 450);
    EXPECT(second.saved());
    EXPECT(second.generation == 2);

    DuplicateWindow restored{1000};
    const auto loaded = store.restore(restored, 500);
    EXPECT(loaded.restored);
    EXPECT(loaded.generation == 2);
    EXPECT(loaded.source == DuplicateCheckpointSource::slot_b);
    EXPECT(restored.observe(key, 500).observation ==
           DuplicateObservation::duplicate);
    EXPECT(store.reset() == DuplicateCheckpointStoreError::none);
    EXPECT(!backend.present[0]);
    EXPECT(!backend.present[1]);
}

void test_applied_then_failed_commit_recovers_after_restart() {
    FakeKvBackend backend{};
    backend.fail_commit = true;
    backend.apply_then_fail = true;
    DuplicateCheckpointKvStorage storage{backend};
    DuplicateCheckpointStore store{
        storage, kGroupContext, kGroupEpoch};
    const DuplicateKey key{1, kGroupEpoch, 3};
    const auto saved = store.save(window_with(key), 400);
    EXPECT(saved.error == DuplicateCheckpointStoreError::storage_failure);
    EXPECT(backend.present[0]);

    backend.fail_commit = false;
    DuplicateWindow restored{1000};
    const auto loaded = store.restore(restored, 500);
    EXPECT(loaded.restored);
    EXPECT(loaded.generation == 1);
    EXPECT(loaded.source == DuplicateCheckpointSource::slot_a);
    EXPECT(loaded.recovery_required);
    EXPECT(restored.observe(key, 500).observation ==
           DuplicateObservation::duplicate);
}

}  // namespace

int main() {
    test_fixed_binding_names_are_bounded_and_distinct();
    test_public_argument_validation_precedes_backend_io();
    test_read_uses_exact_binding_and_size();
    test_read_rejects_wrong_blob_length_and_io_failure();
    test_write_requires_durable_backend_commit();
    test_write_and_commit_failures_are_visible();
    test_erase_is_idempotent_commit_bound_and_fail_visible();
    test_store_composition_preserves_rotation_restore_and_reset();
    test_applied_then_failed_commit_recovers_after_restart();

    if (failures != 0) {
        std::cerr << failures
                  << " duplicate checkpoint key/value storage assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 9 duplicate checkpoint key/value storage scenario groups\n";
    return EXIT_SUCCESS;
}
