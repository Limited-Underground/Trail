#include "opentrail/companion_protocol.hpp"

#include <array>

namespace opentrail::companion {
namespace {

constexpr std::array<std::uint8_t, 4> kInfoMagic{'O', 'T', 'B', '0'};
constexpr std::array<std::uint8_t, 4> kFragmentMagic{'O', 'T', 'C', '0'};

void write_u16_le(std::uint8_t* destination, std::uint16_t value) {
    destination[0] = static_cast<std::uint8_t>(value & 0xFFU);
    destination[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

std::uint16_t read_u16_le(const std::uint8_t* source) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(source[0]) |
        (static_cast<std::uint16_t>(source[1]) << 8U));
}

void write_u32_le(std::uint8_t* destination, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        destination[index] =
            static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

std::uint32_t read_u32_le(const std::uint8_t* source) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(source[index]) << (index * 8U);
    }
    return value;
}

bool has_magic(const std::uint8_t* source,
               const std::array<std::uint8_t, 4>& magic) {
    for (std::size_t index = 0; index < magic.size(); ++index) {
        if (source[index] != magic[index]) {
            return false;
        }
    }
    return true;
}

bool known_role(CompanionDeviceRole role) {
    return role == CompanionDeviceRole::screenless_client;
}

bool known_kind(CompanionFrameKind kind) {
    switch (kind) {
        case CompanionFrameKind::snapshot_request:
        case CompanionFrameKind::action_request:
        case CompanionFrameKind::snapshot:
        case CompanionFrameKind::action_result:
        case CompanionFrameKind::event:
            return true;
    }
    return false;
}

bool request_kind(CompanionFrameKind kind) {
    return kind == CompanionFrameKind::snapshot_request ||
           kind == CompanionFrameKind::action_request;
}

CompanionCodecError validate_info(const CompanionProtocolInfo& info) {
    if (!known_role(info.role)) {
        return CompanionCodecError::unknown_role;
    }
    if ((info.capabilities & ~kCompanionKnownCapabilityMask) != 0) {
        return CompanionCodecError::unknown_capability;
    }
    if (info.max_fragment_payload_bytes == 0 ||
        info.max_fragment_payload_bytes > kCompanionMaxFragmentPayloadBytes ||
        info.minimum_att_mtu < 23 ||
        info.minimum_att_mtu < static_cast<std::uint16_t>(
            kCompanionFragmentHeaderBytes +
            info.max_fragment_payload_bytes + 3U) ||
        info.max_fragment_count == 0 ||
        info.max_fragment_count > kCompanionMaxFragmentCount ||
        info.max_active_controllers != 1) {
        return CompanionCodecError::invalid_limit;
    }
    return CompanionCodecError::none;
}

CompanionCodecError validate_fragment(const CompanionFragment& fragment) {
    if (!known_kind(fragment.kind)) {
        return CompanionCodecError::unknown_frame_kind;
    }
    if (fragment.session_nonce == 0) {
        return CompanionCodecError::invalid_session_nonce;
    }
    if (fragment.exchange_id == 0) {
        return CompanionCodecError::invalid_exchange_id;
    }
    if (fragment.fragment_count == 0 ||
        fragment.fragment_count > kCompanionMaxFragmentCount ||
        fragment.fragment_index >= fragment.fragment_count) {
        return CompanionCodecError::invalid_fragment;
    }
    if (fragment.payload_bytes > kCompanionMaxFragmentPayloadBytes) {
        return CompanionCodecError::payload_too_large;
    }
    return CompanionCodecError::none;
}

}  // namespace

CompanionEncodeResult encode_companion_protocol_info(
    const CompanionProtocolInfo& info,
    radio::MutableByteView output) {
    if (output.data == nullptr) {
        return {CompanionCodecError::invalid_argument, 0};
    }
    if (output.size < kCompanionProtocolInfoBytes) {
        return {CompanionCodecError::output_too_small,
                kCompanionProtocolInfoBytes};
    }
    const auto validation = validate_info(info);
    if (validation != CompanionCodecError::none) {
        return {validation, 0};
    }

    std::array<std::uint8_t, kCompanionProtocolInfoBytes> candidate{};
    for (std::size_t index = 0; index < kInfoMagic.size(); ++index) {
        candidate[index] = kInfoMagic[index];
    }
    candidate[4] = kCompanionProtocolMajor;
    candidate[5] = kCompanionProtocolMinor;
    candidate[6] = static_cast<std::uint8_t>(info.role);
    candidate[7] = info.capabilities;
    write_u16_le(candidate.data() + 8, info.max_fragment_payload_bytes);
    write_u16_le(candidate.data() + 10, info.minimum_att_mtu);
    candidate[12] = info.max_fragment_count;
    candidate[13] = info.max_active_controllers;
    candidate[14] = 0;
    candidate[15] = 0;

    for (std::size_t index = 0; index < candidate.size(); ++index) {
        output.data[index] = candidate[index];
    }
    return {CompanionCodecError::none, candidate.size()};
}

