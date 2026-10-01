#pragma once
#include <array>
#include <cstdint>
#include "opentrail/persistent_storage.hpp"
#include "opentrail/enrollment_identity_storage_contract.hpp"

namespace opentrail::targets::heltec_v4_bench {
// One serialized runtime owner must own storage and reset. Construction does
// not initialize NVS, create keys, provision identity or activate enrollment.
// Ordinary application-protected storage; no hostile physical rollback claim.
class EnrollmentIdentityNvsStorage final : public persistence::PersistentStorage {
public:
    EnrollmentIdentityNvsStorage();
    ~EnrollmentIdentityNvsStorage() override;
    EnrollmentIdentityNvsStorage(const EnrollmentIdentityNvsStorage&) = delete;
    EnrollmentIdentityNvsStorage& operator=(const EnrollmentIdentityNvsStorage&) = delete;
    persistence::StorageReadResult read_slot(persistence::StorageDomain, std::size_t,
        persistence::MutableStorageByteView) override;
    persistence::StorageError erase_slot(persistence::StorageDomain, std::size_t) override;
    persistence::StorageError write_slot(persistence::StorageDomain, std::size_t,
        std::size_t, persistence::StorageByteView) override;
    persistence::StorageError sync_slot(persistence::StorageDomain, std::size_t) override;
private:
    using Bytes = std::array<std::uint8_t, persistence::kPersistentSlotBytes>;
    bool guard();
    persistence::StorageError fault();
    std::uint64_t generation_;
    bool failed_{false}, pending_{false};
    Bytes before_{}, staged_{};
};
// Called by the actual all-domain reset port before erasing any user domain.
// Invalidates every existing adapter even if erase/commit subsequently fails.
// A future runtime must also retire its identity owner and volatile key copies.
void invalidate_enrollment_identity_storage_for_reset();
}
