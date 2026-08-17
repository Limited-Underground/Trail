#include "companion_authorization_nvs_backend.hpp"

#include <cstring>

#include "esp_err.h"

namespace opentrail::target::heltec_v4_bench {
namespace {

using namespace opentrail::companion;

bool exact_text(const char* actual, const char* expected) {
    return actual != nullptr && expected != nullptr &&
           std::strcmp(actual, expected) == 0;
}

bool valid_context(const char* partition_label,
                   const char* namespace_name) {
    return exact_text(partition_label,
                      kCompanionAuthorizationProtectedPartitionLabel) &&
           exact_text(namespace_name,
                      kCompanionAuthorizationProtectedNamespace);
}

bool valid_key(const char* key) {
    return exact_text(key, kCompanionAuthorizationProtectedSlotAKey) ||
           exact_text(key, kCompanionAuthorizationProtectedSlotBKey);
}

CompanionAuthorizationProtectedKvBackendError read_error(esp_err_t error) {
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return CompanionAuthorizationProtectedKvBackendError::not_found;
    }
    if (error == ESP_ERR_NVS_INVALID_HANDLE ||
        error == ESP_ERR_NVS_NOT_INITIALIZED) {
        return CompanionAuthorizationProtectedKvBackendError::not_ready;
    }
    return CompanionAuthorizationProtectedKvBackendError::failed;
}

}  // namespace

EspIdfCompanionAuthorizationNvsBackend::
    EspIdfCompanionAuthorizationNvsBackend(nvs_handle_t handle)
    : handle_(handle) {}

CompanionAuthorizationProtectedKvBackendError
EspIdfCompanionAuthorizationNvsBackend::read_blob(
    const char* partition_label,
    const char* namespace_name,
    const char* key,
    std::uint8_t* output,
    std::size_t capacity,
    std::size_t& actual_size) {
    actual_size = 0;
    if (handle_ == 0) {
        return CompanionAuthorizationProtectedKvBackendError::not_ready;
    }
    if (!valid_context(partition_label, namespace_name) || !valid_key(key) ||
        output == nullptr ||
        capacity != kCompanionAuthorizationDurableRecordBytes) {
        return CompanionAuthorizationProtectedKvBackendError::failed;
    }

    std::size_t required_size = 0;
    const auto queried = nvs_get_blob(handle_, key, nullptr, &required_size);
    if (queried != ESP_OK) {
        return read_error(queried);
    }
    actual_size = required_size;
    if (required_size != kCompanionAuthorizationDurableRecordBytes) {
        // Returning success with the observed inexact size lets OT-067 reject
        // malformed media as uncertain without publishing any record bytes.
        return CompanionAuthorizationProtectedKvBackendError::none;
    }

    std::size_t read_size = capacity;
    const auto read = nvs_get_blob(handle_, key, output, &read_size);
    if (read != ESP_OK) {
        // The size query already observed a present exact value. Any later
        // error can be a concurrent or ambiguous media change and therefore
        // cannot be reduced to absent, not-ready, or safely failed.
        return CompanionAuthorizationProtectedKvBackendError::uncertain;
    }
    actual_size = read_size;
    return read_size == kCompanionAuthorizationDurableRecordBytes
               ? CompanionAuthorizationProtectedKvBackendError::none
               : CompanionAuthorizationProtectedKvBackendError::uncertain;
}

CompanionAuthorizationProtectedKvBackendError
EspIdfCompanionAuthorizationNvsBackend::write_blob(
    const char* partition_label,
    const char* namespace_name,
    const char* key,
    const std::uint8_t* data,
    std::size_t size) {
    if (handle_ == 0) {
        return CompanionAuthorizationProtectedKvBackendError::not_ready;
    }
    if (!valid_context(partition_label, namespace_name) || !valid_key(key) ||
        data == nullptr ||
        size != kCompanionAuthorizationDurableRecordBytes) {
        return CompanionAuthorizationProtectedKvBackendError::failed;
    }

    // Once the native write has been invoked, an error cannot establish that
    // no bytes were staged or made durable. Preserve the stronger OT-067
    // uncertainty contract rather than translating native error detail.
    return nvs_set_blob(handle_, key, data, size) == ESP_OK
               ? CompanionAuthorizationProtectedKvBackendError::none
               : CompanionAuthorizationProtectedKvBackendError::uncertain;
}

CompanionAuthorizationProtectedKvBackendError
EspIdfCompanionAuthorizationNvsBackend::commit(
    const char* partition_label,
    const char* namespace_name) {
    if (handle_ == 0) {
        return CompanionAuthorizationProtectedKvBackendError::not_ready;
    }
    if (!valid_context(partition_label, namespace_name)) {
        return CompanionAuthorizationProtectedKvBackendError::failed;
    }

    return nvs_commit(handle_) == ESP_OK
               ? CompanionAuthorizationProtectedKvBackendError::none
               : CompanionAuthorizationProtectedKvBackendError::uncertain;
}

}  // namespace opentrail::target::heltec_v4_bench
