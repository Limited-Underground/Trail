#include "opentrail/group_pair.hpp"
#include "opentrail/durable_group_roster.hpp"

#include <algorithm>

#include "opentrail/configuration_store.hpp"

namespace opentrail::identity::roster {
namespace {

using Error = DurableRosterError;
using KvError = persistence::PersistentKvBackendError;
using IndexError = persistence::GroupPairIndexError;
constexpr auto& kPartition = persistence::kPersistentKvPartitionLabel;
constexpr char kNamespace[] = "ot_group_rost";
constexpr const char* kKeys[] = {"g0", "g1", "g2", "g3"};
constexpr std::array<std::uint8_t, 4> kMagic{'O', 'T', 'R', '1'};
static_assert(std::size(kKeys) == persistence::GroupPairIndex::kMaximumGroups);
static_assert(sizeof(kNamespace) - 1 <= 15);

template <typename T>
void put(std::uint8_t*& out, T value) {
    for (std::size_t i = sizeof(T); i != 0; --i) {
        *out++ = static_cast<std::uint8_t>(value >> (8 * (i - 1)));
    }
}

template <typename T>
T get(const std::uint8_t*& in) {
    T value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) value = static_cast<T>((value << 8) | *in++);
    return value;
}

template <std::size_t N>
void put_bytes(std::uint8_t*& out, const std::array<std::uint8_t, N>& bytes) {
    out = std::copy(bytes.begin(), bytes.end(), out);
}

template <std::size_t N>
std::array<std::uint8_t, N> get_bytes(const std::uint8_t*& in) {
    std::array<std::uint8_t, N> bytes{};
    std::copy_n(in, N, bytes.data());
    in += N;
    return bytes;
}

const Member* member(const Roster& roster, const PublicKey& key) {
    const auto end = roster.members.begin() + roster.member_count;
    const auto found = std::find_if(roster.members.begin(), end, [&key](const Member& m) {
        return m.key == key;
    });
    return found == end ? nullptr : &*found;
}

class Busy {
public:
    explicit Busy(bool& busy) : busy_(busy) { busy_ = true; }
    ~Busy() { busy_ = false; }
private:
    bool& busy_;
};

}  // namespace

bool DurableGroupRoster::Record::intact() const {
    const auto* crc = bytes.data() + kCrc;
    return std::equal(kMagic.begin(), kMagic.end(), bytes.begin()) &&
           get<std::uint32_t>(crc) == persistence::configuration_crc32({bytes.data(), kCrc});
}

bool DurableGroupRoster::Record::names(std::uint64_t group, const PublicKey& admin,
                                      const PublicKey& local) const {
    const auto* in = bytes.data() + kMagic.size();
    return get<std::uint64_t>(in) == group &&
           get_bytes<kPublicKeyBytes>(in) == admin && get_bytes<kPublicKeyBytes>(in) == local;
}

DurableGroupRoster::DurableGroupRoster(persistence::PersistentKvBackend& backend,
    persistence::GroupPairIndex& index, const RosterCrypto& crypto, const PublicKey& admin,
    std::uint64_t group, const PublicKey& local, std::uint32_t minimum_version)
    : backend_(backend), index_(index), crypto_(crypto), admin_(admin), local_(local),
      group_(group), minimum_version_(minimum_version) {}

