#include "opentrail/direct_conversation_owner.hpp"

#include <algorithm>
#include <limits>

namespace opentrail::companion {
namespace {

using KvError = persistence::PersistentKvBackendError;
constexpr auto kPartition = persistence::kPersistentKvPartitionLabel;
constexpr std::array<std::uint8_t, 4> kFormat{'O', 'T', 'D', '2'};
constexpr std::array<std::uint8_t, 4> kLegacyFormat{'O', 'T', 'D', '1'};
constexpr std::size_t kHeaderBytes = 16;
constexpr std::size_t kRecordBytes = 81;

bool nonzero(const DirectIdentity& value) {
    return value != DirectIdentity{};
}

void put_u64(std::uint8_t* out, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) {
        out[i] = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

std::uint64_t get_u64(const std::uint8_t* in) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(in[i]) << (8 * i);
    }
    return value;
}

bool same_ticket(const SelectedEnrollmentRequest& a, const SelectedEnrollmentRequest& b) {
    const auto& x = a.authority;
    const auto& y = b.authority;
    if (x.device != y.device || x.runtime != y.runtime || x.owner != y.owner ||
        x.owner_generation != y.owner_generation ||
        x.transport_generation != y.transport_generation ||
        x.controller != y.controller || x.session_nonce != y.session_nonce) return false;
    return a.connection_handle == b.connection_handle && a.delivery_token == b.delivery_token &&
           a.exchange_id == b.exchange_id && a.admitted_ms == b.admitted_ms &&
           a.deadline_ms == b.deadline_ms && a.role == b.role &&
           a.pairing_code == b.pairing_code;
}

}  // namespace

bool DirectConversationOwner::valid(const DirectBinding& binding) {
    return nonzero(binding.peer) && nonzero(binding.scope);
}

DirectConversationOwner::Bytes DirectConversationOwner::encode(
    const Records& records, std::uint64_t sequence) {
    static_assert(kCatalogBytes == kHeaderBytes + (kConversations + kBlocked) * kRecordBytes);
    Bytes bytes{};
    std::copy(kFormat.begin(), kFormat.end(), bytes.begin());
    put_u64(bytes.data() + 4, sequence);
    for (std::size_t i = 0; i < records.size(); ++i) {
        const Record& record = records[i];
        auto* out = bytes.data() + kHeaderBytes + i * kRecordBytes;
        out[0] = static_cast<std::uint8_t>(record.state);
        std::copy(record.binding.peer.begin(), record.binding.peer.end(), out + 1);
        std::copy(record.binding.scope.begin(), record.binding.scope.end(), out + 33);
        put_u64(out + 65, record.session);
        put_u64(out + 73, record.generation);
    }
    return bytes;
}

bool DirectConversationOwner::decode(const Bytes& bytes, Records& records,
                                     std::uint64_t& sequence, bool& legacy) {
    legacy = std::equal(kLegacyFormat.begin(), kLegacyFormat.end(), bytes.begin());
    if ((!legacy && !std::equal(kFormat.begin(), kFormat.end(), bytes.begin())) ||
        std::any_of(bytes.begin() + 12, bytes.begin() + kHeaderBytes,
                    [](std::uint8_t value) { return value != 0; })) return false;
    sequence = get_u64(bytes.data() + 4);
    if (sequence > std::numeric_limits<std::uint32_t>::max()) return false;
    std::size_t conversations = 0;
    std::size_t blocks = 0;
    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto* in = bytes.data() + kHeaderBytes + i * kRecordBytes;
        if (in[0] > static_cast<std::uint8_t>(State::blocked)) return false;
        auto& record = records[i];
        record.state = static_cast<State>(in[0]);
        std::copy_n(in + 1, record.binding.peer.size(), record.binding.peer.begin());
        std::copy_n(in + 33, record.binding.scope.size(), record.binding.scope.begin());
        record.session = get_u64(in + 65);
        record.generation = get_u64(in + 73);
        if (record.state == State::empty) {
            if (nonzero(record.binding.peer) || nonzero(record.binding.scope) ||
                record.session || record.generation) return false;
            continue;
        }
        if (!nonzero(record.binding.peer) || record.generation > sequence ||
            ((record.session == 0) != (record.generation == 0))) return false;
        if (!legacy && (record.state == State::blocked) != (i >= kConversations)) return false;
        if (record.state == State::blocked) {
            if (nonzero(record.binding.scope) || record.generation) return false;
        } else {
            if (!valid(record.binding) ||
                (record.state == State::local_accepted && record.generation)) return false;
            ++conversations;
        }
        if (record.state == State::blocked || record.state == State::retiring_block) ++blocks;
        for (std::size_t previous = 0; previous < i; ++previous) {
            const auto& other = records[previous];
            if (other.state != State::empty &&
                (other.binding.peer == record.binding.peer ||
                 (nonzero(record.binding.scope) && other.binding.scope == record.binding.scope))) {
                return false;
            }
        }
    }
    if (conversations > kConversations || blocks > kBlocked) return false;
    if (legacy) {
        // OTD1 could place a block or a conversation in any row. It predated
        // durable scoped journals, so its rows can be assigned once on import.
        Records separated{};
        std::size_t conversation = 0;
        std::size_t block = kConversations;
        for (const auto& record : records) {
            if (record.state == State::empty) continue;
            separated[record.state == State::blocked ? block++ : conversation++] = record;
        }
        records = separated;
    }
    return true;
}

