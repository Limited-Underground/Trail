#pragma once

#include <cstddef>
#include <cstdint>

#include "opentrail/lora_airtime.hpp"

namespace opentrail::protocol {

inline constexpr std::size_t kCandidateProtectedHeaderBytes = 44;
inline constexpr std::size_t kCandidateAuthenticationTagBytes = 16;
inline constexpr std::size_t kEd25519CandidateSignatureBytes = 64;
inline constexpr std::size_t kMaximumProtectedPacketFragments = 16;

enum class ProtectedPacketBudgetError : std::uint8_t {
    none = 0,
    invalid_request,
    no_plaintext_capacity,
    fragment_limit_exceeded,
    airtime_failure,
};

struct ProtectedPacketBudgetRequest {
    std::size_t transport_mtu{0};
    std::size_t authenticated_header_bytes{0};
    std::size_t authentication_tag_bytes{0};
    std::size_t source_authentication_bytes{0};
    std::size_t forwarding_wrapper_bytes{0};
    std::size_t plaintext_bytes{0};
    std::size_t maximum_fragments{0};
    radio::LoRaAirtimeSettings airtime{};
};

struct ProtectedPacketBudgetResult {
    ProtectedPacketBudgetError error{ProtectedPacketBudgetError::invalid_request};
    std::size_t overhead_bytes{0};
    std::size_t maximum_plaintext_per_frame{0};
    std::size_t fragment_count{0};
    std::size_t final_frame_bytes{0};
    std::uint64_t total_frame_bytes{0};
    std::uint64_t total_airtime_us{0};

    [[nodiscard]] constexpr bool calculated() const {
        return error == ProtectedPacketBudgetError::none;
    }
};

// Computes only capacity and theoretical LoRa airtime. It does not define or
// encode packet v1, perform cryptography, or authorize fragmentation policy.
[[nodiscard]] ProtectedPacketBudgetResult calculate_protected_packet_budget(
    const ProtectedPacketBudgetRequest& request);

}  // namespace opentrail::protocol
