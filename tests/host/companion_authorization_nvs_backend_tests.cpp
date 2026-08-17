#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "companion_authorization_nvs_backend.hpp"

namespace {

using namespace opentrail::companion;
using opentrail::target::heltec_v4_bench::
    EspIdfCompanionAuthorizationNvsBackend;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

struct FakeNvs {
    std::array<std::array<std::uint8_t, 64>, 2> bytes{};
    std::array<std::size_t, 2> sizes{};
    std::array<bool, 2> present{};
    esp_err_t query_error{ESP_OK};
    esp_err_t read_error{ESP_OK};
    esp_err_t set_error{ESP_OK};
    esp_err_t commit_error{ESP_OK};
    std::uint32_t query_calls{0};
    std::uint32_t read_calls{0};
    std::uint32_t set_calls{0};
    std::uint32_t commit_calls{0};
    nvs_handle_t last_handle{0};
    int last_slot{-1};
};

FakeNvs fake{};

int slot_for_key(const char* key) {
    if (key == nullptr) {
        return -1;
    }
    if (std::strcmp(key, kCompanionAuthorizationProtectedSlotAKey) == 0) {
        return 0;
    }
    if (std::strcmp(key, kCompanionAuthorizationProtectedSlotBKey) == 0) {
        return 1;
    }
    return -1;
}

void reset_fake() { fake = {}; }

std::array<std::uint8_t, kCompanionAuthorizationDurableRecordBytes> record(
    std::uint8_t seed) {
    std::array<std::uint8_t,
               kCompanionAuthorizationDurableRecordBytes> result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = static_cast<std::uint8_t>(seed + index);
    }
    return result;
}

void seed_slot(int slot,
               const std::array<std::uint8_t,
                                kCompanionAuthorizationDurableRecordBytes>& value,
               std::size_t size =
                   kCompanionAuthorizationDurableRecordBytes) {
    fake.bytes[slot].fill(0);
    std::copy_n(value.begin(), std::min(size, value.size()),
                fake.bytes[slot].begin());
    fake.sizes[slot] = size;
    fake.present[slot] = true;
}

void test_unopened_backend_fails_closed_without_native_calls() {
    reset_fake();
    EspIdfCompanionAuthorizationNvsBackend backend{0};
    std::array<std::uint8_t,
               kCompanionAuthorizationDurableRecordBytes> output{};
    std::size_t actual_size = 99;
    EXPECT(backend.read_blob(
               kCompanionAuthorizationProtectedPartitionLabel,
               kCompanionAuthorizationProtectedNamespace,
               kCompanionAuthorizationProtectedSlotAKey,
               output.data(), output.size(), actual_size) ==
           CompanionAuthorizationProtectedKvBackendError::not_ready);
    EXPECT(actual_size == 0);
    EXPECT(backend.write_blob(
               kCompanionAuthorizationProtectedPartitionLabel,
               kCompanionAuthorizationProtectedNamespace,
               kCompanionAuthorizationProtectedSlotAKey,
               output.data(), output.size()) ==
           CompanionAuthorizationProtectedKvBackendError::not_ready);
    EXPECT(backend.commit(
               kCompanionAuthorizationProtectedPartitionLabel,
               kCompanionAuthorizationProtectedNamespace) ==
           CompanionAuthorizationProtectedKvBackendError::not_ready);
    EXPECT(fake.query_calls == 0 && fake.read_calls == 0 &&
           fake.set_calls == 0 && fake.commit_calls == 0);
}

