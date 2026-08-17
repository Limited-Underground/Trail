#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "opentrail/companion_authorization_protected_kv_media.hpp"

namespace {

using namespace opentrail::companion;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

class FakeKvBackend final : public CompanionAuthorizationProtectedKvBackend {
public:
    CompanionAuthorizationProtectedKvBackendError read_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        std::uint8_t* output,
        std::size_t capacity,
        std::size_t& actual_size) override {
        ++read_calls;
        binding_exact = binding_exact && valid_binding(partition_label,
                                                        namespace_name);
        const int slot = slot_for_key(key);
        if (!binding_exact || slot < 0 || output == nullptr) {
            return CompanionAuthorizationProtectedKvBackendError::failed;
        }
        last_slot = slot;
        if (reenter_on_read && !reentry_fired && media != nullptr) {
            reentry_fired = true;
            reentry_result = media->read_slot(
                CompanionAuthorizationProtectedSlot::b).error;
        }
        if (read_error != CompanionAuthorizationProtectedKvBackendError::none) {
            return read_error;
        }
        if (!present[slot]) {
            return CompanionAuthorizationProtectedKvBackendError::not_found;
        }
        actual_size = sizes[slot];
        const auto copy_size = std::min(capacity, actual_size);
        std::copy_n(durable[slot].begin(), copy_size, output);
        return CompanionAuthorizationProtectedKvBackendError::none;
    }

    CompanionAuthorizationProtectedKvBackendError write_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        const std::uint8_t* data,
        std::size_t size) override {
        ++write_calls;
        binding_exact = binding_exact && valid_binding(partition_label,
                                                        namespace_name);
        const int slot = slot_for_key(key);
        if (!binding_exact || slot < 0 || data == nullptr ||
            size != kCompanionAuthorizationDurableRecordBytes) {
            return CompanionAuthorizationProtectedKvBackendError::failed;
        }
        last_slot = slot;
        if (reenter_on_write && !reentry_fired && media != nullptr) {
            reentry_fired = true;
            reentry_result = media->read_slot(
                CompanionAuthorizationProtectedSlot::a).error;
        }
        if (write_error !=
            CompanionAuthorizationProtectedKvBackendError::none) {
            if (apply_write_before_error) {
                seed(slot, data, size);
            }
            return write_error;
        }
        pending = true;
        pending_slot = slot;
        std::copy_n(data, size, pending_bytes.begin());
        return CompanionAuthorizationProtectedKvBackendError::none;
    }

    CompanionAuthorizationProtectedKvBackendError commit(
        const char* partition_label,
        const char* namespace_name) override {
        ++commit_calls;
        binding_exact = binding_exact && valid_binding(partition_label,
                                                        namespace_name);
        if (!binding_exact) {
            return CompanionAuthorizationProtectedKvBackendError::failed;
        }
        if (reenter_on_commit && !reentry_fired && media != nullptr) {
            reentry_fired = true;
            reentry_result = media->read_slot(
                CompanionAuthorizationProtectedSlot::a).error;
        }
        if (commit_error !=
            CompanionAuthorizationProtectedKvBackendError::none) {
            if (apply_commit_before_error) {
                apply_pending();
            }
            return commit_error;
        }
        apply_pending();
        return CompanionAuthorizationProtectedKvBackendError::none;
    }

    void seed(int slot,
              const std::uint8_t* data,
              std::size_t size) {
        durable[slot].fill(0);
        const auto copy_size = std::min(size, durable[slot].size());
        std::copy_n(data, copy_size, durable[slot].begin());
        sizes[slot] = size;
        present[slot] = true;
    }

    void seed(int slot,
              const std::array<std::uint8_t,
                               kCompanionAuthorizationDurableRecordBytes>& data,
              std::size_t size =
                  kCompanionAuthorizationDurableRecordBytes) {
        seed(slot, data.data(), size);
    }

    void remove(int slot) {
        durable[slot].fill(0);
        sizes[slot] = 0;
        present[slot] = false;
    }

    static bool valid_binding(const char* partition_label,
                              const char* namespace_name) {
        return partition_label != nullptr && namespace_name != nullptr &&
               std::strcmp(partition_label,
                           kCompanionAuthorizationProtectedPartitionLabel) == 0 &&
               std::strcmp(namespace_name,
                           kCompanionAuthorizationProtectedNamespace) == 0;
    }

    static int slot_for_key(const char* key) {
        if (key == nullptr) {
            return -1;
        }
        if (std::strcmp(key,
                       kCompanionAuthorizationProtectedSlotAKey) == 0) {
            return 0;
        }
        if (std::strcmp(key,
                       kCompanionAuthorizationProtectedSlotBKey) == 0) {
            return 1;
        }
        return -1;
    }

    void apply_pending() {
        if (pending) {
            seed(pending_slot, pending_bytes);
            pending = false;
            pending_slot = -1;
        }
    }

    std::array<std::array<std::uint8_t, 64>, 2> durable{};
    std::array<bool, 2> present{};
    std::array<std::size_t, 2> sizes{};
    std::array<std::uint8_t,
               kCompanionAuthorizationDurableRecordBytes> pending_bytes{};
    bool pending{false};
    int pending_slot{-1};
    int last_slot{-1};
    CompanionAuthorizationProtectedKvBackendError read_error{
        CompanionAuthorizationProtectedKvBackendError::none};
    CompanionAuthorizationProtectedKvBackendError write_error{
        CompanionAuthorizationProtectedKvBackendError::none};
    CompanionAuthorizationProtectedKvBackendError commit_error{
        CompanionAuthorizationProtectedKvBackendError::none};
    bool apply_write_before_error{false};
    bool apply_commit_before_error{false};
    bool binding_exact{true};
    std::uint32_t read_calls{0};
    std::uint32_t write_calls{0};
    std::uint32_t commit_calls{0};
    CompanionAuthorizationProtectedKvSlotMedia* media{nullptr};
    bool reenter_on_read{false};
    bool reenter_on_write{false};
    bool reenter_on_commit{false};
    bool reentry_fired{false};
    CompanionAuthorizationProtectedStoreError reentry_result{
        CompanionAuthorizationProtectedStoreError::none};
};

