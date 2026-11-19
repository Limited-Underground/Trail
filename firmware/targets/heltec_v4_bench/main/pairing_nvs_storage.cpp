#include "pairing_nvs_storage.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"

namespace opentrail::target::heltec_v4_bench {
namespace {
PairingNvsStorage* g_live = nullptr;
bool g_reset_retired = false;

// One recursive lock serializes every storage operation with retire() and the
// factory-reset hook, which may run on another task (review R2A-6): reset waits
// for an in-flight write, and every later write fails.
SemaphoreHandle_t storage_lock() {
    static StaticSemaphore_t buffer;
    static SemaphoreHandle_t handle = xSemaphoreCreateRecursiveMutexStatic(&buffer);
    return handle;
}
struct Guard {
    Guard() { xSemaphoreTakeRecursive(storage_lock(), portMAX_DELAY); }
    ~Guard() { xSemaphoreGiveRecursive(storage_lock()); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};
}  // namespace

const esp_partition_t* PairingNvsStorage::partition() {
    const auto* p = esp_partition_find_first(static_cast<esp_partition_type_t>(0x40),
                                             static_cast<esp_partition_subtype_t>(0x00), kPartition);
    if (p == nullptr || std::strcmp(p->label, kPartition) != 0 ||
        p->address != kPartitionAddress || p->size != kPartitionBytes ||
        p->encrypted || p->readonly) {
        return nullptr;
    }
    return p;
}

PairingNvsStorage::PairingNvsStorage()
    : views_{{{*this, Store::identity}, {*this, Store::journal},
              {*this, Store::binding}, {*this, Store::boot}, {*this, Store::completion}}} {
    Guard guard;
    if (g_reset_retired) return;
    const auto* p = partition();
    if (p == nullptr) return;
    mounted_ = nvs_flash_init_partition_ptr(p) == ESP_OK;
    good_ = mounted_ &&
            nvs_open_from_partition(kPartition, kNamespace, NVS_READWRITE, &handle_) == ESP_OK;
    if (good_ && (!inventory() || !budget_ok())) good_ = false;
}

PairingNvsStorage::~PairingNvsStorage() { retire(); }

void PairingNvsStorage::retire() {
    Guard guard;
    good_ = false;
    cache_.clear();
    wipe(pending_bytes_);
    if (handle_ != 0) { nvs_close(handle_); handle_ = 0; }
    if (mounted_) { (void)nvs_flash_deinit_partition(kPartition); mounted_ = false; }
    if (g_live == this) g_live = nullptr;
}

bool register_pairing_storage(PairingNvsStorage* storage) {
    Guard guard;
    if (g_reset_retired || storage == nullptr || g_live != nullptr) return false;
    g_live = storage;
    return true;
}

void retire_pairing_storage_for_reset() {
    Guard guard;
    g_reset_retired = true;
    if (g_live != nullptr) g_live->retire();
    g_live = nullptr;
}

bool PairingNvsStorage::budget_ok() {
    Guard guard;
    if (!good_) return false;
    nvs_stats_t stats{};
    std::size_t own = 0;
    const auto started_us = esp_timer_get_time();
    const bool read_ok = nvs_get_stats(kPartition, &stats) == ESP_OK &&
                         nvs_get_used_entry_count(handle_, &own) == ESP_OK;
    ++budget_checks_;
    budget_us_ += static_cast<std::uint64_t>(esp_timer_get_time() - started_us);
    if (!read_ok ||
        stats.total_entries != kTotalEntries || stats.namespace_count != 1 ||
        own > kPairingEntries || own > stats.used_entries) {
        (void)fault();
        return false;
    }
    return true;
}

persistence::StorageReadResult PairingNvsStorage::read(std::uint64_t g, Namespace n, Domain d, std::size_t s,
                                                       persistence::MutableStorageByteView out) {
    if (static_cast<unsigned>(n) >= static_cast<unsigned>(Namespace::count)) return {Error::invalid_argument, 0};
    return read_tuple(g, static_cast<unsigned>(n), d, s, out);
}
PairingNvsStorage::Error PairingNvsStorage::erase(std::uint64_t g, Namespace n, Domain d, std::size_t s) {
    if (static_cast<unsigned>(n) >= static_cast<unsigned>(Namespace::count)) return Error::invalid_argument;
    return erase_tuple(g, static_cast<unsigned>(n), d, s);
}
PairingNvsStorage::Error PairingNvsStorage::write(std::uint64_t g, Namespace n, Domain d, std::size_t s,
                                                  std::size_t o, persistence::StorageByteView in) {
    if (static_cast<unsigned>(n) >= static_cast<unsigned>(Namespace::count)) return Error::invalid_argument;
    return write_tuple(g, static_cast<unsigned>(n), d, s, o, in);
}
PairingNvsStorage::Error PairingNvsStorage::sync(std::uint64_t g, Namespace n, Domain d, std::size_t s) {
    if (static_cast<unsigned>(n) >= static_cast<unsigned>(Namespace::count)) return Error::invalid_argument;
    return sync_tuple(g, static_cast<unsigned>(n), d, s);
}

void PairingNvsStorage::wipe(Bytes& bytes) {
    for (volatile auto& b : bytes) b = 0;
}

bool PairingNvsStorage::key_for(std::uint64_t g, unsigned n, Domain d, std::size_t s, char (&key)[16]) {
    if (s >= pairing_key::kSlots) return false;
    return pairing_key::format(g, n, static_cast<unsigned>(d), static_cast<unsigned>(s), kCapacity, key);
}

static_assert(persistence::kStorageDomainCount == pairing_key::kDomains);
static_assert(persistence::kPersistentSlotCount == pairing_key::kSlots);
static_assert(sizeof(nvs_entry_info_t{}.key) == pairing_key::kKeyBytes);

std::size_t PairingNvsStorage::index(const pairing_key::Key& key) {
    const auto i = pairing_key::slot(key, kCapacity);
    if (i >= pairing_key::table_size(kCapacity)) {
        (void)fault();
        return known_.size() - 1;  // guard slot
    }
    return i;
}

bool PairingNvsStorage::inventory() {
    nvs_iterator_t it = nullptr;
    auto status = nvs_entry_find(kPartition, nullptr, NVS_TYPE_ANY, &it);
    while (status == ESP_OK) {
        nvs_entry_info_t info{};
        bool found = false;
        if (nvs_entry_info(it, &info) != ESP_OK || std::strcmp(info.namespace_name, kNamespace) != 0 ||
            info.type != NVS_TYPE_BLOB) {
            nvs_release_iterator(it);
            return false;
        }
        for (std::uint64_t g = 0; g <= kCapacity && !found; ++g)
            for (unsigned n = 0; n < pairing_key::kNamespaces && !found; ++n)
                for (unsigned d = 0; d < 5 && !found; ++d)
                    for (unsigned s = 0; s < 2 && !found; ++s) {
                        char key[16]{};
                        found = key_for(g, n, static_cast<Domain>(d), s, key) && std::strcmp(info.key, key) == 0;
                    }
        if (!found) { nvs_release_iterator(it); return false; }
        std::size_t size = 0;
        if (nvs_get_blob(handle_, info.key, nullptr, &size) != ESP_OK || size != 64) {
            nvs_release_iterator(it);
            return false;
        }
        known_[index(info.key)] = true;
        status = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    return status == ESP_ERR_NVS_NOT_FOUND;
}

bool PairingNvsStorage::load(const pairing_key::Key& key, Bytes& out) {
    if (!good_) return false;
    if (pending_ && std::strcmp(key, pending_key_.data()) == 0) { out = pending_bytes_; return true; }
    if (cache_.get(key, out)) return true;  // I-066
    const auto started_us = esp_timer_get_time();
    ++nvs_reads_;
    std::size_t size = 0;
    const auto status = nvs_get_blob(handle_, key, nullptr, &size);
    if (status == ESP_ERR_NVS_NOT_FOUND) {
        if (known_[index(key)]) { (void)fault(); return false; }
        out.fill(0xff);
        cache_.put(key, out);
        nvs_read_us_ += static_cast<std::uint64_t>(esp_timer_get_time() - started_us);
        return true;
    }
    if (status != ESP_OK || size != out.size()) { (void)fault(); return false; }
    if (nvs_get_blob(handle_, key, out.data(), &size) != ESP_OK || size != out.size()) {
        (void)fault();
        return false;
    }
    known_[index(key)] = true;
    cache_.put(key, out);
    nvs_read_us_ += static_cast<std::uint64_t>(esp_timer_get_time() - started_us);
    return true;
}

persistence::StorageReadResult PairingNvsStorage::read_tuple(std::uint64_t g, unsigned n, Domain d, std::size_t s,
                                                             persistence::MutableStorageByteView out) {
    Guard guard;
    char key[16]{};
    if (!key_for(g, n, d, s, key) || !out.data || out.size != 64) return {Error::invalid_argument, 0};
    Bytes bytes{};
    const bool ok = load(key, bytes);
    if (ok) std::copy(bytes.begin(), bytes.end(), out.data);
    wipe(bytes);
    return {ok ? Error::none : Error::io_failure, ok ? 64U : 0U};
}

bool PairingNvsStorage::start_mutation(const pairing_key::Key& key) {
    if (!good_ || !budget_ok()) return false;
    if (pending_ && std::strcmp(key, pending_key_.data()) != 0) { (void)fault(); return false; }
    return true;
}

PairingNvsStorage::Error PairingNvsStorage::erase_tuple(std::uint64_t g, unsigned n, Domain d, std::size_t s) {
    Guard guard;
    char key[16]{};
    if (!key_for(g, n, d, s, key)) return Error::invalid_argument;
    if (!start_mutation(key)) return Error::io_failure;
    cache_.drop(key);
    const auto status = nvs_erase_key(handle_, key);
    if (status != ESP_OK && status != ESP_ERR_NVS_NOT_FOUND) return fault();
    if (status == ESP_ERR_NVS_NOT_FOUND && known_[index(key)] && !(pending_ && pending_erased_)) return fault();
    std::copy(key, key + 16, pending_key_.begin());
    pending_bytes_.fill(0xff);
    pending_ = true;
    pending_erased_ = true;
    return Error::none;
}

PairingNvsStorage::Error PairingNvsStorage::write_tuple(std::uint64_t g, unsigned n, Domain d, std::size_t s,
                                                        std::size_t offset, persistence::StorageByteView in) {
    Guard guard;
    char key[16]{};
    if (!key_for(g, n, d, s, key) || !in.data || !in.size || offset > 64 || in.size > 64 - offset) {
        return Error::invalid_argument;
    }
    if (!start_mutation(key)) return Error::io_failure;
    cache_.drop(key);
    Bytes bytes{};
    if (!load(key, bytes)) return Error::io_failure;
    // A new key must fit the pairing ceiling before touching NVS.
    std::size_t own = 0, size = 0;
    const auto status = nvs_get_blob(handle_, key, nullptr, &size);
    if (nvs_get_used_entry_count(handle_, &own) != ESP_OK ||
        (status != ESP_OK && status != ESP_ERR_NVS_NOT_FOUND) ||
        (status == ESP_OK && size != 64) || own > kPairingEntries ||
        (status == ESP_ERR_NVS_NOT_FOUND && (known_[index(key)] || (pending_ && !pending_erased_)) &&
         !(pending_ && pending_erased_)) ||
        (status == ESP_ERR_NVS_NOT_FOUND && own > kPairingEntries - 4)) {
        wipe(bytes);
        return fault();
    }
    for (std::size_t i = 0; i < in.size; ++i) {
        if ((bytes[offset + i] & in.data[i]) != in.data[i]) { wipe(bytes); return Error::write_requires_erase; }
    }
    std::copy(in.data, in.data + in.size, bytes.begin() + offset);
    cache_.drop(key);  // flash changes now; the verified bytes return at sync
    if (nvs_set_blob(handle_, key, bytes.data(), bytes.size()) != ESP_OK) { wipe(bytes); return fault(); }
    std::copy(key, key + 16, pending_key_.begin());
    pending_bytes_ = bytes;
    wipe(bytes);
    pending_ = true;
    pending_erased_ = false;
    return Error::none;
}

PairingNvsStorage::Error PairingNvsStorage::sync_tuple(std::uint64_t g, unsigned n, Domain d, std::size_t s) {
    Guard guard;
    char key[16]{};
    if (!key_for(g, n, d, s, key)) return Error::invalid_argument;
    if (!good_) return Error::io_failure;
    if (!pending_) return Error::none;
    if (std::strcmp(key, pending_key_.data()) != 0 || nvs_commit(handle_) != ESP_OK) return fault();
    pending_ = false;
    Bytes actual{};
    bool observed = false;
    if (pending_erased_) {
        std::size_t size = 0;
        observed = nvs_get_blob(handle_, key, nullptr, &size) == ESP_ERR_NVS_NOT_FOUND;
        if (observed) { known_[index(key)] = false; actual.fill(0xff); }
    } else {
        known_[index(key)] = true;
        observed = load(key, actual);
    }
    const bool ok = observed && actual == pending_bytes_ && budget_ok();
    if (ok) cache_.put(key, actual);  // the bytes just verified in NVS
    wipe(actual);
    wipe(pending_bytes_);
    return ok ? Error::none : fault();
}

PairingNvsStorage::Error PairingNvsStorage::fault() {
    good_ = false;
    cache_.clear();
    wipe(pending_bytes_);
    return Error::io_failure;
}

}  // namespace opentrail::target::heltec_v4_bench
