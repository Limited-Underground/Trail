#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/identity_model.hpp"

namespace opentrail::security {

inline constexpr std::size_t kTrafficKeyContextBytes = 52;
inline constexpr std::uint8_t kTrafficKeyContextVersion = 1;

enum class TrafficKeyPurpose : std::uint8_t {
    group_aead_key = 1,
    nonce_prefix = 2,
    counter_domain_id = 3,
};

enum class TrafficKeyContextError : std::uint8_t {
    none = 0,
    invalid_group,
    invalid_epoch,
    invalid_sender,
    invalid_purpose,
};

struct TrafficKeyContextRequest {
    std::uint64_t group_id{0};
    std::uint32_t group_epoch{0};
    identity::IdentityFingerprint sender_fingerprint{};
    TrafficKeyPurpose purpose{TrafficKeyPurpose::group_aead_key};
};

struct TrafficKeyContextResult {
    TrafficKeyContextError error{TrafficKeyContextError::invalid_group};
    std::array<std::uint8_t, kTrafficKeyContextBytes> bytes{};

    [[nodiscard]] constexpr bool encoded() const {
        return error == TrafficKeyContextError::none;
    }
};

// Encodes public domain-separation input for a future audited KDF adapter. It
// does not accept or produce secret material and performs no cryptography.
[[nodiscard]] TrafficKeyContextResult encode_traffic_key_context(
    const TrafficKeyContextRequest& request);

}  // namespace opentrail::security
