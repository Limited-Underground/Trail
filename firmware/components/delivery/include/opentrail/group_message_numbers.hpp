#pragma once

#include <cstdint>

#include "opentrail/group_roster.hpp"
#include "opentrail/persistent_storage_kv.hpp"

namespace opentrail::delivery {

enum class GroupMessageNumberError : std::uint8_t {
    none,
    invalid_scope,
    missing,
    capacity,
    scope_mismatch,
    invalid_record,
    storage_failure,
    exhausted,
};

struct GroupMessageNumberResult {
    GroupMessageNumberError error;
    std::uint32_t number{0};
};

// One sender and qualified group own a sequence across roster versions, rekeys
// and restarts. The caller serializes catalog operations and keeps only one
// live allocator per scope. Give each allocator an exclusive backend handle.
// The backend must replace a committed blob atomically and return committed
// bytes on read. Discard the handle after any uncertain write or commit.
class GroupMessageNumbers {
public:
    static constexpr std::uint32_t kLeaseSize = 32;

    GroupMessageNumbers(persistence::PersistentKvBackend& backend,
                        const identity::roster::GroupName& group, const identity::roster::Fingerprint& sender);

    GroupMessageNumbers(const GroupMessageNumbers&) = delete;
    GroupMessageNumbers& operator=(const GroupMessageNumbers&) = delete;

    // Use during durable group creation only. Repeating initialization for an
    // existing scope preserves its high-water mark. Retain the record while
    // the same sender identity can send again under the same group name.
    [[nodiscard]] GroupMessageNumberError initialize();
    [[nodiscard]] GroupMessageNumberResult next();

private:
    [[nodiscard]] GroupMessageNumberError locate(const char*& key, std::uint32_t& through);
    [[nodiscard]] GroupMessageNumberError save(const char* key, std::uint32_t through);

    persistence::PersistentKvBackend& backend_;
    const identity::roster::GroupName group_;
    const identity::roster::Fingerprint sender_;
    std::uint32_t next_{0};
    std::uint32_t lease_end_{0};
    bool faulted_{false};
};

}  // namespace opentrail::delivery