class FakeFloor final : public CompanionAuthorizationRollbackFloor {
public:
    CompanionAuthorizationRollbackFloorSnapshot read_floor() override {
        ++read_calls;
        return {read_error, generation};
    }

    CompanionAuthorizationRollbackFloorSnapshot compare_and_advance(
        std::uint32_t expected_generation,
        std::uint32_t new_generation) override {
        ++advance_calls;
        if (advance_error != CompanionAuthorizationProtectedStoreError::none) {
            return {advance_error, generation};
        }
        if (generation != expected_generation ||
            new_generation != expected_generation + 1) {
            return {CompanionAuthorizationProtectedStoreError::conflict,
                    generation};
        }
        generation = new_generation;
        return {CompanionAuthorizationProtectedStoreError::none, generation};
    }

    std::uint32_t generation{0};
    CompanionAuthorizationProtectedStoreError read_error{
        CompanionAuthorizationProtectedStoreError::none};
    CompanionAuthorizationProtectedStoreError advance_error{
        CompanionAuthorizationProtectedStoreError::none};
    std::uint32_t read_calls{0};
    std::uint32_t advance_calls{0};
};

CompanionAuthorizationRecord owner_record(std::uint32_t generation,
                                           std::uint64_t low) {
    return {0, CompanionAuthorizationRecordState::owned, 0, generation,
            {low, low + 1}};
}

std::array<std::uint8_t, kCompanionAuthorizationDurableRecordBytes> encoded(
    const CompanionAuthorizationRecord& record) {
    std::array<std::uint8_t,
               kCompanionAuthorizationDurableRecordBytes> bytes{};
    EXPECT(encode_companion_authorization_durable_record(record, bytes) ==
           CompanionAuthorizationDurableCodecError::none);
    return bytes;
}