bool DurableGroupRoster::encode_record(const RosterState& state, Record& record) const {
    record.bytes.fill(0);
    auto* out = record.bytes.data();
    put_bytes(out, kMagic);
    put(out, group_);
    put_bytes(out, admin_);
    put_bytes(out, local_);
    put(out, minimum_version_);
    put(out, static_cast<std::uint8_t>(state.state_));
    put(out, static_cast<std::uint8_t>(state.conflict_));
    put(out, static_cast<std::uint8_t>(state.known_next_));
    put(out, static_cast<std::uint8_t>(state.resolved_next_));
    std::uint8_t accepted = 0;
    for (std::size_t i = 0; i < state.held_.member_count; ++i) {
        if (state.accepted(state.held_.members[i].key)) accepted |= 1U << i;
    }
    put(out, accepted);
    auto* length = out;
    out += sizeof(std::uint16_t);
    put_bytes(out, state.conflict_digest_);
    for (const auto& known : state.known_) {
        put(out, known.version);
        put_bytes(out, known.digest);
    }
    for (const auto& digest : state.resolved_) put_bytes(out, digest);
    const auto size = state.has_roster_ ? encode(state.held_, out, kMaximumRosterBytes) : 0;
    if (state.has_roster_ && size == 0) return false;
    put(length, static_cast<std::uint16_t>(size));
    auto* crc = record.bytes.data() + Record::kCrc;
    put(crc, persistence::configuration_crc32({record.bytes.data(), Record::kCrc}));
    return true;
}

bool DurableGroupRoster::decode_record(const Record& record, RosterState& state) {
    const auto* in = record.bytes.data() + Record::kScope;
    if (get<std::uint32_t>(in) != minimum_version_) return false;
    const auto stored = static_cast<State>(get<std::uint8_t>(in));
    const auto conflict = static_cast<ConflictKind>(get<std::uint8_t>(in));
    const auto known_next = get<std::uint8_t>(in);
    const auto resolved_next = get<std::uint8_t>(in);
    const auto accepted = get<std::uint8_t>(in);
    const auto size = get<std::uint16_t>(in);
    const auto conflict_digest = get_bytes<kDigestBytes>(in);
    const auto* seen = in;
    const auto* signed_roster = seen + kKnownDigests * (4 + kDigestBytes) + kResolvedConflicts * kDigestBytes;
    if (stored < State::awaiting_roster || stored > State::distrusted ||
        conflict > ConflictKind::known_version_mismatch ||
        (stored == State::conflict) != (conflict != ConflictKind::none) ||
        (stored == State::conflict) != (conflict_digest != Digest{}) ||
        known_next >= kKnownDigests || resolved_next >= kResolvedConflicts ||
        size > kMaximumRosterBytes ||
        std::any_of(signed_roster + size, signed_roster + kMaximumRosterBytes,
                    [](std::uint8_t b) { return b != 0; })) return false;

    if (size == 0) {
        if ((stored != State::awaiting_roster && stored != State::distrusted) || accepted ||
            known_next || resolved_next ||
            std::any_of(seen, signed_roster, [](std::uint8_t b) { return b != 0; })) return false;
        if (!state.initialize(admin_, group_, local_, minimum_version_)) return false;
        state.state_ = stored;
        return true;
    }

    auto& parsed = workspace_.parsed;
    parsed = Roster{};
    if (decode(signed_roster, size, parsed) != FormatError::none) return false;
    const auto* self = member(parsed, local_);
    if (stored == State::awaiting_roster || (stored == State::left && self) ||
        (!self && stored != State::left && stored != State::distrusted)) return false;
    // On reopen the trusted roster can record this board's removal. Validate it
    // with the pinned admin and the same parser before restoring the left state.
    if (!state.initialize(admin_, group_, self ? local_ : admin_, minimum_version_) ||
        state.offer(signed_roster, size, crypto_) != Outcome::accepted) return false;
    state.own_ = local_;
    bool has_current = false;
    for (std::size_t i = 0; i < state.known_.size(); ++i) {
        const auto version = get<std::uint32_t>(in);
        const auto digest = get_bytes<kDigestBytes>(in);
        if (version > parsed.version || (version == 0 && digest != Digest{})) return false;
        if (version == parsed.version) {
            if (digest != state.held_digest_ || has_current) return false;
            has_current = true;
        }
        for (std::size_t j = 0; version != 0 && j < i; ++j) {
            if (state.known_[j].version == version) return false;
        }
        state.known_[i] = {version, digest};
    }
    if (!has_current) return false;
    for (auto& digest : state.resolved_) digest = get_bytes<kDigestBytes>(in);
    if ((accepted >> parsed.member_count) != 0 || (!self && accepted != 0)) return false;
    for (std::size_t i = 0; i < parsed.member_count; ++i) {
        if ((accepted & (1U << i)) && !state.accept_member(parsed.members[i].key)) return false;
    }
    state.state_ = stored;
    state.conflict_ = conflict;
    state.conflict_digest_ = conflict_digest;
    state.known_next_ = known_next;
    state.resolved_next_ = resolved_next;
    return true;
}

