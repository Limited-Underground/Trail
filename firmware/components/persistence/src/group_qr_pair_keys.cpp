#include "opentrail/group_qr_pair_keys.hpp"

#include <algorithm>

#include "opentrail/configuration_store.hpp"

namespace opentrail::persistence {
namespace {
using Error = GroupQrPairKeyError;
using KvError = PersistentKvBackendError;
constexpr char kPartition[] = "ot_state";
constexpr std::array<std::uint8_t, 5> kHeader{'O', 'T', 'G', 'K', 1};
constexpr std::size_t kGroupOffset = 8 + identity::roster::kFingerprintBytes;
constexpr std::size_t kEntriesOffset = kGroupOffset + 8;
constexpr std::size_t kChecksumOffset = GroupQrPairKeys::kRecordBytes - 4;

template <std::size_t N>
bool nonzero(const std::array<std::uint8_t, N>& value) {
    return std::any_of(value.begin(), value.end(), [](std::uint8_t b) { return b != 0; });
}

// Key bytes are cleared through a volatile pointer so the store is not elided.
void wipe(void* data, std::size_t size) {
    volatile auto* p = static_cast<volatile std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) p[i] = 0;
}

void put_u(std::uint8_t*& p, std::uint64_t value, std::size_t bytes) {
    for (std::size_t i = 0; i < bytes; ++i) *p++ = static_cast<std::uint8_t>(value >> (8 * i));
}
std::uint64_t get_u(const std::uint8_t*& p, std::size_t bytes) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < bytes; ++i) value |= static_cast<std::uint64_t>(*p++) << (8 * i);
    return value;
}
template <std::size_t N>
void put_a(std::uint8_t*& p, const std::array<std::uint8_t, N>& value) { p = std::copy(value.begin(), value.end(), p); }
template <std::size_t N>
void get_a(const std::uint8_t*& p, std::array<std::uint8_t, N>& value) { std::copy_n(p, N, value.begin()); p += N; }

bool entry_valid(const GroupQrPairKeys::Entry& e, const identity::roster::PublicKey& local) {
    return nonzero(e.peer) && e.peer != local && e.epoch != 0 && nonzero(e.transaction) && nonzero(e.transmit) &&
           nonzero(e.receive) && e.transmit != e.receive;
}

bool same_entry(const GroupQrPairKeys::Entry& a, const GroupQrPairKeys::Entry& b) {
    return a.peer == b.peer && a.epoch == b.epoch && a.transaction == b.transaction && a.transmit == b.transmit &&
           a.receive == b.receive;
}
} // namespace

void GroupQrPairKeys::drop() {
    wipe(groups_.data(), sizeof(groups_));
    groups_ = {};
    loaded_ = false;
}

void GroupQrPairKeys::encode(const Group& group, Record& out) const {
    out.fill(0);
    std::copy(kHeader.begin(), kHeader.end(), out.begin());
    std::copy(local_.begin(), local_.end(), out.begin() + 8);
    auto* g = out.data() + kGroupOffset;
    put_u(g, group.id, 8);
    auto* p = out.data() + kEntriesOffset;
    for (const auto& slot : group.peers) {
        if (slot.used) {
            auto* q = p;
            *q++ = 1;
            put_a(q, slot.entry.peer);
            put_u(q, slot.entry.epoch, 4);
            put_a(q, slot.entry.transaction);
            put_a(q, slot.entry.transmit);
            put_a(q, slot.entry.receive);
        }
        p += kEntryBytes;
    }
    auto* c = out.data() + kChecksumOffset;
    put_u(c, configuration_crc32({out.data(), kChecksumOffset}), 4);
}

bool GroupQrPairKeys::decode(const Record& in, Group& group) const {
    const auto* c = in.data() + kChecksumOffset;
    if (!std::equal(kHeader.begin(), kHeader.end(), in.begin()) || in[5] || in[6] || in[7] ||
        get_u(c, 4) != configuration_crc32({in.data(), kChecksumOffset}) ||
        !std::equal(local_.begin(), local_.end(), in.begin() + 8)) return false;
    Group staged{};
    staged.present = true;
    const auto* g = in.data() + kGroupOffset;
    staged.id = get_u(g, 8);
    const auto* p = in.data() + kEntriesOffset;
    for (auto& slot : staged.peers) {
        if (p[0] > 1) return false;
        if (p[0] == 1) {
            const auto* q = p + 1;
            slot.used = true;
            get_a(q, slot.entry.peer);
            slot.entry.epoch = static_cast<std::uint32_t>(get_u(q, 4));
            get_a(q, slot.entry.transaction);
            get_a(q, slot.entry.transmit);
            get_a(q, slot.entry.receive);
            if (staged.id == 0 || !entry_valid(slot.entry, local_)) return false;
        } else if (std::any_of(p, p + kEntryBytes, [](std::uint8_t b) { return b != 0; })) {
            return false;
        }
        p += kEntryBytes;
    }
    for (std::size_t i = 0; i < staged.peers.size(); ++i)
        for (std::size_t j = i + 1; j < staged.peers.size(); ++j)
            if (staged.peers[i].used && staged.peers[j].used && staged.peers[i].entry.peer == staged.peers[j].entry.peer)
                return false;
    group = staged;
    wipe(staged.peers.data(), sizeof(staged.peers));
    return true;
}

