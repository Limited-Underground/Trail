#pragma once
// D-112 public pairing decisions. Dedicated storage owns both five-slot banks.
// A durable vote records an already authorized local decision. It creates no
// identity, gesture, membership or traffic authority. The session supplies those
// checks and re-verifies the retained binding before recovery or signing.
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include "opentrail/enrollment_identity_store.hpp"
#include "opentrail/persistent_storage.hpp"

namespace opentrail::security_evaluation {
enum class PairingCompletionPhase : std::uint8_t {
    none, confirmed, prepared, completing, committed, peer_confirmed, aborted, uncertain
};
enum class PairingCompletionKind : std::uint8_t { prepared = 1, committed = 2, aborted = 3 };
using PairingCompletionReceipt = std::array<std::uint8_t, 100>;

struct PairingCompletionSnapshot {
    PairingCompletionPhase phase{PairingCompletionPhase::none};
    InvitationKey transaction{}, peer{};
    std::uint64_t generation{};
};
struct PairingCompletionRecord {
    InvitationKey transaction{}, peer{}, transcript{}, decision{};
    std::uint64_t group{}, generation{};
    std::uint32_t epoch{};
    InvitationRole role{};
    PairingCompletionPhase phase{PairingCompletionPhase::confirmed};
    std::uint8_t flags{};
    std::array<std::uint8_t, 64> peer_prepared_signature{}, peer_committed_signature{};
};

inline std::array<std::uint8_t, 52> pairing_receipt_signing_bytes(const PairingCompletionReceipt& receipt) {
    std::array<std::uint8_t, 52> bytes{};
    constexpr std::array<std::uint8_t, 16> domain{
        'O','T','-','P','A','I','R','-','R','E','C','E','I','P','T',1};
    std::memcpy(bytes.data(), domain.data(), domain.size());
    std::memcpy(bytes.data() + domain.size(), receipt.data(), 36);
    return bytes;
}

class PairingCompletionJournal final {
public:
    using Record = PairingCompletionRecord;
    using Guard = bool (*)(void*);
    static constexpr std::uint8_t kLocalPrepared = 1, kLocalAborted = 2;
    static constexpr std::uint8_t kPeerPrepared = 4, kPeerCommitted = 8, kPeerAborted = 16;

    explicit PairingCompletionJournal(persistence::PersistentStorage& storage) : storage_(storage) {}
    PairingCompletionJournal(const PairingCompletionJournal&) = delete;
    PairingCompletionJournal& operator=(const PairingCompletionJournal&) = delete;

    bool initialize() {
        return operation([&] {
            if (initialized_) return false;
            initialized_ = true;
            if (!read(retained_)) return refuse();
            if (blank(retained_[0]) && blank(retained_[1])) {
                empty_ = true;
                return true;
            }
            Record records[2]{};
            std::uint64_t sequences[2]{};
            bool valid[2]{};
            for (unsigned slot = 0; slot < 2; ++slot) {
                valid[slot] = decode(retained_[slot], records[slot], sequences[slot]) &&
                              (sequences[slot] - 1) % 2 == slot;
            }
            if (!valid[0] && !valid[1]) {
                // Marker state alone says nothing about an unknown payload.
                // Only an ordered prefix of the two unvoted first records can
                // be logical empty; preserve every observed byte until begin().
                if (blank(retained_[1]) && incomplete_marker(marker(retained_[0])) &&
                    canonical_prefix(retained_[0], nullptr, 1, required_payload(marker(retained_[0])))) {
                    empty_ = true;
                    return true;
                }
                return refuse();
            }
            const unsigned latest = !valid[0] ? 1 : !valid[1] ? 0 :
                                    sequences[1] > sequences[0] ? 1 : 0;
            const unsigned older = 1 - latest;
            if (valid[older] &&
                (sequences[latest] != sequences[older] + 1 ||
                 !follows(records[older], records[latest]))) return refuse();
            if (!valid[older] && !blank(retained_[older]) &&
                !interrupted_bank(retained_[older], records[latest], sequences[latest])) return refuse();
            record_ = records[latest];
            sequence_ = sequences[latest];
            current_slot_ = latest;
            return true;
        });
    }
    // Exact readback for an owner about to act on the decision.
    bool load(Record& output) {
        Record staged{};
        const bool ok = operation([&] {
            if (!initialized_ || empty_ || !exact()) return false;
            staged = record_;
            return true;
        });
        if (ok) output = staged;
        return ok;
    }
    // Cached immutable observation used for display only.
    PairingCompletionSnapshot snapshot() const {
        if (!initialized_) return {};
        if (failed_ || busy_) return {PairingCompletionPhase::uncertain, record_.transaction, record_.peer, record_.generation};
        if (empty_) return {};
        return {record_.phase, record_.transaction, record_.peer, record_.generation};
    }
    bool locally_prepared() const { return observable() && (record_.flags & kLocalPrepared) != 0; }
    bool has_peer_prepared() const { return observable() && (record_.flags & kPeerPrepared) != 0; }
    bool has_peer_committed() const { return observable() && (record_.flags & kPeerCommitted) != 0; }
    bool failed() const { return failed_; }