bool DirectConversationOwner::load() {
    if (loaded_) return healthy_;
    loaded_ = true;
    Bytes bytes{};
    std::size_t size = 0;
    const auto result = backend_.read_blob(kPartition, kNamespace, kCatalogKey,
                                           bytes.data(), bytes.size(), size);
    if (result == KvError::not_found && size == 0) {
        healthy_ = true;
        return true;
    }
    bool legacy = false;
    if (result != KvError::none || size != bytes.size() ||
        !decode(bytes, records_, sequence_, legacy)) return false;
    healthy_ = true;
    if (legacy && !save()) return false;
    for (std::size_t slot = 0; slot < kConversations; ++slot) {
        const auto state = records_[slot].state;
        if ((state == State::retiring_end || state == State::retiring_block) &&
            !finish_retirement(slot)) {
            healthy_ = false;
            return false;
        }
    }
    return true;
}

bool DirectConversationOwner::save() {
    const auto bytes = encode(records_, sequence_);
    Bytes echo{};
    std::size_t size = 0;
    healthy_ = backend_.write_blob(kPartition, kNamespace, kCatalogKey,
                                   bytes.data(), bytes.size()) == KvError::none &&
               backend_.commit(kPartition, kNamespace) == KvError::none &&
               backend_.read_blob(kPartition, kNamespace, kCatalogKey,
                                  echo.data(), echo.size(), size) == KvError::none &&
               size == bytes.size() && echo == bytes;
    return healthy_;
}

std::optional<std::size_t> DirectConversationOwner::find(const DirectIdentity& peer) const {
    for (std::size_t i = 0; i < records_.size(); ++i) {
        if (records_[i].state != State::empty && records_[i].binding.peer == peer) return i;
    }
    return std::nullopt;
}

bool DirectConversationOwner::blocked(const DirectIdentity& peer) const {
    const auto index = find(peer);
    return healthy_ && index && (records_[*index].state == State::blocked ||
                                 records_[*index].state == State::retiring_block);
}

void DirectConversationOwner::cancel() {
    const auto pending = request_.request();
    if (request_.cancel_exact(pending)) pairing_.cancel(pending);
    local_vote_.reset();
}

void DirectConversationOwner::observe(const DeviceNameAuthority& current,
                                     std::uint16_t handle) {
    const auto pending = request_.request();
    if (request_.pending() && !request_.observe(current, handle)) {
        pairing_.cancel(pending);
        local_vote_.reset();
    }
}

DirectResult DirectConversationOwner::check(const DeviceNameAuthority& current,
                                            const SelectedEnrollmentRequest& ticket) {
    if (!healthy_) return DirectResult::unavailable;
    if (!request_.pending() || !same_ticket(request_.request(), ticket)) return DirectResult::stale;
    observe(current, ticket.connection_handle);
    return request_.pending() ? DirectResult::ok : DirectResult::expired;
}

DirectResult DirectConversationOwner::begin(
    const DeviceNameAuthority& current, std::uint16_t handle, std::uint64_t delivery,
    std::uint32_t exchange, CompanionEnrollmentRole role, std::uint32_t code) {
    if (!healthy_) return DirectResult::unavailable;
    if (role == CompanionEnrollmentRole::none || code == 0 || delivery == 0) return DirectResult::refused;
    if (delivery <= last_delivery_) return DirectResult::stale;
    if (request_.pending()) observe(current, request_.request().connection_handle);
    const auto result = request_.admit(current, handle, delivery, exchange, role, code);
    if (result == SelectedEnrollmentRequestResult::busy) return DirectResult::busy;
    if (result != SelectedEnrollmentRequestResult::admitted) return DirectResult::refused;
    // The protected configuration lane issues monotonically increasing
    // delivery tokens for this runtime. Consume it even if F5 cannot start.
    last_delivery_ = delivery;
    if (pairing_.begin(request_.request())) return DirectResult::ok;
    cancel();
    return DirectResult::unavailable;
}