void test_invalid_context_key_and_arguments_precede_native_calls() {
    reset_fake();
    EspIdfCompanionAuthorizationNvsBackend backend{7};
    auto bytes = record(1);
    std::size_t actual_size = 0;
    EXPECT(backend.read_blob("nvs", kCompanionAuthorizationProtectedNamespace,
                             kCompanionAuthorizationProtectedSlotAKey,
                             bytes.data(), bytes.size(), actual_size) ==
           CompanionAuthorizationProtectedKvBackendError::failed);
    EXPECT(backend.read_blob(kCompanionAuthorizationProtectedPartitionLabel,
                             "other", kCompanionAuthorizationProtectedSlotAKey,
                             bytes.data(), bytes.size(), actual_size) ==
           CompanionAuthorizationProtectedKvBackendError::failed);
    EXPECT(backend.read_blob(kCompanionAuthorizationProtectedPartitionLabel,
                             kCompanionAuthorizationProtectedNamespace,
                             "other", bytes.data(), bytes.size(), actual_size) ==
           CompanionAuthorizationProtectedKvBackendError::failed);
    EXPECT(backend.read_blob(kCompanionAuthorizationProtectedPartitionLabel,
                             kCompanionAuthorizationProtectedNamespace,
                             kCompanionAuthorizationProtectedSlotAKey,
                             nullptr, bytes.size(), actual_size) ==
           CompanionAuthorizationProtectedKvBackendError::failed);
    EXPECT(backend.read_blob(kCompanionAuthorizationProtectedPartitionLabel,
                             kCompanionAuthorizationProtectedNamespace,
                             kCompanionAuthorizationProtectedSlotAKey,
                             bytes.data(), bytes.size() - 1, actual_size) ==
           CompanionAuthorizationProtectedKvBackendError::failed);
    EXPECT(backend.write_blob(kCompanionAuthorizationProtectedPartitionLabel,
                              kCompanionAuthorizationProtectedNamespace,
                              "other", bytes.data(), bytes.size()) ==
           CompanionAuthorizationProtectedKvBackendError::failed);
    EXPECT(backend.write_blob(kCompanionAuthorizationProtectedPartitionLabel,
                              kCompanionAuthorizationProtectedNamespace,
                              kCompanionAuthorizationProtectedSlotAKey,
                              nullptr, bytes.size()) ==
           CompanionAuthorizationProtectedKvBackendError::failed);
    EXPECT(backend.commit("nvs",
                          kCompanionAuthorizationProtectedNamespace) ==
           CompanionAuthorizationProtectedKvBackendError::failed);
    EXPECT(fake.query_calls == 0 && fake.read_calls == 0 &&
           fake.set_calls == 0 && fake.commit_calls == 0);
}

void test_missing_read_maps_not_found_without_second_read() {
    reset_fake();
    EspIdfCompanionAuthorizationNvsBackend backend{7};
    std::array<std::uint8_t,
               kCompanionAuthorizationDurableRecordBytes> output{};
    std::size_t actual_size = 99;
    EXPECT(backend.read_blob(kCompanionAuthorizationProtectedPartitionLabel,
                             kCompanionAuthorizationProtectedNamespace,
                             kCompanionAuthorizationProtectedSlotBKey,
                             output.data(), output.size(), actual_size) ==
           CompanionAuthorizationProtectedKvBackendError::not_found);
    EXPECT(actual_size == 0);
    EXPECT(fake.query_calls == 1 && fake.read_calls == 0);
    EXPECT(fake.last_slot == 1 && fake.last_handle == 7);
}

void test_exact_read_queries_then_reads_selected_slot() {
    reset_fake();
    const auto expected = record(20);
    seed_slot(0, expected);
    EspIdfCompanionAuthorizationNvsBackend backend{9};
    std::array<std::uint8_t,
               kCompanionAuthorizationDurableRecordBytes> output{};
    std::size_t actual_size = 0;
    EXPECT(backend.read_blob(kCompanionAuthorizationProtectedPartitionLabel,
                             kCompanionAuthorizationProtectedNamespace,
                             kCompanionAuthorizationProtectedSlotAKey,
                             output.data(), output.size(), actual_size) ==
           CompanionAuthorizationProtectedKvBackendError::none);
    EXPECT(actual_size == expected.size());
    EXPECT(output == expected);
    EXPECT(fake.query_calls == 1 && fake.read_calls == 1);
    EXPECT(fake.last_slot == 0 && fake.last_handle == 9);
}