DurableRosterError DurableGroupRoster::read(persistence::PersistentKvBackend& backend, const char* key,
                                         Record& record) {
    std::size_t size = 0;
    const auto result = backend.read_blob(
        kPartition, kNamespace, key, record.bytes.data(), record.bytes.size(), size);
    if (result == KvError::not_found) return Error::missing;
    if (result == KvError::invalid_argument) return Error::invalid_record;
    if (result != KvError::none) return Error::storage_failure;
    return size == record.bytes.size() && record.intact() ? Error::none : Error::invalid_record;
}

DurableRosterError DurableGroupRoster::inspect_slot(persistence::PersistentKvBackend& backend,
                                                   std::size_t slot, DurableRosterSlot& output) {
    if (slot >= std::size(kKeys)) return Error::invalid_scope;
    Record record{};
    const auto loaded = read(backend, kKeys[slot], record);
    if (loaded != Error::none) return loaded;

    DurableRosterSlot candidate{};
    const auto* in = record.bytes.data() + kMagic.size();
    candidate.group_id = get<std::uint64_t>(in);
    candidate.admin = get_bytes<kPublicKeyBytes>(in);
    candidate.local = get_bytes<kPublicKeyBytes>(in);
    candidate.minimum_version = get<std::uint32_t>(in);
    if (!candidate.group_id || !candidate.minimum_version ||
        candidate.admin == PublicKey{} || candidate.local == PublicKey{}) {
        return Error::invalid_record;
    }
    output = candidate;
    return Error::none;
}

DurableRosterError DurableGroupRoster::verify_current() {
    if (faulted_) return Error::storage_failure;
    if (!ready_) return Error::not_ready;
    auto& stored = workspace_.readback;
    auto& expected = workspace_.serialized;
    const auto loaded = read(backend_, key_, stored);
    if (loaded != Error::none || !encode_record(state_, expected) || stored.bytes != expected.bytes) {
        ready_ = false;
        return loaded == Error::none ? Error::invalid_record : loaded;
    }
    return Error::none;
}

DurableRosterError DurableGroupRoster::reconcile() {
    if (state_.state() == State::active || state_.state() == State::left ||
        (state_.state() == State::distrusted && state_.has_roster())) {
        if (index_.prune(state_) != IndexError::none) return Error::index_failure;
    } else {
        const auto status = index_.group_status(group_name_);
        if (status != IndexError::none && (status != IndexError::not_indexed || state_.has_roster())) {
            return Error::index_failure;
        }
    }
    ready_ = true;
    return Error::none;
}

DurableRosterError DurableGroupRoster::save(const char* key, const RosterState& next) {
    auto& bytes = workspace_.serialized;
    auto& check = workspace_.readback;
    if (!encode_record(next, bytes)) return Error::invalid_record;
    if (backend_.write_blob(kPartition, kNamespace, key, bytes.bytes.data(), bytes.bytes.size()) != KvError::none ||
        backend_.commit(kPartition, kNamespace) != KvError::none ||
        read(backend_, key, check) != Error::none || check.bytes != bytes.bytes) {
        ready_ = false;
        faulted_ = true;
        return Error::storage_failure;
    }
    key_ = key;
    state_ = next;
    ready_ = false;
    return reconcile();
}

