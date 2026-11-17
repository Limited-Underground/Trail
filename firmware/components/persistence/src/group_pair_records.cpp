#include "opentrail/group_pair_records.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "opentrail/configuration_store.hpp"

namespace opentrail::persistence {
namespace {

namespace roster = identity::roster;
using Error = GroupPairRecordsError;
using Scope = roster::DurablePairScope;
using Bytes = std::array<std::uint8_t, kPersistentSlotBytes * kPersistentSlotCount>;

constexpr std::size_t kScopeBytes = 1 + 8 + 32 + 8 + 32 + 32 + 4 + 4;
constexpr std::size_t kProofBytes = roster::kBindingBytes + 2 * roster::kSignatureBytes;
constexpr std::size_t kRecordBytes = 4 + kScopeBytes + 1 + 4 + 4 + kProofBytes + 4;
constexpr std::size_t kLaneBytes = 4 + kScopeBytes + 4 + 1 + std::tuple_size_v<Bytes> + 4;
constexpr std::array<std::uint8_t, 4> kPairMagic{'O', 'T', 'P', 1}, kLaneMagic{'O', 'T', 'L', 1};

struct Writer {
    std::uint8_t* at;
    template <class Number> void number(Number number) {
        for (std::size_t left = sizeof(Number); left > 0; --left) {
            *at++ = static_cast<std::uint8_t>(number >> (8 * (left - 1)));
        }
    }
    template <std::size_t Size> void bytes(const std::array<std::uint8_t, Size>& value) {
        at = std::copy(value.begin(), value.end(), at);
    }
};

struct Reader {
    const std::uint8_t* at;
    template <class Number> Number number() {
        Number result{};
        for (std::size_t index = 0; index < sizeof(Number); ++index) {
            result = static_cast<Number>((static_cast<std::uint64_t>(result) << 8) | *at++);
        }
        return result;
    }
    template <std::size_t Size> void bytes(std::array<std::uint8_t, Size>& value) {
        std::copy_n(at, Size, value.begin());
        at += Size;
    }
};

void put_scope(Writer& writer, const Scope& scope) {
    writer.number(static_cast<std::uint8_t>(scope.location.slot));
    writer.number(scope.location.incarnation);
    writer.bytes(scope.group.admin);
    writer.number(scope.group.group_id);
    writer.bytes(scope.local);
    writer.bytes(scope.peer);
    writer.number(scope.local_added);
    writer.number(scope.peer_added);
}

Scope get_scope(Reader& reader) {
    Scope scope{};
    scope.location.slot = reader.number<std::uint8_t>();
    scope.location.incarnation = reader.number<std::uint64_t>();
    reader.bytes(scope.group.admin);
    scope.group.group_id = reader.number<std::uint64_t>();
    reader.bytes(scope.local);
    reader.bytes(scope.peer);
    scope.local_added = reader.number<std::uint32_t>();
    scope.peer_added = reader.number<std::uint32_t>();
    return scope;
}

bool same_group(const roster::GroupName& a, const roster::GroupName& b) {
    return a.admin == b.admin && a.group_id == b.group_id;
}

bool same_incarnation(const Scope& a, const Scope& b) {
    return same_group(a.group, b.group) && a.local == b.local && a.peer == b.peer &&
           a.local_added == b.local_added && a.peer_added == b.peer_added &&
           a.location.slot == b.location.slot && a.location.incarnation == b.location.incarnation;
}

template <std::size_t Size>
bool zero(const std::array<std::uint8_t, Size>& bytes) {
    return std::all_of(bytes.begin(), bytes.end(), [](auto byte) { return byte == 0; });
}

bool valid_scope(const Scope& scope, std::size_t slot) {
    return slot < GroupPairIndex::kMaximumPairs && scope.location.slot == slot &&
           scope.location.incarnation && scope.group.group_id && scope.local_added &&
           scope.peer_added && !zero(scope.group.admin) && !zero(scope.local) &&
           !zero(scope.peer) && scope.local != scope.peer;
}

template <std::size_t Size>
bool valid_blob(const std::array<std::uint8_t, Size>& blob,
                const std::array<std::uint8_t, 4>& magic) {
    Reader crc{blob.data() + Size - 4};
    return std::equal(magic.begin(), magic.end(), blob.begin()) &&
           crc.number<std::uint32_t>() == configuration_crc32({blob.data(), Size - 4});
}

template <std::size_t Size>
Error read_blob(PersistentKvBackend& backend, const char* key, std::array<std::uint8_t, Size>& blob) {
    std::size_t actual{};
    const auto result = backend.read_blob(kPersistentKvPartitionLabel, kGroupPairRecordsNamespace,
                                          key, blob.data(), blob.size(), actual);
    if (result == PersistentKvBackendError::not_found) return Error::missing;
    if (result != PersistentKvBackendError::none) return Error::storage_failure;
    return actual == blob.size() ? Error::none : Error::malformed;
}

using PairKey = std::array<char, 8>;

PairKey pair_key(std::size_t slot) {
    PairKey key{};
    std::snprintf(key.data(), key.size(), "p%02u", static_cast<unsigned>(slot));
    return key;
}

PairKey lane_key(std::size_t slot, GroupPairLane lane) {
    PairKey key{};
    std::snprintf(key.data(), key.size(), "p%02ul%u",
                  static_cast<unsigned>(slot), static_cast<unsigned>(lane));
    return key;
}

}  // namespace

struct GroupPairRecords::Record {
    Scope scope{};
    GroupPairHistory history{};
    std::array<std::uint8_t, kProofBytes> proof{};
};

GroupPairRecords::GroupPairRecords(PersistentKvBackend& backend, roster::DurableGroupRoster& owner,
                                   roster::GroupState& live_group, const roster::PublicKey& peer_key,
                                   const roster::Fingerprint& peer)
    : backend_(backend), roster_(owner), live_group_(live_group), peer_key_(peer_key), peer_(peer),
      lanes_{{{*this, GroupPairLane::data_tx}, {*this, GroupPairLane::ack_tx},
              {*this, GroupPairLane::data_rx}, {*this, GroupPairLane::ack_rx}}} {}

std::optional<Scope> GroupPairRecords::current_scope() {
    if (faulted_) return std::nullopt;
    return roster_.find_pair(peer_);
}

bool GroupPairRecords::current_binding(const Scope& scope, roster::PairStep step) const {
    const auto& live_roster = live_group_.roster();
    roster::PublicKey live_key{};
    if (!scope_ || !same_incarnation(scope, *scope_) ||
        (step != roster::PairStep::committed &&
         (scope.roster_version != scope_->roster_version || scope.roster_digest != scope_->roster_digest)) ||
        live_roster.state() != roster::State::active ||
        live_roster.roster().version != scope.roster_version || live_roster.digest() != scope.roster_digest ||
        !live_roster.key_for(peer_, live_key) || live_key != peer_key_) return false;
    const auto status = live_group_.pair_status(peer_key_);
    return status && status->binding == step && status->binding_id == binding_id_ && status->used_epoch == epoch_;
}

Error GroupPairRecords::load_record(std::size_t slot, Record& output) {
    std::array<std::uint8_t, kRecordBytes> blob{};
    const auto key = pair_key(slot);
    const auto result = read_blob(backend_, key.data(), blob);
    if (result != Error::none) return result;
    if (!valid_blob(blob, kPairMagic)) return Error::malformed;

    Reader reader{blob.data() + kPairMagic.size()};
    output.scope = get_scope(reader);
    output.history.phase = static_cast<GroupPairPhase>(reader.number<std::uint8_t>());
    output.history.used_epoch = reader.number<std::uint32_t>();
    output.history.committed_epoch = reader.number<std::uint32_t>();
    reader.bytes(output.proof);
    if (!valid_scope(output.scope, slot)) return Error::malformed;

    const auto& h = output.history;
    if (h.phase == GroupPairPhase::empty) {
        return h.used_epoch == 0 && h.committed_epoch == 0 && zero(output.proof) ? Error::none : Error::malformed;
    }
    if (zero(output.proof) || !h.used_epoch) return Error::malformed;
    switch (h.phase) {
        case GroupPairPhase::prepared:
        case GroupPairPhase::activation_possible:
            return h.committed_epoch < h.used_epoch ? Error::none : Error::malformed;
        case GroupPairPhase::committed:
            return h.committed_epoch == h.used_epoch ? Error::none : Error::malformed;
        default:
            return Error::malformed;
    }
}

Error GroupPairRecords::store_record(const Scope& scope, Record& record) {
    record.scope = scope;
    std::array<std::uint8_t, kRecordBytes> blob{};
    std::array<std::uint8_t, kRecordBytes> readback{};
    Writer writer{blob.data()};
    writer.bytes(kPairMagic);
    put_scope(writer, scope);
    writer.number(static_cast<std::uint8_t>(record.history.phase));
    writer.number(record.history.used_epoch);
    writer.number(record.history.committed_epoch);
    writer.bytes(record.proof);
    writer.number(configuration_crc32({blob.data(), blob.size() - 4}));
    const auto key = pair_key(scope.location.slot);
    return write_verified(key.data(), blob.data(), blob.size(), readback.data()) ? Error::none : Error::storage_failure;
}

Error GroupPairRecords::load_lane(const Scope& scope, std::uint32_t epoch, GroupPairLane lane, Image& output) {
    std::array<std::uint8_t, kLaneBytes> blob{};
    const auto key = lane_key(scope.location.slot, lane);
    const auto result = read_blob(backend_, key.data(), blob);
    if (result != Error::none) return result;
    if (!valid_blob(blob, kLaneMagic)) return Error::malformed;
    Reader reader{blob.data() + kLaneMagic.size()};
    if (!same_incarnation(scope, get_scope(reader))) return Error::wrong_incarnation;
    if (reader.number<std::uint32_t>() != epoch) return Error::bad_epoch;
    if (reader.number<std::uint8_t>() != static_cast<std::uint8_t>(lane)) return Error::malformed;
    reader.bytes(output);
    return Error::none;
}

Error GroupPairRecords::store_lane(const Scope& scope, std::uint32_t epoch, GroupPairLane lane, const Image& input) {
    std::array<std::uint8_t, kLaneBytes> blob{};
    std::array<std::uint8_t, kLaneBytes> readback{};
    Writer writer{blob.data()};
    writer.bytes(kLaneMagic);
    put_scope(writer, scope);
    writer.number(epoch);
    writer.number(static_cast<std::uint8_t>(lane));
    writer.bytes(input);
    writer.number(configuration_crc32({blob.data(), blob.size() - 4}));
    const auto key = lane_key(scope.location.slot, lane);
    return write_verified(key.data(), blob.data(), blob.size(), readback.data()) ? Error::none : Error::storage_failure;
}

bool GroupPairRecords::write_verified(const char* key, const std::uint8_t* bytes, std::size_t size,
                                      std::uint8_t* readback) {
    constexpr auto space = kGroupPairRecordsNamespace;
    constexpr auto partition = kPersistentKvPartitionLabel;
    std::size_t actual{};
    if (backend_.write_blob(partition, space, key, bytes, size) != PersistentKvBackendError::none ||
        backend_.commit(partition, space) != PersistentKvBackendError::none ||
        backend_.read_blob(partition, space, key, readback, size, actual) != PersistentKvBackendError::none ||
        actual != size || !std::equal(bytes, bytes + size, readback)) {
        faulted_ = true;
        return false;
    }
    return true;
}

Error GroupPairRecords::provision() {
    if (faulted_ || scope_) return Error::unavailable;
    const auto scope = roster_.reserve_pair(peer_);
    if (!scope || !valid_scope(*scope, scope->location.slot)) return Error::unavailable;
    Record prior{};
    const auto result = load_record(scope->location.slot, prior);
    if (result != Error::none && result != Error::missing) return result;
    if (result == Error::none && prior.scope.location.incarnation >= scope->location.incarnation) {
        return Error::wrong_incarnation;
    }
    Record record{};
    return store_record(*scope, record);
}

Error GroupPairRecords::history(GroupPairHistory& output) {
    const auto scope = current_scope();
    if (!scope) return Error::unavailable;
    Record record{};
    const auto result = load_record(scope->location.slot, record);
    if (result != Error::none) return result;
    if (!same_incarnation(*scope, record.scope)) return Error::wrong_incarnation;
    output = record.history;
    return Error::none;
}

Error GroupPairRecords::prepare(const roster::VerifiedPairBinding& proof) {
    if (scope_) return Error::wrong_state;  // Borrowed lane views never rebind.
    const auto scope = current_scope();
    if (!scope) return Error::unavailable;
    Record record{};
    const auto result = load_record(scope->location.slot, record);
    if (result != Error::none) return result;
    if (!same_incarnation(*scope, record.scope)) return Error::wrong_incarnation;
    const auto& start = proof.start();
    const bool initiator = proof.local_role() == roster::PairRole::initiator;
    if (!same_group(scope->group, start.group) || start.roster_version != scope->roster_version ||
        start.roster_digest != scope->roster_digest ||
        (initiator ? start.initiator : start.responder) != scope->local ||
        (initiator ? start.responder : start.initiator) != scope->peer) return Error::wrong_binding;
    if (std::uint64_t{proof.epoch()} != std::uint64_t{record.history.used_epoch} + 1) return Error::bad_epoch;

    scope_ = *scope;
    binding_id_ = proof.id();
    epoch_ = proof.epoch();
    if (!current_binding(*scope, roster::PairStep::bound)) {
        scope_.reset();
        return Error::wrong_binding;
    }
    if (roster::binding_bytes(start, proof.reply(), record.proof.data(), roster::kBindingBytes) !=
        roster::kBindingBytes) return Error::wrong_binding;
    auto next = std::copy(proof.reply().signature.begin(), proof.reply().signature.end(),
                          record.proof.begin() + roster::kBindingBytes);
    std::copy(proof.initiator_binding_signature().begin(), proof.initiator_binding_signature().end(), next);

    // Burn the verified epoch before replacing any lane. An interruption can
    // leave prepared public evidence but can never expose partial traffic state.
    record.history.used_epoch = epoch_;
    record.history.phase = GroupPairPhase::prepared;
    const auto prepared = store_record(*scope, record);
    if (prepared != Error::none) return prepared;
    Image empty{};
    empty.fill(0xff);
    for (std::size_t index = 0; index < lanes_.size(); ++index) {
        const auto lane = static_cast<GroupPairLane>(index);
        const auto stored = store_lane(*scope, epoch_, lane, empty);
        if (stored != Error::none) return stored;
    }
    return Error::none;
}

Error GroupPairRecords::check_lanes(const Scope& scope, std::uint32_t epoch) {
    Image image{};
    for (std::size_t index = 0; index < lanes_.size(); ++index) {
        const auto result = load_lane(scope, epoch, static_cast<GroupPairLane>(index), image);
        if (result != Error::none) return result;
    }
    return Error::none;
}

Error GroupPairRecords::live_record(Record& output, GroupPairPhase phase, roster::PairStep step) {
    const auto scope = current_scope();
    if (!scope || !current_binding(*scope, step)) return Error::unavailable;
    if (phase != GroupPairPhase::committed &&
        (scope->roster_version != scope_->roster_version || scope->roster_digest != scope_->roster_digest)) {
        return Error::wrong_binding;
    }
    const auto result = load_record(scope->location.slot, output);
    if (result != Error::none) return result;
    if (!same_incarnation(*scope, output.scope)) return Error::wrong_incarnation;
    if (output.history.phase != phase || output.history.used_epoch != epoch_) return Error::wrong_state;
    return Error::none;
}

Error GroupPairRecords::transition(GroupPairPhase from, GroupPairPhase to, roster::PairStep step) {
    Record record{};
    const auto loaded = live_record(record, from, step);
    if (loaded != Error::none) return loaded;
    const auto lanes = check_lanes(*scope_, epoch_);
    if (lanes != Error::none) return lanes;
    record.history.phase = to;
    if (to == GroupPairPhase::committed) record.history.committed_epoch = epoch_;
    return store_record(*scope_, record);
}

Error GroupPairRecords::mark_activation_possible() {
    return transition(GroupPairPhase::prepared, GroupPairPhase::activation_possible, roster::PairStep::activation_possible);
}

Error GroupPairRecords::commit() {
    return transition(GroupPairPhase::activation_possible, GroupPairPhase::committed, roster::PairStep::committed);
}

PersistentStorage* GroupPairRecords::storage(GroupPairLane lane) {
    const auto index = static_cast<std::size_t>(lane);
    Image image{};
    return index < lanes_.size() && access(lane, image) ? &lanes_[index] : nullptr;
}

bool GroupPairRecords::access(GroupPairLane lane, Image& output) {
    Record record{};
    if (live_record(record, GroupPairPhase::committed, roster::PairStep::committed) != Error::none) return false;
    for (std::size_t index = 0; index < lanes_.size(); ++index) {
        const auto candidate = static_cast<GroupPairLane>(index);
        if (candidate == lane) continue;
        Image unused{};
        if (load_lane(*scope_, epoch_, candidate, unused) != Error::none) return false;
    }
    return load_lane(*scope_, epoch_, lane, output) == Error::none;
}

bool GroupPairRecords::save(GroupPairLane lane, const Image& input) {
    return store_lane(*scope_, epoch_, lane, input) == Error::none;
}

bool GroupPairRecords::LaneStorage::valid_slot(StorageDomain domain, std::size_t slot) {
    return domain == StorageDomain::outbound_counter_state && slot < kPersistentSlotCount;
}

StorageReadResult GroupPairRecords::LaneStorage::read_slot(StorageDomain domain, std::size_t slot,
                                                          MutableStorageByteView destination) {
    if (!valid_slot(domain, slot) || !destination.data || destination.size != kPersistentSlotBytes) {
        return {StorageError::invalid_argument, 0};
    }
    Image image{};
    if (!owner_.access(lane_, image)) return {StorageError::io_failure, 0};
    std::copy_n(image.data() + slot * kPersistentSlotBytes, kPersistentSlotBytes, destination.data);
    return {StorageError::none, kPersistentSlotBytes};
}

StorageError GroupPairRecords::LaneStorage::erase_slot(StorageDomain domain, std::size_t slot) {
    if (!valid_slot(domain, slot)) return StorageError::invalid_argument;
    Image image{};
    if (!owner_.access(lane_, image)) return StorageError::io_failure;
    auto& working = working_[slot];
    working = {};
    working.bytes.fill(0xff);
    std::copy(working.bytes.begin(), working.bytes.end(), image.data() + slot * kPersistentSlotBytes);
    if (!owner_.save(lane_, image)) return StorageError::io_failure;
    working.erased = true;
    return StorageError::none;
}

StorageError GroupPairRecords::LaneStorage::write_slot(StorageDomain domain, std::size_t slot,
                                                       std::size_t offset, StorageByteView source) {
    if (!valid_slot(domain, slot) || !source.data || !source.size ||
        offset > kPersistentSlotBytes || source.size > kPersistentSlotBytes - offset) {
        return StorageError::invalid_argument;
    }
    auto& working = working_[slot];
    if (!working.erased) return StorageError::write_requires_erase;
    for (std::size_t index = 0; index < source.size; ++index) {
        if ((working.bytes[offset + index] & source.data[index]) != source.data[index]) {
            return StorageError::write_requires_erase;
        }
    }
    std::copy_n(source.data, source.size, working.bytes.data() + offset);
    working.dirty = true;
    return StorageError::none;
}

StorageError GroupPairRecords::LaneStorage::sync_slot(StorageDomain domain, std::size_t slot) {
    if (!valid_slot(domain, slot)) return StorageError::invalid_argument;
    auto& working = working_[slot];
    if (!working.erased) return StorageError::write_requires_erase;
    Image image{};
    if (!owner_.access(lane_, image)) return StorageError::io_failure;
    if (!working.dirty) return StorageError::none;
    std::copy(working.bytes.begin(), working.bytes.end(), image.data() + slot * kPersistentSlotBytes);
    if (!owner_.save(lane_, image)) return StorageError::io_failure;
    working.dirty = false;
    return StorageError::none;
}

}  // namespace opentrail::persistence