void test_fixed_binding_names_are_exact_bounded_and_distinct() {
    EXPECT(std::strcmp(kCompanionAuthorizationProtectedPartitionLabel,
                       "ot_auth") == 0);
    EXPECT(std::strcmp(kCompanionAuthorizationProtectedNamespace,
                       "ot_owner") == 0);
    EXPECT(std::strcmp(kCompanionAuthorizationProtectedSlotAKey,
                       "oap_slot_a") == 0);
    EXPECT(std::strcmp(kCompanionAuthorizationProtectedSlotBKey,
                       "oap_slot_b") == 0);
    EXPECT(std::strcmp(kCompanionAuthorizationProtectedSlotAKey,
                       kCompanionAuthorizationProtectedSlotBKey) != 0);
    EXPECT(std::strlen(kCompanionAuthorizationProtectedPartitionLabel) <= 15);
    EXPECT(std::strlen(kCompanionAuthorizationProtectedNamespace) <= 15);
}

void test_invalid_slot_fails_without_backend_io() {
    FakeKvBackend backend{};
    CompanionAuthorizationProtectedKvSlotMedia media{backend};
    const auto invalid =
        static_cast<CompanionAuthorizationProtectedSlot>(0xffU);
    EXPECT(media.read_slot(invalid).error ==
           CompanionAuthorizationProtectedStoreError::failed);
    EXPECT(media.write_slot(invalid, encoded(owner_record(1, 10))) ==
           CompanionAuthorizationProtectedStoreError::failed);
    EXPECT(backend.read_calls == 0);
    EXPECT(backend.write_calls == 0);
    EXPECT(backend.commit_calls == 0);
}

void test_missing_and_exact_reads_use_only_the_selected_slot() {
    FakeKvBackend backend{};
    const auto record = encoded(owner_record(1, 10));
    backend.seed(1, record);
    CompanionAuthorizationProtectedKvSlotMedia media{backend};
    const auto missing = media.read_slot(
        CompanionAuthorizationProtectedSlot::a);
    EXPECT(missing.error == CompanionAuthorizationProtectedStoreError::none);
    EXPECT(!missing.present);
    const auto present = media.read_slot(
        CompanionAuthorizationProtectedSlot::b);
    EXPECT(present.error == CompanionAuthorizationProtectedStoreError::none);
    EXPECT(present.present);
    EXPECT(present.record == record);
    EXPECT(backend.last_slot == 1);
    EXPECT(backend.binding_exact);
}

void test_inexact_reads_and_backend_errors_never_publish_bytes() {
    const auto record = encoded(owner_record(1, 10));
    for (const std::size_t size : {std::size_t{31}, std::size_t{33}}) {
        FakeKvBackend backend{};
        backend.seed(0, record, size);
        CompanionAuthorizationProtectedKvSlotMedia media{backend};
        const auto result = media.read_slot(
            CompanionAuthorizationProtectedSlot::a);
        EXPECT(result.error ==
               CompanionAuthorizationProtectedStoreError::uncertain);
        EXPECT(!result.present);
        EXPECT(result.record == decltype(result.record){});
    }

    for (const auto backend_error : {
             CompanionAuthorizationProtectedKvBackendError::not_ready,
             CompanionAuthorizationProtectedKvBackendError::failed,
             CompanionAuthorizationProtectedKvBackendError::uncertain}) {
        FakeKvBackend backend{};
        backend.read_error = backend_error;
        CompanionAuthorizationProtectedKvSlotMedia media{backend};
        const auto result = media.read_slot(
            CompanionAuthorizationProtectedSlot::a);
        const auto expected =
            backend_error ==
                    CompanionAuthorizationProtectedKvBackendError::not_ready
                ? CompanionAuthorizationProtectedStoreError::not_ready
                : backend_error ==
                          CompanionAuthorizationProtectedKvBackendError::failed
                      ? CompanionAuthorizationProtectedStoreError::failed
                      : CompanionAuthorizationProtectedStoreError::uncertain;
        EXPECT(result.error == expected);
        EXPECT(!result.present);
    }
}

