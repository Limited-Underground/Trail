#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "opentrail/group_qr_invite.hpp"
#include "opentrail/persistent_storage_kv.hpp"

namespace opentrail::persistence {

enum class GroupQrReceiptError : std::uint8_t {
    none = 0,
    missing,         // reopen only: nothing retained yet
    refused,         // contradicts a retained receipt, is late, or no slot is free
    invalid,         // the receipt does not describe one exact transaction
    invalid_record,  // retained bytes do not parse or belong to another identity
    storage_failure, // read, write or commit failed; the outcome may be uncertain
};

// The durable owner behind qr::Authority::persist, readback and fresh_unclaimed
// on both boards (F8 step 2b). One fixed record keeps up to kSlots retained QR
// decisions, one per invite ID.
// - The first decision for an invite ID consumes it: fresh_unclaimed is false
//   from then on, whatever scope or peer a later attempt names.
// - A later decision for the same ID must name the same exact transaction,
//   identities and original deadline. It may only advance prepared to
//   committed or aborted. Repeating the retained kind is a no-op.
// - Positive kinds (prepared, committed) are refused at or after the original
//   deadline; aborted is always accepted for a prepared transaction.
// - A full record evicts the oldest committed or aborted decision, never a
//   prepared one. Invites are boot-scoped on the admin board, so an evicted ID
//   cannot be presented again by the issuer.
// - After any failed write the cache is dropped; the caller must reopen and
//   observe what was retained.
class GroupQrReceipts {
public:
    static constexpr std::size_t kSlots = 4;
    static constexpr std::size_t kRetainedBytes = 3 * 32 + 2 + 4 * 32 + 16 + 8 + 4 + 4 + 1 + 2 * 32 + 8;
    static constexpr std::size_t kRecordBytes =
        8 + identity::roster::kFingerprintBytes + kSlots * (1 + 8 + kRetainedBytes) + 4;
    static_assert(kRetainedBytes == 331 && kRecordBytes == 1404);
    static constexpr const char* kNamespace = "ot_group_qrr";
    static constexpr const char* kKey = "receipts";

    // local: this board's identity public key (Retained::local).
    GroupQrReceipts(PersistentKvBackend& backend, const identity::roster::PublicKey& local)
        : backend_(backend), local_(local) {}
    GroupQrReceipts(const GroupQrReceipts&) = delete;
    GroupQrReceipts& operator=(const GroupQrReceipts&) = delete;

    [[nodiscard]] GroupQrReceiptError reopen();
    // False while unloaded (fails closed).
    [[nodiscard]] bool fresh_unclaimed(const identity::roster::qr::Nonce& invite) const;
    [[nodiscard]] GroupQrReceiptError persist(const identity::roster::qr::Retained&, std::uint64_t original_deadline,
                                              std::uint64_t now);
    // Exact match of the retained decision for that invite, in the loaded image.
    [[nodiscard]] bool readback(const identity::roster::qr::Retained&) const;
    // The retained decision and its original deadline, for recovery after restart.
    [[nodiscard]] std::optional<identity::roster::qr::Retained> find(const identity::roster::qr::Nonce& invite,
                                                                     std::uint64_t* deadline = nullptr) const;
    // The retained decision in slot `index` (0 to kSlots - 1), if any, for
    // recovery work that walks every kept decision.
    [[nodiscard]] std::optional<identity::roster::qr::Retained> at(std::size_t index) const;

private:
    struct Slot {
        bool used{false};
        std::uint64_t sequence{0};
        std::uint64_t deadline{0};
        identity::roster::qr::Retained retained{};
    };
    using Record = std::array<std::uint8_t, kRecordBytes>;
    using Slots = std::array<Slot, kSlots>;
    void encode(const Slots&, Record&) const;
    [[nodiscard]] bool decode(const Record&, Slots&) const;
    [[nodiscard]] const Slot* slot_for(const identity::roster::qr::Nonce&) const;

    PersistentKvBackend& backend_;
    const identity::roster::PublicKey local_;
    Slots slots_{};
    std::uint64_t next_sequence_{1};
    bool loaded_{false};
};

} // namespace opentrail::persistence
