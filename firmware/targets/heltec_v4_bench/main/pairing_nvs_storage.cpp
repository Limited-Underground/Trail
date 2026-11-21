#include "pairing_nvs_storage.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "opentrail/durable_group_roster.hpp"

namespace opentrail::target::heltec_v4_bench {
namespace {
constexpr std::size_t kPairingScope = 0, kRosterScope = 1, kCatalogScope = 2;
constexpr std::size_t kScopeCount = 3;
constexpr auto& kPartition = PairingNvsStorage::kPartition;
using Catalog = persistence::GroupPairIndex;
using Roster = identity::roster::DurableGroupRoster;

namespace budget = board_storage_budget;
static_assert(Catalog::kMaximumGroups <= 10);
static_assert(Roster::kStorageRecordBytes <= Catalog::kMaximumSerializedBytes);
static_assert(Catalog::kMaximumSerializedBytes <= 2048);
static_assert(static_cast<unsigned>(PairingNvsStorage::Domain::outbound_counter_state) ==
              pairing_key::kPacketLaneDomain);

constexpr char kSealKey[] = "f0000000017d0s0";
constexpr std::array<std::uint8_t, budget::kFormatSealBytes> kSeal{'O', 'T', 'S', '1'};
static_assert(pairing_key::slot(kSealKey, PairingNvsStorage::kCapacity) <
              pairing_key::table_size(PairingNvsStorage::kCapacity));

struct Scope {
    const char* name;
    std::size_t record_bytes;
    std::size_t budget;
};
constexpr std::array<Scope, kScopeCount> kScopes{{
    {PairingNvsStorage::kNamespace, persistence::kPersistentSlotBytes,
        budget::kPairingEntries + budget::kFormatSealEntries},
    {"ot_group_rost", Roster::kStorageRecordBytes, budget::kRosterEntries},
    {"ot_group_idx", Catalog::kMaximumSerializedBytes, budget::kCatalogEntries},
}};

// The existing storage lock protects this registry, the partition mount, and
// each namespace handle. The pairing pointer covers construction through
// retirement, including the interval before register_pairing_storage().
PairingNvsStorage* g_pairing_owner = nullptr;
bool g_pairing_registered = false;
std::array<GroupNvsStorage*, 2> g_group_owners{};
std::array<bool, kScopeCount> g_namespaces{};
constexpr std::size_t kGroupKeys = Catalog::kMaximumGroups + 1;
std::array<bool, kGroupKeys> g_group_records{};
std::size_t g_mount_users = 0;
bool g_reset_retired = false;
bool g_unmount_uncertain = false;
bool g_media_uncertain = false;
bool g_sealed = false;
std::size_t g_pairing_tuples = 0;
std::size_t g_d112_tuples = 0;
constexpr unsigned kLegacyLaneFirst = static_cast<unsigned>(PairingNvsStorage::Store::packet_data_tx);
constexpr unsigned kLaneCount = 4;
std::optional<PairingNvsEntryUsage> g_entry_usage;

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

const esp_partition_t* partition() {
    const auto* p = esp_partition_find_first(static_cast<esp_partition_type_t>(0x40),
                                             static_cast<esp_partition_subtype_t>(0x00), kPartition);
    if (p == nullptr || std::strcmp(p->label, kPartition) != 0 ||
        p->address != PairingNvsStorage::kPartitionAddress || p->size != PairingNvsStorage::kPartitionBytes ||
        p->encrypted || p->readonly) {
        return nullptr;
    }
    return p;
}

std::size_t scope_named(const char* name) {
    if (name != nullptr) {
        for (std::size_t i = 0; i < kScopes.size(); ++i)
            if (std::strcmp(name, kScopes[i].name) == 0) return i;
    }
    return kScopeCount;
}

bool pairing_key_valid(const pairing_key::Key& key) {
    return pairing_key::slot(key, PairingNvsStorage::kCapacity) < pairing_key::table_size(PairingNvsStorage::kCapacity);
}

std::size_t group_record_slot(std::size_t scope, const char* key) {
    if (key == nullptr) return kGroupKeys;
    if (scope == kCatalogScope)
        return std::strcmp(key, "catalog") == 0 ? Catalog::kMaximumGroups : kGroupKeys;
    if (scope == kRosterScope && key[0] == 'g' && key[1] >= '0' &&
        key[1] < '0' + static_cast<int>(Catalog::kMaximumGroups) && key[2] == '\0')
        return static_cast<std::size_t>(key[1] - '0');
    return kGroupKeys;
}

bool partition_stats(nvs_stats_t& stats) {
    return nvs_get_stats(kPartition, &stats) == ESP_OK &&
           stats.total_entries == PairingNvsStorage::kTotalEntries &&
           stats.used_entries < budget::kLimit &&
           stats.namespace_count == static_cast<std::size_t>(std::count(g_namespaces.begin(), g_namespaces.end(), true));
}

bool within_budget(std::size_t scope, nvs_handle_t handle, const nvs_stats_t& stats) {
    std::size_t own = 0;
    return (handle == 0 || nvs_get_used_entry_count(handle, &own) == ESP_OK) &&
           own <= kScopes[scope].budget && own <= stats.used_entries;
}

struct ReadOnlyHandles {
    std::array<nvs_handle_t, kScopeCount> values{};
    ~ReadOnlyHandles() {
        for (const auto handle : values) if (handle != 0) nvs_close(handle);
    }
};

bool valid_entry(const nvs_entry_info_t& info, const ReadOnlyHandles& handles) {
    if (info.type != NVS_TYPE_BLOB ||
        std::memchr(info.namespace_name, '\0', sizeof(info.namespace_name)) == nullptr ||
        std::memchr(info.key, '\0', sizeof(info.key)) == nullptr) return false;
    const auto scope = scope_named(info.namespace_name);
    if (scope == kScopeCount || handles.values[scope] == 0) return false;
    std::size_t size = 0;
    if (nvs_get_blob(handles.values[scope], info.key, nullptr, &size) != ESP_OK ||
        size != kScopes[scope].record_bytes) return false;
    if (scope == kPairingScope) return pairing_key_valid(info.key);
    const auto slot = group_record_slot(scope, info.key);
    if (slot == kGroupKeys || g_group_records[slot]) return false;
    g_group_records[slot] = true;
    return true;
}

void unmount_partition() {
    const auto status = nvs_flash_deinit_partition(kPartition);
    g_unmount_uncertain = status != ESP_OK && status != ESP_ERR_NVS_NOT_INITIALIZED;
    g_namespaces.fill(false);
    g_group_records.fill(false);
    g_media_uncertain = false;
    g_sealed = false;
    g_pairing_tuples = 0;
    g_d112_tuples = 0;
}

bool admits_tuple(const pairing_key::Key& key) {
    return g_pairing_tuples < budget::kPairingTupleCount &&
           (key[0] != 'g' || g_d112_tuples < budget::kD112TupleCount);
}

void record_tuple_change(const pairing_key::Key& key, bool adds) {
    if (adds) {
        ++g_pairing_tuples;
        if (key[0] == 'g') ++g_d112_tuples;
    } else {
        --g_pairing_tuples;
        if (key[0] == 'g') --g_d112_tuples;
    }
}

void release_partition() {
    if (--g_mount_users == 0) unmount_partition();
}

bool open_writable(std::size_t scope, nvs_handle_t& handle) {
    nvs_stats_t stats{};
    if (!partition_stats(stats)) {
        g_media_uncertain = true;
        return false;
    }
    if (!budget::can_write(stats.used_entries, stats.available_entries, !g_namespaces[scope], 0, 0))
        return false;
    if (nvs_open_from_partition(kPartition, kScopes[scope].name, NVS_READWRITE, &handle) != ESP_OK) {
        g_media_uncertain = true;
        return false;
    }
    g_namespaces[scope] = true;
    return true;
}

bool seal_readback(nvs_handle_t handle) {
    std::array<std::uint8_t, kSeal.size()> actual{};
    std::size_t size = actual.size();
    return nvs_get_blob(handle, kSealKey, actual.data(), &size) == ESP_OK && size == actual.size() &&
           actual == kSeal;
}

// Read-only admission audits all prior records. Persist the new schema
// before the first mutation, with both the seal and requested write budgeted.
bool authorize_write(std::size_t scope, nvs_handle_t handle, std::size_t bytes, bool grows) {
    if (g_media_uncertain) return false;
    nvs_stats_t stats{};
    if (!partition_stats(stats) || !within_budget(scope, handle, stats)) {
        g_media_uncertain = true;
        return false;
    }
    const std::size_t namespaces = static_cast<std::size_t>(!g_namespaces[scope]) +
        static_cast<std::size_t>(!g_sealed && scope != kPairingScope && !g_namespaces[kPairingScope]);
    const auto seal_entries = g_sealed ? 0 : budget::kFormatSealEntries;
    const auto payload_entries = bytes ? budget::blob_entries(bytes) : 0;
    if (!budget::can_write(stats.used_entries, stats.available_entries, namespaces,
                           seal_entries + (grows ? payload_entries : 0), seal_entries + payload_entries))
        return false;
    if (g_sealed) return true;

    const bool borrow = scope == kPairingScope && handle != 0;
    nvs_handle_t seal_handle = borrow ? handle : 0;
    if (!borrow && !open_writable(kPairingScope, seal_handle)) return false;
    std::size_t size = 0;
    const bool sealed =
        nvs_get_blob(seal_handle, kSealKey, nullptr, &size) == ESP_ERR_NVS_NOT_FOUND &&
        nvs_set_blob(seal_handle, kSealKey, kSeal.data(), kSeal.size()) == ESP_OK &&
        nvs_commit(seal_handle) == ESP_OK && seal_readback(seal_handle) &&
        partition_stats(stats) && within_budget(kPairingScope, seal_handle, stats) &&
        budget::can_write(stats.used_entries, stats.available_entries, !g_namespaces[scope],
                          grows ? payload_entries : 0, payload_entries);
    if (!borrow) nvs_close(seal_handle);
    if (!sealed) {
        g_media_uncertain = true;
        return false;
    }
    g_sealed = true;
    return true;
}

std::size_t group_scope(GroupNvsStorage::Store store) {
    switch (store) {
    case GroupNvsStorage::Store::roster: return kRosterScope;
    case GroupNvsStorage::Store::catalog: return kCatalogScope;
    }
    return kScopeCount;
}

bool group_address(std::size_t scope, const char* partition_label, const char* namespace_name) {
    return scope < kScopeCount && partition_label != nullptr && std::strcmp(partition_label, kPartition) == 0 &&
           scope_named(namespace_name) == scope;
}
}  // namespace

std::array<PairingNvsStorage::PacketRecord, PairingNvsStorage::kCapacity> PairingNvsStorage::packet_records_{};
std::uint8_t PairingNvsStorage::legacy_floor_{};
std::bitset<pairing_key::table_size(PairingNvsStorage::kCapacity) + 1> PairingNvsStorage::known_{};

bool PairingNvsStorage::admit_partition() {
    known_.reset();
    packet_records_.fill({});
    legacy_floor_ = 0;
    ReadOnlyHandles handles;
    for (std::size_t scope = 0; scope < kScopes.size(); ++scope) {
        const auto status = nvs_open_from_partition(kPartition, kScopes[scope].name, NVS_READONLY,
                                                    &handles.values[scope]);
        if (status != ESP_OK && status != ESP_ERR_NVS_NOT_FOUND) return false;
        g_namespaces[scope] = status == ESP_OK;
    }
    // The iterator cannot see unknown empty namespaces. Exact readonly
    // namespace handles and the partition count account for them.
    nvs_stats_t stats{};
    if (!partition_stats(stats)) return false;
    for (std::size_t scope = 0; scope < kScopes.size(); ++scope)
        if (!within_budget(scope, handles.values[scope], stats)) return false;

    nvs_iterator_t iterator = nullptr;
    auto status = nvs_entry_find(kPartition, nullptr, NVS_TYPE_ANY, &iterator);
    while (status == ESP_OK) {
        nvs_entry_info_t info{};
        if (nvs_entry_info(iterator, &info) != ESP_OK || !valid_entry(info, handles)) break;
        if (scope_named(info.namespace_name) == kPairingScope) {
            const auto entry = pairing_key::slot(info.key, kCapacity);
            if (known_[entry]) break;
            known_[entry] = true;
            if (std::strcmp(info.key, kSealKey) == 0) {
                if (!seal_readback(handles.values[kPairingScope])) break;
                g_sealed = true;
            } else {
                if (!admits_tuple(info.key)) break;
                record_tuple_change(info.key, true);
            }
        }
        status = nvs_entry_next(&iterator);
    }
    nvs_release_iterator(iterator);
    return status == ESP_ERR_NVS_NOT_FOUND && inventory_packets(handles.values[kPairingScope]);
}

bool PairingNvsStorage::acquire_partition() {
    if (g_reset_retired || g_unmount_uncertain || g_media_uncertain) return false;
    if (g_mount_users == 0) {
        const auto* p = partition();
        if (p == nullptr || nvs_flash_init_partition_ptr(p) != ESP_OK) return false;
        if (!admit_partition()) {
            unmount_partition();
            return false;
        }
    }
    ++g_mount_users;
    return true;
}

std::optional<PairingNvsEntryUsage> pairing_nvs_entry_usage() {
    Guard guard;
    return g_entry_usage;
}

PairingNvsStorage::PairingNvsStorage()
    : views_{{{*this, Store::identity}, {*this, Store::journal},
              {*this, Store::binding}, {*this, Store::boot}, {*this, Store::completion},
              {*this, Store::f5_state}, {*this, Store::f5_state_tail}}} {
    Guard guard;
    if (g_pairing_owner != nullptr || !acquire_partition()) return;
    mounted_ = true;
    g_pairing_owner = this;
    good_ = open_writable(kPairingScope, handle_);
    if (good_ && !budget_ok()) good_ = false;
}

PairingNvsStorage::~PairingNvsStorage() { retire(); }

bool PairingNvsStorage::ready() const { Guard guard; return good_ && !g_media_uncertain; }

void PairingNvsStorage::retire() {
    Guard guard;
    good_ = false;
    if (pending_) g_media_uncertain = true;
    cache_.clear();
    wipe(pending_bytes_);
    if (handle_ != 0) { nvs_close(handle_); handle_ = 0; }
    if (mounted_) { release_partition(); mounted_ = false; }
    if (g_pairing_owner == this) {
        g_pairing_owner = nullptr;
        g_pairing_registered = false;
    }
}

bool register_pairing_storage(PairingNvsStorage* storage) {
    Guard guard;
    if (g_reset_retired || storage == nullptr || storage != g_pairing_owner ||
        g_pairing_registered || !storage->ready()) return false;
    g_pairing_registered = true;
    return true;
}

bool retire_pairing_storage_for_reset() {
    Guard guard;
    g_reset_retired = true;
    g_entry_usage.reset();
    if (g_pairing_owner != nullptr) g_pairing_owner->retire();
    for (auto* owner : g_group_owners) if (owner != nullptr) owner->retire();
    return g_mount_users == 0 && !g_unmount_uncertain;
}

bool PairingNvsStorage::f5_packet_footprint_absent() {
    Guard guard;
    if (!good_ || g_media_uncertain) return false;
    for (unsigned slot = 1; slot <= kCapacity; ++slot) {
        if (packet_records_[slot - 1].state != PacketState::absent ||
            has_packet_lanes(slot, kLegacyLaneFirst)) return false;
    }
    return true;
}

PairingNvsStorage::Bytes PairingNvsStorage::encode_packet_record(unsigned slot, const PacketRecord& record) {
    Bytes bytes{};
    std::memcpy(bytes.data(), "OTP1", 4);
    bytes[4] = static_cast<std::uint8_t>(record.state);
    bytes[5] = static_cast<std::uint8_t>(slot);
    bytes[6] = record.legacy_floor;
    for (unsigned i = 0; i < 8; ++i) {
        bytes[15 - i] = static_cast<std::uint8_t>(record.generation >> (8 * i));
    }
    std::copy(record.owner.begin(), record.owner.end(), bytes.begin() + 16);
    return bytes;
}

bool PairingNvsStorage::decode_packet_record(unsigned slot, const Bytes& bytes, PacketRecord& record) {
    PacketRecord decoded{};
    decoded.state = static_cast<PacketState>(bytes[4]);
    decoded.legacy_floor = bytes[6];
    for (unsigned i = 8; i < 16; ++i) decoded.generation = (decoded.generation << 8) | bytes[i];
    std::copy_n(bytes.begin() + 16, decoded.owner.size(), decoded.owner.begin());
    if (decoded.state < PacketState::preparing || decoded.state > PacketState::retired ||
        !decoded.generation || decoded.generation > std::numeric_limits<std::uint32_t>::max() ||
        decoded.owner == PacketOwner{} || decoded.legacy_floor > kCapacity ||
        bytes != encode_packet_record(slot, decoded)) return false;
    record = decoded;
    return true;
}

bool PairingNvsStorage::has_packet_lanes(unsigned slot, unsigned first_namespace) {
    for (unsigned lane = first_namespace; lane < first_namespace + kLaneCount; ++lane) {
        for (unsigned bank = 0; bank < pairing_key::kSlots; ++bank) {
            pairing_key::Key key{};
            if (!key_for(slot, lane, Domain::outbound_counter_state, bank, key) ||
                known_[pairing_key::slot(key, kCapacity)]) return true;
        }
    }
    return false;
}

bool PairingNvsStorage::inventory_packets(nvs_handle_t handle) {
    for (unsigned slot = 1; slot <= kCapacity; ++slot) {
        pairing_key::Key key{};
        if (!key_for(slot, pairing_key::kPacketClaim, static_cast<Domain>(0), 0, key)) return false;
        auto& record = packet_records_[slot - 1];
        if (known_[pairing_key::slot(key, kCapacity)]) {
            Bytes bytes{};
            std::size_t size = bytes.size();
            if (nvs_get_blob(handle, key, bytes.data(), &size) != ESP_OK || size != bytes.size() ||
                !decode_packet_record(slot, bytes, record)) return false;
            legacy_floor_ = std::max(legacy_floor_, record.legacy_floor);
            for (unsigned prior = 0; prior < slot - 1; ++prior) {
                if (packet_records_[prior].owner == record.owner) return false;
            }
        }
        const bool legacy = has_packet_lanes(slot, kLegacyLaneFirst);
        const bool reclaimed = has_packet_lanes(slot, pairing_key::kReclaimedLaneFirst);
        if ((reclaimed && record.state == PacketState::absent) ||
            (legacy && (reclaimed || record.state == PacketState::ready || record.state == PacketState::retired)))
            return false;
    }
    return true;
}

bool PairingNvsStorage::save_packet_record(unsigned slot, const PacketRecord& record) {
    if (!good_ || g_media_uncertain) return false;
    if (pending_) { (void)fault(); return false; }
    pairing_key::Key key{};
    if (!key_for(slot, pairing_key::kPacketClaim, static_cast<Domain>(0), 0, key)) return false;

    const auto& previous = packet_records_[slot - 1];
    Bytes actual{};
    std::size_t size = actual.size();
    const auto status = nvs_get_blob(handle_, key, actual.data(), &size);
    const bool adds = previous.state == PacketState::absent;
    if (adds) {
        if (status != ESP_ERR_NVS_NOT_FOUND) { (void)fault(); return false; }
        if (!admits_tuple(key)) return false;
    } else if (status != ESP_OK || size != actual.size() || actual != encode_packet_record(slot, previous)) {
        (void)fault();
        return false;
    }

    const auto bytes = encode_packet_record(slot, record);
    if (!authorize_mutation(bytes.size(), adds)) return false;
    size = actual.size();
    if (nvs_set_blob(handle_, key, bytes.data(), bytes.size()) != ESP_OK ||
        nvs_commit(handle_) != ESP_OK ||
        nvs_get_blob(handle_, key, actual.data(), &size) != ESP_OK ||
        size != actual.size() || actual != bytes || !budget_ok()) {
        (void)fault();
        return false;
    }
    known_[index(key)] = true;
    if (adds) record_tuple_change(key, true);
    packet_records_[slot - 1] = record;
    legacy_floor_ = std::max(legacy_floor_, record.legacy_floor);
    return true;
}

bool PairingNvsStorage::clear_packet_lanes(unsigned slot, unsigned first_namespace) {
    for (unsigned lane = first_namespace; lane < first_namespace + kLaneCount; ++lane) {
        for (unsigned bank = 0; bank < pairing_key::kSlots; ++bank) {
            pairing_key::Key key{};
            if (!key_for(slot, lane, Domain::outbound_counter_state, bank, key)) return false;
            const auto entry = index(key);
            std::size_t size = 0;
            const auto status = nvs_get_blob(handle_, key, nullptr, &size);
            if (!known_[entry] && status == ESP_ERR_NVS_NOT_FOUND) continue;
            if (!known_[entry] || status != ESP_OK || size != persistence::kPersistentSlotBytes ||
                nvs_erase_key(handle_, key) != ESP_OK || nvs_commit(handle_) != ESP_OK ||
                nvs_get_blob(handle_, key, nullptr, &size) != ESP_ERR_NVS_NOT_FOUND) {
                (void)fault();
                return false;
            }
            cache_.drop(key);
            known_[entry] = false;
            record_tuple_change(key, false);
        }
    }
    return true;
}

std::optional<PairingNvsStorage::PacketClaim> PairingNvsStorage::claim_packets(
    const PacketOwner& owner, std::uint64_t generation, std::uint64_t legacy_floor) {
    Guard guard;
    if (!good_ || g_media_uncertain || owner == PacketOwner{} || !generation || generation > std::numeric_limits<std::uint32_t>::max() ||
        legacy_floor > kCapacity || legacy_floor >= generation) return std::nullopt;
    const auto certified_floor = std::max(legacy_floor_, static_cast<std::uint8_t>(legacy_floor));
    unsigned owned = 0, legacy = 0, free = 0;
    for (unsigned slot = 1; slot <= kCapacity; ++slot) {
        const auto& previous = packet_records_[slot - 1];
        if (previous.owner == owner) {
            if (previous.generation >= generation) return std::nullopt;
            owned = slot;
        } else if (previous.state == PacketState::absent && has_packet_lanes(slot, kLegacyLaneFirst)) {
            if (!legacy && slot <= certified_floor) legacy = slot;
        } else if (!free && (previous.state == PacketState::absent || previous.state == PacketState::retired)) {
            free = slot;
        }
    }
    const unsigned available = legacy && legacy_floor ? legacy : free;
    const unsigned slot = owned ? owned : (available ? available : legacy);
    if (!slot) return std::nullopt;

    // Keep the claim durable across every erase. A restart needs a new
    // reservation, and only the same owner can finish its interrupted cleanup.
    PacketRecord next{owner, generation, PacketState::preparing, certified_floor};
    if (!save_packet_record(slot, next) || !clear_packet_lanes(slot, kLegacyLaneFirst) ||
        !clear_packet_lanes(slot, pairing_key::kReclaimedLaneFirst)) return std::nullopt;
    next.state = PacketState::ready;
    if (!save_packet_record(slot, next)) return std::nullopt;
    return PacketClaim(owner, generation, slot);
}

bool PairingNvsStorage::owns_packet_claim(const PacketClaim& claim) const {
    if (!good_ || g_media_uncertain || !claim.slot_ || claim.slot_ > kCapacity) return false;
    const auto& current = packet_records_[claim.slot_ - 1];
    return current.state == PacketState::ready && current.generation == claim.generation_ &&
           current.owner == claim.owner_;
}

bool PairingNvsStorage::retire_packets(const PacketClaim& claim) {
    Guard guard;
    if (!owns_packet_claim(claim)) return false;
    auto record = packet_records_[claim.slot_ - 1];
    record.state = PacketState::retired;
    return save_packet_record(claim.slot_, record);
}

unsigned PairingNvsStorage::PacketView::lane_namespace() const {
    const auto lane = static_cast<unsigned>(store_);
    if (lane < kLegacyLaneFirst || lane >= kLegacyLaneFirst + kLaneCount ||
        !owner_.owns_packet_claim(claim_)) return 0;
    return pairing_key::kReclaimedLaneFirst + lane - kLegacyLaneFirst;
}

persistence::StorageReadResult PairingNvsStorage::PacketView::read_slot(
    Domain domain, std::size_t bank, persistence::MutableStorageByteView out) {
    Guard guard;
    const auto lane = lane_namespace();
    if (!lane) return {Error::invalid_argument, 0};
    return owner_.read_tuple(claim_.slot_, lane, domain, bank, out);
}

PairingNvsStorage::Error PairingNvsStorage::PacketView::erase_slot(Domain domain, std::size_t bank) {
    Guard guard;
    const auto lane = lane_namespace();
    if (!lane) return Error::invalid_argument;
    return owner_.erase_tuple(claim_.slot_, lane, domain, bank);
}

PairingNvsStorage::Error PairingNvsStorage::PacketView::write_slot(
    Domain domain, std::size_t bank, std::size_t offset, persistence::StorageByteView in) {
    Guard guard;
    const auto lane = lane_namespace();
    if (!lane) return Error::invalid_argument;
    return owner_.write_tuple(claim_.slot_, lane, domain, bank, offset, in);
}

PairingNvsStorage::Error PairingNvsStorage::PacketView::sync_slot(Domain domain, std::size_t bank) {
    Guard guard;
    const auto lane = lane_namespace();
    if (!lane) return Error::invalid_argument;
    return owner_.sync_tuple(claim_.slot_, lane, domain, bank);
}

bool PairingNvsStorage::budget_ok() {
    Guard guard;
    if (!good_ || g_media_uncertain) return false;
    nvs_stats_t stats{};
    const auto started_us = esp_timer_get_time();
    const bool read_ok = partition_stats(stats) && within_budget(kPairingScope, handle_, stats);
    ++budget_checks_;
    const auto observed_us = esp_timer_get_time();
    budget_us_ += static_cast<std::uint64_t>(observed_us - started_us);
    if (!read_ok) {
        (void)fault();
        return false;
    }
    if (stats.free_entries == stats.total_entries - stats.used_entries) {
        g_entry_usage = PairingNvsEntryUsage{
            static_cast<std::uint64_t>(observed_us / 1000),
            static_cast<std::uint32_t>(stats.used_entries),
            static_cast<std::uint32_t>(stats.free_entries),
            static_cast<std::uint32_t>(stats.total_entries)};
    }
    return true;
}

bool PairingNvsStorage::authorize_mutation(std::size_t bytes, bool grows) {
    const auto started_us = esp_timer_get_time();
    const bool authorized = authorize_write(kPairingScope, handle_, bytes, grows);
    ++budget_checks_;
    budget_us_ += static_cast<std::uint64_t>(esp_timer_get_time() - started_us);
    return authorized;
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

bool PairingNvsStorage::load(const pairing_key::Key& key, Bytes& out) {
    if (!good_ || g_media_uncertain) return false;
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
    if (status != ESP_OK || !known_[index(key)] || size != out.size()) { (void)fault(); return false; }
    if (nvs_get_blob(handle_, key, out.data(), &size) != ESP_OK || size != out.size()) {
        (void)fault();
        return false;
    }
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
    if (!good_ || g_media_uncertain) return false;
    if (pending_ && std::strcmp(key, pending_key_.data()) != 0) { (void)fault(); return false; }
    return true;
}

PairingNvsStorage::Error PairingNvsStorage::erase_tuple(std::uint64_t g, unsigned n, Domain d, std::size_t s) {
    Guard guard;
    char key[16]{};
    if (!key_for(g, n, d, s, key)) return Error::invalid_argument;
    if (!start_mutation(key) || !authorize_mutation(0, false)) return Error::io_failure;
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
    const bool formerly_present = known_[index(key)];
    const bool expect_present = pending_ ? !pending_erased_ : formerly_present;
    std::size_t size = 0;
    const auto status = nvs_get_blob(handle_, key, nullptr, &size);
    if (expect_present ? (status != ESP_OK || size != bytes.size()) : status != ESP_ERR_NVS_NOT_FOUND) {
        wipe(bytes);
        return fault();
    }
    if (!formerly_present && !admits_tuple(key)) {
        wipe(bytes);
        return Error::io_failure;
    }
    for (std::size_t i = 0; i < in.size; ++i) {
        if ((bytes[offset + i] & in.data[i]) != in.data[i]) { wipe(bytes); return Error::write_requires_erase; }
    }
    if (!authorize_mutation(bytes.size(), !expect_present)) {
        wipe(bytes);
        return Error::io_failure;
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
    if (!good_ || g_media_uncertain) return Error::io_failure;
    if (!pending_) return Error::none;
    if (std::strcmp(key, pending_key_.data()) != 0 || nvs_commit(handle_) != ESP_OK) return fault();
    pending_ = false;
    Bytes actual{};
    const auto entry = index(key);
    const bool formerly_present = known_[entry];
    std::size_t size = actual.size();
    const auto status = nvs_get_blob(handle_, key, actual.data(), &size);
    const bool observed = pending_erased_
        ? status == ESP_ERR_NVS_NOT_FOUND
        : status == ESP_OK && size == actual.size();
    if (pending_erased_) actual.fill(0xff);
    const bool ok = observed && actual == pending_bytes_ && budget_ok();
    if (ok) {
        if (formerly_present == pending_erased_) record_tuple_change(key, !pending_erased_);
        known_[entry] = !pending_erased_;
        cache_.put(key, actual);
    }
    wipe(actual);
    wipe(pending_bytes_);
    return ok ? Error::none : fault();
}

PairingNvsStorage::Error PairingNvsStorage::fault() {
    g_media_uncertain = true;
    good_ = false;
    cache_.clear();
    wipe(pending_bytes_);
    return Error::io_failure;
}

GroupNvsStorage::~GroupNvsStorage() { retire(); }

bool GroupNvsStorage::ready() {
    Guard guard;
    if (retired_ || g_reset_retired || g_media_uncertain) return false;
    if (mounted_) return good_;
    const auto scope = group_scope(store_);
    if (scope == kScopeCount) return false;
    auto& owner = g_group_owners[scope - 1];
    if (owner != nullptr || !PairingNvsStorage::acquire_partition()) return false;
    mounted_ = true;
    owner = this;
    const auto status = nvs_open_from_partition(kPartition, kScopes[scope].name, NVS_READONLY, &handle_);
    good_ = status == (g_namespaces[scope] ? ESP_OK : ESP_ERR_NVS_NOT_FOUND);
    return good_;
}

void GroupNvsStorage::retire() {
    Guard guard;
    good_ = false;
    retired_ = true;
    staged_size_ = 0;
    staged_.fill(0);
    staged_key_.fill(0);
    if (handle_ != 0) { nvs_close(handle_); handle_ = 0; }
    if (mounted_) {
        g_group_owners[group_scope(store_) - 1] = nullptr;
        release_partition();
        mounted_ = false;
    }
}

GroupNvsStorage::Error GroupNvsStorage::read_blob(const char* partition_label, const char* namespace_name,
    const char* key, std::uint8_t* data, std::size_t capacity, std::size_t& actual) {
    Guard guard;
    actual = 0;
    const auto scope = group_scope(store_);
    const auto slot = group_record_slot(scope, key);
    if (!group_address(scope, partition_label, namespace_name) || slot == kGroupKeys ||
        data == nullptr || capacity < kScopes[scope].record_bytes) return Error::invalid_argument;
    if (!good_ || g_media_uncertain) return Error::io_failure;
    if (handle_ == 0) return Error::not_found;
    std::size_t size = 0;
    const auto status = nvs_get_blob(handle_, key, nullptr, &size);
    if (status == ESP_ERR_NVS_NOT_FOUND && !g_group_records[slot]) return Error::not_found;
    if (status != ESP_OK || !g_group_records[slot] || size != kScopes[scope].record_bytes ||
        nvs_get_blob(handle_, key, data, &size) != ESP_OK || size != kScopes[scope].record_bytes) return fault();
    actual = size;
    return Error::none;
}

GroupNvsStorage::Error GroupNvsStorage::write_blob(const char* partition_label, const char* namespace_name,
    const char* key, const std::uint8_t* data, std::size_t size) {
    Guard guard;
    const auto scope = group_scope(store_);
    if (!group_address(scope, partition_label, namespace_name) || group_record_slot(scope, key) == kGroupKeys ||
        data == nullptr || size != kScopes[scope].record_bytes) return Error::invalid_argument;
    if (!good_ || g_media_uncertain) return Error::io_failure;
    if (staged_size_ != 0) return fault();
    std::copy_n(data, size, staged_.begin());
    std::copy_n(key, std::strlen(key) + 1, staged_key_.begin());
    staged_size_ = size;
    return Error::none;
}

GroupNvsStorage::Error GroupNvsStorage::erase_key(const char*, const char*, const char*) {
    // Distrust is durable group state. Group metadata is erased only by the
    // existing factory-reset owner after retiring every partition handle.
    return Error::invalid_argument;
}

GroupNvsStorage::Error GroupNvsStorage::commit(const char* partition_label, const char* namespace_name) {
    Guard guard;
    const auto scope = group_scope(store_);
    if (!group_address(scope, partition_label, namespace_name)) return Error::invalid_argument;
    if (!good_ || g_media_uncertain) return Error::io_failure;
    if (staged_size_ == 0) return Error::invalid_argument;

    const auto slot = group_record_slot(scope, staged_key_.data());
    if (slot == kGroupKeys) return fault();
    std::size_t prior_size = 0;
    const auto prior = handle_ == 0 ? ESP_ERR_NVS_NOT_FOUND
        : nvs_get_blob(handle_, staged_key_.data(), nullptr, &prior_size);
    if (g_group_records[slot] ? (prior != ESP_OK || prior_size != staged_size_)
                              : prior != ESP_ERR_NVS_NOT_FOUND) return fault();

    if (!authorize_write(scope, handle_, staged_size_, !g_group_records[slot])) {
        staged_size_ = 0;
        staged_.fill(0);
        staged_key_.fill(0);
        return Error::io_failure;
    }
    if (!writable_) {
        nvs_handle_t writable{};
        if (!open_writable(scope, writable)) return fault();
        if (handle_ != 0) nvs_close(handle_);
        handle_ = writable;
        writable_ = true;
    }
    nvs_stats_t stats{};
    std::array<std::uint8_t, Catalog::kMaximumSerializedBytes> actual{};
    std::size_t size = actual.size();
    if (nvs_set_blob(handle_, staged_key_.data(), staged_.data(), staged_size_) != ESP_OK ||
        nvs_commit(handle_) != ESP_OK ||
        nvs_get_blob(handle_, staged_key_.data(), actual.data(), &size) != ESP_OK || size != staged_size_ ||
        !std::equal(actual.begin(), actual.begin() + size, staged_.begin())) return fault();
    if (!partition_stats(stats)) {
        g_media_uncertain = true;
        return fault();
    }
    if (!within_budget(scope, handle_, stats)) return fault();
    g_group_records[slot] = true;
    staged_size_ = 0;
    return Error::none;
}

GroupNvsStorage::Error GroupNvsStorage::fault() {
    good_ = false;
    staged_size_ = 0;
    staged_.fill(0);
    staged_key_.fill(0);
    return Error::io_failure;
}

}  // namespace opentrail::target::heltec_v4_bench