void test_write_uses_exact_slot_and_requires_commit() {
    FakeKvBackend backend{};
    CompanionAuthorizationProtectedKvSlotMedia media{backend};
    const auto record = encoded(owner_record(1, 10));
    EXPECT(media.write_slot(CompanionAuthorizationProtectedSlot::b, record) ==
           CompanionAuthorizationProtectedStoreError::none);
    EXPECT(backend.write_calls == 1);
    EXPECT(backend.commit_calls == 1);
    EXPECT(backend.last_slot == 1);
    EXPECT(backend.present[1]);
    EXPECT(std::equal(record.begin(), record.end(),
                      backend.durable[1].begin()));
    EXPECT(backend.binding_exact);
}

void test_write_failures_do_not_commit_and_uncertainty_is_preserved() {
    const auto record = encoded(owner_record(1, 10));
    for (const auto backend_error : {
             CompanionAuthorizationProtectedKvBackendError::not_ready,
             CompanionAuthorizationProtectedKvBackendError::failed,
             CompanionAuthorizationProtectedKvBackendError::uncertain}) {
        FakeKvBackend backend{};
        backend.write_error = backend_error;
        backend.apply_write_before_error =
            backend_error ==
            CompanionAuthorizationProtectedKvBackendError::uncertain;
        CompanionAuthorizationProtectedKvSlotMedia media{backend};
        const auto result = media.write_slot(
            CompanionAuthorizationProtectedSlot::a, record);
        const auto expected =
            backend_error ==
                    CompanionAuthorizationProtectedKvBackendError::not_ready
                ? CompanionAuthorizationProtectedStoreError::not_ready
                : backend_error ==
                          CompanionAuthorizationProtectedKvBackendError::failed
                      ? CompanionAuthorizationProtectedStoreError::failed
                      : CompanionAuthorizationProtectedStoreError::uncertain;
        EXPECT(result == expected);
        EXPECT(backend.commit_calls == 0);
    }
}

void test_every_post_write_commit_error_is_uncertain() {
    const auto record = encoded(owner_record(1, 10));
    for (const auto backend_error : {
             CompanionAuthorizationProtectedKvBackendError::not_ready,
             CompanionAuthorizationProtectedKvBackendError::failed,
             CompanionAuthorizationProtectedKvBackendError::uncertain}) {
        for (const bool apply_first : {false, true}) {
            FakeKvBackend backend{};
            backend.commit_error = backend_error;
            backend.apply_commit_before_error = apply_first;
            CompanionAuthorizationProtectedKvSlotMedia media{backend};
            EXPECT(media.write_slot(
                       CompanionAuthorizationProtectedSlot::a, record) ==
                   CompanionAuthorizationProtectedStoreError::uncertain);
            EXPECT(backend.commit_calls == 1);
            EXPECT(backend.present[0] == apply_first);
        }
    }
}

void test_callback_reentry_is_contained_as_uncertain() {
    const auto record = encoded(owner_record(1, 10));
    for (const int phase : {0, 1, 2}) {
        FakeKvBackend backend{};
        CompanionAuthorizationProtectedKvSlotMedia media{backend};
        backend.media = &media;
        backend.reenter_on_read = phase == 0;
        backend.reenter_on_write = phase == 1;
        backend.reenter_on_commit = phase == 2;
        CompanionAuthorizationProtectedStoreError result{};
        if (phase == 0) {
            result = media.read_slot(
                CompanionAuthorizationProtectedSlot::a).error;
        } else {
            result = media.write_slot(
                CompanionAuthorizationProtectedSlot::a, record);
        }
        EXPECT(result == CompanionAuthorizationProtectedStoreError::uncertain);
        EXPECT(backend.reentry_result ==
               CompanionAuthorizationProtectedStoreError::uncertain);
        EXPECT(backend.reentry_fired);
    }
}