CompanionInfoDecodeResult decode_companion_protocol_info(
    radio::ByteView encoded) {
    if (encoded.data == nullptr) {
        return {CompanionCodecError::invalid_argument, {}};
    }
    if (encoded.size != kCompanionProtocolInfoBytes ||
        !has_magic(encoded.data, kInfoMagic)) {
        return {CompanionCodecError::malformed, {}};
    }
    if (encoded.data[4] != kCompanionProtocolMajor ||
        encoded.data[5] != kCompanionProtocolMinor) {
        return {CompanionCodecError::unsupported_version, {}};
    }
    if (encoded.data[14] != 0 || encoded.data[15] != 0) {
        return {CompanionCodecError::reserved_bits_set, {}};
    }

    CompanionProtocolInfo info{};
    info.role = static_cast<CompanionDeviceRole>(encoded.data[6]);
    info.capabilities = encoded.data[7];
    info.max_fragment_payload_bytes = read_u16_le(encoded.data + 8);
    info.minimum_att_mtu = read_u16_le(encoded.data + 10);
    info.max_fragment_count = encoded.data[12];
    info.max_active_controllers = encoded.data[13];
    const auto validation = validate_info(info);
    if (validation != CompanionCodecError::none) {
        return {validation, {}};
    }
    return {CompanionCodecError::none, info};
}

CompanionEncodeResult encode_companion_fragment(
    const CompanionFragment& fragment,
    radio::MutableByteView output) {
    if (output.data == nullptr) {
        return {CompanionCodecError::invalid_argument, 0};
    }
    const auto validation = validate_fragment(fragment);
    if (validation != CompanionCodecError::none) {
        return {validation, 0};
    }
    const auto encoded_bytes =
        kCompanionFragmentHeaderBytes + fragment.payload_bytes;
    if (output.size < encoded_bytes) {
        return {CompanionCodecError::output_too_small, encoded_bytes};
    }

    std::array<std::uint8_t, kCompanionMaxFragmentBytes> candidate{};
    for (std::size_t index = 0; index < kFragmentMagic.size(); ++index) {
        candidate[index] = kFragmentMagic[index];
    }
    candidate[4] = kCompanionProtocolMajor;
    candidate[5] = kCompanionProtocolMinor;
    candidate[6] = static_cast<std::uint8_t>(fragment.kind);
    candidate[7] = 0;
    write_u32_le(candidate.data() + 8, fragment.session_nonce);
    write_u32_le(candidate.data() + 12, fragment.exchange_id);
    candidate[16] = fragment.fragment_index;
    candidate[17] = fragment.fragment_count;
    write_u16_le(candidate.data() + 18, fragment.payload_bytes);
    for (std::size_t index = 0; index < fragment.payload_bytes; ++index) {
        candidate[kCompanionFragmentHeaderBytes + index] =
            fragment.payload[index];
    }

    for (std::size_t index = 0; index < encoded_bytes; ++index) {
        output.data[index] = candidate[index];
    }
    return {CompanionCodecError::none, encoded_bytes};
}

CompanionFragmentDecodeResult decode_companion_fragment(
    radio::ByteView encoded) {
    if (encoded.data == nullptr) {
        return {CompanionCodecError::invalid_argument, {}};
    }
    if (encoded.size < kCompanionFragmentHeaderBytes ||
        !has_magic(encoded.data, kFragmentMagic)) {
        return {CompanionCodecError::malformed, {}};
    }
    if (encoded.data[4] != kCompanionProtocolMajor ||
        encoded.data[5] != kCompanionProtocolMinor) {
        return {CompanionCodecError::unsupported_version, {}};
    }
    if (encoded.data[7] != 0) {
        return {CompanionCodecError::reserved_bits_set, {}};
    }

    CompanionFragment fragment{};
    fragment.kind = static_cast<CompanionFrameKind>(encoded.data[6]);
    fragment.session_nonce = read_u32_le(encoded.data + 8);
    fragment.exchange_id = read_u32_le(encoded.data + 12);
    fragment.fragment_index = encoded.data[16];
    fragment.fragment_count = encoded.data[17];
    fragment.payload_bytes = read_u16_le(encoded.data + 18);
    const auto validation = validate_fragment(fragment);
    if (validation != CompanionCodecError::none) {
        return {validation, {}};
    }
    if (encoded.size !=
        kCompanionFragmentHeaderBytes + fragment.payload_bytes) {
        return {CompanionCodecError::malformed, {}};
    }
    for (std::size_t index = 0; index < fragment.payload_bytes; ++index) {
        fragment.payload[index] =
            encoded.data[kCompanionFragmentHeaderBytes + index];
    }
    return {CompanionCodecError::none, fragment};
}