GroupQrPairKeyError GroupQrPairKeys::reopen() {
    drop();
    if (!nonzero(local_)) return Error::invalid;
    bool any = false;
    Groups loaded{};
    for (std::size_t i = 0; i < kGroups; ++i) {
        Record bytes{};
        std::size_t size = 0;
        const auto read = backend_.read_blob(kPartition, kNamespace, kKeys[i], bytes.data(), bytes.size(), size);
        if (read == KvError::not_found) continue;
        if (read != KvError::none) { wipe(bytes.data(), bytes.size()); wipe(loaded.data(), sizeof(loaded)); return Error::storage_failure; }
        const bool ok = size == bytes.size() && decode(bytes, loaded[i]);
        wipe(bytes.data(), bytes.size());
        if (!ok) { wipe(loaded.data(), sizeof(loaded)); return Error::invalid_record; }
        any = true;
    }
    for (std::size_t i = 0; i < kGroups; ++i)
        for (std::size_t j = i + 1; j < kGroups; ++j)
            if (loaded[i].id != 0 && loaded[i].id == loaded[j].id) { wipe(loaded.data(), sizeof(loaded)); return Error::invalid_record; }
    groups_ = loaded;
    wipe(loaded.data(), sizeof(loaded));
    loaded_ = true;
    return any ? Error::none : Error::missing;
}

GroupQrPairKeyError GroupQrPairKeys::write(std::size_t index, const Group& group) {
    Record bytes{}, readback{};
    encode(group, bytes);
    drop();  // any failure from here may have applied the record
    std::size_t size = 0;
    const bool ok = backend_.write_blob(kPartition, kNamespace, kKeys[index], bytes.data(), bytes.size()) ==
                        KvError::none &&
                    backend_.commit(kPartition, kNamespace) == KvError::none &&
                    backend_.read_blob(kPartition, kNamespace, kKeys[index], readback.data(), readback.size(), size) ==
                        KvError::none &&
                    size == readback.size() && readback == bytes;
    wipe(bytes.data(), bytes.size());
    wipe(readback.data(), readback.size());
    if (!ok) return Error::storage_failure;
    return reopen() == Error::invalid_record ? Error::storage_failure : Error::none;
}

GroupQrPairKeyError GroupQrPairKeys::store(std::uint64_t group, const Entry& entry) {
    if (!loaded_) return Error::storage_failure;
    if (group == 0 || !entry_valid(entry, local_)) return Error::invalid;
    auto at = std::find_if(groups_.begin(), groups_.end(), [&](const Group& g) { return g.id == group; });
    if (at == groups_.end()) at = std::find_if(groups_.begin(), groups_.end(), [](const Group& g) { return g.id == 0; });
    if (at == groups_.end()) return Error::refused;  // four groups already hold keys
    Group next = *at;
    next.id = group;
    for (const auto& slot : next.peers) {
        if (slot.used && slot.entry.peer == entry.peer) {
            const bool same = same_entry(slot.entry, entry);
            wipe(next.peers.data(), sizeof(next.peers));
            return same ? Error::none : Error::refused;
        }
    }
    const auto free = std::find_if(next.peers.begin(), next.peers.end(), [](const Slot& s) { return !s.used; });
    if (free == next.peers.end()) { wipe(next.peers.data(), sizeof(next.peers)); return Error::refused; }
    *free = {true, entry};
    const auto index = static_cast<std::size_t>(at - groups_.begin());
    const auto result = write(index, next);
    wipe(next.peers.data(), sizeof(next.peers));
    return result;
}

bool GroupQrPairKeys::find(std::uint64_t group, const identity::roster::PublicKey& peer, Entry& out) const {
    if (!loaded_ || group == 0) return false;
    for (const auto& g : groups_) {
        if (g.id != group) continue;
        for (const auto& slot : g.peers) {
            if (slot.used && slot.entry.peer == peer) { out = slot.entry; return true; }
        }
    }
    return false;
}

std::size_t GroupQrPairKeys::count(std::uint64_t group) const {
    if (!loaded_ || group == 0) return 0;
    for (const auto& g : groups_)
        if (g.id == group)
            return static_cast<std::size_t>(std::count_if(g.peers.begin(), g.peers.end(), [](const Slot& s) { return s.used; }));
    return 0;
}

GroupQrPairKeyError GroupQrPairKeys::wipe_group(std::uint64_t group) {
    if (!loaded_) return Error::storage_failure;
    if (group == 0) return Error::invalid;
    const auto at = std::find_if(groups_.begin(), groups_.end(), [&](const Group& g) { return g.id == group; });
    if (at == groups_.end()) return Error::none;  // nothing kept for that group
    Group empty{};
    empty.present = true;
    return write(static_cast<std::size_t>(at - groups_.begin()), empty);
}

} // namespace opentrail::persistence
