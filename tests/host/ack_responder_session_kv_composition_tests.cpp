#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "opentrail/ack_responder_session_store.hpp"
#include "opentrail/persistent_storage_kv.hpp"

namespace {

using namespace opentrail::persistence;

constexpr std::uint64_t kConsumerId = 0x0102030405060708ULL;
constexpr std::uint32_t kAuthorizationEpoch = 7;
constexpr std::uint32_t kInitialBootSession = 0x11223344U;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

class ProtocolKvBackend final : public PersistentKvBackend {
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
                partition_label,
                kPersistentKvPartitionLabel) == 0 &&
            std::strcmp(
                namespace_name,
                kPersistentKvProtocolNamespace) == 0;
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

AckResponderSessionRequest request(
    std::uint64_t consumer_id = kConsumerId,
    std::uint32_t authorization_epoch = kAuthorizationEpoch,
    std::uint32_t initial_boot_session_id = kInitialBootSession) {
    return {consumer_id, authorization_epoch, initial_boot_session_id};
}

AckResponderSessionAllocation allocate(
    ProtocolKvBackend& backend,
    const AckResponderSessionRequest& session_request = request()) {
    PersistentStorageKv storage(backend);
    AckResponderSessionStore store(storage);
    return store.allocate(session_request);
}

void test_first_session_uses_exact_protocol_namespace() {
    ProtocolKvBackend backend;

    const auto allocated = allocate(backend);

    EXPECT(allocated.allocated());
    EXPECT(allocated.initialized);
    EXPECT(allocated.generation == 1);
    EXPECT(allocated.boot_session_id == kInitialBootSession);
    EXPECT(allocated.written_slot == 0);
    EXPECT(backend.present[0]);
    EXPECT(!backend.present[1]);
    EXPECT(backend.exact_binding);
    EXPECT(backend.write_calls == 2);
    EXPECT(backend.commit_calls == 2);
}

void test_restarts_increment_sessions_and_rotate_slots() {
    ProtocolKvBackend backend;
    const auto first = allocate(backend);
    const auto second = allocate(backend);
    const auto third = allocate(backend);

    EXPECT(first.allocated());
    EXPECT(second.allocated());
    EXPECT(third.allocated());
    EXPECT(second.boot_session_id == kInitialBootSession + 1);
    EXPECT(second.generation == 2);
    EXPECT(second.written_slot == 1);
    EXPECT(third.boot_session_id == kInitialBootSession + 2);
    EXPECT(third.generation == 3);
    EXPECT(third.written_slot == 0);
    EXPECT(backend.present[0]);
    EXPECT(backend.present[1]);
    EXPECT(backend.exact_binding);
}

void test_unapplied_prepared_commit_retries_after_restart() {
    ProtocolKvBackend backend;
    EXPECT(allocate(backend).allocated());
    backend.fail_commit(1, false);

    const auto uncertain = allocate(backend);

    EXPECT(uncertain.error == AckResponderSessionError::storage_failure);
    EXPECT(!uncertain.allocated());
    EXPECT(!backend.present[1]);

    backend.clear_failure();
    const auto recovered = allocate(backend);
    EXPECT(recovered.allocated());
    EXPECT(recovered.boot_session_id == kInitialBootSession + 1);
    EXPECT(recovered.generation == 2);
}

void test_applied_marker_commit_skips_uncertain_session_after_restart() {
    ProtocolKvBackend backend;
    EXPECT(allocate(backend).allocated());
    backend.fail_commit(2, true);

    const auto uncertain = allocate(backend);

    EXPECT(uncertain.error == AckResponderSessionError::storage_failure);
    EXPECT(!uncertain.allocated());
    EXPECT(backend.present[1]);

    backend.clear_failure();
    const auto recovered = allocate(backend);
    EXPECT(recovered.allocated());
    EXPECT(recovered.boot_session_id == kInitialBootSession + 2);
    EXPECT(recovered.generation == 3);
    EXPECT(recovered.written_slot == 0);
}

void test_explicit_reset_erases_both_keys_before_reseed() {
    ProtocolKvBackend backend;
    EXPECT(allocate(backend).allocated());
    EXPECT(allocate(backend).allocated());
    EXPECT(backend.present[0] && backend.present[1]);

    PersistentStorageKv storage(backend);
    AckResponderSessionStore store(storage);
    EXPECT(store.reset() == AckResponderSessionError::none);
    EXPECT(!backend.present[0]);
    EXPECT(!backend.present[1]);

    const auto reseeded = allocate(
        backend,
        request(0x1112131415161718ULL, 9, 0x55667788U));
    EXPECT(reseeded.allocated());
    EXPECT(reseeded.initialized);
    EXPECT(reseeded.generation == 1);
    EXPECT(reseeded.boot_session_id == 0x55667788U);
    EXPECT(backend.exact_binding);
}

void test_wrong_sized_value_fails_closed_without_mutation() {
    ProtocolKvBackend backend;
    backend.seed_wrong_size(0, kPersistentSlotBytes - 1);

    const auto allocated = allocate(backend);

    EXPECT(allocated.error == AckResponderSessionError::storage_failure);
    EXPECT(!allocated.allocated());
    EXPECT(backend.write_calls == 0);
    EXPECT(backend.erase_calls == 0);
    EXPECT(backend.commit_calls == 0);
    EXPECT(backend.exact_binding);
}

}  // namespace

int main() {
    test_first_session_uses_exact_protocol_namespace();
    test_restarts_increment_sessions_and_rotate_slots();
    test_unapplied_prepared_commit_retries_after_restart();
    test_applied_marker_commit_skips_uncertain_session_after_restart();
    test_explicit_reset_erases_both_keys_before_reseed();
    test_wrong_sized_value_fails_closed_without_mutation();

    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 6 ACK session key/value composition groups\n";
    return EXIT_SUCCESS;
}
