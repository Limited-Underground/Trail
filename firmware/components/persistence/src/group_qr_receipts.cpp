#include "opentrail/group_qr_receipts.hpp"

#include <algorithm>

#include "opentrail/configuration_store.hpp"

namespace opentrail::persistence {
namespace {
namespace qr = identity::roster::qr;
using Error = GroupQrReceiptError;
using KvError = PersistentKvBackendError;
using Kind = qr::Kind;
using Side = qr::Side;
constexpr char kPartition[] = "ot_state";
constexpr std::array<std::uint8_t, 5> kHeader{'O', 'T', 'Q', 'R', 1};
constexpr std::size_t kSlotsOffset = 8 + identity::roster::kFingerprintBytes;
constexpr std::size_t kSlotBytes = 1 + 8 + GroupQrReceipts::kRetainedBytes;
constexpr std::size_t kChecksumOffset = GroupQrReceipts::kRecordBytes - 4;
constexpr std::size_t kKindOffset = 3 * 32 + 1;  // within an encoded retained decision

template <std::size_t N>
bool nonzero(const std::array<std::uint8_t, N>& value) {
    return std::any_of(value.begin(), value.end(), [](std::uint8_t b) { return b != 0; });
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

using Encoded = std::array<std::uint8_t, GroupQrReceipts::kRetainedBytes>;

Encoded encode_retained(const qr::Retained& r, std::uint64_t deadline) {
    Encoded out{};
    auto* p = out.data();
    put_a(p, r.decision.scope);
    put_a(p, r.decision.transaction);
    put_a(p, r.decision.manifest);
    *p++ = static_cast<std::uint8_t>(r.decision.side);
    *p++ = static_cast<std::uint8_t>(r.decision.kind);
    put_a(p, r.state.joiner);
    put_a(p, r.state.admin);
    put_a(p, r.state.roster_digest);
    put_a(p, r.state.transcript);
    put_a(p, r.state.invite_id);
    put_u(p, r.state.group, 8);
    put_u(p, r.state.roster_version, 4);
    put_u(p, r.state.epoch, 4);
    *p++ = static_cast<std::uint8_t>(r.state.rule);
    put_a(p, r.local);
    put_a(p, r.peer);
    put_u(p, deadline, 8);
    return out;
}

qr::Retained decode_retained(const std::uint8_t* p, std::uint64_t& deadline) {
    qr::Retained r{};
    get_a(p, r.decision.scope);
    get_a(p, r.decision.transaction);
    get_a(p, r.decision.manifest);
    r.decision.side = static_cast<Side>(*p++);
    r.decision.kind = static_cast<Kind>(*p++);
    get_a(p, r.state.joiner);
    get_a(p, r.state.admin);
    get_a(p, r.state.roster_digest);
    get_a(p, r.state.transcript);
    get_a(p, r.state.invite_id);
    r.state.group = get_u(p, 8);
    r.state.roster_version = static_cast<std::uint32_t>(get_u(p, 4));
    r.state.epoch = static_cast<std::uint32_t>(get_u(p, 4));
    r.state.rule = static_cast<identity::roster::LocationRule>(*p++);
    get_a(p, r.local);
    get_a(p, r.peer);
    deadline = get_u(p, 8);
    return r;
}

bool receipt_kind(Kind kind) { return kind == Kind::prepared || kind == Kind::committed || kind == Kind::aborted; }

bool shape_valid(const qr::Retained& r, std::uint64_t deadline) {
    const auto& d = r.decision;
    const auto& s = r.state;
    const bool admin = d.side == Side::admin;
    return (admin || d.side == Side::joiner) && receipt_kind(d.kind) && deadline != 0 &&
           nonzero(d.scope) && nonzero(d.transaction) && nonzero(d.manifest) && nonzero(s.transcript) &&
           nonzero(s.invite_id) && nonzero(s.roster_digest) && s.group != 0 && s.roster_version != 0 &&
           s.rule <= identity::roster::LocationRule::required && nonzero(s.joiner) && nonzero(s.admin) &&
           s.joiner != s.admin && r.local == (admin ? s.admin : s.joiner) && r.peer == (admin ? s.joiner : s.admin);
}

// Same exact transaction and identities; only the decision kind may differ.
bool same_transaction(const Encoded& a, const Encoded& b) {
    for (std::size_t i = 0; i < a.size(); ++i)
        if (i != kKindOffset && a[i] != b[i]) return false;
    return true;
}
} // namespace

void GroupQrReceipts::encode(const Slots& slots, Record& out) const {
    out.fill(0);
    std::copy(kHeader.begin(), kHeader.end(), out.begin());
    std::copy(local_.begin(), local_.end(), out.begin() + 8);
    auto* p = out.data() + kSlotsOffset;
    for (const auto& slot : slots) {
        if (slot.used) {
            p[0] = 1;
            auto* q = p + 1;
            put_u(q, slot.sequence, 8);
            const auto bytes = encode_retained(slot.retained, slot.deadline);
            std::copy(bytes.begin(), bytes.end(), q);
        }
        p += kSlotBytes;
    }
    auto* c = out.data() + kChecksumOffset;
    put_u(c, configuration_crc32({out.data(), kChecksumOffset}), 4);
}

bool GroupQrReceipts::decode(const Record& in, Slots& slots) const {
    const auto* c = in.data() + kChecksumOffset;
    if (!std::equal(kHeader.begin(), kHeader.end(), in.begin()) || in[5] || in[6] || in[7] ||
        get_u(c, 4) != configuration_crc32({in.data(), kChecksumOffset}) ||
        !std::equal(local_.begin(), local_.end(), in.begin() + 8)) return false;
    slots = {};
    const auto* p = in.data() + kSlotsOffset;
    for (auto& slot : slots) {
        if (p[0] > 1) return false;
        if (p[0] == 1) {
            const auto* q = p + 1;
            slot.used = true;
            slot.sequence = get_u(q, 8);
            slot.retained = decode_retained(q, slot.deadline);
            if (slot.sequence == 0 || !shape_valid(slot.retained, slot.deadline) ||
                slot.retained.local != local_) return false;
        } else if (std::any_of(p, p + kSlotBytes, [](std::uint8_t b) { return b != 0; })) {
            return false;
        }
        p += kSlotBytes;
    }
    for (std::size_t i = 0; i < slots.size(); ++i)
        for (std::size_t j = i + 1; j < slots.size(); ++j)
            if (slots[i].used && slots[j].used &&
                (slots[i].retained.state.invite_id == slots[j].retained.state.invite_id ||
                 slots[i].sequence == slots[j].sequence)) return false;
    return true;
}

GroupQrReceiptError GroupQrReceipts::reopen() {
    loaded_ = false;
    slots_ = {};
    next_sequence_ = 1;
    if (!nonzero(local_)) return Error::invalid;
    Record bytes{};
    std::size_t size = 0;
    const auto read = backend_.read_blob(kPartition, kNamespace, kKey, bytes.data(), bytes.size(), size);
    if (read == KvError::not_found) {
        loaded_ = true;
        return Error::missing;
    }
    if (read != KvError::none) return Error::storage_failure;
    Slots slots{};
    if (size != bytes.size() || !decode(bytes, slots)) return Error::invalid_record;
    slots_ = slots;
    for (const auto& slot : slots_) if (slot.used) next_sequence_ = std::max(next_sequence_, slot.sequence + 1);
    loaded_ = true;
    return Error::none;
}

const GroupQrReceipts::Slot* GroupQrReceipts::slot_for(const qr::Nonce& invite) const {
    const auto found = std::find_if(slots_.begin(), slots_.end(), [&](const Slot& slot) {
        return slot.used && slot.retained.state.invite_id == invite;
    });
    return found == slots_.end() ? nullptr : &*found;
}

bool GroupQrReceipts::fresh_unclaimed(const qr::Nonce& invite) const {
    return loaded_ && nonzero(invite) && slot_for(invite) == nullptr;
}

GroupQrReceiptError GroupQrReceipts::persist(const qr::Retained& retained, std::uint64_t deadline, std::uint64_t now) {
    if (!loaded_) return Error::storage_failure;
    if (!shape_valid(retained, deadline) || retained.local != local_)
        return Error::invalid;
    const auto kind = retained.decision.kind;
    if (kind != Kind::aborted && now >= deadline) return Error::refused;
    const auto encoded = encode_retained(retained, deadline);

    auto slots = slots_;
    if (const auto* existing = slot_for(retained.state.invite_id)) {
        const auto prior = encode_retained(existing->retained, existing->deadline);
        if (!same_transaction(prior, encoded)) return Error::refused;
        const auto prior_kind = existing->retained.decision.kind;
        if (prior_kind == kind) return Error::none;
        if (prior_kind != Kind::prepared) return Error::refused;
        auto& slot = slots[static_cast<std::size_t>(existing - slots_.data())];
        slot.retained.decision.kind = kind;
    } else {
        if (kind == Kind::committed) return Error::refused;  // commit follows a retained prepare
        auto free = std::find_if(slots.begin(), slots.end(), [](const Slot& slot) { return !slot.used; });
        if (free == slots.end()) {
            free = slots.end();
            for (auto it = slots.begin(); it != slots.end(); ++it) {
                if (it->retained.decision.kind == Kind::prepared) continue;
                if (free == slots.end() || it->sequence < free->sequence) free = it;
            }
            if (free == slots.end()) return Error::refused;  // every slot is in flight
        }
        *free = {true, next_sequence_, deadline, retained};
    }

    Record bytes{};
    encode(slots, bytes);
    loaded_ = false;  // any failure from here may have applied the record
    if (backend_.write_blob(kPartition, kNamespace, kKey, bytes.data(), bytes.size()) != KvError::none ||
        backend_.commit(kPartition, kNamespace) != KvError::none) return Error::storage_failure;
    Record readback_bytes{};
    std::size_t size = 0;
    if (backend_.read_blob(kPartition, kNamespace, kKey, readback_bytes.data(), readback_bytes.size(), size) !=
            KvError::none || size != readback_bytes.size() || readback_bytes != bytes) return Error::storage_failure;
    slots_ = slots;
    for (const auto& slot : slots_) if (slot.used) next_sequence_ = std::max(next_sequence_, slot.sequence + 1);
    loaded_ = true;
    return Error::none;
}

bool GroupQrReceipts::readback(const qr::Retained& retained) const {
    if (!loaded_) return false;
    const auto* slot = slot_for(retained.state.invite_id);
    if (!slot) return false;
    const auto a = encode_retained(slot->retained, slot->deadline);
    const auto b = encode_retained(retained, slot->deadline);
    return a == b;
}

std::optional<qr::Retained> GroupQrReceipts::at(std::size_t index) const {
    if (!loaded_ || index >= slots_.size() || !slots_[index].used) return std::nullopt;
    return slots_[index].retained;
}

std::optional<qr::Retained> GroupQrReceipts::find(const qr::Nonce& invite, std::uint64_t* deadline) const {
    if (!loaded_) return std::nullopt;
    const auto* slot = slot_for(invite);
    if (!slot) return std::nullopt;
    if (deadline) *deadline = slot->deadline;
    return slot->retained;
}

} // namespace opentrail::persistence
