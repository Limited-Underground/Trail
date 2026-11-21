#pragma once
// Shared retained state storage (D-086 pairing and D-116 groups).
//
// A standard NVS instance inside the existing raw `ot_state` partition (1 MB,
// application type 0x40), mounted on demand by admitted owners. Factory reset erases
// every byte of `ot_state`; it must call retire_pairing_storage_for_reset()
// first so no NVS handle stays open over the erased flash.
//
// The storage logic follows the board-tested enrollment evaluation storage
// (heltec_v4_enrollment_candidate_eval/main/candidate_nvs_storage.hpp): sparse
// 64-byte tuples keyed by generation/namespace/domain/slot, one staged mutation
// committed and read back, an inventory check on open, and the bounded NVS-entry
// ceiling for the pairing records. One serialized owner owns every tuple.
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "board_storage_budget.hpp"
#include "pairing_nvs_reset.hpp"

#include "opentrail/group_pair_index.hpp"
#include "opentrail/pairing_completion_archive.hpp"

#include "nvs.h"
#include "esp_partition.h"
#include "opentrail/session_generation_storage.hpp"

#include "pairing_slot_cache.hpp"
#include "pairing_storage_key.hpp"

namespace opentrail::target::heltec_v4_bench {

struct PairingNvsEntryUsage {
    std::uint64_t observed_at_ms;
    std::uint32_t used_entries;
    std::uint32_t free_entries;
    std::uint32_t total_entries;
};

// Last successful read of ot_state allocation entries. Never mounts or scans NVS.
[[nodiscard]] std::optional<PairingNvsEntryUsage> pairing_nvs_entry_usage();

class PairingNvsStorage final : public security_evaluation::EvaluationGenerationBackend,
                              public security_evaluation::PairingCompletionArchive {
public:
    // I-066 diagnostics: slot reads served from RAM, and reads that reached NVS.
    // Counted since the last reset_read_stats() (each pairing attempt): slot reads
    // served from RAM, slot reads that reached NVS, and the budget checks
    // (nvs_get_stats plus nvs_get_used_entry_count, which scan the partition).
    struct ReadStats {
        std::uint32_t cache_hits, nvs_reads, nvs_read_ms, budget_checks, budget_ms;
    };
    ReadStats read_stats() const {
        return {cache_.hits() - hits_at_reset_, nvs_reads_, static_cast<std::uint32_t>(nvs_read_us_ / 1000),
                budget_checks_, static_cast<std::uint32_t>(budget_us_ / 1000)};
    }
    void reset_read_stats() {
        hits_at_reset_ = cache_.hits();
        nvs_reads_ = 0;
        nvs_read_us_ = 0;
        budget_checks_ = 0;
        budget_us_ = 0;
    }
    using Namespace = security_evaluation::EvaluationNamespace;
    using Domain = persistence::StorageDomain;
    using Error = persistence::StorageError;
    enum class Store : unsigned {
        identity = 7, journal = 8, binding = 9, boot = 10, completion = 11,
        f5_state = 12, packet_data_tx = 13, packet_ack_tx = 14,
        packet_data_rx = 15, packet_ack_rx = 16, f5_state_tail = 17
    };
    // I-055: every pairing attempt, even a failed or interrupted one, permanently
    // consumes one session generation. Capacity 2 blocked both bench boards after
    // two attempts. 32 allows routine retries; a factory reset restores it.
    static constexpr std::uint64_t kCapacity = pairing_key::kCapacity;
    static constexpr char kPartition[] = "ot_state";
    static constexpr char kNamespace[] = "ot_pair";
    static constexpr std::uint32_t kPartitionAddress = 0xf00000;
    static constexpr std::uint32_t kPartitionBytes = 0x100000;
    // 256 pages x 126 entries in ot_state.
    static constexpr std::size_t kTotalEntries = 256 * 126;
    // D-163 models 899 resident payload tuples at five entries each.
    // The separate format seal is paid for by the shared staging reserve.
    static constexpr std::size_t kPairingEntries = board_storage_budget::kPairingEntries;

    PairingNvsStorage();
    ~PairingNvsStorage() override;
    PairingNvsStorage(const PairingNvsStorage&) = delete;
    PairingNvsStorage& operator=(const PairingNvsStorage&) = delete;

    bool ready() const;
    // Journal, binding and boot views for the enrollment session. The board
    // identity stays in its existing default-NVS namespace (ot_identity_v1).
    persistence::PersistentStorage& external(Store store) {
        const auto n = static_cast<unsigned>(store);
        if (n >= 7 && n <= 12) return views_[n - 7];
        if (store == Store::f5_state_tail) return views_[6];
        return denied_;
    }
    using PacketOwner = std::array<std::uint8_t, 32>;
    class PacketView;
    class PacketClaim {
        friend class PairingNvsStorage;
        friend class PacketView;
        PacketClaim(const PacketOwner& owner, std::uint64_t generation, unsigned slot)
            : owner_(owner), generation_(generation), slot_(slot) {}
        PacketOwner owner_;
        std::uint64_t generation_;
        unsigned slot_;
    public:
        PacketClaim(const PacketClaim&) = default;
        PacketClaim& operator=(const PacketClaim&) = default;
    };

    // The caller must have reserved generation durably for this owner and
    // destroyed all borrowers of older generations, including queued work.
    // Only the verified sole legacy F5 owner may supply its retired legacy
    // floor. This proof stays with the inventory for later claims of those
    // legacy slots. Foreign live or interrupted claims are never reclaimed.
    std::optional<PacketClaim> claim_packets(const PacketOwner&, std::uint64_t generation,
                                             std::uint64_t legacy_floor = 0);
    // The caller must stop all borrowers before explicitly retiring a claim.
    bool retire_packets(const PacketClaim&);

    class PacketView final : public persistence::PersistentStorage {
    public:
        PacketView(PairingNvsStorage& owner, Store store, const PacketClaim& claim)
            : owner_(owner), store_(store), claim_(claim) {}
        persistence::StorageReadResult read_slot(Domain, std::size_t, persistence::MutableStorageByteView) override;
        Error erase_slot(Domain, std::size_t) override;
        Error write_slot(Domain, std::size_t, std::size_t, persistence::StorageByteView) override;
        Error sync_slot(Domain, std::size_t) override;
    private:
        unsigned lane_namespace() const;
        PairingNvsStorage& owner_;
        Store store_;
        const PacketClaim claim_;
    };
    // Seed recovery cannot rewind an allocation whose lane records exist.
    bool f5_packet_footprint_absent();
    persistence::PersistentStorage* archive_storage(std::uint64_t generation) override {
        if (!ready() || generation == 0 || generation > kCapacity) return nullptr;
        auto& view = archive_views_[static_cast<std::size_t>(generation - 1)];
        if (!view) view.emplace(*this, Store::completion, generation);
        return &*view;
    }
    std::uint64_t archive_capacity() const override { return kCapacity; }
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
    friend class GroupNvsStorage;
    static bool acquire_partition();
    static bool admit_partition();

    class DeniedView final : public persistence::PersistentStorage {
    public:
        persistence::StorageReadResult read_slot(Domain, std::size_t,
            persistence::MutableStorageByteView) override { return {Error::invalid_argument,0}; }
        Error erase_slot(Domain, std::size_t) override { return Error::invalid_argument; }
        Error write_slot(Domain, std::size_t, std::size_t, persistence::StorageByteView) override {
            return Error::invalid_argument;
        }
        Error sync_slot(Domain, std::size_t) override { return Error::invalid_argument; }
    };
    class View final : public persistence::PersistentStorage {
    public:
        View(PairingNvsStorage& owner, Store store, std::uint64_t generation = 0)
            : owner_(owner), store_(store), generation_(generation) {}
        View(const View&) = delete;
        View& operator=(const View&) = delete;
        persistence::StorageReadResult read_slot(Domain d, std::size_t s,
                                                 persistence::MutableStorageByteView out) override {
            return owner_.read_tuple(generation_, static_cast<unsigned>(store_), d, s, out);
        }
        Error erase_slot(Domain d, std::size_t s) override {
            return owner_.erase_tuple(generation_, static_cast<unsigned>(store_), d, s);
        }
        Error write_slot(Domain d, std::size_t s, std::size_t o, persistence::StorageByteView in) override {
            return owner_.write_tuple(generation_, static_cast<unsigned>(store_), d, s, o, in);
        }
        Error sync_slot(Domain d, std::size_t s) override {
            return owner_.sync_tuple(generation_, static_cast<unsigned>(store_), d, s);
        }
    private:
        PairingNvsStorage& owner_;
        Store store_;
        std::uint64_t generation_;
    };
    using Bytes = std::array<std::uint8_t, persistence::kPersistentSlotBytes>;

    enum class PacketState : std::uint8_t { absent, preparing, ready, retired };
    struct PacketRecord {
        PacketOwner owner{};
        std::uint64_t generation{};
        PacketState state{PacketState::absent};
        std::uint8_t legacy_floor{};
    };
    static Bytes encode_packet_record(unsigned slot, const PacketRecord&);
    static bool decode_packet_record(unsigned slot, const Bytes&, PacketRecord&);
    static bool inventory_packets(nvs_handle_t handle);
    static bool has_packet_lanes(unsigned slot, unsigned first_namespace);
    bool save_packet_record(unsigned slot, const PacketRecord&);
    bool clear_packet_lanes(unsigned slot, unsigned first_namespace);
    bool owns_packet_claim(const PacketClaim&) const;

    static bool key_for(std::uint64_t g, unsigned n, Domain d, std::size_t s, char (&key)[16]);
    // Table position of a validated key; any other key faults and maps to the
    // guard slot at the end, never past the table (I-058).
    std::size_t index(const pairing_key::Key& key);
    static void wipe(Bytes& bytes);
    bool load(const pairing_key::Key& key, Bytes& out);
    bool start_mutation(const pairing_key::Key& key);
    bool authorize_mutation(std::size_t bytes, bool grows);
    persistence::StorageReadResult read_tuple(std::uint64_t, unsigned, Domain, std::size_t,
                                              persistence::MutableStorageByteView);
    Error erase_tuple(std::uint64_t, unsigned, Domain, std::size_t);
    Error write_tuple(std::uint64_t, unsigned, Domain, std::size_t, std::size_t,
                      persistence::StorageByteView);
    Error sync_tuple(std::uint64_t, unsigned, Domain, std::size_t);
    Error fault();

    nvs_handle_t handle_{};
    bool good_{}, mounted_{}, pending_{}, pending_erased_{};
    // Fixed retained proof + both F5 banks + active/candidate lane working
    // sets fit this bounded cache; hardware latency remains a bench observation.
    PairingSlotCache<128> cache_{};
    std::uint32_t nvs_reads_{0}, hits_at_reset_{0}, budget_checks_{0};
    std::uint64_t nvs_read_us_{0}, budget_us_{0};
    std::array<char, 16> pending_key_{};
    Bytes pending_bytes_{};
    DeniedView denied_;
    std::array<View, 7> views_;
    std::array<std::optional<View>, kCapacity> archive_views_;
    // Shared inventory is populated before any owner becomes ready. Only the
    // single pairing owner mutates payload tuples, under the partition lock.
    static std::array<PacketRecord, kCapacity> packet_records_;
    static std::uint8_t legacy_floor_;
    static std::bitset<pairing_key::table_size(kCapacity) + 1> known_;
};

// Each instance exclusively owns one group namespace. Construction does no I/O.
// ready() admits the shared partition and opens an existing namespace read-only.
// A mutation is staged in RAM, then committed using the exact namespace handle.
class GroupNvsStorage final : public persistence::PersistentKvBackend {
public:
    enum class Store { roster, catalog };

    explicit GroupNvsStorage(Store store) : store_(store) {}
    ~GroupNvsStorage() override;
    GroupNvsStorage(const GroupNvsStorage&) = delete;
    GroupNvsStorage& operator=(const GroupNvsStorage&) = delete;

    [[nodiscard]] bool ready();
    void retire();

    using Error = persistence::PersistentKvBackendError;
    Error read_blob(const char*, const char*, const char*, std::uint8_t*, std::size_t, std::size_t&) override;
    Error write_blob(const char*, const char*, const char*, const std::uint8_t*, std::size_t) override;
    Error erase_key(const char*, const char*, const char*) override;
    Error commit(const char*, const char*) override;

private:
    Error fault();

    const Store store_;
    nvs_handle_t handle_{};
    bool mounted_{false}, good_{false}, retired_{false}, writable_{false};
    std::array<std::uint8_t, persistence::GroupPairIndex::kMaximumSerializedBytes> staged_{};
    std::array<char, 16> staged_key_{};
    std::size_t staged_size_{0};
};

// Registers the single live pairing owner. Reset can retire its mount even
// before the registration call.
bool register_pairing_storage(PairingNvsStorage*);

}  // namespace opentrail::target::heltec_v4_bench