void test_real_store_rotates_and_restores_through_kv_media() {
    FakeKvBackend backend{};
    FakeFloor floor{};
    CompanionAuthorizationProtectedKvSlotMedia media{backend};
    TwoSlotCompanionAuthorizationProtectedStore store{media, floor};
    const auto first = encoded(owner_record(1, 10));
    const auto second = encoded(owner_record(2, 20));
    EXPECT(store.compare_commit_and_verify(0, 1, first).error ==
           CompanionAuthorizationProtectedStoreError::none);
    EXPECT(backend.present[0]);
    EXPECT(store.compare_commit_and_verify(1, 2, second).error ==
           CompanionAuthorizationProtectedStoreError::none);
    EXPECT(backend.present[1]);
    EXPECT(floor.generation == 2);

    CompanionAuthorizationProtectedKvSlotMedia reboot_media{backend};
    TwoSlotCompanionAuthorizationProtectedStore reboot_store{reboot_media,
                                                              floor};
    const auto restored = reboot_store.load_verified();
    EXPECT(restored.error == CompanionAuthorizationProtectedStoreError::none);
    EXPECT(restored.record_present);
    EXPECT(restored.trusted_generation == 2);
    EXPECT(restored.record == second);
}

void test_rollback_prepared_ahead_and_commit_ambiguity_stay_closed() {
    const auto first = encoded(owner_record(1, 10));
    const auto second = encoded(owner_record(2, 20));

    FakeKvBackend rolled_back_backend{};
    FakeFloor rolled_back_floor{};
    CompanionAuthorizationProtectedKvSlotMedia rolled_back_media{
        rolled_back_backend};
    TwoSlotCompanionAuthorizationProtectedStore rolled_back_store{
        rolled_back_media, rolled_back_floor};
    EXPECT(rolled_back_store.compare_commit_and_verify(0, 1, first).error ==
           CompanionAuthorizationProtectedStoreError::none);
    EXPECT(rolled_back_store.compare_commit_and_verify(1, 2, second).error ==
           CompanionAuthorizationProtectedStoreError::none);
    rolled_back_backend.remove(1);
    EXPECT(rolled_back_store.load_verified().error ==
           CompanionAuthorizationProtectedStoreError::uncertain);

    FakeKvBackend prepared_backend{};
    FakeFloor prepared_floor{};
    CompanionAuthorizationProtectedKvSlotMedia prepared_media{prepared_backend};
    TwoSlotCompanionAuthorizationProtectedStore prepared_store{prepared_media,
                                                                prepared_floor};
    EXPECT(prepared_store.compare_commit_and_verify(0, 1, first).error ==
           CompanionAuthorizationProtectedStoreError::none);
    EXPECT(prepared_media.write_slot(
               CompanionAuthorizationProtectedSlot::b, second) ==
           CompanionAuthorizationProtectedStoreError::none);
    EXPECT(prepared_store.load_verified().error ==
           CompanionAuthorizationProtectedStoreError::uncertain);

    FakeKvBackend ambiguous_backend{};
    ambiguous_backend.commit_error =
        CompanionAuthorizationProtectedKvBackendError::uncertain;
    ambiguous_backend.apply_commit_before_error = true;
    FakeFloor ambiguous_floor{};
    CompanionAuthorizationProtectedKvSlotMedia ambiguous_media{
        ambiguous_backend};
    TwoSlotCompanionAuthorizationProtectedStore ambiguous_store{
        ambiguous_media, ambiguous_floor};
    EXPECT(ambiguous_store.compare_commit_and_verify(0, 1, first).error ==
           CompanionAuthorizationProtectedStoreError::uncertain);
    EXPECT(ambiguous_floor.generation == 0);
    EXPECT(ambiguous_backend.present[0]);
    EXPECT(ambiguous_store.load_verified().error ==
           CompanionAuthorizationProtectedStoreError::uncertain);
}

}  // namespace

int main() {
    test_fixed_binding_names_are_exact_bounded_and_distinct();
    test_invalid_slot_fails_without_backend_io();
    test_missing_and_exact_reads_use_only_the_selected_slot();
    test_inexact_reads_and_backend_errors_never_publish_bytes();
    test_write_uses_exact_slot_and_requires_commit();
    test_write_failures_do_not_commit_and_uncertainty_is_preserved();
    test_every_post_write_commit_error_is_uncertain();
    test_callback_reentry_is_contained_as_uncertain();
    test_real_store_rotates_and_restores_through_kv_media();
    test_rollback_prepared_ahead_and_commit_ambiguity_stay_closed();

    if (failures != 0) {
        std::cerr << failures
                  << " protected KV slot-media assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 protected KV slot-media scenario groups\n";
    return EXIT_SUCCESS;
}
