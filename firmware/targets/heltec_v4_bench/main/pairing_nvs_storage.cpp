#include "pairing_nvs_storage.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"

namespace opentrail::target::heltec_v4_bench {
namespace {
PairingNvsStorage* g_live = nullptr;
bool g_reset_retired = false;
constexpr unsigned kLegacyLaneFirst = static_cast<unsigned>(PairingNvsStorage::Store::packet_data_tx);
constexpr unsigned kLaneCount = 4;

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
              {*this, Store::binding}, {*this, Store::boot}, {*this, Store::completion},
              {*this, Store::f5_state}, {*this, Store::f5_state_tail}}} {
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

bool PairingNvsStorage::ready() const { Guard guard; return good_; }

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

bool PairingNvsStorage::f5_packet_footprint_absent() {
    Guard guard;
    if (!good_) return false;
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
        for (unsigned domain = 0; domain < pairing_key::kDomains; ++domain) {
            for (unsigned bank = 0; bank < pairing_key::kSlots; ++bank) {
                pairing_key::Key key{};
                if (!key_for(slot, lane, static_cast<Domain>(domain), bank, key) || known_[index(key)])
                    return true;
            }
        }
    }
    return false;
}

bool PairingNvsStorage::inventory_packets() {
    for (unsigned slot = 1; slot <= kCapacity; ++slot) {
        pairing_key::Key key{};
        if (!key_for(slot, pairing_key::kPacketClaim, static_cast<Domain>(0), 0, key)) return false;
        auto& record = packet_records_[slot - 1];
        if (known_[index(key)]) {
            Bytes bytes{};
            std::size_t size = bytes.size();
            if (nvs_get_blob(handle_, key, bytes.data(), &size) != ESP_OK || size != bytes.size() ||
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
    if (!good_ || !budget_ok()) return false;
    if (pending_) { (void)fault(); return false; }
    pairing_key::Key key{};
    if (!key_for(slot, pairing_key::kPacketClaim, static_cast<Domain>(0), 0, key)) return false;

    const auto& previous = packet_records_[slot - 1];
    Bytes actual{};
    std::size_t size = actual.size();
    const auto status = nvs_get_blob(handle_, key, actual.data(), &size);
    if (previous.state == PacketState::absent) {
        if (status != ESP_ERR_NVS_NOT_FOUND) { (void)fault(); return false; }
        std::size_t entries = 0;
        if (nvs_get_used_entry_count(handle_, &entries) != ESP_OK) { (void)fault(); return false; }
        if (entries > kPairingEntries - 4) return false;
    } else if (status != ESP_OK || size != actual.size() || actual != encode_packet_record(slot, previous)) {
        (void)fault();
        return false;
    }

    const auto bytes = encode_packet_record(slot, record);
    size = actual.size();
    if (nvs_set_blob(handle_, key, bytes.data(), bytes.size()) != ESP_OK ||
        nvs_commit(handle_) != ESP_OK ||
        nvs_get_blob(handle_, key, actual.data(), &size) != ESP_OK ||
        size != actual.size() || actual != bytes || !budget_ok()) {
        (void)fault();
        return false;
    }
    known_[index(key)] = true;
    packet_records_[slot - 1] = record;
    legacy_floor_ = std::max(legacy_floor_, record.legacy_floor);
    return true;
}

bool PairingNvsStorage::clear_packet_lanes(unsigned slot, unsigned first_namespace) {
    for (unsigned lane = first_namespace; lane < first_namespace + kLaneCount; ++lane) {
        for (unsigned domain = 0; domain < pairing_key::kDomains; ++domain) {
            for (unsigned bank = 0; bank < pairing_key::kSlots; ++bank) {
                pairing_key::Key key{};
                if (!key_for(slot, lane, static_cast<Domain>(domain), bank, key)) return false;
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
            }
        }
    }
    return true;
}

std::optional<PairingNvsStorage::PacketClaim> PairingNvsStorage::claim_packets(
    const PacketOwner& owner, std::uint64_t generation, std::uint64_t legacy_floor) {
    Guard guard;
    if (!good_ || owner == PacketOwner{} || !generation || generation > std::numeric_limits<std::uint32_t>::max() ||
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
    if (!good_ || !claim.slot_ || claim.slot_ > kCapacity) return false;
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
        // Parse a canonical bounded key once. Unknown families/namespaces
        // refuse before any erase/write, including incompatible older F5 keys.
        char observed[16]{};
        std::memcpy(observed,info.key,sizeof observed);
        found = pairing_key::slot(observed,kCapacity) < pairing_key::table_size(kCapacity);
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
    return status == ESP_ERR_NVS_NOT_FOUND && inventory_packets();
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
