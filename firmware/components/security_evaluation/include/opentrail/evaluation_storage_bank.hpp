#pragma once
// OT239 evaluation mapping, not an ESP-IDF/NVS implementation. One serialized
// backend owns the namespace/domain/slot tuple. It must never alias tuples or
// mutate another tuple from a read. The bank prevents caller-selected duplicate
// namespace bindings; the backend remains a trusted board-specific boundary.
#include <array>
#include "opentrail/persistent_storage.hpp"

namespace opentrail::security_evaluation {
enum class EvaluationNamespace : std::uint8_t {
    boot, role, transmit, receive, activation, membership, enrollment, count
};
class EvaluationStorageBackend {
public:
    virtual ~EvaluationStorageBackend() = default;
    virtual persistence::StorageReadResult read(EvaluationNamespace, persistence::StorageDomain,
        std::size_t, persistence::MutableStorageByteView) = 0;
    virtual persistence::StorageError erase(EvaluationNamespace, persistence::StorageDomain,
        std::size_t) = 0;
    virtual persistence::StorageError write(EvaluationNamespace, persistence::StorageDomain,
        std::size_t, std::size_t, persistence::StorageByteView) = 0;
    virtual persistence::StorageError sync(EvaluationNamespace, persistence::StorageDomain,
        std::size_t) = 0;
};
class EvaluationStorageBank final {
    class View final : public persistence::PersistentStorage {
    public:
        View(EvaluationStorageBackend& backend, EvaluationNamespace space) : backend_(backend), space_(space) {}
        View(const View&) = delete;
        View& operator=(const View&) = delete;
        persistence::StorageReadResult read_slot(persistence::StorageDomain d, std::size_t s,
            persistence::MutableStorageByteView out) override {
            if (!valid(d,s) || !out.data || out.size != persistence::kPersistentSlotBytes)
                return {persistence::StorageError::invalid_argument,0};
            return backend_.read(space_,d,s,out);
        }
        persistence::StorageError erase_slot(persistence::StorageDomain d, std::size_t s) override {
            return valid(d,s) ? backend_.erase(space_,d,s) : persistence::StorageError::invalid_argument;
        }
        persistence::StorageError write_slot(persistence::StorageDomain d, std::size_t s,
            std::size_t offset, persistence::StorageByteView in) override {
            if (!valid(d,s) || !in.data || !in.size || offset > persistence::kPersistentSlotBytes ||
                in.size > persistence::kPersistentSlotBytes-offset) return persistence::StorageError::invalid_argument;
            return backend_.write(space_,d,s,offset,in);
        }
        persistence::StorageError sync_slot(persistence::StorageDomain d, std::size_t s) override {
            return valid(d,s) ? backend_.sync(space_,d,s) : persistence::StorageError::invalid_argument;
        }
    private:
        static bool valid(persistence::StorageDomain d,std::size_t s) {
            return static_cast<std::size_t>(d)<persistence::kStorageDomainCount && s<persistence::kPersistentSlotCount;
        }
        EvaluationStorageBackend& backend_;
        const EvaluationNamespace space_;
    };
public:
    explicit EvaluationStorageBank(EvaluationStorageBackend& backend)
        : views_{{{backend,EvaluationNamespace::boot},{backend,EvaluationNamespace::role},
                  {backend,EvaluationNamespace::transmit},{backend,EvaluationNamespace::receive},
                  {backend,EvaluationNamespace::activation},{backend,EvaluationNamespace::membership},
                  {backend,EvaluationNamespace::enrollment}}} {}
    EvaluationStorageBank(const EvaluationStorageBank&) = delete;
    EvaluationStorageBank& operator=(const EvaluationStorageBank&) = delete;
    persistence::PersistentStorage* get(EvaluationNamespace space) {
        const auto index=static_cast<std::size_t>(space);
        return index<views_.size() ? &views_[index] : nullptr;
    }
private:
    std::array<View,static_cast<std::size_t>(EvaluationNamespace::count)> views_;
};
} // namespace opentrail::security_evaluation
