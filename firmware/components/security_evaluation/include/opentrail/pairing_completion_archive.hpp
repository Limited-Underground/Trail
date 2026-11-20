#pragma once
// Immutable D-112 terminal-decision archive. Each generation owns a separate
// dedicated PersistentStorage. These public records preserve signed replies;
// they never grant membership or restore traffic. The session verifies the
// current local identity and admits a fresh bounded recovery exchange.
#include "opentrail/pairing_completion_journal.hpp"

namespace opentrail::security_evaluation {
struct PairingCompletionArchiveEntry {
    InvitationKey own{}, peer{};
    PairingCompletionReceipt receipt{};
    std::uint64_t group{}, generation{};
    std::uint32_t epoch{};
};
class PairingCompletionArchive {
public:
    virtual ~PairingCompletionArchive() = default;
    virtual persistence::PersistentStorage* archive_storage(std::uint64_t generation) = 0;
    virtual std::uint64_t archive_capacity() const = 0;
};

class PairingCompletionArchiveRecord final {
public:
    using Guard = bool (*)(void*);
    explicit PairingCompletionArchiveRecord(persistence::PersistentStorage& storage) : storage_(storage) {}
    PairingCompletionArchiveRecord(const PairingCompletionArchiveRecord&) = delete;
    PairingCompletionArchiveRecord& operator=(const PairingCompletionArchiveRecord&) = delete;

    bool initialize() {
        return operation([&] {
            if (initialized_) return false;
            initialized_ = true;
            if (!read(retained_)) return refuse();
            if (blank(retained_[0]) && blank(retained_[1])) {
                empty_ = true;
                return true;
            }
            if (!blank(retained_[1]) || !decode(retained_[0], entry_)) return refuse();
            return true;
        });
    }
    bool empty() const { return initialized_ && !failed_ && !busy_ && empty_; }
    bool failed() const { return failed_; }
    bool load(PairingCompletionArchiveEntry& output) {
        PairingCompletionArchiveEntry staged{};
        const bool ok = operation([&] {
            if (!initialized_ || empty_ || !exact()) return false;
            staged = entry_;
            return true;
        });
        if (ok) output = staged;
        return ok;
    }
    // The trusted session has already verified the transaction's terminal abort.
    // A Prepared receipt here preserves history and cannot authorize completion.
    bool retain(const PairingCompletionArchiveEntry& entry, Guard guard = nullptr, void* context = nullptr) {
        if (busy_) return refuse();
        if (failed_) return false;
        guard_ = guard;
        guard_context_ = context;
        const bool ok = operation([&] {
            if (!live() || !initialized_ || !exact() || !valid(entry)) return false;
            const auto bytes = encode(entry);
            if (!empty_) return bytes == retained_[0];
            auto expected = retained_;
            // Both banks were verified erased. This immutable record is never
            // erased or overwritten, even after a torn first installation.
            for (unsigned domain = 0; domain < 5; ++domain) {
                const unsigned count = domain == 4 ? 60 : 64;
                if (!write(domain, 0, bytes[domain].data(), count) || !sync(domain)) return refuse();
                std::memcpy(expected[0][domain].data(), bytes[domain].data(), count);
            }
            Media observed{};
            if (!read(observed) || observed != expected ||
                !write(4, 60, bytes[4].data() + 60, 4) || !sync(4)) return refuse();
            expected[0] = bytes;
            if (!read(observed) || observed != expected) return refuse();
            retained_ = expected;
            entry_ = entry;
            empty_ = false;
            return live();
        });
        guard_ = nullptr;
        guard_context_ = nullptr;
        return ok;
    }

