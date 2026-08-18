#pragma once

#include <cstddef>
#include <cstdint>

#include "opentrail/companion_protocol.hpp"

namespace opentrail::companion {

// One fixed, privacy-safe OTB0/v0 record for an unauthenticated BLE link.
// It exposes only the screenless role and the smallest codec-valid transport
// bounds. Capabilities remain zero until a separately authorized session exists.
inline constexpr std::size_t kCompanionPublicLinkInfoBytes =
    kCompanionProtocolInfoBytes;

struct CompanionPublicLinkInfoDecodeResult {
    CompanionCodecError error{CompanionCodecError::malformed};

    [[nodiscard]] constexpr bool decoded() const {
        return error == CompanionCodecError::none;
    }
};

// There is deliberately no caller-supplied record: this API can emit only the
// one fixed public link-info value and cannot carry identity or configuration.
[[nodiscard]] CompanionEncodeResult encode_companion_public_link_info(
    radio::MutableByteView output);

// Successful decoding means the bytes equal the exact fixed public value. No
// decoded identity, state, authority, or mutation surface is returned.
[[nodiscard]] CompanionPublicLinkInfoDecodeResult
decode_companion_public_link_info(radio::ByteView encoded);

}  // namespace opentrail::companion