    bool begin(const Record& record, Guard guard = nullptr, void* context = nullptr) {
        return guarded(guard, context, [&] {
            if (!initialized_ || !exact() || record.phase != PairingCompletionPhase::confirmed ||
                record.flags != 0 || !valid_record(record)) return false;
            if (!empty_ && !follows(record_, record)) return false;
            return persist(record);
        });
    }
    bool prepare(Guard guard = nullptr, void* context = nullptr, Guard reserve = nullptr) {
        vote_reservation_ = reserve;
        const bool result = update(guard, context, [&](Record& next) {
            if (next.flags & kLocalPrepared) return true;
            if (next.phase != PairingCompletionPhase::confirmed || (next.flags & (kLocalAborted | kPeerAborted))) return false;
            next.flags |= kLocalPrepared;
            next.phase = PairingCompletionPhase::prepared;
            return true;
        });
        vote_reservation_ = nullptr;
        return result;
    }
    bool mark_completing(Guard guard = nullptr, void* context = nullptr) {
        return update(guard, context, [&](Record& next) {
            if (!positive_pair(next) || terminal(next.phase)) return false;
            if (next.phase == PairingCompletionPhase::committed || next.phase == PairingCompletionPhase::peer_confirmed) return true;
            next.phase = PairingCompletionPhase::completing;
            return true;
        });
    }
    bool mark_committed(Guard guard = nullptr, void* context = nullptr) {
        return update(guard, context, [&](Record& next) {
            if (!positive_pair(next) || terminal(next.phase)) return false;
            if (next.phase == PairingCompletionPhase::committed || next.phase == PairingCompletionPhase::peer_confirmed) return true;
            if (next.phase != PairingCompletionPhase::completing) return false;
            next.phase = PairingCompletionPhase::committed;
            return true;
        });
    }
    bool mark_peer_confirmed(Guard guard = nullptr, void* context = nullptr) {
        return update(guard, context, [&](Record& next) {
            if (!positive_pair(next) || !(next.flags & kPeerCommitted) ||
                (next.phase != PairingCompletionPhase::committed && next.phase != PairingCompletionPhase::peer_confirmed)) return false;
            next.phase = PairingCompletionPhase::peer_confirmed;
            return true;
        });
    }
    bool abort(Guard guard = nullptr, void* context = nullptr) {
        return update(guard, context, [&](Record& next) {
            if (next.flags & kLocalPrepared) return false;
            if (next.flags & kLocalAborted) return true;
            if (next.phase != PairingCompletionPhase::confirmed && next.phase != PairingCompletionPhase::aborted) return false;
            next.flags |= kLocalAborted;
            next.phase = PairingCompletionPhase::aborted;
            return true;
        });
    }
    bool accept_peer(const PairingCompletionReceipt& receipt, Guard guard = nullptr, void* context = nullptr) {
        return update(guard, context, [&](Record& next) {
            const auto peer_role = next.role == InvitationRole::initiator ? InvitationRole::responder : InvitationRole::initiator;
            if (receipt[0] != 1 || receipt[2] != static_cast<std::uint8_t>(peer_role) || receipt[3] != 0 ||
                std::memcmp(receipt.data() + 4, next.transaction.data(), 32) != 0) return false;
            const auto bytes = pairing_receipt_signing_bytes(receipt);
            if (crypto_sign_verify_detached(receipt.data() + 36, bytes.data(), bytes.size(), next.peer.data()) != 0) return false;
            const auto kind = static_cast<PairingCompletionKind>(receipt[1]);
            if (kind == PairingCompletionKind::prepared) {
                if (next.flags & kPeerAborted) return false;
                if (next.flags & kPeerPrepared)
                    return std::memcmp(next.peer_prepared_signature.data(), receipt.data() + 36, 64) == 0;
                std::memcpy(next.peer_prepared_signature.data(), receipt.data() + 36, 64);
                next.flags |= kPeerPrepared;
                return true;
            }
            if (kind == PairingCompletionKind::committed) {
                if (!positive_pair(next)) return false;
                if (next.flags & kPeerCommitted)
                    return std::memcmp(next.peer_committed_signature.data(), receipt.data() + 36, 64) == 0;
                std::memcpy(next.peer_committed_signature.data(), receipt.data() + 36, 64);
                next.flags |= kPeerCommitted;
                return true;
            }
            if (kind == PairingCompletionKind::aborted) {
                if (next.flags & (kPeerPrepared | kPeerCommitted)) return false;
                if (next.phase == PairingCompletionPhase::committed || next.phase == PairingCompletionPhase::peer_confirmed) return false;
                if (next.flags & kPeerAborted)
                    return std::memcmp(next.peer_prepared_signature.data(), receipt.data() + 36, 64) == 0;
                std::memcpy(next.peer_prepared_signature.data(), receipt.data() + 36, 64);
                next.flags |= kPeerAborted;
                next.phase = PairingCompletionPhase::aborted;
                return true;
            }
            return false;
        });
    }
    bool export_receipt(PairingCompletionKind kind, EnrollmentIdentityStore& identity, PairingCompletionReceipt& output) {
        PairingCompletionReceipt staged{};
        const bool ok = operation([&] {
            if (!initialized_ || empty_ || !exact()) return false;
            const bool available = kind == PairingCompletionKind::prepared ? (record_.flags & kLocalPrepared) != 0 :
                kind == PairingCompletionKind::aborted ? (record_.flags & kLocalAborted) != 0 :
                kind == PairingCompletionKind::committed &&
                    (record_.phase == PairingCompletionPhase::committed || record_.phase == PairingCompletionPhase::peer_confirmed);
            InvitationKey own{};
            if (!available || !identity.public_key(own) || own == record_.peer) return false;
            staged[0] = 1;
            staged[1] = static_cast<std::uint8_t>(kind);
            staged[2] = static_cast<std::uint8_t>(record_.role);
            std::memcpy(staged.data() + 4, record_.transaction.data(), 32);
            const auto bytes = pairing_receipt_signing_bytes(staged);
            std::array<std::uint8_t, 64> signature{};
            if (!identity.sign(bytes.data(), bytes.size(), signature) || !exact()) return false;
            std::memcpy(staged.data() + 36, signature.data(), signature.size());
            return true;
        });
        if (ok) output = staged;
        return ok;
    }

private:
    friend class EnrollmentCandidateSession;
    // Only the session may reserve cleanup of an exact pre-C1 candidate.
    // This starts with a terminal negative decision and has no positive path.
    bool reserve_unvoted_abort(const Record& record, Guard guard, void* context) {
        return guarded(guard, context, [&] {
            return guard && initialized_ && exact() &&
                   (empty_ || (record_.phase == PairingCompletionPhase::aborted &&
                               record.generation > record_.generation)) &&
                   record.phase == PairingCompletionPhase::aborted &&
                   record.flags == kLocalAborted && valid_record(record) && persist(record);
        });
    }
    using Bank = std::array<std::array<std::uint8_t, 64>, 5>;
    using Media = std::array<Bank, 2>;
    static_assert(persistence::kStorageDomainCount == 5 && persistence::kPersistentSlotCount == 2 &&
                  persistence::kPersistentSlotBytes == 64);
    static constexpr std::uint32_t kMarker = 0x32435044U;
    static void put(std::uint8_t* out, std::uint64_t value, unsigned bytes) {
        for (unsigned i = 0; i < bytes; ++i) out[i] = static_cast<std::uint8_t>(value >> (8 * i));
    }
    static std::uint64_t get(const std::uint8_t* in, unsigned bytes) {
        std::uint64_t value = 0;
        for (unsigned i = 0; i < bytes; ++i) value |= static_cast<std::uint64_t>(in[i]) << (8 * i);
        return value;
    }
    static std::array<std::uint8_t, 320> flatten(const Bank& bank) {
        std::array<std::uint8_t, 320> bytes{};
        for (unsigned d = 0; d < 5; ++d) std::memcpy(bytes.data() + d * 64, bank[d].data(), 64);
        return bytes;
    }
    static std::uint32_t marker(const Bank& bank) { return static_cast<std::uint32_t>(get(bank[4].data() + 60, 4)); }
    static bool blank(const Bank& bank) {
        for (const auto& slot : bank) for (const auto byte : slot) if (byte != 0xff) return false;
        return true;
    }
    static bool nonzero(const std::array<std::uint8_t, 64>& value) {
        for (const auto byte : value) if (byte) return true;
        return false;
    }
    static bool terminal(PairingCompletionPhase phase) {
        return phase == PairingCompletionPhase::aborted || phase == PairingCompletionPhase::uncertain;
    }
    static bool positive_pair(const Record& record) {
        return (record.flags & (kLocalPrepared | kPeerPrepared)) == (kLocalPrepared | kPeerPrepared) &&
               !(record.flags & (kLocalAborted | kPeerAborted));
    }
    static bool valid_record(const Record& record) {
        if (!invitation_detail::nonzero(record.transaction) || !invitation_detail::nonzero(record.peer) ||
            !invitation_detail::nonzero(record.transcript) || !invitation_detail::nonzero(record.decision) ||
            !record.group || !record.generation || !record.epoch ||
            (record.role != InvitationRole::initiator && record.role != InvitationRole::responder) ||
            record.phase <= PairingCompletionPhase::none || record.phase >= PairingCompletionPhase::uncertain ||
            (record.flags & ~std::uint8_t{31}) ||
            (record.flags & kLocalPrepared && record.flags & kLocalAborted) ||
            (record.flags & kPeerPrepared && record.flags & kPeerAborted) ||
            (record.flags & kPeerCommitted && !(record.flags & kPeerPrepared))) return false;
        const bool peer_decision = (record.flags & (kPeerPrepared | kPeerAborted)) != 0;
        if (peer_decision != nonzero(record.peer_prepared_signature) ||
            ((record.flags & kPeerCommitted) != 0) != nonzero(record.peer_committed_signature)) return false;
        switch (record.phase) {
            case PairingCompletionPhase::confirmed: return !(record.flags & (kLocalPrepared | kLocalAborted | kPeerAborted | kPeerCommitted));
            case PairingCompletionPhase::prepared: return (record.flags & kLocalPrepared) && !(record.flags & (kLocalAborted | kPeerAborted));
            case PairingCompletionPhase::completing:
            case PairingCompletionPhase::committed: return positive_pair(record);
            case PairingCompletionPhase::peer_confirmed: return positive_pair(record) && (record.flags & kPeerCommitted);
            case PairingCompletionPhase::aborted: return (record.flags & (kLocalAborted | kPeerAborted)) != 0;
            default: return false;
        }
    }
    static bool same_descriptor(const Record& a, const Record& b) {
        return a.transaction == b.transaction && a.peer == b.peer && a.transcript == b.transcript &&
               a.decision == b.decision && a.group == b.group && a.generation == b.generation &&
               a.epoch == b.epoch && a.role == b.role;
    }
    static bool follows(const Record& a, const Record& b) {
        if (!same_descriptor(a, b)) {
            return (a.phase == PairingCompletionPhase::peer_confirmed || a.phase == PairingCompletionPhase::aborted) &&
                   ((b.phase == PairingCompletionPhase::confirmed && b.flags == 0) ||
                    (a.phase == PairingCompletionPhase::aborted && b.phase == PairingCompletionPhase::aborted &&
                     b.flags == kLocalAborted)) && b.generation > a.generation;
        }
        if ((a.flags & b.flags) != a.flags) return false;
        if ((a.flags & (kPeerPrepared | kPeerAborted)) && a.peer_prepared_signature != b.peer_prepared_signature) return false;
        if ((a.flags & kPeerCommitted) && a.peer_committed_signature != b.peer_committed_signature) return false;
        if (a.phase == PairingCompletionPhase::aborted) return b.phase == a.phase;
        if (b.phase == PairingCompletionPhase::aborted) return (b.flags & (kLocalAborted | kPeerAborted)) != 0;
        return static_cast<unsigned>(b.phase) >= static_cast<unsigned>(a.phase);
    }
    static Bank encode(const Record& record, std::uint64_t sequence) {
        std::array<std::uint8_t, 320> bytes{};
        std::memcpy(bytes.data(), "OTPC", 4);
        bytes[4] = 1; bytes[5] = static_cast<std::uint8_t>(record.role);
        bytes[6] = static_cast<std::uint8_t>(record.phase); bytes[7] = record.flags;
        put(bytes.data() + 8, sequence, 8); put(bytes.data() + 16, record.group, 8);
        put(bytes.data() + 24, record.generation, 8); put(bytes.data() + 32, record.epoch, 4);
        std::memcpy(bytes.data() + 48, record.transaction.data(), 32);
        std::memcpy(bytes.data() + 80, record.peer.data(), 32);
        std::memcpy(bytes.data() + 112, record.transcript.data(), 32);
        std::memcpy(bytes.data() + 144, record.decision.data(), 32);
        std::memcpy(bytes.data() + 176, record.peer_prepared_signature.data(), 64);
        std::memcpy(bytes.data() + 240, record.peer_committed_signature.data(), 64);
        InvitationKey digest{};
        crypto_hash_sha256(digest.data(), bytes.data(), 304);
        std::memcpy(bytes.data() + 304, digest.data(), 12);
        put(bytes.data() + 316, kMarker, 4);
        Bank bank{};
        for (unsigned d = 0; d < 5; ++d) std::memcpy(bank[d].data(), bytes.data() + d * 64, 64);
        return bank;
    }
    static bool decode(const Bank& bank, Record& record, std::uint64_t& sequence) {
        const auto bytes = flatten(bank);
        Record candidate{};
        candidate.role = static_cast<InvitationRole>(bytes[5]);
        candidate.phase = static_cast<PairingCompletionPhase>(bytes[6]); candidate.flags = bytes[7];
        sequence = get(bytes.data() + 8, 8);
        candidate.group = get(bytes.data() + 16, 8); candidate.generation = get(bytes.data() + 24, 8);
        candidate.epoch = static_cast<std::uint32_t>(get(bytes.data() + 32, 4));
        std::memcpy(candidate.transaction.data(), bytes.data() + 48, 32);
        std::memcpy(candidate.peer.data(), bytes.data() + 80, 32);
        std::memcpy(candidate.transcript.data(), bytes.data() + 112, 32);
        std::memcpy(candidate.decision.data(), bytes.data() + 144, 32);
        std::memcpy(candidate.peer_prepared_signature.data(), bytes.data() + 176, 64);
        std::memcpy(candidate.peer_committed_signature.data(), bytes.data() + 240, 64);
        if (!sequence || !valid_record(candidate) || bank != encode(candidate, sequence)) return false;
        // Stored peer decisions remain cryptographic evidence after restart.
        for (const auto kind : {PairingCompletionKind::prepared, PairingCompletionKind::committed}) {
            const bool exists = kind == PairingCompletionKind::prepared ?
                (candidate.flags & (kPeerPrepared | kPeerAborted)) != 0 : (candidate.flags & kPeerCommitted) != 0;
            if (!exists) continue;
            PairingCompletionReceipt receipt{};
            receipt[0] = 1;
            receipt[1] = static_cast<std::uint8_t>(kind == PairingCompletionKind::prepared && candidate.flags & kPeerAborted ?
                                                PairingCompletionKind::aborted : kind);
            receipt[2] = static_cast<std::uint8_t>(candidate.role == InvitationRole::initiator ?
                                                 InvitationRole::responder : InvitationRole::initiator);
            std::memcpy(receipt.data() + 4, candidate.transaction.data(), 32);
            const auto signed_bytes = pairing_receipt_signing_bytes(receipt);
            const auto& signature = kind == PairingCompletionKind::prepared ?
                candidate.peer_prepared_signature : candidate.peer_committed_signature;
            if (crypto_sign_verify_detached(signature.data(), signed_bytes.data(), signed_bytes.size(), candidate.peer.data()) != 0) return false;
        }
        record = candidate;
        return true;
    }
    static Record unpack(const std::array<std::uint8_t, 320>& bytes) {
        Record record{};
        record.role = static_cast<InvitationRole>(bytes[5]);
        record.phase = static_cast<PairingCompletionPhase>(bytes[6]); record.flags = bytes[7];
        record.group = get(bytes.data() + 16, 8); record.generation = get(bytes.data() + 24, 8);
        record.epoch = static_cast<std::uint32_t>(get(bytes.data() + 32, 4));
        std::memcpy(record.transaction.data(), bytes.data() + 48, 32);
        std::memcpy(record.peer.data(), bytes.data() + 80, 32);
        std::memcpy(record.transcript.data(), bytes.data() + 112, 32);
        std::memcpy(record.decision.data(), bytes.data() + 144, 32);
        std::memcpy(record.peer_prepared_signature.data(), bytes.data() + 176, 64);
        std::memcpy(record.peer_committed_signature.data(), bytes.data() + 240, 64);
        return record;
    }
    static bool installed_prefix(std::uint32_t value) {
        for (unsigned bytes = 0; bytes < 4; ++bytes) {
            const auto mask = bytes ? (std::uint32_t{1} << (bytes * 8)) - 1 : 0;
            if (value == ((kMarker & mask) | ~mask)) return true;
        }
        return false;
    }
    static bool incomplete_marker(std::uint32_t value) {
        if (installed_prefix(value)) return true;
        // Retrying an incomplete candidate first retires its existing marker.
        for (unsigned installed = 0; installed < 4; ++installed) {
            const auto mask = installed ? (std::uint32_t{1} << (installed * 8)) - 1 : 0;
            const auto before = (kMarker & mask) | ~mask;
            for (unsigned cleared = 1; cleared <= 4; ++cleared) {
                const auto clear = cleared == 4 ? ~std::uint32_t{0} :
                                   (std::uint32_t{1} << (cleared * 8)) - 1;
                if (value == (before & ~clear)) return true;
            }
        }
        return false;
    }
    static unsigned required_payload(std::uint32_t value) {
        // Only an erased marker, or its retirement prefix, can follow a short
        // payload. Any retained installed byte proves all 316 payload bytes
        // and their checksum had already passed readback before marker I/O.
        for (unsigned cleared = 0; cleared <= 4; ++cleared) {
            const auto mask = cleared == 4 ? ~std::uint32_t{0} :
                              cleared ? (std::uint32_t{1} << (cleared * 8)) - 1 : 0;
            if (value == ~mask) return 0;
        }
        return 316;
    }
    static bool finish_nonzero(std::array<std::uint8_t, 320>& bytes,
                               unsigned at, unsigned count, unsigned prefix) {
        for (unsigned i = at; i < at + count; ++i) if (bytes[i]) return true;
        const auto unknown = prefix > at ? prefix : at;
        if (unknown >= at + count) return false;
        bytes[unknown] = 1;
        return true;
    }
    static bool canonical_prefix(const Bank& bank, const Record* previous, std::uint64_t sequence,
                                 unsigned payload_floor) {
        const auto observed = flatten(bank);
        unsigned minimum = payload_floor;
        for (unsigned i = 0; i < 316; ++i) if (observed[i] != 0xff && minimum < i + 1) minimum = i + 1;
        for (unsigned prefix = minimum; prefix <= 316; ++prefix) {
            // Fixed header bytes reject foreign layouts before any hash work.
            constexpr std::array<std::uint8_t, 5> header{'O','T','P','C',1};
            bool fixed = true;
            for (unsigned i = 0; i < header.size() && i < prefix; ++i)
                if (observed[i] != header[i]) fixed = false;
            for (unsigned i = 8; i < 16 && i < prefix; ++i)
                if (observed[i] != static_cast<std::uint8_t>(sequence >> ((i - 8) * 8))) fixed = false;
            for (unsigned i = 36; i < 48 && i < prefix; ++i) if (observed[i]) fixed = false;
            if (!fixed) continue;
            // Same-binding updates, or an unvoted new descriptor only after
            // the previous terminal record. Unknown suffixes confer no vote.
            for (unsigned mode = 0; mode < (previous ? 2U : 1U); ++mode) {
                const bool fresh = !previous || mode == 1;
                if (previous && fresh && previous->phase != PairingCompletionPhase::aborted &&
                    previous->phase != PairingCompletionPhase::peer_confirmed) continue;
                for (unsigned role = 1; role <= 2; ++role) {
                    if (previous && !fresh && static_cast<unsigned>(previous->role) != role) continue;
                    for (unsigned phase = 1; phase <= 6; ++phase) for (unsigned flags = 0; flags < 32; ++flags) {
                        if (fresh && !((phase == static_cast<unsigned>(PairingCompletionPhase::confirmed) && flags == 0) ||
                            (phase == static_cast<unsigned>(PairingCompletionPhase::aborted) && flags == kLocalAborted &&
                             (!previous || previous->phase == PairingCompletionPhase::aborted)))) continue;
                        if ((prefix > 5 && observed[5] != role) || (prefix > 6 && observed[6] != phase) ||
                            (prefix > 7 && observed[7] != flags)) continue;
                        Record seed{};
                        if (!fresh) seed = *previous;
                        else {
                            seed.transaction.fill(1); seed.peer.fill(1); seed.transcript.fill(1); seed.decision.fill(1);
                            seed.group = 1; seed.generation = previous ? previous->generation + 1 : 1; seed.epoch = 1;
                        }
                        seed.role = static_cast<InvitationRole>(role);
                        seed.phase = static_cast<PairingCompletionPhase>(phase); seed.flags = static_cast<std::uint8_t>(flags);
                        if (!(flags & (kPeerPrepared | kPeerAborted))) seed.peer_prepared_signature.fill(0);
                        else if (!nonzero(seed.peer_prepared_signature)) seed.peer_prepared_signature.fill(1);
                        if (!(flags & kPeerCommitted)) seed.peer_committed_signature.fill(0);
                        else if (!nonzero(seed.peer_committed_signature)) seed.peer_committed_signature.fill(1);
                        auto completed = flatten(encode(seed, sequence));
                        std::memcpy(completed.data(), observed.data(), prefix);
                        if (fresh) {
                            bool fields = finish_nonzero(completed, 16, 8, prefix) &&
                                          finish_nonzero(completed, 24, 8, prefix) &&
                                          finish_nonzero(completed, 32, 4, prefix);
                            for (const auto offset : {48U, 80U, 112U, 144U})
                                fields = finish_nonzero(completed, offset, 32, prefix) && fields;
                            if (!fields) continue;
                            if (previous && get(completed.data() + 24, 8) <= previous->generation) {
                                for (unsigned i = prefix > 24 ? prefix : 24; i < 32; ++i) completed[i] = 0xff;
                            }
                        }
                        const auto candidate = unpack(completed);
                        if (!valid_record(candidate) || (previous && (!follows(*previous, candidate) ||
                            (fresh ? candidate.generation <= previous->generation : !same_descriptor(*previous, candidate))))) continue;
                        if (!valid_signatures(candidate, prefix)) continue;
                        const auto encoded = flatten(encode(candidate, sequence));
                        if (std::memcmp(encoded.data(), observed.data(), prefix) == 0) return true;
                    }
                }
            }
        }
        return false;
    }
    static bool retired_prefix(const Bank& observed, const Bank& original) {
        auto expected = original;
        for (unsigned cleared = 1; cleared <= 4; ++cleared) {
            expected = original;
            std::memset(expected[4].data() + 60, 0, cleared);
            if (observed == expected) return true;
        }
        // erase_slot operates in domain order, after complete retirement.
        for (unsigned erased = 0; erased < 5; ++erased) {
            expected[erased].fill(0xff);
            if (observed == expected) return true;
        }
        return false;
    }
    static bool interrupted_bank(const Bank& bank, const Record& current, std::uint64_t sequence) {
        if (sequence != std::numeric_limits<std::uint64_t>::max() && incomplete_marker(marker(bank)) &&
            canonical_prefix(bank, &current, sequence + 1, required_payload(marker(bank)))) return true;
        if (sequence <= 1) return false;
        // An intact predecessor with only its marker retired is self-proving.
        auto repaired = bank;
        put(repaired[4].data() + 60, kMarker, 4);
        Record predecessor{}; std::uint64_t prior_sequence = 0;
        if (decode(repaired, predecessor, prior_sequence) && prior_sequence + 1 == sequence &&
            follows(predecessor, current) && retired_prefix(bank, repaired)) return true;
        // If erase removed its header, reconstruct only exact same-binding
        // predecessors from the durable current record. Foreign tails stay put.
        for (unsigned phase = 1; phase <= 6; ++phase) for (unsigned flags = 0; flags < 32; ++flags) {
            auto prior = current;
            prior.phase = static_cast<PairingCompletionPhase>(phase); prior.flags = static_cast<std::uint8_t>(flags);
            if (!(flags & (kPeerPrepared | kPeerAborted))) prior.peer_prepared_signature.fill(0);
            if (!(flags & kPeerCommitted)) prior.peer_committed_signature.fill(0);
            if (valid_record(prior) && follows(prior, current) &&
                retired_prefix(bank, encode(prior, sequence - 1))) return true;
        }
        return false;
    }
    static bool valid_signatures(const Record& candidate, unsigned prefix) {
        for (const auto kind : {PairingCompletionKind::prepared, PairingCompletionKind::committed}) {
            const unsigned end = kind == PairingCompletionKind::prepared ? 240 : 304;
            const bool exists = kind == PairingCompletionKind::prepared ?
                (candidate.flags & (kPeerPrepared | kPeerAborted)) != 0 : (candidate.flags & kPeerCommitted) != 0;
            if (!exists || prefix < end) continue;
            PairingCompletionReceipt receipt{};
            receipt[0] = 1;
            receipt[1] = static_cast<std::uint8_t>(kind == PairingCompletionKind::prepared && candidate.flags & kPeerAborted ?
                                                PairingCompletionKind::aborted : kind);
            receipt[2] = static_cast<std::uint8_t>(candidate.role == InvitationRole::initiator ?
                                                 InvitationRole::responder : InvitationRole::initiator);
            std::memcpy(receipt.data() + 4, candidate.transaction.data(), 32);
            const auto signed_bytes = pairing_receipt_signing_bytes(receipt);
            const auto& signature = kind == PairingCompletionKind::prepared ?
                candidate.peer_prepared_signature : candidate.peer_committed_signature;
            if (crypto_sign_verify_detached(signature.data(), signed_bytes.data(), signed_bytes.size(), candidate.peer.data()) != 0) return false;
        }
        return true;
    }
    bool observable() const { return initialized_ && !empty_ && !failed_ && !busy_; }
    bool refuse() { failed_ = true; return false; }
    bool live() { return !failed_ && (!guard_ || guard_(guard_context_)) && !failed_; }
    template<class Action> bool operation(Action action) {
        if (busy_) return refuse();
        if (failed_) return false;
        busy_ = true;
        const bool ok = action();
        busy_ = false;
        return ok && !failed_;
    }
    template<class Action> bool guarded(Guard guard, void* context, Action action) {
        if (busy_) return refuse();
        if (failed_) return false;
        guard_ = guard; guard_context_ = context;
        const bool ok = operation([&] { return live() && action() && live(); });
        guard_ = nullptr; guard_context_ = nullptr;
        return ok;
    }
    template<class Change> bool update(Guard guard, void* context, Change change) {
        return guarded(guard, context, [&] {
            if (!initialized_ || empty_ || !exact()) return false;
            Record next = record_;
            if (!change(next) || !valid_record(next) || !follows(record_, next)) return false;
            if (encode(next, sequence_) == encode(record_, sequence_)) return true;
            return persist(next);
        });
    }
    bool read(Media& media) {
        for (unsigned slot = 0; slot < 2; ++slot) for (unsigned d = 0; d < 5; ++d) {
            if (!live()) return false;
            const auto result = storage_.read_slot(static_cast<persistence::StorageDomain>(d), slot, {media[slot][d].data(), 64});
            if (!live() || !result.read() || result.bytes_read != 64) return false;
        }
        return true;
    }
    bool exact() {
        Media observed{};
        return read(observed) && observed == retained_ ? true : refuse();
    }
    bool sync(unsigned d, unsigned slot) {
        return live() && storage_.sync_slot(static_cast<persistence::StorageDomain>(d), slot) == persistence::StorageError::none && live();
    }
    bool write(unsigned d, unsigned slot, unsigned offset, const std::uint8_t* bytes, unsigned count) {
        return live() && storage_.write_slot(static_cast<persistence::StorageDomain>(d), slot, offset, {bytes, count}) ==
                         persistence::StorageError::none && live();
    }
    bool persist(const Record& next) {
        if (sequence_ == std::numeric_limits<std::uint64_t>::max()) return refuse();
        const unsigned target = empty_ ? 0 : 1 - current_slot_;
        const auto encoded = encode(next, sequence_ + 1);
        auto expected = retained_;
        Media observed{};
        // Invalidate the old target marker before erasing any of its payload.
        // A power cut can then never leave a formerly committed marker beside
        // a partially erased older bank. The current bank stays intact.
        if (!blank(expected[target])) {
            const std::array<std::uint8_t, 4> retired_marker{};
            if (!write(4, target, 60, retired_marker.data(), 4) || !sync(4, target)) return refuse();
            std::memcpy(expected[target][4].data() + 60, retired_marker.data(), 4);
            if (!read(observed) || observed != expected) return refuse();
        }
        for (unsigned d = 0; d < 5; ++d) {
            if (!live() || storage_.erase_slot(static_cast<persistence::StorageDomain>(d), target) != persistence::StorageError::none ||
                !sync(d, target)) return refuse();
            expected[target][d].fill(0xff);
        }
        if (!read(observed) || observed != expected) return refuse();
        for (unsigned d = 0; d < 5; ++d) {
            const unsigned count = d == 4 ? 60 : 64;
            if (!write(d, target, 0, encoded[d].data(), count) || !sync(d, target)) return refuse();
            std::memcpy(expected[target][d].data(), encoded[d].data(), count);
        }
        if (!read(observed) || observed != expected) return refuse();
        // The exact request/window decision is reserved at this serialized
        // boundary before the one-way marker. Later withdrawal ends the live
        // attempt while this pre-expiry decision remains recoverable.
        if (vote_reservation_ && !vote_reservation_(guard_context_)) return refuse();
        if (!write(4, target, 60, encoded[4].data() + 60, 4) || !sync(4, target)) return refuse();
        expected[target] = encoded;
        if (!read(observed) || observed != expected) return refuse();
        retained_ = expected; record_ = next; current_slot_ = target;
        ++sequence_; empty_ = false;
        return true;
    }
    persistence::PersistentStorage& storage_;
    Media retained_{};
    Record record_{};
    std::uint64_t sequence_{};
    unsigned current_slot_{};
    bool initialized_{}, empty_{}, failed_{}, busy_{};
    Guard guard_{};
    Guard vote_reservation_{};
    void* guard_context_{};
};
}  // namespace opentrail::security_evaluation