DirectResult DirectConversationOwner::accept(const DeviceNameAuthority& current,
                                             const SelectedEnrollmentRequest& ticket) {
    const auto checked = check(current, ticket);
    if (checked != DirectResult::ok) return checked;
    const auto pending = request_.request();
    const auto binding = pairing_.reviewed(pending);
    if (!binding || !valid(*binding)) return DirectResult::refused;
    if (local_vote_) {
        return *local_vote_ == *binding ? DirectResult::ok : DirectResult::stale;
    }
    const auto existing = find(binding->peer);
    if (existing) {
        if (blocked(binding->peer)) { cancel(); return DirectResult::blocked; }
        const auto& record = records_[*existing];
        if (record.state != State::local_accepted || !(record.binding == *binding)) {
            cancel();
            return DirectResult::stale;
        }
    } else {
        const auto end = records_.begin() + kConversations;
        const auto slot = std::find_if(records_.begin(), end, [](const Record& r) {
            return r.state == State::empty;
        });
        if (slot == end) { cancel(); return DirectResult::full; }
        for (auto other = records_.begin(); other != end; ++other) {
            if (other->binding.scope == binding->scope) { cancel(); return DirectResult::refused; }
        }
        *slot = {*binding, 0, 0, State::local_accepted};
        if (!save()) { cancel(); return DirectResult::unavailable; }
    }
    if (!pairing_.commit_local_vote(pending, *binding)) {
        cancel();
        return DirectResult::unavailable;
    }
    local_vote_ = binding;
    return DirectResult::ok;
}

DirectResult DirectConversationOwner::decline(const DeviceNameAuthority& current,
                                              const SelectedEnrollmentRequest& ticket) {
    const auto checked = check(current, ticket);
    if (checked != DirectResult::ok) return checked;
    if (local_vote_) return DirectResult::stale;
    cancel();
    return DirectResult::ok;
}

DirectResult DirectConversationOwner::finish(const DeviceNameAuthority& current,
                                             const SelectedEnrollmentRequest& ticket) {
    const auto checked = check(current, ticket);
    if (checked != DirectResult::ok) return checked;
    if (!local_vote_) return DirectResult::refused;
    const auto index = find(local_vote_->peer);
    if (!index || records_[*index].state != State::local_accepted ||
        !(records_[*index].binding == *local_vote_)) return DirectResult::stale;
    const auto pending = request_.request();
    if (!pairing_.peer_committed(pending, *local_vote_)) return DirectResult::busy;
    records_[*index].state = State::accepted;
    if (!save()) { cancel(); return DirectResult::unavailable; }
    request_.cancel_exact(pending);
    local_vote_.reset();
    return DirectResult::ok;
}

std::optional<std::size_t> DirectConversationOwner::scope_slot(const DirectBinding& binding) const {
    if (!healthy_) return std::nullopt;
    const auto index = find(binding.peer);
    if (!index || *index >= kConversations) return std::nullopt;
    const auto& record = records_[*index];
    if (!(record.binding == binding) ||
        (record.state != State::local_accepted && record.state != State::accepted)) return std::nullopt;
    return index;
}

std::optional<DirectTrafficGrant> DirectConversationOwner::traffic_grant(const DirectBinding& binding) const {
    const auto slot = scope_slot(binding);
    if (!slot || !live_[*slot]) return std::nullopt;
    const auto& record = records_[*slot];
    if (record.state != State::accepted || !record.generation ||
        record.session != pairing_.ready_session(binding)) return std::nullopt;
    return DirectTrafficGrant{*slot, record.session, record.generation};
}

std::uint64_t DirectConversationOwner::send_generation(const DirectBinding& binding) const {
    const auto grant = traffic_grant(binding);
    return grant ? grant->generation : 0;
}

DirectResult DirectConversationOwner::activate(const DirectBinding& binding) {
    if (!healthy_) return DirectResult::unavailable;
    const auto index = find(binding.peer);
    if (blocked(binding.peer)) return DirectResult::blocked;
    if (!index) return DirectResult::refused;
    auto& record = records_[*index];
    if (record.state != State::accepted || !(record.binding == binding)) return DirectResult::stale;
    if (send_generation(binding)) return DirectResult::ok;
    const auto session = pairing_.ready_session(binding);
    if (!session || session <= record.session) return DirectResult::stale;
    if (sequence_ == std::numeric_limits<std::uint32_t>::max()) return DirectResult::full;
    live_[*index] = false;
    if (record.generation && !packets_.quiesce_and_retire(binding.scope, record.generation)) {
        return DirectResult::unavailable;
    }
    record.session = session;
    record.generation = ++sequence_;
    if (!save() || !packets_.claim(binding.scope, record.generation)) return DirectResult::unavailable;
    live_[*index] = pairing_.ready_session(binding) == session;
    return live_[*index] ? DirectResult::ok : DirectResult::stale;
}

