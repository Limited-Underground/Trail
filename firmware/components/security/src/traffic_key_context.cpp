#include "opentrail/traffic_key_context.hpp"

#include <algorithm>

namespace opentrail::security {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{{'O', 'T', 'K', 'D'}};

bool all_zero(const identity::IdentityFingerprint& fingerprint) {
    return std::all_of(fingerprint.begin(), fingerprint.end(),
                       [](std::uint8_t value) { return value == 0; });
}

bool valid_purpose(TrafficKeyPurpose purpose) {
    return purpose == TrafficKeyPurpose::group_aead_key ||
           purpose == TrafficKeyPurpose::nonce_prefix ||
           purpose == TrafficKeyPurpose::counter_domain_id;
}

void write_u32_be(std::uint8_t* output, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        output[index] = static_cast<std::uint8_t>(
            (value >> ((3 - index) * 8)) & 0xFFU);
    }
}

void write_u64_be(std::uint8_t* output, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        output[index] = static_cast<std::uint8_t>(
            (value >> ((7 - index) * 8)) & 0xFFU);
    }
}

}  // namespace

TrafficKeyContextResult encode_traffic_key_context(
    const TrafficKeyContextRequest& request) {
    TrafficKeyContextResult result{};
    if (request.group_id == 0) {
        result.error = TrafficKeyContextError::invalid_group;
        return result;
    }
    if (request.group_epoch == 0) {
        result.error = TrafficKeyContextError::invalid_epoch;
        return result;
    }
    if (all_zero(request.sender_fingerprint)) {
        result.error = TrafficKeyContextError::invalid_sender;
        return result;
    }
    if (!valid_purpose(request.purpose)) {
        result.error = TrafficKeyContextError::invalid_purpose;
        return result;
    }

    std::copy(kMagic.begin(), kMagic.end(), result.bytes.begin());
    result.bytes[4] = kTrafficKeyContextVersion;
    result.bytes[5] = static_cast<std::uint8_t>(request.purpose);
    result.bytes[6] = 0;
    result.bytes[7] = static_cast<std::uint8_t>(kTrafficKeyContextBytes);
    write_u64_be(result.bytes.data() + 8, request.group_id);
    write_u32_be(result.bytes.data() + 16, request.group_epoch);
    std::copy(request.sender_fingerprint.begin(),
              request.sender_fingerprint.end(), result.bytes.begin() + 20);
    result.error = TrafficKeyContextError::none;
    return result;
}

}  // namespace opentrail::security
