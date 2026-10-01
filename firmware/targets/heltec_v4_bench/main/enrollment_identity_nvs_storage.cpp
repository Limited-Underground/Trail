#include "enrollment_identity_nvs_storage.hpp"
#include "heltec_v4_factory_reset_storage.hpp"
#include "nvs.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>

namespace opentrail::targets::heltec_v4_bench {
namespace {
using namespace persistence;
using namespace security_evaluation;
using Bytes = std::array<std::uint8_t, kPersistentSlotBytes>;
std::atomic<std::uint64_t> reset_generation{1};
void wipe(Bytes& bytes) { for (volatile std::uint8_t& value : bytes) value = 0; }
struct Scratch { Bytes bytes{}; ~Scratch() { wipe(bytes); } };
struct Handle { nvs_handle_t value{0}; ~Handle() { if(value) nvs_close(value); } };
bool valid_slot(StorageDomain domain, std::size_t slot) {
    return static_cast<unsigned>(domain) < kStorageDomainCount && slot < kPersistentSlotCount;
}
bool identity_slot(StorageDomain domain, std::size_t slot) {
    return domain == StorageDomain::secret_material && slot == 0;
}
// Any reset-namespace residue (including corrupt/legacy records) prevents
// normal identity access. Reset cleanup alone owns interpretation and recovery.
bool reset_absent() {
    Handle handle;
    const auto opened = nvs_open(kHeltecV4FactoryResetMarkerNamespace, NVS_READONLY, &handle.value);
    if(opened == ESP_ERR_NVS_NOT_FOUND) return true;
    if(opened != ESP_OK) return false;
    std::size_t entries = 0;
    return nvs_get_used_entry_count(handle.value, &entries) == ESP_OK && entries == 0;
}
bool inventory(nvs_handle_t handle, bool& present) {
    present = false;
    nvs_iterator_t it = nullptr;
    auto result = nvs_entry_find_in_handle(handle, NVS_TYPE_ANY, &it);
    while(result == ESP_OK) {
        nvs_entry_info_t info{};
        if(nvs_entry_info(it, &info) != ESP_OK || present ||
           std::strcmp(info.key, kEnrollmentIdentityStorageKey) != 0 || info.type != NVS_TYPE_BLOB) {
            nvs_release_iterator(it); return false;
        }
        present = true;
        result = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    return result == ESP_ERR_NVS_NOT_FOUND;
}
bool load(Bytes& output) {
    Handle handle;
    const auto opened = nvs_open(kEnrollmentIdentityStorageNamespace, NVS_READONLY, &handle.value);
    if(opened == ESP_ERR_NVS_NOT_FOUND) { output.fill(0xff); return true; }
    if(opened != ESP_OK) return false;
    bool present = false;
    if(!inventory(handle.value, present)) return false;
    if(!present) { output.fill(0xff); return true; }
    std::size_t size = 0;
    if(nvs_get_blob(handle.value, kEnrollmentIdentityStorageKey, nullptr, &size) != ESP_OK || size != output.size()) return false;
    Scratch scratch;
    if(nvs_get_blob(handle.value, kEnrollmentIdentityStorageKey, scratch.bytes.data(), &size) != ESP_OK || size != output.size()) return false;
    output = scratch.bytes; return true;
}
}
EnrollmentIdentityNvsStorage::EnrollmentIdentityNvsStorage()
    : generation_(reset_generation.load(std::memory_order_acquire)) {}
EnrollmentIdentityNvsStorage::~EnrollmentIdentityNvsStorage() { wipe(before_); wipe(staged_); }
StorageError EnrollmentIdentityNvsStorage::fault() {
    failed_ = true; pending_ = false; wipe(before_); wipe(staged_); return StorageError::io_failure;
}
bool EnrollmentIdentityNvsStorage::guard() {
    if(failed_ || generation_ == std::numeric_limits<std::uint64_t>::max() ||
       generation_ != reset_generation.load(std::memory_order_acquire) || !reset_absent()) {
        fault(); return false;
    }
    return true;
}
StorageReadResult EnrollmentIdentityNvsStorage::read_slot(StorageDomain domain, std::size_t slot,
    MutableStorageByteView output) {
    if(!valid_slot(domain,slot) || !output.data || output.size != kPersistentSlotBytes) return {StorageError::invalid_argument,0};
    Scratch bytes;
    if(!guard() || !load(bytes.bytes) || !guard()) return {fault(),0};
    if(!identity_slot(domain,slot)) bytes.bytes.fill(0xff);
    std::copy(bytes.bytes.begin(),bytes.bytes.end(),output.data);
    return {StorageError::none,kPersistentSlotBytes};
}
StorageError EnrollmentIdentityNvsStorage::erase_slot(StorageDomain, std::size_t) {
    // Normal identity provisioning has no erase/retry fallback.
    return StorageError::invalid_argument;
}
StorageError EnrollmentIdentityNvsStorage::write_slot(StorageDomain domain, std::size_t slot,
    std::size_t offset, StorageByteView input) {
    if(!identity_slot(domain,slot) || !input.data || input.size == 0 || offset > kPersistentSlotBytes ||
       input.size > kPersistentSlotBytes-offset) return StorageError::invalid_argument;
    if(!guard()) return StorageError::io_failure;
    if(pending_ || !load(before_) || !guard()) return fault();
    staged_ = before_;
    for(std::size_t i=0;i<input.size;++i)
        if((before_[offset+i] & input.data[i]) != input.data[i]) { wipe(before_);wipe(staged_);return StorageError::write_requires_erase; }
    std::copy(input.data,input.data+input.size,staged_.begin()+offset);
    pending_ = true; return StorageError::none;
}
StorageError EnrollmentIdentityNvsStorage::sync_slot(StorageDomain domain, std::size_t slot) {
    if(!identity_slot(domain,slot)) return StorageError::invalid_argument;
    if(!guard()) return StorageError::io_failure;
    if(!pending_) return StorageError::none;
    Scratch observed;
    if(!load(observed.bytes) || observed.bytes != before_ || !guard()) return fault();
    {
        Handle handle;
        if(nvs_open(kEnrollmentIdentityStorageNamespace,NVS_READWRITE,&handle.value) != ESP_OK) return fault();
        // From set onward a failure may have persisted the whole record.
        // Poison the owner; never erase or retry to make it look successful.
        if(nvs_set_blob(handle.value,kEnrollmentIdentityStorageKey,staged_.data(),staged_.size()) != ESP_OK ||
           nvs_commit(handle.value) != ESP_OK) return fault();
    }
    if(!load(observed.bytes) || observed.bytes != staged_ || !guard()) return fault();
    pending_ = false; wipe(before_); wipe(staged_); return StorageError::none;
}
void invalidate_enrollment_identity_storage_for_reset() {
    const auto current = reset_generation.load(std::memory_order_acquire);
    if(current != std::numeric_limits<std::uint64_t>::max())
        reset_generation.store(current+1,std::memory_order_release);
}
}