DurableRosterError DurableGroupRoster::open(bool create, const std::uint8_t* initial, std::size_t size) {
    if (busy_) return Error::not_ready;
    const Busy hold(busy_);
    ready_ = false;
    if (faulted_) return Error::storage_failure;
    group_name_.group_id = group_;
    if (!group_ || !minimum_version_ || admin_ == PublicKey{} || local_ == PublicKey{} ||
        !crypto_.fingerprint(admin_, group_name_.admin) || group_name_.admin == Fingerprint{} ||
        !crypto_.fingerprint(local_, local_fingerprint_) || local_fingerprint_ == Fingerprint{} ||
        index_.local_identity() != local_fingerprint_) {
        return Error::invalid_scope;
    }

    const char* vacant = nullptr;
    const char* matched = nullptr;
    auto& next = workspace_.next;
    next = RosterState{};
    for (const char* key : kKeys) {
        auto& record = workspace_.readback;
        const auto loaded = read(backend_, key, record);
        if (loaded == Error::missing) {
            if (!vacant) vacant = key;
            continue;
        }
        if (loaded != Error::none) return loaded;
        if (!record.names(group_, admin_, local_)) continue;
        if (matched || !decode_record(record, next)) return Error::invalid_record;
        matched = key;
    }
    if (matched) {
        if (initial) return Error::refused;
        key_ = matched;
        state_ = next;
        return reconcile();
    }
    if (!create) return Error::missing;
    const auto in_index = index_.group_status(group_name_, initial != nullptr);
    if (in_index == IndexError::none) return Error::missing;
    if (in_index == IndexError::capacity) return Error::capacity;
    if (in_index != IndexError::not_indexed) return Error::index_failure;
    if (!vacant) return Error::capacity;
    if (!next.initialize(admin_, group_, local_, minimum_version_)) return Error::invalid_scope;
    if (initial && (next.offer(initial, size, crypto_) != Outcome::accepted ||
                    next.roster().version != 1)) return Error::invalid_record;
    return save(vacant, next);
}

DurableRosterError DurableGroupRoster::initialize() { return open(true); }

DurableRosterError DurableGroupRoster::create_owned(const std::uint8_t* bytes, std::size_t size) {
    if (admin_ != local_ || minimum_version_ != 1) return Error::invalid_scope;
    if (!bytes || size == 0) return Error::invalid_record;
    return open(true, bytes, size);
}

DurableRosterError DurableGroupRoster::reopen() { return open(false); }

DurableRosterError DurableGroupRoster::hydrate_group(GroupState& live) {
    if (busy_) return Error::not_ready;
    const Busy hold(busy_);
    const auto ready = verify_current();
    if (ready != Error::none) return ready;
    if (state_.state() != State::active) return Error::refused;
    return live.hydrate_roster(state_) ? Error::none : Error::invalid_scope;
}

DurableRosterResult DurableGroupRoster::offer(const std::uint8_t* bytes, std::size_t size) {
    if (busy_) return {Error::not_ready};
    const Busy hold(busy_);
    const auto ready = verify_current();
    if (ready != Error::none) return {ready};
    auto& next = workspace_.next;
    next = state_;
    const auto outcome = next.offer(bytes, size, crypto_);
    if (outcome != Outcome::accepted && outcome != Outcome::accepted_removed_self && outcome != Outcome::conflict) {
        return {Error::none, outcome};
    }
    const auto* prior = member(state_.roster(), local_);
    const auto* current = member(next.roster(), local_);
    if (!current || (prior && current->added_in != prior->added_in)) {
        next.accepted_ = {};
        next.accepted_count_ = 0;
    }
    const auto saved = save(key_, next);
    return saved == Error::none ? DurableRosterResult{saved, outcome} : DurableRosterResult{saved};
}

DurableRosterError DurableGroupRoster::accept_member(const PublicKey& peer) {
    if (busy_) return Error::not_ready;
    const Busy hold(busy_);
    const auto ready = verify_current();
    if (ready != Error::none) return ready;
    auto& next = workspace_.next;
    next = state_;
    if (!next.accept_member(peer)) return Error::refused;
    return state_.accepted(peer) ? Error::none : save(key_, next);
}