    // The current authenticated terminal journal remains authoritative while
    // retrying this exact interrupted archive installation.
    bool repair_exact(const PairingCompletionArchiveEntry& entry, Guard guard = nullptr, void* context = nullptr) {
        if (busy_) return refuse();
        if (!initialized_ || !failed_ || !valid(entry)) return false;
        failed_ = false;
        guard_ = guard;
        guard_context_ = context;
        const bool repaired = operation([&] {
            Media observed{};
            if (!read(observed) || !blank(observed[1])) return refuse();
            const auto expected = encode(entry);
            PairingCompletionArchiveEntry complete{};
            if (decode(observed[0], complete)) {
                if (observed[0] != expected) return refuse();
                retained_ = observed; entry_ = entry; empty_ = false;
                return true;
            }
            if (get(observed[0][4].data() + 60, 4) == kMarker) return refuse();
            for (unsigned domain = 0; domain < 5; ++domain) {
                for (unsigned byte = 0; byte < 64; ++byte) {
                    if (observed[0][domain][byte] != 0xff &&
                        observed[0][domain][byte] != expected[domain][byte]) return refuse();
                }
            }
            // Only bytes belonging to this signed, uncommitted archive entry
            // may be recycled. A complete or different entry is preserved.
            for (unsigned domain = 0; domain < 5; ++domain) {
                if (!live() || storage_.erase_slot(static_cast<persistence::StorageDomain>(domain), 0) !=
                    persistence::StorageError::none || !sync(domain)) return refuse();
                observed[0][domain].fill(0xff);
            }
            Media erased{};
            if (!read(erased) || erased != observed) return refuse();
            retained_ = erased; empty_ = true;
            return true;
        });
        guard_ = nullptr;
        guard_context_ = nullptr;
        return repaired && retain(entry, guard, context);
    }

private:
    using Bank = std::array<std::array<std::uint8_t, 64>, 5>;
    using Media = std::array<Bank, 2>;
    static_assert(persistence::kStorageDomainCount == 5 && persistence::kPersistentSlotCount == 2 &&
                  persistence::kPersistentSlotBytes == 64);
    static constexpr std::uint32_t kMarker = 0x31415250U;
    static void put(std::uint8_t* output, std::uint64_t value, unsigned count) {
        for (unsigned i = 0; i < count; ++i) output[i] = static_cast<std::uint8_t>(value >> (8 * i));
    }
    static std::uint64_t get(const std::uint8_t* input, unsigned count) {
        std::uint64_t value{};
        for (unsigned i = 0; i < count; ++i) value |= static_cast<std::uint64_t>(input[i]) << (8 * i);
        return value;
    }
    static bool valid(const PairingCompletionArchiveEntry& entry) {
        const auto& receipt = entry.receipt;
        const auto kind = static_cast<PairingCompletionKind>(receipt[1]);
        if (!entry.group || !entry.generation || !entry.epoch ||
            !invitation_detail::nonzero(entry.own) || !invitation_detail::nonzero(entry.peer) || entry.own == entry.peer ||
            receipt[0] != 1 || receipt[3] != 0 ||
            (receipt[2] != static_cast<std::uint8_t>(InvitationRole::initiator) &&
             receipt[2] != static_cast<std::uint8_t>(InvitationRole::responder)) ||
            (kind != PairingCompletionKind::prepared && kind != PairingCompletionKind::aborted)) return false;
        InvitationKey transaction{};
        std::memcpy(transaction.data(), receipt.data() + 4, transaction.size());
        if (!invitation_detail::nonzero(transaction)) return false;
        const auto signed_bytes = pairing_receipt_signing_bytes(receipt);
        return crypto_sign_verify_detached(receipt.data() + 36, signed_bytes.data(), signed_bytes.size(), entry.own.data()) == 0;
    }
    static bool blank(const Bank& bank) {
        for (const auto& slot : bank) for (const auto byte : slot) if (byte != 0xff) return false;
        return true;
    }
    static Bank encode(const PairingCompletionArchiveEntry& entry) {
        std::array<std::uint8_t, 320> bytes{};
        std::memcpy(bytes.data(), "OTPA", 4);
        bytes[4] = 1;
        put(bytes.data() + 8, entry.generation, 8);
        put(bytes.data() + 16, entry.group, 8);
        put(bytes.data() + 24, entry.epoch, 4);
        std::memcpy(bytes.data() + 32, entry.own.data(), entry.own.size());
        std::memcpy(bytes.data() + 64, entry.peer.data(), entry.peer.size());
        std::memcpy(bytes.data() + 96, entry.receipt.data(), entry.receipt.size());
        InvitationKey digest{};
        crypto_hash_sha256(digest.data(), bytes.data(), 284);
        std::memcpy(bytes.data() + 284, digest.data(), digest.size());
        put(bytes.data() + 316, kMarker, 4);
        Bank output{};
        for (unsigned domain = 0; domain < 5; ++domain)
            std::memcpy(output[domain].data(), bytes.data() + domain * 64, 64);
        return output;
    }
    static bool decode(const Bank& bank, PairingCompletionArchiveEntry& output) {
        std::array<std::uint8_t, 320> bytes{};
        for (unsigned domain = 0; domain < 5; ++domain)
            std::memcpy(bytes.data() + domain * 64, bank[domain].data(), 64);
        PairingCompletionArchiveEntry candidate{};
        candidate.generation = get(bytes.data() + 8, 8);
        candidate.group = get(bytes.data() + 16, 8);
        candidate.epoch = static_cast<std::uint32_t>(get(bytes.data() + 24, 4));
        std::memcpy(candidate.own.data(), bytes.data() + 32, candidate.own.size());
        std::memcpy(candidate.peer.data(), bytes.data() + 64, candidate.peer.size());
        std::memcpy(candidate.receipt.data(), bytes.data() + 96, candidate.receipt.size());
        if (!valid(candidate) || bank != encode(candidate)) return false;
        output = candidate;
        return true;
    }
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
    bool read(Media& media) {
        for (unsigned slot = 0; slot < 2; ++slot) for (unsigned domain = 0; domain < 5; ++domain) {
            if (!live()) return false;
            const auto result = storage_.read_slot(static_cast<persistence::StorageDomain>(domain), slot,
                                                  {media[slot][domain].data(), 64});
            if (!live() || !result.read() || result.bytes_read != 64) return false;
        }
        return true;
    }
    bool exact() {
        Media observed{};
        return read(observed) && observed == retained_ ? true : refuse();
    }
    bool write(unsigned domain, unsigned offset, const std::uint8_t* input, unsigned count) {
        return live() && storage_.write_slot(static_cast<persistence::StorageDomain>(domain), 0, offset, {input, count}) ==
                         persistence::StorageError::none && live();
    }
    bool sync(unsigned domain) {
        return live() && storage_.sync_slot(static_cast<persistence::StorageDomain>(domain), 0) ==
                         persistence::StorageError::none && live();
    }
    persistence::PersistentStorage& storage_;
    Media retained_{};
    PairingCompletionArchiveEntry entry_{};
    bool initialized_{}, empty_{}, busy_{}, failed_{};
    Guard guard_{};
    void* guard_context_{};
};
}  // namespace opentrail::security_evaluation
