#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/companion_configuration_codec.hpp"

namespace opentrail::companion {

// OTDC v1: the protected app's commands for conversations outside a group
// (F9/A6). The payload rides inside a negotiated OTC0 configuration frame.
// Wire validation only: the board's DirectConversationOwner decides authority.
// Carried on negotiated profile 5 (OTC0 minor 5) as kinds 0x0c and 0x90.
inline constexpr std::uint8_t kConfigurationDirectCommandKind = 0x0c;
inline constexpr std::uint8_t kConfigurationDirectResultKind = 0x90;
inline constexpr std::size_t kDirectConfigurationHeaderBytes = 8;
inline constexpr std::size_t kDirectConfigurationIdentityBytes = 32;
inline constexpr std::size_t kDirectConfigurationConversations = 8;
inline constexpr std::size_t kDirectConfigurationBlocked = 16;
inline constexpr std::size_t kDirectConfigurationMaxTextBytes = 90;  // OTXT, one frame (D-060)

enum class DirectConfigurationOperation : std::uint8_t {
    begin = 1,         // role + code: start an invitation attempt
    review = 2,        // read the peer the authenticated attempt reached
    accept = 3,        // binding: local consent for exactly that peer and scope
    decline = 4,
    finish = 5,        // binding: busy until both boards have committed
    read = 6,          // slot: conversation state and current traffic generation
    end = 7,           // binding
    block = 8,         // binding
    unblock = 9,       // peer
    read_blocked = 10, // block slot
    send = 11,         // slot + generation + message id + text
    received = 12,     // board to app: slot + generation + message id + text
    delivery = 13,     // board to app: slot + generation + message id + phase
};

// Same order as DirectResult.
enum class DirectConfigurationStatus : std::uint8_t {
    ok = 0, busy = 1, refused = 2, blocked = 3, full = 4, expired = 5, stale = 6, unavailable = 7,
};

enum class DirectConfigurationState : std::uint8_t {
    empty = 0, waiting_for_peer = 1, accepted = 2, ready = 3, ending = 4,
};

enum class DirectConfigurationDelivery : std::uint8_t {
    queued = 1, delivered = 2, failed = 3, unconfirmed = 4,
};

struct DirectConfigurationBinding {
    std::array<std::uint8_t, kDirectConfigurationIdentityBytes> peer{};
    std::array<std::uint8_t, kDirectConfigurationIdentityBytes> scope{};
    bool operator==(const DirectConfigurationBinding& other) const {
        return peer == other.peer && scope == other.scope;
    }
    bool operator!=(const DirectConfigurationBinding& other) const { return !(*this == other); }
};

struct DirectConfigurationText {
    std::uint8_t slot{0};
    std::uint64_t generation{0};
    std::uint32_t message_id{0};
    std::uint8_t size{0};
    std::array<std::uint8_t, kDirectConfigurationMaxTextBytes> bytes{};
};

struct DirectConfigurationCommand {
    DirectConfigurationOperation operation{DirectConfigurationOperation::review};
    std::uint8_t role{0};          // begin: 1 starts, 2 joins
    std::uint32_t code{0};         // begin
    std::uint8_t slot{0};          // read, read_blocked
    DirectConfigurationBinding binding{};  // accept, finish, end, block; unblock uses peer only
    DirectConfigurationText text{};        // send
};

struct DirectConfigurationResult {
    DirectConfigurationOperation operation{DirectConfigurationOperation::review};
    DirectConfigurationStatus status{DirectConfigurationStatus::unavailable};
    // review ok: binding. read ok: slot, state, generation, binding.
    // read_blocked ok: slot, binding.peer.
    std::uint8_t slot{0};
    DirectConfigurationState state{DirectConfigurationState::empty};
    std::uint64_t generation{0};
    DirectConfigurationBinding binding{};
    DirectConfigurationText text{};  // received; delivery uses slot, generation, message_id
    DirectConfigurationDelivery phase{DirectConfigurationDelivery::queued};
};

[[nodiscard]] ConfigurationEncodeResult encode_direct_configuration_command(
    const DirectConfigurationCommand&, std::uint8_t*, std::size_t);
[[nodiscard]] ConfigurationDecodeResult<DirectConfigurationCommand> decode_direct_configuration_command(
    const std::uint8_t*, std::size_t);
[[nodiscard]] ConfigurationEncodeResult encode_direct_configuration_result(
    const DirectConfigurationResult&, std::uint8_t*, std::size_t);
[[nodiscard]] ConfigurationDecodeResult<DirectConfigurationResult> decode_direct_configuration_result(
    const std::uint8_t*, std::size_t);

}  // namespace opentrail::companion