DurableRosterError DurableGroupRoster::keep_current() {
    if (busy_) return Error::not_ready;
    const Busy hold(busy_);
    const auto ready = verify_current();
    if (ready != Error::none) return ready;
    auto& next = workspace_.next;
    next = state_;
    return next.keep_current() ? save(key_, next) : Error::refused;
}

DurableRosterError DurableGroupRoster::stop_trusting() {
    if (busy_) return Error::not_ready;
    const Busy hold(busy_);
    const auto ready = verify_current();
    if (ready != Error::none) return ready;
    if (state_.state() == State::distrusted) return Error::none;
    auto& next = workspace_.next;
    next = state_;
    next.stop_trusting();
    next.conflict_digest_ = {};
    return save(key_, next);
}

DurableRosterError DurableGroupRoster::snapshot(DurableRosterSnapshot& output, std::uint8_t* out,
                                               std::size_t capacity) {
    if (busy_) return Error::not_ready;
    const Busy hold(busy_);
    const auto ready = verify_current();
    if (ready != Error::none) return ready;
    const auto trust = state_.state();
    if ((trust != State::active && trust != State::distrusted) || !state_.has_roster()) {
        return Error::refused;
    }
    const auto encoded = encode(state_.roster(), out, capacity);
    if (encoded == 0) return Error::refused;
    output = {group_name_, state_.digest(), trust, state_.roster().version, encoded};
    return Error::none;
}

std::optional<LocationRule> DurableGroupRoster::read_active_location_rule(
    std::size_t slot, std::uint32_t version, const Fingerprint& admin, const Digest& digest) {
    if (busy_ || faulted_ || slot >= std::size(kKeys) || version == 0 ||
        admin == Fingerprint{} || digest == Digest{}) return std::nullopt;
    const Busy hold(busy_);

    auto& record = workspace_.readback;
    if (read(backend_, kKeys[slot], record) != Error::none || !record.names(group_, admin_, local_)) {
        return std::nullopt;
    }
    Fingerprint actual_admin{};
    if (!crypto_.fingerprint(admin_, actual_admin) || actual_admin != admin) return std::nullopt;

    auto& verified = workspace_.next;
    verified = {};
    if (!decode_record(record, verified) || verified.state() != State::active ||
        !verified.has_roster() || verified.roster().version != version ||
        verified.digest() != digest) return std::nullopt;
    return verified.roster().location_rule;
}

std::size_t DurableGroupRoster::copy_signed_roster(std::uint8_t* out, std::size_t capacity) {
    if (busy_) return 0;
    const Busy hold(busy_);
    if (verify_current() != Error::none || state_.state() != State::active) return 0;
    return encode(state_.roster(), out, capacity);
}

std::optional<DurablePairScope> DurableGroupRoster::lookup(const Fingerprint& peer, bool reserve) {
    if (busy_) return std::nullopt;
    const Busy hold(busy_);
    if (verify_current() != Error::none || state_.state() != State::active || peer == local_fingerprint_) {
        return std::nullopt;
    }
    PublicKey peer_key{};
    if (!state_.key_for(peer, peer_key)) return std::nullopt;
    const auto* self = member(state_.roster(), local_);
    const auto* remote = member(state_.roster(), peer_key);
    if (!self || !remote || (remote->added_in > self->added_in && !state_.accepted(peer_key))) {
        return std::nullopt;
    }
    auto found = index_.find(state_, peer);
    if (reserve) {
        if (found.error != IndexError::not_indexed) return std::nullopt;
        found = index_.reserve(state_, peer);
    }
    if (!found.location) return std::nullopt;
    return DurablePairScope{group_name_, local_fingerprint_, peer, self->added_in, remote->added_in,
                           state_.roster().version, state_.digest(), *found.location};
}

std::optional<DurablePairScope> DurableGroupRoster::find_pair(const Fingerprint& peer) {
    return lookup(peer, false);
}
std::optional<DurablePairScope> DurableGroupRoster::reserve_pair(const Fingerprint& peer) {
    return lookup(peer, true);
}

}  // namespace opentrail::identity::roster
