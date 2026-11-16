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
    // I-068 diagnostics: identity reads served from the RAM copy, and reads that
    // went to NVS (each opens NVS and scans for the reset marker twice).
    struct ReadStats { std::uint32_t cache_hits, nvs_reads; };
    ReadStats read_stats() const { return {cache_hits_, nvs_reads_}; }
    void reset_read_stats() { cache_hits_ = 0; nvs_reads_ = 0; }
    // Terminal for the RAM copy: wipes it (the record holds the private seed) and
    // never fills it again. Called when the owning identity is retired, including
    // reset containment (review of I-068).
    void retire_cache();
private:
    using Bytes = std::array<std::uint8_t, persistence::kPersistentSlotBytes>;
    bool guard();
    persistence::StorageError fault();
    std::uint64_t generation_;
    bool failed_{false}, pending_{false};
    Bytes before_{}, staged_{};
    // I-068: one pairing reads this record about 11,000 times; on the board each
    // NVS read took about 4 ms, so the final save blocked for 26 s. The copy is
    // the bytes last verified in NVS under guard(). It is used only while no write
    // is pending and the reset generation is unchanged (factory reset and the
    // reset marker write both bump it); a write drops it, a verified sync
    // refreshes it, and any fault wipes it.
    Bytes cached_{};
    bool cached_valid_{false}, cache_retired_{false};
    std::uint32_t cache_hits_{0}, nvs_reads_{0};
    void drop_cache();
};
// Called by the actual all-domain reset port before erasing any user domain.
// Invalidates every existing adapter even if erase/commit subsequently fails.
// A future runtime must also retire its identity owner and volatile key copies.
void invalidate_enrollment_identity_storage_for_reset();
}
