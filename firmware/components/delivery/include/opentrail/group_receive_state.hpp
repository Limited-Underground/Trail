#pragma once

#include "opentrail/group_inbox.hpp"
#include "opentrail/group_roster.hpp"
#include "opentrail/persistent_storage_kv.hpp"

namespace opentrail::delivery {

enum class GroupReceiveStateError : std::uint8_t {
    none,
    invalid_scope,
    invalid_frame,
    missing,
    capacity,
    invalid_record,
    storage_failure,
};

struct DurableGroupReceiveResult {
    GroupReceiveStateError error;
    // Present only after successful admission and any required durable update,
    // or when an authenticated frame was refused by the normal inbox gates.
    std::optional<GroupReceiveResult> disposition;
};

struct GroupReceiveGaps {
    GroupReceiveStateError error;
    std::uint32_t count{0};
};

// One receiving identity and one sending identity in a qualified group. This
// history belongs to the sender's number sequence, independently of a pair
// slot, roster version, or traffic epoch. It holds checksums, not message bodies.
//
// The caller serializes receive and catalog operations, keeps one live owner
// per scope, and supplies the current roster, pair and authenticated plaintext.
// The backend handle must be exclusive, atomically replace one committed blob
// and read only committed bytes. Discard it after any uncertain mutation.
class GroupReceiveState {
public:
    static constexpr std::size_t kRetainedSenders = 4 * kGroupOtherMembers;

    GroupReceiveState(persistence::PersistentKvBackend& backend, const identity::roster::GroupName& group,
                      const GroupMemberId& local, const GroupMemberId& sender);
    GroupReceiveState(const GroupReceiveState&) = delete;
    GroupReceiveState& operator=(const GroupReceiveState&) = delete;

    // Provision only for a known new receive history during trusted group
    // admission. Repeating provisioning preserves an existing valid window.
    // A missing history during normal receive or rejoin must never be silently
    // provisioned. There is no reclamation while the sender can still rejoin.
    [[nodiscard]] GroupReceiveStateError initialize();

    [[nodiscard]] DurableGroupReceiveResult receive(const SenderStanding& standing, const GroupView& view,
                                                    GroupContent content, const std::uint8_t* plaintext,
                                                    std::size_t size);
    [[nodiscard]] GroupReceiveGaps missing();

private:
    using Window = GroupInbox::Window;
    static constexpr std::size_t kScopeBytes = 3 * kGroupMemberIdBytes + sizeof(std::uint64_t);
    static constexpr std::size_t kRecordBytes =
        4 + kScopeBytes + 4 * sizeof(std::uint32_t) + kGroupInboxWindow * sizeof(std::uint64_t);
    using Record = std::array<std::uint8_t, kRecordBytes>;

    [[nodiscard]] GroupReceiveStateError read(const char* key, Record& record, Window& window);
    [[nodiscard]] GroupReceiveStateError load(Window& window, const char*& free_key);
    [[nodiscard]] GroupReceiveStateError save(const char* key, const Window& window);
    [[nodiscard]] bool matches(const Record& record) const;
    [[nodiscard]] static bool valid_window(const Window& window);

    persistence::PersistentKvBackend& backend_;
    const identity::roster::GroupName group_;
    const GroupMemberId local_;
    const GroupMemberId sender_;
    const char* key_{nullptr};  // assigned only after an unambiguous read or verified initial commit
    bool faulted_{false};
};

}  // namespace opentrail::delivery