void test_inexact_size_is_reported_without_copy_or_second_read() {
    const auto value = record(30);
    for (const std::size_t size : {std::size_t{31}, std::size_t{33}}) {
        reset_fake();
        seed_slot(0, value, size);
        EspIdfCompanionAuthorizationNvsBackend backend{4};
        std::array<std::uint8_t,
                   kCompanionAuthorizationDurableRecordBytes> output{};
        output.fill(0xA5);
        const auto original = output;
        std::size_t actual_size = 0;
        EXPECT(backend.read_blob(
                   kCompanionAuthorizationProtectedPartitionLabel,
                   kCompanionAuthorizationProtectedNamespace,
                   kCompanionAuthorizationProtectedSlotAKey,
                   output.data(), output.size(), actual_size) ==
               CompanionAuthorizationProtectedKvBackendError::none);
        EXPECT(actual_size == size);
        EXPECT(output == original);
        EXPECT(fake.query_calls == 1 && fake.read_calls == 0);
    }
}

void test_query_and_read_errors_map_closed() {
    for (const auto pair : {
             std::pair{ESP_ERR_NVS_INVALID_HANDLE,
                       CompanionAuthorizationProtectedKvBackendError::not_ready},
             std::pair{ESP_ERR_NVS_NOT_INITIALIZED,
                       CompanionAuthorizationProtectedKvBackendError::not_ready},
             std::pair{ESP_FAIL,
                       CompanionAuthorizationProtectedKvBackendError::failed}}) {
        reset_fake();
        fake.query_error = pair.first;
        EspIdfCompanionAuthorizationNvsBackend backend{3};
        auto output = record(1);
        std::size_t actual_size = 0;
        EXPECT(backend.read_blob(
                   kCompanionAuthorizationProtectedPartitionLabel,
                   kCompanionAuthorizationProtectedNamespace,
                   kCompanionAuthorizationProtectedSlotAKey,
                   output.data(), output.size(), actual_size) == pair.second);
        EXPECT(fake.query_calls == 1 && fake.read_calls == 0);
    }

    for (const auto read_error : {ESP_ERR_NVS_INVALID_LENGTH,
                                  ESP_ERR_NVS_NOT_FOUND,
                                  ESP_ERR_NVS_INVALID_HANDLE, ESP_FAIL}) {
        reset_fake();
        seed_slot(0, record(1));
        fake.read_error = read_error;
        EspIdfCompanionAuthorizationNvsBackend backend{3};
        auto output = record(2);
        std::size_t actual_size = 0;
        EXPECT(backend.read_blob(
                   kCompanionAuthorizationProtectedPartitionLabel,
                   kCompanionAuthorizationProtectedNamespace,
                   kCompanionAuthorizationProtectedSlotAKey,
                   output.data(), output.size(), actual_size) ==
               CompanionAuthorizationProtectedKvBackendError::uncertain);
        EXPECT(fake.query_calls == 1 && fake.read_calls == 1);
    }
}

void test_set_uses_exact_handle_key_and_maps_any_native_error_uncertain() {
    const auto value = record(40);
    reset_fake();
    EspIdfCompanionAuthorizationNvsBackend backend{11};
    EXPECT(backend.write_blob(kCompanionAuthorizationProtectedPartitionLabel,
                              kCompanionAuthorizationProtectedNamespace,
                              kCompanionAuthorizationProtectedSlotBKey,
                              value.data(), value.size()) ==
           CompanionAuthorizationProtectedKvBackendError::none);
    EXPECT(fake.set_calls == 1 && fake.last_handle == 11 && fake.last_slot == 1);
    EXPECT(std::equal(value.begin(), value.end(), fake.bytes[1].begin()));

    reset_fake();
    fake.set_error = ESP_FAIL;
    EspIdfCompanionAuthorizationNvsBackend failed{12};
    EXPECT(failed.write_blob(kCompanionAuthorizationProtectedPartitionLabel,
                             kCompanionAuthorizationProtectedNamespace,
                             kCompanionAuthorizationProtectedSlotAKey,
                             value.data(), value.size()) ==
           CompanionAuthorizationProtectedKvBackendError::uncertain);
    EXPECT(fake.set_calls == 1);
}

