#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/outbound_counter_lease_store.hpp"

namespace opentrail::security {

inline constexpr std::size_t kAeadNonceBytes = 12;
inline constexpr std::size_t kAeadNoncePrefixBytes = 4;

using AeadNoncePrefix = std::array<std::uint8_t, kAeadNoncePrefixBytes>;
using AeadNonceBytes = std::array<std::uint8_t, kAeadNonceBytes>;

enum class AeadNonceError : std::uint8_t {
    none = 0,
    invalid_domain,
    domain_mismatch,
    invalid_counter,
};

struct AeadNonceRequest {
    // Domain attached to the durable counter lease.
    persistence::CounterDomainId lease_domain_id{};
    // Domain attached by the crypto adapter to the traffic key and prefix.
    persistence::CounterDomainId key_domain_id{};
    AeadNoncePrefix key_domain_prefix{};
    std::uint64_t counter{0};
};

struct AeadNonceResult {
    AeadNonceError error{AeadNonceError::invalid_domain};
    AeadNonceBytes bytes{};

    [[nodiscard]] constexpr bool composed() const {
        return error == AeadNonceError::none;
    }
};

// Composes a 96-bit nonce as the crypto-adapter-supplied 32-bit key-domain
// prefix followed by the rollback-safe 64-bit counter in network byte order.
// This function performs no key derivation, hashing, encryption, or allocation.
[[nodiscard]] AeadNonceResult compose_aead_nonce(
    const AeadNonceRequest& request);

}  // namespace opentrail::security
