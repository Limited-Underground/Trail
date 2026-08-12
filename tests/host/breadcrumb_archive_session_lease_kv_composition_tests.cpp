#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "opentrail/breadcrumb_archive_session_lease_store.hpp"
#include "opentrail/persistent_storage_kv.hpp"

namespace {

using namespace opentrail::persistence;

constexpr std::uint64_t kInitialSession = 0x0102030405060708ULL;
constexpr std::uint32_t kLeaseSize = 4;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

class ArchiveKvBackend final : public PersistentKvBackend {
public:
    PersistentKvBackendError read_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        std::uint8_t* output,
        std::size_t capacity,
        std::size_t& actual_size) override {
        ++read_calls;
        const int slot = slot_for(key);
        if (!binding_matches(partition_label, namespace_name) || slot < 0 ||
            output == nullptr) {
            return PersistentKvBackendError::invalid_argument;
        }
        if (!present[slot]) {
            return PersistentKvBackendError::not_found;
        }
        actual_size = sizes[slot];
        if (capacity < actual_size) {
            return PersistentKvBackendError::invalid_argument;
        }
        std::copy(
            durable[slot].begin(),
            durable[slot].begin() + static_cast<std::ptrdiff_t>(actual_size),
            output);
        return PersistentKvBackendError::none;
    }

    PersistentKvBackendError write_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        const std::uint8_t* data,
        std::size_t size) override {
        ++write_calls;
        const int slot = slot_for(key);
        if (!binding_matches(partition_label, namespace_name) || slot < 0 ||
            data == nullptr || size != kPersistentSlotBytes) {
            return PersistentKvBackendError::invalid_argument;
        }
        pending = Pending::write;
        pending_slot = slot;
        std::copy(data, data + size, pending_bytes.begin());
        return PersistentKvBackendError::none;
    }

    PersistentKvBackendError erase_key(
        const char* partition_label,
        const char* namespace_name,
        const char* key) override {
        ++erase_calls;
        const int slot = slot_for(key);
        if (!binding_matches(partition_label, namespace_name) || slot < 0) {
            return PersistentKvBackendError::invalid_argument;
        }
        if (!present[slot]) {
            return PersistentKvBackendError::not_found;
        }
        pending = Pending::erase;
        pending_slot = slot;
        return PersistentKvBackendError::none;
    }

    PersistentKvBackendError commit(
        const char* partition_label,
        const char* namespace_name) override {
        ++commit_calls;
        if (!binding_matches(partition_label, namespace_name)) {
            return PersistentKvBackendError::invalid_argument;
        }
        if (fail_commit_call != 0 && commit_calls == fail_commit_call) {
            if (apply_then_fail) {
                apply_pending();
            } else {
                pending = Pending::none;
            }
            return PersistentKvBackendError::io_failure;
        }
        apply_pending();
        return PersistentKvBackendError::none;
    }

    void fail_commit(std::uint32_t call, bool apply_first) {
        commit_calls = 0;
        fail_commit_call = call;
        apply_then_fail = apply_first;
    }

    void clear_failure() {
        commit_calls = 0;
        fail_commit_call = 0;
        apply_then_fail = false;
        pending = Pending::none;
    }

    void seed_wrong_size(std::size_t slot, std::size_t size) {
        durable[slot].fill(0xA5U);
        sizes[slot] = size;
        present[slot] = true;
    }

    bool binding_matches(
        const char* partition_label,
        const char* namespace_name) {
        const bool exact =
            partition_label != nullptr && namespace_name != nullptr &&
            std::strcmp(
                partition_label, kPersistentKvPartitionLabel) == 0 &&
            std::strcmp(
                namespace_name, kPersistentKvArchiveNamespace) == 0;
        exact_binding = exact_binding && exact;
        return exact;
    }

    int slot_for(const char* key) const {
        if (key == nullptr) {
            return -1;
        }
        if (std::strcmp(key, kPersistentKvSlotAKey) == 0) {
            return 0;
        }
        if (std::strcmp(key, kPersistentKvSlotBKey) == 0) {
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
            sizes[pending_slot] = kPersistentSlotBytes;
            present[pending_slot] = true;
        } else if (pending == Pending::erase) {
            durable[pending_slot].fill(0);
            sizes[pending_slot] = 0;
            present[pending_slot] = false;
        }
        pending = Pending::none;
    }

    std::array<std::array<std::uint8_t, kPersistentSlotBytes>, 2> durable{};
    std::array<std::uint8_t, kPersistentSlotBytes> pending_bytes{};
    std::array<bool, 2> present{};
    std::array<std::size_t, 2> sizes{};
    Pending pending{Pending::none};
    int pending_slot{-1};
    std::uint32_t fail_commit_call{0};
    bool apply_then_fail{false};
    bool exact_binding{true};
    std::uint32_t read_calls{0};
    std::uint32_t write_calls{0};
    std::uint32_t erase_calls{0};
    std::uint32_t commit_calls{0};
};

