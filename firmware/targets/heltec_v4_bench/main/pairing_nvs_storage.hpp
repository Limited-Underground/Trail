#pragma once
// Board pairing storage (V1 F5, step R2a; owner decision D-086).
//
// A standard NVS instance inside the existing raw `ot_state` partition (1 MB,
// application type 0x40), opened only when pairing starts. Factory reset erases
// every byte of `ot_state`; it must call retire_pairing_storage_for_reset()
// first so no NVS instance stays mounted over the erased flash.
//
// The storage logic follows the board-tested enrollment evaluation storage
// (heltec_v4_enrollment_candidate_eval/main/candidate_nvs_storage.hpp): sparse
// 64-byte tuples keyed by generation/namespace/domain/slot, one staged mutation
// committed and read back, an inventory check on open, and the same 204-entry
// ceiling for the pairing records. One serialized owner owns every tuple.
#include <array>
#include <cstddef>
#include <cstdint>

#include "nvs.h"
#include "esp_partition.h"
#include "opentrail/session_generation_storage.hpp"

#include "pairing_storage_key.hpp"

namespace opentrail::target::heltec_v4_bench {

class PairingNvsStorage final : public security_evaluation::EvaluationGenerationBackend {
public:
    using Namespace = security_evaluation::EvaluationNamespace;
    using Domain = persistence::StorageDomain;
    using Error = persistence::StorageError;
    enum class Store : unsigned { identity = 7, journal, binding, boot };
    // I-055: every pairing attempt, even a failed or interrupted one, permanently
    // consumes one session generation. Capacity 2 blocked both bench boards after
    // two attempts. 32 allows routine retries; a factory reset restores it.
    static constexpr std::uint64_t kCapacity = 32;
    static constexpr char kPartition[] = "ot_state";
    static constexpr char kNamespace[] = "ot_pair";
    static constexpr std::uint32_t kPartitionAddress = 0xf00000;
    static constexpr std::uint32_t kPartitionBytes = 0x100000;
    // 256 pages x 126 entries in ot_state.
    static constexpr std::size_t kTotalEntries = 256 * 126;
    // Worst case for the pairing records: 37 fixed slots plus 7 per generation,
    // 4 NVS entries per 64-byte slot (F5-R2 plan budget). 1,044 at capacity 32,
    // about 3% of ot_state.
    static constexpr std::size_t kPairingEntries = (37 + 7 * kCapacity) * 4;
    static_assert(kPairingEntries == 1044 && kPairingEntries < kTotalEntries / 8);

    PairingNvsStorage();
    ~PairingNvsStorage() override;
    PairingNvsStorage(const PairingNvsStorage&) = delete;
    PairingNvsStorage& operator=(const PairingNvsStorage&) = delete;

    bool ready() const { return good_; }
    // Journal, binding and boot views for the enrollment session. The board
    // identity stays in its existing default-NVS namespace (ot_identity_v1).
    persistence::PersistentStorage& external(Store store) {
        return views_[static_cast<unsigned>(store) - 7];
    }
    bool budget_ok();
    // Closes the handle and unmounts the NVS instance. Terminal.
    void retire();

    persistence::StorageReadResult read(std::uint64_t, Namespace, Domain, std::size_t,
                                        persistence::MutableStorageByteView) override;
    Error erase(std::uint64_t, Namespace, Domain, std::size_t) override;
    Error write(std::uint64_t, Namespace, Domain, std::size_t, std::size_t,
                persistence::StorageByteView) override;
    Error sync(std::uint64_t, Namespace, Domain, std::size_t) override;

private:
    class View final : public persistence::PersistentStorage {
    public:
        View(PairingNvsStorage& owner, Store store) : owner_(owner), store_(store) {}
        persistence::StorageReadResult read_slot(Domain d, std::size_t s,
                                                 persistence::MutableStorageByteView out) override {
            return owner_.read_tuple(0, static_cast<unsigned>(store_), d, s, out);
        }
        Error erase_slot(Domain d, std::size_t s) override {
            return owner_.erase_tuple(0, static_cast<unsigned>(store_), d, s);
        }
        Error write_slot(Domain d, std::size_t s, std::size_t o, persistence::StorageByteView in) override {
            return owner_.write_tuple(0, static_cast<unsigned>(store_), d, s, o, in);
        }
        Error sync_slot(Domain d, std::size_t s) override {
            return owner_.sync_tuple(0, static_cast<unsigned>(store_), d, s);
        }
    private:
        PairingNvsStorage& owner_;
        Store store_;
    };
    using Bytes = std::array<std::uint8_t, persistence::kPersistentSlotBytes>;

    static const esp_partition_t* partition();
    static bool key_for(std::uint64_t g, unsigned n, Domain d, std::size_t s, char (&key)[16]);
    // Table position of a validated key; any other key faults and maps to the
    // guard slot at the end, never past the table (I-058).
    std::size_t index(const char* key);
    static void wipe(Bytes& bytes);
    bool inventory();
    bool load(const char* key, Bytes& out);
    bool start_mutation(const char* key);
    persistence::StorageReadResult read_tuple(std::uint64_t, unsigned, Domain, std::size_t,
                                              persistence::MutableStorageByteView);
    Error erase_tuple(std::uint64_t, unsigned, Domain, std::size_t);
    Error write_tuple(std::uint64_t, unsigned, Domain, std::size_t, std::size_t,
                      persistence::StorageByteView);
    Error sync_tuple(std::uint64_t, unsigned, Domain, std::size_t);
    Error fault();

    nvs_handle_t handle_{};
    bool good_{}, mounted_{}, pending_{}, pending_erased_{};
    std::array<char, 16> pending_key_{};
    Bytes pending_bytes_{};
    std::array<View, 4> views_;
    // One entry per possible key for generations 0..kCapacity, plus a guard slot.
    std::array<bool, pairing_key::table_size(kCapacity) + 1> known_{};
};

// Factory reset calls this before erasing `ot_state`. Safe when no pairing
// storage was ever opened. After it, no PairingNvsStorage may be constructed.
void retire_pairing_storage_for_reset();
// Registers the single live instance so reset can retire it.
bool register_pairing_storage(PairingNvsStorage*);

}  // namespace opentrail::target::heltec_v4_bench
