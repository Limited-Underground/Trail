#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "opentrail/group_roster.hpp"
#include "opentrail/persistent_storage_kv.hpp"

namespace opentrail::persistence {

enum class GroupQrPairKeyError : std::uint8_t {
    none = 0,
    missing,          // reopen only: nothing kept yet
    refused,          // another key for this pair, a full group, or a bad entry
    invalid,          // the entry does not describe one pair
    invalid_record,   // kept bytes do not parse or belong to another identity
    storage_failure,  // read, write or commit failed; the outcome may be uncertain
};

// D-179: the keys of a QR-joined group pair, kept on the board (F8 step 2d-3,
// Link Test only for writing). One fixed record per group in the group pair
// namespace, up to five peers each, bound to this board's identity and
// protected by a CRC. The same at-rest protection as the F5 pairing keys
// (plain ot_state, no flash encryption).
// - A pair's keys are written once, by the caller only after the committed
//   QR receipt reads back. Writing the same entry again is a no-op; any
//   other entry for that pair is refused.
// - Stop Trust overwrites the group's record with an empty one (group
//   storage never erases). Factory reset erases the whole partition.
// - After any failed write the cache is dropped; the caller must reopen.
// The cache holds key bytes; it is wiped on drop and destruction.
class GroupQrPairKeys {
public:
    using Key = std::array<std::uint8_t, 32>;
    static constexpr std::size_t kGroups = 4;
    static constexpr std::size_t kPeers = identity::roster::kMaximumMembers - 1;
    static constexpr std::size_t kEntryBytes = 1 + 32 + 4 + 32 + 2 * 32;
    static constexpr std::size_t kRecordBytes = 8 + identity::roster::kFingerprintBytes + 8 + kPeers * kEntryBytes + 4;
    static_assert(kEntryBytes == 133 && kRecordBytes == 717);
    static constexpr const char* kNamespace = "ot_group_pair";
    static constexpr std::array<const char*, kGroups> kKeys{"qk0", "qk1", "qk2", "qk3"};

    struct Entry {
        identity::roster::PublicKey peer{};
        std::uint32_t epoch{0};
        identity::roster::Digest transaction{};  // the committed QR receipt's transaction
        Key transmit{}, receive{};
    };

    GroupQrPairKeys(PersistentKvBackend& backend, const identity::roster::PublicKey& local)
        : backend_(backend), local_(local) {}
    GroupQrPairKeys(const GroupQrPairKeys&) = delete;
    GroupQrPairKeys& operator=(const GroupQrPairKeys&) = delete;
    ~GroupQrPairKeys() { drop(); }

    [[nodiscard]] GroupQrPairKeyError reopen();
    [[nodiscard]] GroupQrPairKeyError store(std::uint64_t group, const Entry&);
    // The kept keys of one pair, in the loaded image.
    [[nodiscard]] bool find(std::uint64_t group, const identity::roster::PublicKey& peer, Entry& out) const;
    [[nodiscard]] std::size_t count(std::uint64_t group) const;
    // Stop Trust: every key of that group is overwritten.
    [[nodiscard]] GroupQrPairKeyError wipe_group(std::uint64_t group);

private:
    struct Slot {
        bool used{false};
        Entry entry{};
    };
    struct Group {
        bool present{false};  // a record exists under this key (possibly empty)
        std::uint64_t id{0};
        std::array<Slot, kPeers> peers{};
    };
    using Record = std::array<std::uint8_t, kRecordBytes>;
    using Groups = std::array<Group, kGroups>;
    void encode(const Group&, Record&) const;
    [[nodiscard]] bool decode(const Record&, Group&) const;
    [[nodiscard]] GroupQrPairKeyError write(std::size_t index, const Group&);
    void drop();

    PersistentKvBackend& backend_;
    const identity::roster::PublicKey local_;
    Groups groups_{};
    bool loaded_{false};
};

} // namespace opentrail::persistence
