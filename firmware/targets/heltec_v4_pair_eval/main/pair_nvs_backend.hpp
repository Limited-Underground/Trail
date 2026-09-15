#pragma once
#include <algorithm>
#include <array>
#include <cstring>
#include "nvs.h"
#include "opentrail/persistent_storage_kv.hpp"

namespace opentrail::target::heltec_v4_pair_eval {
// Evaluation-only mapping, following the frozen policy backend contract. Each
// object exclusively owns one handle/transaction in ordinary NVS. Only the two
// 64-byte ledger slots are accessible; no namespace or partition erase exists.
class PairNvsBackend final : public persistence::PersistentKvBackend {
public:
    enum class Store { boot, role, tx, rx };
    using Error = persistence::PersistentKvBackendError;
    explicit PairNvsBackend(Store store) {
        const char* name = store == Store::boot ? "ot230_boot" :
                           store == Store::role ? "ot230_role" :
                           store == Store::tx ? "ot230_tx" :
                           store == Store::rx ? "ot230_rx" : nullptr;
        if (name != nullptr) good_ = nvs_open(name, NVS_READWRITE, &handle_) == ESP_OK;
    }
    ~PairNvsBackend() { if (handle_ != 0) nvs_close(handle_); }
    PairNvsBackend(const PairNvsBackend&) = delete;
    PairNvsBackend& operator=(const PairNvsBackend&) = delete;
    bool ready() const { return good_; }
    Error read_blob(const char* partition, const char* name, const char* key,
                    std::uint8_t* output, std::size_t capacity, std::size_t& size) override {
        size = 0;
        if (!context(partition, name) || !slot(key) || output == nullptr ||
            capacity != persistence::kPersistentSlotBytes) return Error::invalid_argument;
        if (!good_) return Error::io_failure;
        std::size_t actual = 0;
        auto status = nvs_get_blob(handle_, key, nullptr, &actual);
        if (status == ESP_ERR_NVS_NOT_FOUND) return Error::not_found;
        if (status != ESP_OK || actual != capacity) return fault();
        std::array<std::uint8_t, persistence::kPersistentSlotBytes> scratch{};
        status = nvs_get_blob(handle_, key, scratch.data(), &actual);
        if (status != ESP_OK || actual != capacity) return fault();
        std::copy(scratch.begin(), scratch.end(), output);
        size = actual;
        return Error::none;
    }
    Error write_blob(const char* partition, const char* name, const char* key,
                     const std::uint8_t* data, std::size_t size) override {
        if (!context(partition, name) || !slot(key) || data == nullptr ||
            size != persistence::kPersistentSlotBytes) return Error::invalid_argument;
        if (!good_) return Error::io_failure;
        return nvs_set_blob(handle_, key, data, size) == ESP_OK ? Error::none : fault();
    }
    Error erase_key(const char* partition, const char* name, const char* key) override {
        if (!context(partition, name) || !slot(key)) return Error::invalid_argument;
        if (!good_) return Error::io_failure;
        const auto status = nvs_erase_key(handle_, key);
        if (status == ESP_ERR_NVS_NOT_FOUND) return Error::not_found;
        return status == ESP_OK ? Error::none : fault();
    }
    Error commit(const char* partition, const char* name) override {
        if (!context(partition, name)) return Error::invalid_argument;
        if (!good_) return Error::io_failure;
        return nvs_commit(handle_) == ESP_OK ? Error::none : fault();
    }
private:
    static bool exact(const char* value, const char* expected) {
        return value != nullptr && std::strcmp(value, expected) == 0;
    }
    static bool context(const char* partition, const char* name) {
        return exact(partition, persistence::kPersistentKvPartitionLabel) &&
               exact(name, persistence::kPersistentKvCounterNamespace);
    }
    static bool slot(const char* key) {
        return exact(key, persistence::kPersistentKvSlotAKey) ||
               exact(key, persistence::kPersistentKvSlotBKey);
    }
    Error fault() { good_ = false; return Error::io_failure; }
    nvs_handle_t handle_{0};
    bool good_{false};
};
} // namespace opentrail::target::heltec_v4_pair_eval