bool DirectConversationOwner::finish_retirement(std::size_t slot) {
    auto& record = records_[slot];
    const bool deny = record.state == State::retiring_block;
    const auto tombstone = deny ? std::find_if(records_.begin() + kConversations, records_.end(),
                                             [](const Record& entry) { return entry.state == State::empty; })
                                : records_.end();
    if (deny && tombstone == records_.end()) return false;
    if (record.generation &&
        !packets_.quiesce_and_retire(record.binding.scope, record.generation)) return false;
    if (!pairing_.retire_scope(slot, record.binding)) return false;
    if (deny) {
        tombstone->binding.peer = record.binding.peer;
        tombstone->state = State::blocked;
    }
    record = {};
    return save();
}

DirectResult DirectConversationOwner::retire(const DirectBinding& binding, State state) {
    if (!healthy_) return DirectResult::unavailable;
    const auto index = find(binding.peer);
    if (!index) {
        return state == State::retiring_block ? block_reviewed(binding) : DirectResult::stale;
    }
    auto& record = records_[*index];
    if (record.state == State::blocked) {
        return state == State::retiring_block ? DirectResult::ok : DirectResult::blocked;
    }
    if (!(record.binding == binding)) return DirectResult::stale;
    if (record.state == State::retiring_end || record.state == State::retiring_block) {
        if (record.state != state) return DirectResult::stale;
        return finish_retirement(*index) ? DirectResult::ok : DirectResult::unavailable;
    }
    if (state == State::retiring_block) {
        const auto blocks = std::count_if(records_.begin(), records_.end(), [](const Record& r) {
            return r.state == State::blocked || r.state == State::retiring_block;
        });
        if (blocks >= static_cast<std::ptrdiff_t>(kBlocked)) return DirectResult::full;
    }
    live_[*index] = false;
    if (local_vote_ && *local_vote_ == binding) cancel();
    record.state = state;
    if (!save()) {
        // An uncertain write grants no new authority. Drain current borrowers
        // even though durable completion cannot be reported on this boot.
        if (record.generation) (void)packets_.quiesce_and_retire(binding.scope, record.generation);
        return DirectResult::unavailable;
    }
    return finish_retirement(*index) ? DirectResult::ok : DirectResult::unavailable;
}

// I-105: an unwanted invitation can be blocked before anyone accepts. Only
// the peer the authenticated pending attempt reviewed can be named, so a
// block never comes from an unverified identity. It holds no scope or slot.
DirectResult DirectConversationOwner::block_reviewed(const DirectBinding& binding) {
    if (!valid(binding) || !request_.pending()) return DirectResult::stale;
    const auto pending = request_.request();
    const auto reviewed = pairing_.reviewed(pending);
    if (!reviewed || !(*reviewed == binding)) return DirectResult::stale;
    // Blocks still being retired keep their reserved tombstones.
    const auto blocks = std::count_if(records_.begin(), records_.end(), [](const Record& r) {
        return r.state == State::blocked || r.state == State::retiring_block;
    });
    if (blocks >= static_cast<std::ptrdiff_t>(kBlocked)) return DirectResult::full;
    const auto tombstone = std::find_if(records_.begin() + kConversations, records_.end(),
                                        [](const Record& entry) { return entry.state == State::empty; });
    if (tombstone == records_.end()) return DirectResult::full;
    cancel();
    tombstone->binding.peer = binding.peer;
    tombstone->state = State::blocked;
    return save() ? DirectResult::ok : DirectResult::unavailable;
}

DirectResult DirectConversationOwner::end(const DirectBinding& binding) {
    return retire(binding, State::retiring_end);
}

DirectResult DirectConversationOwner::block(const DirectBinding& binding) {
    return retire(binding, State::retiring_block);
}

DirectResult DirectConversationOwner::unblock(const DirectIdentity& peer) {
    if (!healthy_) return DirectResult::unavailable;
    const auto index = find(peer);
    if (!index || records_[*index].state != State::blocked) return DirectResult::stale;
    records_[*index] = {};
    return save() ? DirectResult::ok : DirectResult::unavailable;
}

}  // namespace opentrail::companion