BreadcrumbArchiveSessionLeaseRequest request(
    std::uint64_t initial_session_id = kInitialSession,
    std::uint32_t lease_size = kLeaseSize) {
    return {initial_session_id, lease_size};
}

BreadcrumbArchiveSessionLeaseAllocation allocate(
    ArchiveKvBackend& backend,
    const BreadcrumbArchiveSessionLeaseRequest& lease_request = request()) {
    PersistentStorageKv storage{backend};
    BreadcrumbArchiveSessionLeaseStore store{storage};
    return store.allocate(lease_request);
}

void test_first_lease_uses_exact_archive_namespace() {
    ArchiveKvBackend backend{};
    const auto allocated = allocate(backend);
    EXPECT(allocated.allocated());
    EXPECT(allocated.initialized);
    EXPECT(allocated.first_session_id == kInitialSession);
    EXPECT(allocated.final_session_id == kInitialSession + 3);
    EXPECT(allocated.generation == 1 && allocated.written_slot == 0);
    EXPECT(backend.present[0] && !backend.present[1]);
    EXPECT(backend.exact_binding);
    EXPECT(backend.write_calls == 2 && backend.commit_calls == 2);
}

void test_restart_leases_do_not_overlap_and_rotate_slots() {
    ArchiveKvBackend backend{};
    const auto first = allocate(backend);
    const auto second = allocate(backend, request(kInitialSession, 2));
    const auto third = allocate(backend);
    EXPECT(first.allocated() && second.allocated() && third.allocated());
    EXPECT(second.first_session_id == first.final_session_id + 1);
    EXPECT(second.final_session_id == second.first_session_id + 1);
    EXPECT(second.generation == 2 && second.written_slot == 1);
    EXPECT(third.first_session_id == second.final_session_id + 1);
    EXPECT(third.final_session_id == third.first_session_id + 3);
    EXPECT(third.generation == 3 && third.written_slot == 0);
    EXPECT(backend.present[0] && backend.present[1]);
}

void test_unapplied_prepared_commit_retries_same_range() {
    ArchiveKvBackend backend{};
    const auto first = allocate(backend);
    EXPECT(first.allocated());
    backend.fail_commit(1, false);
    const auto uncertain = allocate(backend);
    EXPECT(uncertain.error ==
           BreadcrumbArchiveSessionLeaseError::storage_failure);
    EXPECT(!backend.present[1]);

    backend.clear_failure();
    const auto recovered = allocate(backend);
    EXPECT(recovered.allocated());
    EXPECT(recovered.first_session_id == first.final_session_id + 1);
    EXPECT(recovered.generation == 2);
}

void test_applied_marker_commit_skips_uncertain_range() {
    ArchiveKvBackend backend{};
    const auto first = allocate(backend);
    EXPECT(first.allocated());
    backend.fail_commit(2, true);
    const auto uncertain = allocate(backend);
    EXPECT(uncertain.error ==
           BreadcrumbArchiveSessionLeaseError::storage_failure);
    EXPECT(backend.present[1]);

    backend.clear_failure();
    const auto recovered = allocate(backend);
    EXPECT(recovered.allocated());
    EXPECT(recovered.first_session_id ==
           first.final_session_id + kLeaseSize + 1);
    EXPECT(recovered.generation == 3 && recovered.written_slot == 0);
}

void test_wrong_sized_value_fails_without_mutation() {
    ArchiveKvBackend backend{};
    backend.seed_wrong_size(0, kPersistentSlotBytes - 1);
    const auto allocated = allocate(backend);
    EXPECT(allocated.error ==
           BreadcrumbArchiveSessionLeaseError::storage_failure);
    EXPECT(backend.write_calls == 0 && backend.erase_calls == 0 &&
           backend.commit_calls == 0);
    EXPECT(backend.exact_binding);
}

}  // namespace

int main() {
    test_first_lease_uses_exact_archive_namespace();
    test_restart_leases_do_not_overlap_and_rotate_slots();
    test_unapplied_prepared_commit_retries_same_range();
    test_applied_marker_commit_skips_uncertain_range();
    test_wrong_sized_value_fails_without_mutation();

    if (failures != 0) {
        std::cerr << failures
                  << " archive lease key/value assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 5 archive lease key/value composition groups\n";
    return EXIT_SUCCESS;
}
