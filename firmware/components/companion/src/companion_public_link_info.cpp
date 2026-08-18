#include "opentrail/companion_public_link_info.hpp"

namespace opentrail::companion {
namespace {

constexpr CompanionProtocolInfo public_link_info() {
    return {
        CompanionDeviceRole::screenless_client,
        0,
        1,
        24,
        1,
        1,
    };
}

bool is_exact_public_link_info(const CompanionProtocolInfo& info) {
    const auto expected = public_link_info();
    return info.role == expected.role &&
           info.capabilities == expected.capabilities &&
           info.max_fragment_payload_bytes ==
               expected.max_fragment_payload_bytes &&
           info.minimum_att_mtu == expected.minimum_att_mtu &&
           info.max_fragment_count == expected.max_fragment_count &&
           info.max_active_controllers == expected.max_active_controllers;
}

}  // namespace

CompanionEncodeResult encode_companion_public_link_info(
    radio::MutableByteView output) {
    return encode_companion_protocol_info(public_link_info(), output);
}

CompanionPublicLinkInfoDecodeResult decode_companion_public_link_info(
    radio::ByteView encoded) {
    const auto decoded = decode_companion_protocol_info(encoded);
    if (!decoded.decoded()) {
        return {decoded.error};
    }
    if (!is_exact_public_link_info(decoded.info)) {
        return {CompanionCodecError::malformed};
    }
    return {CompanionCodecError::none};
}

}  // namespace opentrail::companion
