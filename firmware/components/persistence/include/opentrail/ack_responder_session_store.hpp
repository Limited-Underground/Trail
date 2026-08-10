#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/persistent_storage.hpp"

namespace opentrail::persistence {

inline constexpr std::uint8_t kAckResponderSessionEnvelopeVersion = 1;
inline constexpr std::uint8_t kAckResponderSessionSchemaVersion = 1;
inline constexpr std::size_t kAckResponderSessionCommitOffset = 60;
inline constexpr std::uint32_t kAckResponderSessionCommitMarker = 0xA6175E55U;

enum class AckResponderSessionError : std::uint8_t {
    none = 0,
    invalid_request,
    no_valid_state,
    storage_failure,
    integrity_failure,
    unsupported_version,
    generation_conflict,
    identity_mismatch,
    generation_exhausted,
    boot_session_exhausted,
    verification_failure,
};

enum class AckResponderSessionSlotState : std::uint8_t {
    blank = 0,
    valid,
    uncommitted,
    malformed,
    integrity_failure,
    unsupported_version,
    storage_failure,
};

struct AckResponderSessionRequest {
    std::uint64_t consumer_id{0};
    std::uint32_t authorization_epoch{0};
    std::uint32_t initial_boot_session_id{0};
};

struct AckResponderSessionAllocation {
    AckResponderSessionError error{AckResponderSessionError::no_valid_state};
    std::uint32_t generation{0};
    std::uint32_t boot_session_id{0};
    std::size_t written_slot{kPersistentSlotCount};
    std::array<AckResponderSessionSlotState, kPersistentSlotCount> slot_states{};
    bool initialized{false};

    [[nodiscard]] constexpr bool allocated() const {
        return error == AckResponderSessionError::none;
    }
};

// Allocates one durable, nonzero boot-session ID before it is returned to the
// ACK responder. Identity/authorization changes require an explicit reset.
class AckResponderSessionStore {
public:
    explicit AckResponderSessionStore(PersistentStorage& storage);

    [[nodiscard]] AckResponderSessionAllocation allocate(
        const AckResponderSessionRequest& request);
    [[nodiscard]] AckResponderSessionError reset();

private:
    PersistentStorage& storage_;
};

}  // namespace opentrail::persistence
