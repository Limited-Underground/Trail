#include "opentrail/aead_nonce.hpp"

#include <algorithm>

namespace opentrail::security {
namespace {

bool all_zero(const persistence::CounterDomainId& domain) {
    return std::all_of(domain.begin(), domain.end(), [](std::uint8_t value) {
        return value == 0;
    });
}

}  // namespace

AeadNonceResult compose_aead_nonce(const AeadNonceRequest& request) {
    AeadNonceResult result{};
    if (all_zero(request.lease_domain_id) ||
        all_zero(request.key_domain_id)) {
        result.error = AeadNonceError::invalid_domain;
        return result;
    }
    if (request.lease_domain_id != request.key_domain_id) {
        result.error = AeadNonceError::domain_mismatch;
        return result;
    }
    if (request.counter == 0) {
        result.error = AeadNonceError::invalid_counter;
        return result;
    }

    std::copy(request.key_domain_prefix.begin(),
              request.key_domain_prefix.end(), result.bytes.begin());
    for (std::size_t index = 0; index < 8; ++index) {
        const auto shift = static_cast<unsigned>((7 - index) * 8);
        result.bytes[kAeadNoncePrefixBytes + index] =
            static_cast<std::uint8_t>((request.counter >> shift) & 0xFFU);
    }
    result.error = AeadNonceError::none;
    return result;
}

}  // namespace opentrail::security
