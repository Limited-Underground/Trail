#include "opentrail/group_join_claims.hpp"

#include <algorithm>

#include "opentrail/configuration_store.hpp"

namespace opentrail::persistence {
namespace {
using Error = GroupJoinClaimError;
using KvError = PersistentKvBackendError;
constexpr char kPartition[] = "ot_state";
constexpr std::array<std::uint8_t, 5> kHeader{'O', 'T', 'G', 'J', 1};

bool nonzero(const std::uint8_t* bytes, std::size_t size) {
    return std::any_of(bytes, bytes + size, [](std::uint8_t b) { return b != 0; });
}

void put_u64(std::uint8_t* out, std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i) out[i] = static_cast<std::uint8_t>(value >> (8 * i));
}
std::uint64_t get_u64(const std::uint8_t* in) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(in[i]) << (8 * i);
    return value;
}
void put_u32(std::uint8_t* out, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) out[i] = static_cast<std::uint8_t>(value >> (8 * i));
}
std::uint32_t get_u32(const std::uint8_t* in) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(in[i]) << (8 * i);
    return value;
}
constexpr std::size_t kEntriesOffset = 8 + identity::roster::kFingerprintBytes;
constexpr std::size_t kChecksumOffset = GroupJoinClaims::kRecordBytes - 4;
} // namespace

void GroupJoinClaims::encode(const std::array<Entry, kEntries>& entries, std::uint8_t count, std::uint8_t next,
                             Record& out) const {
    out.fill(0);
    std::copy(kHeader.begin(), kHeader.end(), out.begin());
    out[5] = count;
    out[6] = next;
    std::copy(local_.begin(), local_.end(), out.begin() + 8);
    auto* p = out.data() + kEntriesOffset;
    for (std::size_t i = 0; i < count; ++i, p += kEntryBytes) {
        std::copy(entries[i].admin.begin(), entries[i].admin.end(), p);
        put_u64(p + entries[i].admin.size(), entries[i].group);
        std::copy(entries[i].invite.begin(), entries[i].invite.end(), p + entries[i].admin.size() + 8);
    }
    put_u32(out.data() + kChecksumOffset, configuration_crc32({out.data(), kChecksumOffset}));
}

bool GroupJoinClaims::decode(const Record& in, std::array<Entry, kEntries>& entries, std::uint8_t& count,
                             std::uint8_t& next) const {
    if (!std::equal(kHeader.begin(), kHeader.end(), in.begin()) || in[7] != 0 ||
        get_u32(in.data() + kChecksumOffset) != configuration_crc32({in.data(), kChecksumOffset}) ||
        !std::equal(local_.begin(), local_.end(), in.begin() + 8)) return false;
    count = in[5];
    next = in[6];
    if (count > kEntries || next >= kEntries || (count < kEntries && next != count)) return false;
    entries = {};
    const auto* p = in.data() + kEntriesOffset;
    for (std::size_t i = 0; i < kEntries; ++i, p += kEntryBytes) {
        if (i >= count) {
            if (nonzero(p, kEntryBytes)) return false;
            continue;
        }
        auto& entry = entries[i];
        std::copy_n(p, entry.admin.size(), entry.admin.begin());
        entry.group = get_u64(p + entry.admin.size());
        std::copy_n(p + entry.admin.size() + 8, entry.invite.size(), entry.invite.begin());
        if (!nonzero(entry.admin.data(), entry.admin.size()) || entry.group == 0 ||
            !nonzero(entry.invite.data(), entry.invite.size())) return false;
    }
    return true;
}

GroupJoinClaimError GroupJoinClaims::reopen() {
    loaded_ = false;
    entries_ = {};
    count_ = next_ = 0;
    if (!nonzero(local_.data(), local_.size())) return Error::invalid;
    Record bytes{};
    std::size_t size = 0;
    const auto read = backend_.read_blob(kPartition, kNamespace, kKey, bytes.data(), bytes.size(), size);
    if (read == KvError::not_found) {
        loaded_ = true;
        return Error::missing;
    }
    if (read != KvError::none) return Error::storage_failure;
    if (size != bytes.size() || !decode(bytes, entries_, count_, next_)) return Error::invalid_record;
    loaded_ = true;
    return Error::none;
}

bool GroupJoinClaims::claimed(const identity::roster::Fingerprint& admin, std::uint64_t group,
                              const InviteId& invite) const {
    // Fails closed: an unloaded record claims everything.
    if (!loaded_) return true;
    // Invite IDs are 128-bit board entropy. Any match is refused, whoever
    // signed it, so a look-alike scope cannot reuse a consumed ID.
    (void)admin;
    (void)group;
    return std::any_of(entries_.begin(), entries_.begin() + count_,
                       [&](const Entry& entry) { return entry.invite == invite; });
}

GroupJoinClaimError GroupJoinClaims::claim(const identity::roster::Fingerprint& admin, std::uint64_t group,
                                           const InviteId& invite) {
    if (!loaded_) return Error::storage_failure;
    if (!nonzero(admin.data(), admin.size()) || group == 0 || !nonzero(invite.data(), invite.size()))
        return Error::invalid;
    if (claimed(admin, group, invite)) return Error::replayed;

    auto entries = entries_;
    auto count = count_;
    const auto slot = next_;
    entries[slot] = {admin, group, invite};
    if (count < kEntries) ++count;
    const auto next = static_cast<std::uint8_t>((slot + 1) % kEntries);
    // While filling, next equals count; once full it names the oldest entry.
    Record bytes{};
    encode(entries, count, next, bytes);

    // Any failure from here may have applied the record. Drop the cache so the
    // next attempt must reopen and observe the retained outcome.
    loaded_ = false;
    if (backend_.write_blob(kPartition, kNamespace, kKey, bytes.data(), bytes.size()) != KvError::none ||
        backend_.commit(kPartition, kNamespace) != KvError::none) return Error::storage_failure;
    Record readback{};
    std::size_t size = 0;
    if (backend_.read_blob(kPartition, kNamespace, kKey, readback.data(), readback.size(), size) != KvError::none ||
        size != readback.size() || readback != bytes) return Error::storage_failure;
    entries_ = entries;
    count_ = count;
    next_ = next;
    loaded_ = true;
    return Error::none;
}

} // namespace opentrail::persistence
