#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "opentrail/update_boot_guard.hpp"
#include "opentrail/update_checkpoint.hpp"
#include "opentrail/update_checkpoint_kv_storage.hpp"
#include "opentrail/update_checkpoint_store.hpp"

namespace {

using namespace opentrail::update;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

class FakeKvBackend final : public UpdateCheckpointKvBackend {
public:
    UpdateCheckpointKvBackendError read_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        std::uint8_t* output,
        std::size_t capacity,
        std::size_t& actual_size) override {
        ++read_calls;
        if (!binding_matches(partition_label, namespace_name)) {
            return UpdateCheckpointKvBackendError::invalid_argument;
        }
        const int slot = slot_for_key(key);
        if (slot < 0 || output == nullptr) {
            return UpdateCheckpointKvBackendError::invalid_argument;
        }
        last_slot = slot;
        if (fail_read_slot == slot) {
            return UpdateCheckpointKvBackendError::io_failure;
        }
        if (!present[slot]) {
            return UpdateCheckpointKvBackendError::not_found;
        }
        actual_size = sizes[slot];
        if (capacity < actual_size) {
            return UpdateCheckpointKvBackendError::invalid_argument;
        }
        std::copy(
            durable[slot].begin(),
            durable[slot].begin() + actual_size,
            output);
        return UpdateCheckpointKvBackendError::none;
    }

    UpdateCheckpointKvBackendError write_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        const std::uint8_t* data,
        std::size_t size) override {
        ++write_calls;
        if (!binding_matches(partition_label, namespace_name)) {
            return UpdateCheckpointKvBackendError::invalid_argument;
        }
        const int slot = slot_for_key(key);
        if (slot < 0 || data == nullptr ||
            size != kUpdateCheckpointRecordBytes) {
            return UpdateCheckpointKvBackendError::invalid_argument;
        }
        last_slot = slot;
        if (fail_write_slot == slot) {
            return UpdateCheckpointKvBackendError::io_failure;
        }
        pending = Pending::write;
        pending_slot = slot;
        std::copy(data, data + size, pending_bytes.begin());
        return UpdateCheckpointKvBackendError::none;
    }

    UpdateCheckpointKvBackendError erase_key(
        const char* partition_label,
        const char* namespace_name,
        const char* key) override {
        ++erase_calls;
        if (!binding_matches(partition_label, namespace_name)) {
            return UpdateCheckpointKvBackendError::invalid_argument;
        }
        const int slot = slot_for_key(key);
        if (slot < 0) {
            return UpdateCheckpointKvBackendError::invalid_argument;
        }
        last_slot = slot;
        if (fail_erase_slot == slot) {
            return UpdateCheckpointKvBackendError::io_failure;
        }
        if (!present[slot]) {
            return UpdateCheckpointKvBackendError::not_found;
        }
        pending = Pending::erase;
        pending_slot = slot;
        return UpdateCheckpointKvBackendError::none;
    }

    UpdateCheckpointKvBackendError commit(
        const char* partition_label,
        const char* namespace_name) override {
        ++commit_calls;
        if (!binding_matches(partition_label, namespace_name)) {
            return UpdateCheckpointKvBackendError::invalid_argument;
        }
        if (fail_commit) {
            if (apply_then_fail) {
                apply_pending();
            } else {
                pending = Pending::none;
            }
            return UpdateCheckpointKvBackendError::io_failure;
        }
        apply_pending();
        return UpdateCheckpointKvBackendError::none;
    }

    void seed(
        int slot,
        const std::array<std::uint8_t, kUpdateCheckpointRecordBytes>& bytes,
        std::size_t size = kUpdateCheckpointRecordBytes) {
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
                               kUpdateCheckpointPartitionLabel) == 0 &&
                           std::strcmp(
                               namespace_name,
                               kUpdateCheckpointNamespace) == 0;
        binding_was_exact = binding_was_exact && exact;
        return exact;
    }

    int slot_for_key(const char* key) const {
        if (key == nullptr) {
            return -1;
        }
        if (std::strcmp(key, kUpdateCheckpointSlotAKey) == 0) {
            return 0;
        }
        if (std::strcmp(key, kUpdateCheckpointSlotBKey) == 0) {
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
            sizes[pending_slot] = kUpdateCheckpointRecordBytes;
            present[pending_slot] = true;
        } else if (pending == Pending::erase) {
            durable[pending_slot].fill(0);
            sizes[pending_slot] = 0;
            present[pending_slot] = false;
        }
        pending = Pending::none;
    }

    std::array<std::array<std::uint8_t, kUpdateCheckpointRecordBytes>, 2>
        durable{};
    std::array<bool, 2> present{};
    std::array<std::size_t, 2> sizes{};
    std::array<std::uint8_t, kUpdateCheckpointRecordBytes> pending_bytes{};
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

