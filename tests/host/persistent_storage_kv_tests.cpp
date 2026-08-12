#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "opentrail/configuration_store.hpp"
#include "opentrail/persistent_storage_kv.hpp"

namespace {

using namespace opentrail::persistence;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

class FakeKvBackend final : public PersistentKvBackend {
public:
    PersistentKvBackendError read_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        std::uint8_t* output,
        std::size_t capacity,
        std::size_t& actual_size) override {
        ++read_calls;
        const int domain = domain_for(namespace_name);
        const int slot = slot_for(key);
        if (!partition_matches(partition_label) || domain < 0 || slot < 0 ||
            output == nullptr) {
            return PersistentKvBackendError::invalid_argument;
        }
        last_domain = domain;
        last_slot = slot;
        if (fail_read_domain == domain && fail_read_slot == slot) {
            return PersistentKvBackendError::io_failure;
        }
        if (!present[domain][slot]) {
            return PersistentKvBackendError::not_found;
        }
        actual_size = sizes[domain][slot];
        if (capacity < actual_size) {
            return PersistentKvBackendError::invalid_argument;
        }
        std::copy(
            durable[domain][slot].begin(),
            durable[domain][slot].begin() + actual_size,
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
        const int domain = domain_for(namespace_name);
        const int slot = slot_for(key);
        if (!partition_matches(partition_label) || domain < 0 || slot < 0 ||
            data == nullptr || size != kPersistentSlotBytes) {
            return PersistentKvBackendError::invalid_argument;
        }
        last_domain = domain;
        last_slot = slot;
        if (fail_write_domain == domain && fail_write_slot == slot) {
            return PersistentKvBackendError::io_failure;
        }
        pending = Pending::write;
        pending_domain = domain;
        pending_slot = slot;
        std::copy(data, data + size, pending_bytes.begin());
        return PersistentKvBackendError::none;
    }

    PersistentKvBackendError erase_key(
        const char* partition_label,
        const char* namespace_name,
        const char* key) override {
        ++erase_calls;
        const int domain = domain_for(namespace_name);
        const int slot = slot_for(key);
        if (!partition_matches(partition_label) || domain < 0 || slot < 0) {
            return PersistentKvBackendError::invalid_argument;
        }
        last_domain = domain;
        last_slot = slot;
        if (fail_erase_domain == domain && fail_erase_slot == slot) {
            return PersistentKvBackendError::io_failure;
        }
        if (!present[domain][slot]) {
            return PersistentKvBackendError::not_found;
        }
        pending = Pending::erase;
        pending_domain = domain;
        pending_slot = slot;
        return PersistentKvBackendError::none;
    }

    PersistentKvBackendError commit(
        const char* partition_label,
        const char* namespace_name) override {
        ++commit_calls;
        const int domain = domain_for(namespace_name);
        if (!partition_matches(partition_label) || domain < 0 ||
            (pending != Pending::none && domain != pending_domain)) {
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

    void seed(
        int domain,
        int slot,
        const std::array<std::uint8_t, kPersistentSlotBytes>& bytes,
        std::size_t size = kPersistentSlotBytes) {
        durable[domain][slot] = bytes;
        sizes[domain][slot] = size;
        present[domain][slot] = true;
    }

    bool partition_matches(const char* partition_label) {
        const bool exact = partition_label != nullptr &&
                           std::strcmp(
                               partition_label,
                               kPersistentKvPartitionLabel) == 0;
        binding_was_exact = binding_was_exact && exact;
        return exact;
    }

    int domain_for(const char* namespace_name) {
        if (namespace_name == nullptr) {
            return -1;
        }
        const char* names[] = {
            kPersistentKvConfigurationNamespace,
            kPersistentKvSecretNamespace,
            kPersistentKvProtocolNamespace,
            kPersistentKvCounterNamespace};
        for (int index = 0; index < 4; ++index) {
            if (std::strcmp(namespace_name, names[index]) == 0) {
                return index;
            }
        }
        return -1;
    }

    int slot_for(const char* key) {
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
            durable[pending_domain][pending_slot] = pending_bytes;
            sizes[pending_domain][pending_slot] = kPersistentSlotBytes;
            present[pending_domain][pending_slot] = true;
        } else if (pending == Pending::erase) {
            durable[pending_domain][pending_slot].fill(0);
            sizes[pending_domain][pending_slot] = 0;
            present[pending_domain][pending_slot] = false;
        }
        pending = Pending::none;
    }

    std::array<
        std::array<std::array<std::uint8_t, kPersistentSlotBytes>, 2>,
        4> durable{};
    std::array<std::array<bool, 2>, 4> present{};
    std::array<std::array<std::size_t, 2>, 4> sizes{};
    std::array<std::uint8_t, kPersistentSlotBytes> pending_bytes{};
    Pending pending{Pending::none};
    int pending_domain{-1};
    int pending_slot{-1};
    int last_domain{-1};
    int last_slot{-1};
    int fail_read_domain{-1};
    int fail_read_slot{-1};
    int fail_write_domain{-1};
    int fail_write_slot{-1};
    int fail_erase_domain{-1};
    int fail_erase_slot{-1};
    std::uint32_t fail_commit_call{0};
    bool apply_then_fail{false};
    bool binding_was_exact{true};
    std::uint32_t read_calls{0};
    std::uint32_t write_calls{0};
    std::uint32_t erase_calls{0};
    std::uint32_t commit_calls{0};
};

std::array<std::uint8_t, kPersistentSlotBytes> sample_bytes(
    std::uint8_t first = 0x42U) {
    std::array<std::uint8_t, kPersistentSlotBytes> bytes{};
    bytes.fill(0xFFU);
    bytes[0] = first;
    bytes[1] = 0xA5U;
    return bytes;
}

RuntimeConfiguration configuration(std::uint8_t brightness = 60) {
    return {
        OperatingRole::client_repeater,
        true,
        true,
        brightness,
        60,
        300};
}

void test_fixed_domain_names_are_bounded_and_distinct() {
    EXPECT(std::strcmp(kPersistentKvPartitionLabel, "ot_state") == 0);
    EXPECT(std::strcmp(kPersistentKvConfigurationNamespace, "ot_config") == 0);
    EXPECT(std::strcmp(kPersistentKvSecretNamespace, "ot_secret") == 0);
    EXPECT(std::strcmp(kPersistentKvProtocolNamespace, "ot_proto") == 0);
    EXPECT(std::strcmp(kPersistentKvCounterNamespace, "ot_counter") == 0);
    EXPECT(std::strcmp(kPersistentKvSlotAKey, "slot_a") == 0);
    EXPECT(std::strcmp(kPersistentKvSlotBKey, "slot_b") == 0);
    EXPECT(std::strcmp(
               kPersistentKvConfigurationNamespace,
               kPersistentKvSecretNamespace) != 0);
    EXPECT(std::strlen(kPersistentKvCounterNamespace) <= 15);
}

void test_public_arguments_fail_before_backend_io() {
    FakeKvBackend backend{};
    PersistentStorageKv storage{backend};
    std::array<std::uint8_t, kPersistentSlotBytes> bytes{};
    const auto invalid = static_cast<StorageDomain>(99);
    EXPECT(storage.read_slot(
               invalid, 0, {bytes.data(), bytes.size()}).error ==
           StorageError::invalid_argument);
    EXPECT(storage.read_slot(
               StorageDomain::configuration,
               2,
               {bytes.data(), bytes.size()}).error ==
           StorageError::invalid_argument);
    EXPECT(storage.read_slot(
               StorageDomain::configuration,
               0,
               {nullptr, bytes.size()}).error ==
           StorageError::invalid_argument);
    EXPECT(storage.erase_slot(invalid, 0) == StorageError::invalid_argument);
    EXPECT(storage.write_slot(
               StorageDomain::configuration, 0, 0, {bytes.data(), 0}) ==
           StorageError::invalid_argument);
    EXPECT(storage.write_slot(
               StorageDomain::configuration, 0, 63, {bytes.data(), 2}) ==
           StorageError::invalid_argument);
    EXPECT(storage.sync_slot(StorageDomain::configuration, 2) ==
           StorageError::invalid_argument);
    EXPECT(backend.read_calls == 0 && backend.write_calls == 0 &&
           backend.erase_calls == 0 && backend.commit_calls == 0);
}

void test_missing_and_present_reads_are_exact() {
    FakeKvBackend backend{};
    PersistentStorageKv storage{backend};
    std::array<std::uint8_t, kPersistentSlotBytes> output{};
    const auto blank = storage.read_slot(
        StorageDomain::configuration,
        0,
        {output.data(), output.size()});
    EXPECT(blank.read() && blank.bytes_read == output.size());
    EXPECT(std::all_of(output.begin(), output.end(), [](std::uint8_t byte) {
        return byte == 0xFFU;
    }));

    const auto seeded = sample_bytes();
    backend.seed(0, 1, seeded);
    const auto read = storage.read_slot(
        StorageDomain::configuration,
        1,
        {output.data(), output.size()});
    EXPECT(read.read() && read.bytes_read == output.size());
    EXPECT(output == seeded);
    EXPECT(backend.last_domain == 0 && backend.last_slot == 1);
    EXPECT(backend.binding_was_exact);
}

void test_read_rejects_wrong_size_and_io_failure() {
    FakeKvBackend backend{};
    backend.seed(0, 0, sample_bytes(), 63);
    PersistentStorageKv storage{backend};
    std::array<std::uint8_t, kPersistentSlotBytes> output{};
    EXPECT(storage.read_slot(
               StorageDomain::configuration,
               0,
               {output.data(), output.size()}).error ==
           StorageError::io_failure);
    backend.fail_read_domain = 0;
    backend.fail_read_slot = 1;
    EXPECT(storage.read_slot(
               StorageDomain::configuration,
               1,
               {output.data(), output.size()}).error ==
           StorageError::io_failure);
}

void test_erase_partial_write_and_sync_preserve_flash_order() {
    FakeKvBackend backend{};
    backend.seed(0, 0, sample_bytes());
    PersistentStorageKv storage{backend};
    EXPECT(storage.erase_slot(StorageDomain::configuration, 0) ==
           StorageError::none);
    EXPECT(!backend.present[0][0]);
    EXPECT(backend.commit_calls == 1);
    const std::array<std::uint8_t, 2> body{0xA0U, 0x05U};
    EXPECT(storage.write_slot(
               StorageDomain::configuration,
               0,
               0,
               {body.data(), body.size()}) == StorageError::none);
    EXPECT(backend.write_calls == 0);

    std::array<std::uint8_t, kPersistentSlotBytes> output{};
    EXPECT(storage.read_slot(
               StorageDomain::configuration,
               0,
               {output.data(), output.size()}).read());
    EXPECT(output[0] == 0xFFU);
    EXPECT(storage.sync_slot(StorageDomain::configuration, 0) ==
           StorageError::none);
    EXPECT(backend.durable[0][0][0] == 0xA0U);
    EXPECT(backend.durable[0][0][1] == 0x05U);
    EXPECT(backend.durable[0][0][2] == 0xFFU);

    const std::array<std::uint8_t, 1> marker{0x80U};
    EXPECT(storage.write_slot(
               StorageDomain::configuration,
               0,
               2,
               {marker.data(), marker.size()}) == StorageError::none);
    EXPECT(storage.sync_slot(StorageDomain::configuration, 0) ==
           StorageError::none);
    EXPECT(backend.durable[0][0][2] == 0x80U);
}

void test_missing_erase_and_clean_sync_are_idempotent() {
    FakeKvBackend backend{};
    PersistentStorageKv storage{backend};
    EXPECT(storage.erase_slot(StorageDomain::protocol_state, 1) ==
           StorageError::none);
    EXPECT(backend.erase_calls == 1);
    EXPECT(backend.commit_calls == 0);
    EXPECT(storage.sync_slot(StorageDomain::protocol_state, 1) ==
           StorageError::none);
    EXPECT(backend.write_calls == 0 && backend.commit_calls == 0);
}

void test_write_requires_erase_and_cannot_set_bits() {
    FakeKvBackend backend{};
    PersistentStorageKv storage{backend};
    const std::array<std::uint8_t, 1> zero{0x00U};
    EXPECT(storage.write_slot(
               StorageDomain::configuration,
               0,
               0,
               {zero.data(), zero.size()}) ==
           StorageError::write_requires_erase);
    EXPECT(storage.erase_slot(StorageDomain::configuration, 0) ==
           StorageError::none);
    EXPECT(storage.write_slot(
               StorageDomain::configuration,
               0,
               0,
               {zero.data(), zero.size()}) == StorageError::none);
    const std::array<std::uint8_t, 1> ones{0xFFU};
    EXPECT(storage.write_slot(
               StorageDomain::configuration,
               0,
               0,
               {ones.data(), ones.size()}) ==
           StorageError::write_requires_erase);
}

void test_backend_failures_latch_until_new_erase() {
    FakeKvBackend backend{};
    backend.seed(0, 0, sample_bytes());
    backend.fail_erase_domain = 0;
    backend.fail_erase_slot = 0;
    PersistentStorageKv storage{backend};
    EXPECT(storage.erase_slot(StorageDomain::configuration, 0) ==
           StorageError::io_failure);
    const std::array<std::uint8_t, 1> byte{0x7FU};
    EXPECT(storage.write_slot(
               StorageDomain::configuration,
               0,
               0,
               {byte.data(), byte.size()}) ==
           StorageError::write_requires_erase);

    backend.fail_erase_domain = -1;
    backend.fail_erase_slot = -1;
    EXPECT(storage.erase_slot(StorageDomain::configuration, 0) ==
           StorageError::none);
    EXPECT(storage.write_slot(
               StorageDomain::configuration,
               0,
               0,
               {byte.data(), byte.size()}) == StorageError::none);
    backend.fail_write_domain = 0;
    backend.fail_write_slot = 0;
    EXPECT(storage.sync_slot(StorageDomain::configuration, 0) ==
           StorageError::io_failure);
    EXPECT(storage.sync_slot(StorageDomain::configuration, 0) ==
           StorageError::write_requires_erase);
}

void test_applied_then_failed_commit_is_readable_but_latched() {
    FakeKvBackend backend{};
    PersistentStorageKv storage{backend};
    EXPECT(storage.erase_slot(StorageDomain::configuration, 0) ==
           StorageError::none);
    const std::array<std::uint8_t, 1> byte{0x7FU};
    EXPECT(storage.write_slot(
               StorageDomain::configuration,
               0,
               0,
               {byte.data(), byte.size()}) == StorageError::none);
    backend.fail_commit_call = 1;
    backend.apply_then_fail = true;
    EXPECT(storage.sync_slot(StorageDomain::configuration, 0) ==
           StorageError::io_failure);
    std::array<std::uint8_t, kPersistentSlotBytes> output{};
    EXPECT(storage.read_slot(
               StorageDomain::configuration,
               0,
               {output.data(), output.size()}).read());
    EXPECT(output[0] == 0x7FU);
    EXPECT(storage.write_slot(
               StorageDomain::configuration,
               0,
               1,
               {byte.data(), byte.size()}) ==
           StorageError::write_requires_erase);
}

void test_domains_are_physically_separate_namespaces() {
    FakeKvBackend backend{};
    PersistentStorageKv storage{backend};
    for (std::size_t domain = 0; domain < kStorageDomainCount; ++domain) {
        const auto typed = static_cast<StorageDomain>(domain);
        EXPECT(storage.erase_slot(typed, 0) == StorageError::none);
        const std::array<std::uint8_t, 1> byte{
            static_cast<std::uint8_t>(0xF0U - domain)};
        EXPECT(storage.write_slot(
                   typed, 0, 0, {byte.data(), byte.size()}) ==
               StorageError::none);
        EXPECT(storage.sync_slot(typed, 0) == StorageError::none);
    }
    for (std::size_t domain = 0; domain < kStorageDomainCount; ++domain) {
        EXPECT(backend.present[domain][0]);
        EXPECT(backend.durable[domain][0][0] ==
               static_cast<std::uint8_t>(0xF0U - domain));
    }
}

void test_configuration_store_round_trip_and_rotation() {
    FakeKvBackend backend{};
    PersistentStorageKv storage{backend};
    ConfigurationStore store{storage};
    const auto first = store.save(configuration(60), 6000);
    EXPECT(first.accepted());
    EXPECT(first.generation == 1 && first.written_slot == 0);
    const auto second = store.save(configuration(70), 12000);
    EXPECT(second.accepted());
    EXPECT(second.generation == 2 && second.written_slot == 1);

    PersistentStorageKv restarted_storage{backend};
    ConfigurationStore restarted{restarted_storage};
    const auto loaded = restarted.load();
    EXPECT(loaded.from_persistence());
    EXPECT(loaded.generation == 2 && loaded.selected_slot == 1);
    EXPECT(loaded.configuration == configuration(70));
}

void test_configuration_final_commit_uncertainty_recovers_on_restart() {
    FakeKvBackend backend{};
    backend.fail_commit_call = 2;
    backend.apply_then_fail = true;
    PersistentStorageKv storage{backend};
    ConfigurationStore store{storage};
    const auto saved = store.save(configuration(60), 6000);
    EXPECT(saved.error == ConfigurationError::storage_failure);
    EXPECT(backend.present[0][0]);

    backend.fail_commit_call = 0;
    PersistentStorageKv restarted_storage{backend};
    ConfigurationStore restarted{restarted_storage};
    const auto loaded = restarted.load();
    EXPECT(loaded.from_persistence());
    EXPECT(loaded.error == ConfigurationError::none);
    EXPECT(loaded.generation == 1 && loaded.selected_slot == 0);
    EXPECT(loaded.configuration == configuration(60));
}

}  // namespace

int main() {
    test_fixed_domain_names_are_bounded_and_distinct();
    test_public_arguments_fail_before_backend_io();
    test_missing_and_present_reads_are_exact();
    test_read_rejects_wrong_size_and_io_failure();
    test_erase_partial_write_and_sync_preserve_flash_order();
    test_missing_erase_and_clean_sync_are_idempotent();
    test_write_requires_erase_and_cannot_set_bits();
    test_backend_failures_latch_until_new_erase();
    test_applied_then_failed_commit_is_readable_but_latched();
    test_domains_are_physically_separate_namespaces();
    test_configuration_store_round_trip_and_rotation();
    test_configuration_final_commit_uncertainty_recovers_on_restart();

    if (failures != 0) {
        std::cerr << failures
                  << " persistent key/value storage assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 12 persistent key/value storage scenario groups\n";
    return EXIT_SUCCESS;
}
