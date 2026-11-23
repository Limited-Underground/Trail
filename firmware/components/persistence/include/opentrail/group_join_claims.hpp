#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/group_roster.hpp"
#include "opentrail/persistent_storage_kv.hpp"

namespace opentrail::persistence {

enum class GroupJoinClaimError : std::uint8_t {
    none = 0,
    missing,         // reopen only: no record yet, nothing claimed
    replayed,        // this invite was already claimed on this board
    invalid,         // bad scope or invite ID
    invalid_record,  // retained bytes do not parse or belong to another identity
    storage_failure, // read, write or commit failed; the outcome may be uncertain
};

// Joiner-side record of invites this board has accepted (F8 live Join).
// One fixed-size record in its own namespace. A claim is durable before the
// board reports a Join as accepted. After an uncertain commit the board
// reopens and treats a present ID as consumed: an interrupted Join spends the
// invite rather than risking a second use. The newest kEntries claims are kept;
// the admin board's boot-scoped issuer and its receipt remain the authority
// for consumption on the admin side.
class GroupJoinClaims {
public:
    using InviteId = std::array<std::uint8_t, 16>;
    static constexpr std::size_t kEntries = 8;
    static constexpr std::size_t kEntryBytes =
        identity::roster::kFingerprintBytes + sizeof(std::uint64_t) + InviteId{}.size();
    static constexpr std::size_t kRecordBytes = 8 + identity::roster::kFingerprintBytes + kEntries * kEntryBytes + 4;
    static_assert(kRecordBytes == 492);
    static constexpr const char* kNamespace = "ot_group_join";
    static constexpr const char* kKey = "claims";

    GroupJoinClaims(PersistentKvBackend& backend, const identity::roster::Fingerprint& local)
        : backend_(backend), local_(local) {}
    GroupJoinClaims(const GroupJoinClaims&) = delete;
    GroupJoinClaims& operator=(const GroupJoinClaims&) = delete;

    // Loads the retained record. missing is a usable empty state.
    [[nodiscard]] GroupJoinClaimError reopen();
    [[nodiscard]] bool claimed(const identity::roster::Fingerprint& admin, std::uint64_t group,
                               const InviteId& invite) const;
    // Requires a successful reopen. Commits, then reads back the exact record.
    [[nodiscard]] GroupJoinClaimError claim(const identity::roster::Fingerprint& admin, std::uint64_t group,
                                            const InviteId& invite);

private:
    struct Entry {
        identity::roster::Fingerprint admin{};
        std::uint64_t group{0};
        InviteId invite{};
    };
    using Record = std::array<std::uint8_t, kRecordBytes>;
    void encode(const std::array<Entry, kEntries>&, std::uint8_t count, std::uint8_t next, Record&) const;
    [[nodiscard]] bool decode(const Record&, std::array<Entry, kEntries>&, std::uint8_t& count,
                              std::uint8_t& next) const;

    PersistentKvBackend& backend_;
    const identity::roster::Fingerprint local_;
    std::array<Entry, kEntries> entries_{};
    std::uint8_t count_{0}, next_{0};
    bool loaded_{false};
};

} // namespace opentrail::persistence