UpdateGuardPolicy policy() {
    return {
        0x1234U,
        10,
        ImageSlot::slot_a,
        health_bit(TrialHealth::runtime_started) |
            health_bit(TrialHealth::watchdog_healthy) |
            health_bit(TrialHealth::configuration_loaded),
        100,
        500,
        3,
        4U * 1024U * 1024U};
}

VerifiedUpdateCandidate candidate() {
    return {
        0x1234U,
        11,
        ImageSlot::slot_b,
        1024U * 1024U,
        true,
        true,
        true,
        true};
}

UpdateBootGuard pending_guard() {
    UpdateBootGuard guard{};
    EXPECT(guard.start(policy()) == UpdateGuardError::none);
    EXPECT(guard.stage(candidate()) == UpdateGuardError::none);
    EXPECT(guard.mark_written({11, ImageSlot::slot_b, true, true}) ==
           UpdateGuardError::none);
    return guard;
}

std::array<std::uint8_t, kUpdateCheckpointRecordBytes> encoded(
    const UpdateBootGuard& guard,
    std::uint64_t generation) {
    UpdateGuardCheckpoint checkpoint{};
    EXPECT(guard.export_checkpoint(generation, checkpoint) ==
           UpdateGuardError::none);
    std::array<std::uint8_t, kUpdateCheckpointRecordBytes> bytes{};
    EXPECT(encode_update_checkpoint(
               checkpoint, bytes.data(), bytes.size()).succeeded());
    return bytes;
}

void test_fixed_binding_names_are_bounded_and_distinct() {
    EXPECT(std::strcmp(kUpdateCheckpointPartitionLabel, "ot_state") == 0);
    EXPECT(std::strcmp(kUpdateCheckpointNamespace, "ot_update") == 0);
    EXPECT(std::strcmp(kUpdateCheckpointSlotAKey, "otu_chk_a") == 0);
    EXPECT(std::strcmp(kUpdateCheckpointSlotBKey, "otu_chk_b") == 0);
    EXPECT(std::strcmp(
               kUpdateCheckpointSlotAKey,
               kUpdateCheckpointSlotBKey) != 0);
    EXPECT(std::strlen(kUpdateCheckpointPartitionLabel) <= 15);
    EXPECT(std::strlen(kUpdateCheckpointNamespace) <= 15);
}

void test_public_argument_validation_precedes_backend_io() {
    FakeKvBackend backend{};
    UpdateCheckpointKvStorage storage{backend};
    std::array<std::uint8_t, kUpdateCheckpointRecordBytes> bytes{};
    EXPECT(storage.read_slot(2, bytes.data(), bytes.size()) ==
           UpdateCheckpointStorageError::invalid_argument);
    EXPECT(storage.read_slot(0, nullptr, bytes.size()) ==
           UpdateCheckpointStorageError::invalid_argument);
    EXPECT(storage.write_slot(0, bytes.data(), bytes.size() - 1) ==
           UpdateCheckpointStorageError::invalid_argument);
    EXPECT(storage.write_slot(0, nullptr, bytes.size()) ==
           UpdateCheckpointStorageError::invalid_argument);
    EXPECT(storage.erase_slot(2) ==
           UpdateCheckpointStorageError::invalid_argument);
    EXPECT(backend.read_calls == 0);
    EXPECT(backend.write_calls == 0);
    EXPECT(backend.erase_calls == 0);
    EXPECT(backend.commit_calls == 0);
}

void test_read_uses_exact_binding_and_size() {
    FakeKvBackend backend{};
    const auto bytes = encoded(pending_guard(), 4);
    backend.seed(1, bytes);
    UpdateCheckpointKvStorage storage{backend};
    std::array<std::uint8_t, kUpdateCheckpointRecordBytes> output{};
    EXPECT(storage.read_slot(0, output.data(), output.size()) ==
           UpdateCheckpointStorageError::not_found);
    EXPECT(storage.read_slot(1, output.data(), output.size()) ==
           UpdateCheckpointStorageError::none);
    EXPECT(output == bytes);
    EXPECT(backend.last_slot == 1);
    EXPECT(backend.binding_was_exact);
}

void test_read_rejects_wrong_blob_length_and_io_failure() {
    FakeKvBackend wrong_size{};
    wrong_size.seed(0, encoded(pending_guard(), 4), 63);
    UpdateCheckpointKvStorage wrong_size_storage{wrong_size};
    std::array<std::uint8_t, kUpdateCheckpointRecordBytes> output{};
    EXPECT(wrong_size_storage.read_slot(
               0, output.data(), output.size()) ==
           UpdateCheckpointStorageError::io_failure);

    FakeKvBackend failed{};
    failed.fail_read_slot = 0;
    UpdateCheckpointKvStorage failed_storage{failed};
    EXPECT(failed_storage.read_slot(0, output.data(), output.size()) ==
           UpdateCheckpointStorageError::io_failure);
}