void test_commit_uses_exact_handle_and_maps_any_native_error_uncertain() {
    reset_fake();
    EspIdfCompanionAuthorizationNvsBackend backend{13};
    EXPECT(backend.commit(kCompanionAuthorizationProtectedPartitionLabel,
                          kCompanionAuthorizationProtectedNamespace) ==
           CompanionAuthorizationProtectedKvBackendError::none);
    EXPECT(fake.commit_calls == 1 && fake.last_handle == 13);

    reset_fake();
    fake.commit_error = ESP_ERR_NVS_NOT_INITIALIZED;
    EspIdfCompanionAuthorizationNvsBackend failed{14};
    EXPECT(failed.commit(kCompanionAuthorizationProtectedPartitionLabel,
                         kCompanionAuthorizationProtectedNamespace) ==
           CompanionAuthorizationProtectedKvBackendError::uncertain);
    EXPECT(fake.commit_calls == 1 && fake.last_handle == 14);
}

}  // namespace

esp_err_t nvs_get_blob(nvs_handle_t handle,
                       const char* key,
                       void* output,
                       std::size_t* length) {
    fake.last_handle = handle;
    fake.last_slot = slot_for_key(key);
    if (output == nullptr) {
        ++fake.query_calls;
        if (fake.query_error != ESP_OK) {
            return fake.query_error;
        }
        if (fake.last_slot < 0 || !fake.present[fake.last_slot]) {
            return ESP_ERR_NVS_NOT_FOUND;
        }
        *length = fake.sizes[fake.last_slot];
        return ESP_OK;
    }
    ++fake.read_calls;
    if (fake.read_error != ESP_OK) {
        return fake.read_error;
    }
    if (fake.last_slot < 0 || !fake.present[fake.last_slot]) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (*length < fake.sizes[fake.last_slot]) {
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    std::copy_n(fake.bytes[fake.last_slot].begin(),
                fake.sizes[fake.last_slot],
                static_cast<std::uint8_t*>(output));
    *length = fake.sizes[fake.last_slot];
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle,
                       const char* key,
                       const void* data,
                       std::size_t length) {
    ++fake.set_calls;
    fake.last_handle = handle;
    fake.last_slot = slot_for_key(key);
    if (fake.set_error != ESP_OK) {
        return fake.set_error;
    }
    if (fake.last_slot < 0 || length > fake.bytes[fake.last_slot].size()) {
        return ESP_FAIL;
    }
    std::copy_n(static_cast<const std::uint8_t*>(data), length,
                fake.bytes[fake.last_slot].begin());
    fake.sizes[fake.last_slot] = length;
    fake.present[fake.last_slot] = true;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle) {
    ++fake.commit_calls;
    fake.last_handle = handle;
    return fake.commit_error;
}

int main() {
    test_unopened_backend_fails_closed_without_native_calls();
    test_invalid_context_key_and_arguments_precede_native_calls();
    test_missing_read_maps_not_found_without_second_read();
    test_exact_read_queries_then_reads_selected_slot();
    test_inexact_size_is_reported_without_copy_or_second_read();
    test_query_and_read_errors_map_closed();
    test_set_uses_exact_handle_key_and_maps_any_native_error_uncertain();
    test_commit_uses_exact_handle_and_maps_any_native_error_uncertain();

    if (failures != 0) {
        std::cerr << failures << " companion authorization NVS backend "
                  << "assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 companion authorization NVS backend groups\n";
    return EXIT_SUCCESS;
}