CompanionSessionError CompanionSessionGuard::validate_evidence(
    const CompanionSessionEvidence& evidence) {
    if (evidence.controller_binding == 0) {
        return CompanionSessionError::invalid_controller_binding;
    }
    if (!evidence.link_encrypted) {
        return CompanionSessionError::link_not_encrypted;
    }
    if (!evidence.authenticated_bond) {
        return CompanionSessionError::bond_not_authenticated;
    }
    if (!evidence.application_authorized) {
        return CompanionSessionError::controller_not_authorized;
    }
    return CompanionSessionError::none;
}

CompanionSessionResult CompanionSessionGuard::open_session(
    const CompanionSessionEvidence& evidence,
    std::uint32_t device_generated_session_nonce) {
    const auto evidence_error = validate_evidence(evidence);
    if (evidence_error != CompanionSessionError::none) {
        return {evidence_error, 0};
    }
    if (status_.active) {
        return {CompanionSessionError::session_in_use, 0};
    }
    if (device_generated_session_nonce == 0) {
        return {CompanionSessionError::session_nonce_invalid, 0};
    }
    if (last_session_nonce_ == UINT32_MAX) {
        return {CompanionSessionError::session_nonce_exhausted, 0};
    }
    if (device_generated_session_nonce <= last_session_nonce_) {
        return {CompanionSessionError::session_nonce_reused, 0};
    }

    controller_binding_ = evidence.controller_binding;
    current_session_nonce_ = device_generated_session_nonce;
    last_session_nonce_ = device_generated_session_nonce;
    last_request_id_ = 0;
    status_.active = true;
    status_.session_nonce = device_generated_session_nonce;
    status_.last_request_id = 0;
    return {CompanionSessionError::none, device_generated_session_nonce};
}

CompanionSessionError CompanionSessionGuard::close_session(
    std::uint64_t controller_binding) {
    if (!status_.active) {
        return CompanionSessionError::no_active_session;
    }
    if (controller_binding == 0 ||
        controller_binding != controller_binding_) {
        return CompanionSessionError::wrong_controller;
    }
    controller_binding_ = 0;
    current_session_nonce_ = 0;
    last_request_id_ = 0;
    status_.active = false;
    status_.session_nonce = 0;
    status_.last_request_id = 0;
    return CompanionSessionError::none;
}

CompanionRequestAdmission CompanionSessionGuard::reject(
    CompanionSessionError error,
    std::uint32_t request_id) {
    ++status_.rejected_requests;
    return {CompanionRequestDisposition::rejected, error, request_id};
}

CompanionRequestAdmission CompanionSessionGuard::admit_request(
    const CompanionSessionEvidence& evidence,
    const CompanionFragment& fragment) {
    const auto evidence_error = validate_evidence(evidence);
    if (evidence_error != CompanionSessionError::none) {
        return reject(evidence_error, fragment.exchange_id);
    }
    if (!status_.active) {
        return reject(CompanionSessionError::no_active_session,
                      fragment.exchange_id);
    }
    if (evidence.controller_binding != controller_binding_) {
        return reject(CompanionSessionError::wrong_controller,
                      fragment.exchange_id);
    }
    if (fragment.session_nonce != current_session_nonce_) {
        return reject(CompanionSessionError::wrong_session,
                      fragment.exchange_id);
    }
    if (!request_kind(fragment.kind)) {
        return reject(CompanionSessionError::wrong_direction,
                      fragment.exchange_id);
    }
    if (fragment.fragment_index != 0 || fragment.fragment_count != 1) {
        return reject(CompanionSessionError::fragmented_request,
                      fragment.exchange_id);
    }
    if (fragment.exchange_id == 0) {
        return reject(CompanionSessionError::request_id_invalid, 0);
    }
    if (fragment.exchange_id == last_request_id_) {
        ++status_.duplicate_requests;
        return {CompanionRequestDisposition::duplicate,
                CompanionSessionError::none,
                fragment.exchange_id};
    }
    if (last_request_id_ == UINT32_MAX) {
        return reject(CompanionSessionError::request_id_exhausted,
                      fragment.exchange_id);
    }
    if (fragment.exchange_id < last_request_id_) {
        return reject(CompanionSessionError::stale_request,
                      fragment.exchange_id);
    }

    last_request_id_ = fragment.exchange_id;
    status_.last_request_id = fragment.exchange_id;
    ++status_.accepted_requests;
    return {CompanionRequestDisposition::accepted_new,
            CompanionSessionError::none,
            fragment.exchange_id};
}

CompanionSessionStatus CompanionSessionGuard::status() const {
    return status_;
}

}  // namespace opentrail::companion