void test_write_requires_durable_backend_commit() {
    FakeKvBackend backend{};
    UpdateCheckpointKvStorage storage{backend};
    const auto bytes = encoded(pending_guard(), 4);
    EXPECT(storage.write_slot(0, bytes.data(), bytes.size()) ==
           UpdateCheckpointStorageError::none);
    EXPECT(backend.write_calls == 1);
    EXPECT(backend.commit_calls == 1);
    EXPECT(backend.present[0]);
    EXPECT(backend.durable[0] == bytes);
    EXPECT(backend.binding_was_exact);
}

void test_write_and_commit_failures_are_visible() {
    const auto bytes = encoded(pending_guard(), 4);
    FakeKvBackend write_failed{};
    write_failed.fail_write_slot = 0;
    UpdateCheckpointKvStorage write_storage{write_failed};
    EXPECT(write_storage.write_slot(0, bytes.data(), bytes.size()) ==
           UpdateCheckpointStorageError::io_failure);
    EXPECT(write_failed.commit_calls == 0);
    EXPECT(!write_failed.present[0]);

    FakeKvBackend commit_failed{};
    commit_failed.fail_commit = true;
    UpdateCheckpointKvStorage commit_storage{commit_failed};
    EXPECT(commit_storage.write_slot(0, bytes.data(), bytes.size()) ==
           UpdateCheckpointStorageError::io_failure);
    EXPECT(commit_failed.commit_calls == 1);
    EXPECT(!commit_failed.present[0]);

    FakeKvBackend uncertain{};
    uncertain.fail_commit = true;
    uncertain.apply_then_fail = true;
    UpdateCheckpointKvStorage uncertain_storage{uncertain};
    EXPECT(uncertain_storage.write_slot(
               0, bytes.data(), bytes.size()) ==
           UpdateCheckpointStorageError::io_failure);
    EXPECT(uncertain.present[0]);
    EXPECT(uncertain.durable[0] == bytes);
}

void test_erase_is_idempotent_commit_bound_and_fail_visible() {
    FakeKvBackend backend{};
    backend.seed(0, encoded(pending_guard(), 4));
    UpdateCheckpointKvStorage storage{backend};
    EXPECT(storage.erase_slot(0) == UpdateCheckpointStorageError::none);
    EXPECT(!backend.present[0]);
    EXPECT(backend.erase_calls == 1);
    EXPECT(backend.commit_calls == 1);
    EXPECT(storage.erase_slot(1) == UpdateCheckpointStorageError::none);
    EXPECT(backend.erase_calls == 2);
    EXPECT(backend.commit_calls == 1);

    backend.seed(1, encoded(pending_guard(), 5));
    backend.fail_erase_slot = 1;
    EXPECT(storage.erase_slot(1) ==
           UpdateCheckpointStorageError::io_failure);
    EXPECT(backend.commit_calls == 1);
    backend.fail_erase_slot = -1;
    backend.fail_commit = true;
    EXPECT(storage.erase_slot(1) ==
           UpdateCheckpointStorageError::io_failure);
    EXPECT(backend.present[1]);
}

void test_store_composition_preserves_rotation_restore_and_reset() {
    FakeKvBackend backend{};
    UpdateCheckpointKvStorage storage{backend};
    UpdateCheckpointStore store{storage};
    const auto guard = pending_guard();
    const auto first = store.save(guard);
    EXPECT(first.saved());
    EXPECT(first.generation == 1);
    EXPECT(first.written_slot == UpdateCheckpointSource::slot_a);
    EXPECT(backend.commit_calls == 1);
    const auto second = store.save(guard);
    EXPECT(second.saved());
    EXPECT(second.generation == 2);
    EXPECT(second.written_slot == UpdateCheckpointSource::slot_b);

    UpdateBootGuard restored{};
    EXPECT(restored.start(policy()) == UpdateGuardError::none);
    const auto loaded = store.restore(restored);
    EXPECT(loaded.restored);
    EXPECT(loaded.generation == 2);
    EXPECT(restored.status().state == UpdateState::pending_reboot);
    EXPECT(store.reset() == UpdateCheckpointStoreError::none);
    EXPECT(!backend.present[0]);
    EXPECT(!backend.present[1]);
}

void test_applied_then_failed_commit_recovers_after_restart() {
    FakeKvBackend backend{};
    backend.fail_commit = true;
    backend.apply_then_fail = true;
    UpdateCheckpointKvStorage storage{backend};
    UpdateCheckpointStore store{storage};
    const auto saved = store.save(pending_guard());
    EXPECT(saved.error == UpdateCheckpointStoreError::storage_failure);
    EXPECT(saved.commit_uncertain);
    EXPECT(backend.present[0]);

    backend.fail_commit = false;
    UpdateBootGuard restored{};
    EXPECT(restored.start(policy()) == UpdateGuardError::none);
    const auto loaded = store.restore(restored);
    EXPECT(loaded.restored);
    EXPECT(loaded.generation == 1);
    EXPECT(loaded.source == UpdateCheckpointSource::slot_a);
    EXPECT(loaded.recovery_required);
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
                  << " update checkpoint key/value storage assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 9 update checkpoint key/value storage scenario groups\n";
    return EXIT_SUCCESS;
}
