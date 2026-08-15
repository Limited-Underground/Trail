#include "opentrail/companion_semantics.hpp"

#include <array>

namespace opentrail::companion {
namespace {

constexpr std::array<std::uint8_t, 4> kSnapshotRequestMagic{'O', 'T', 'X', '0'};
constexpr std::array<std::uint8_t, 4> kStatusSnapshotMagic{'O', 'T', 'N', '0'};
constexpr std::array<std::uint8_t, 4> kActionRequestMagic{'O', 'T', 'A', '0'};
constexpr std::array<std::uint8_t, 4> kActionResultMagic{'O', 'T', 'R', '0'};

template <std::size_t N>
bool has_magic(const std::uint8_t* source,
               const std::array<std::uint8_t, N>& magic) {
    for (std::size_t index = 0; index < magic.size(); ++index) {
        if (source[index] != magic[index]) {
            return false;
        }
    }
    return true;
}

void write_u16_le(std::uint8_t* destination, std::uint16_t value) {
    destination[0] = static_cast<std::uint8_t>(value & 0xFFU);
    destination[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

std::uint16_t read_u16_le(const std::uint8_t* source) {
    return static_cast<std::uint16_t>(source[0]) |
           static_cast<std::uint16_t>(source[1] << 8U);
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

void write_u64_le(std::uint8_t* destination, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        destination[index] =
            static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

std::uint64_t read_u64_le(const std::uint8_t* source) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(source[index]) << (index * 8U);
    }
    return value;
}

template <std::size_t N>
CompanionSemanticCodecError validate_prefix(
    radio::ByteView encoded,
    const std::array<std::uint8_t, N>& magic,
    std::size_t expected_bytes) {
    if (encoded.data == nullptr) {
        return CompanionSemanticCodecError::invalid_argument;
    }
    if (encoded.size != expected_bytes || !has_magic(encoded.data, magic)) {
        return CompanionSemanticCodecError::malformed;
    }
    if (encoded.data[4] != kCompanionSemanticMajor ||
        encoded.data[5] != kCompanionSemanticMinor) {
        return CompanionSemanticCodecError::unsupported_version;
    }
    return CompanionSemanticCodecError::none;
}

bool known_radio(CompanionRadioState value) {
    return value >= CompanionRadioState::unknown &&
           value <= CompanionRadioState::fault;
}

bool known_gnss(CompanionGnssState value) {
    return value >= CompanionGnssState::unknown &&
           value <= CompanionGnssState::fault;
}

bool known_power(CompanionPowerState value) {
    return value >= CompanionPowerState::unknown &&
           value <= CompanionPowerState::fault;
}

bool known_position(CompanionPositionSharingState value) {
    return value >= CompanionPositionSharingState::stopped &&
           value <= CompanionPositionSharingState::fault;
}

bool known_action(CompanionActionKind value) {
    return value >= CompanionActionKind::quick_status &&
           value <= CompanionActionKind::stop_position_sharing;
}

bool known_quick_status(protocol::QuickStatusKind value) {
    return value >= protocol::QuickStatusKind::ok &&
           value <= protocol::QuickStatusKind::available_to_help;
}

bool known_disposition(CompanionActionDisposition value) {
    return value >= CompanionActionDisposition::admitted &&
           value <= CompanionActionDisposition::rejected;
}

bool known_reject_reason(CompanionActionRejectReason value) {
    return value >= CompanionActionRejectReason::none &&
           value <= CompanionActionRejectReason::internal_failure;
}

CompanionSemanticCodecError validate_snapshot(
    const CompanionStatusSnapshot& snapshot) {
    if (snapshot.revision == 0) {
        return CompanionSemanticCodecError::invalid_revision;
    }
    if (!known_radio(snapshot.radio) || !known_gnss(snapshot.gnss) ||
        !known_power(snapshot.power) ||
        !known_position(snapshot.position_sharing)) {
        return CompanionSemanticCodecError::unknown_enum;
    }
    return CompanionSemanticCodecError::none;
}

CompanionSemanticCodecError validate_action(
    CompanionActionKind kind,
    protocol::QuickStatusKind quick_status,
    std::uint64_t critical_alert_id) {
    if (!known_action(kind)) {
        return CompanionSemanticCodecError::unknown_enum;
    }
    if (kind == CompanionActionKind::quick_status) {
        if (!known_quick_status(quick_status)) {
            return CompanionSemanticCodecError::invalid_quick_status;
        }
        return critical_alert_id == 0
                   ? CompanionSemanticCodecError::none
                   : CompanionSemanticCodecError::incoherent_action;
    }
    if (kind == CompanionActionKind::acknowledge_critical_alert) {
        return critical_alert_id != 0
                   ? CompanionSemanticCodecError::none
                   : CompanionSemanticCodecError::invalid_alert_id;
    }
    return critical_alert_id == 0
               ? CompanionSemanticCodecError::none
               : CompanionSemanticCodecError::incoherent_action;
}

std::uint8_t encoded_detail(const CompanionActionRequest& request) {
    return request.kind == CompanionActionKind::quick_status
               ? static_cast<std::uint8_t>(request.quick_status)
               : 0;
}

std::uint8_t encoded_detail(const CompanionActionResult& result) {
    return result.kind == CompanionActionKind::quick_status
               ? static_cast<std::uint8_t>(result.quick_status)
               : 0;
}

CompanionSemanticCodecError validate_result(
    const CompanionActionResult& result) {
    const auto action_error = validate_action(
        result.kind, result.quick_status, result.critical_alert_id);
    if (action_error != CompanionSemanticCodecError::none) {
        return action_error;
    }
    if (!known_disposition(result.disposition) ||
        !known_reject_reason(result.reject_reason)) {
        return CompanionSemanticCodecError::unknown_enum;
    }
    if (result.disposition == CompanionActionDisposition::rejected) {
        if (result.reject_reason == CompanionActionRejectReason::none) {
            return CompanionSemanticCodecError::incoherent_result;
        }
        const bool outbound_action =
            result.kind == CompanionActionKind::quick_status ||
            result.kind == CompanionActionKind::acknowledge_critical_alert;
        if (result.reject_reason == CompanionActionRejectReason::stale_alert &&
            result.kind != CompanionActionKind::acknowledge_critical_alert) {
            return CompanionSemanticCodecError::incoherent_result;
        }
        if (result.reject_reason == CompanionActionRejectReason::queue_full &&
            !outbound_action) {
            return CompanionSemanticCodecError::incoherent_result;
        }
        return CompanionSemanticCodecError::none;
    }
    if (result.reject_reason != CompanionActionRejectReason::none) {
        return CompanionSemanticCodecError::incoherent_result;
    }
    const bool outbound_action =
        result.kind == CompanionActionKind::quick_status ||
        result.kind == CompanionActionKind::acknowledge_critical_alert;
    if (outbound_action &&
        result.disposition != CompanionActionDisposition::queued) {
        return CompanionSemanticCodecError::incoherent_result;
    }
    if (!outbound_action &&
        result.disposition != CompanionActionDisposition::admitted) {
        return CompanionSemanticCodecError::incoherent_result;
    }
    return CompanionSemanticCodecError::none;
}

template <std::size_t N>
CompanionSemanticEncodeResult write_candidate(
    const std::array<std::uint8_t, N>& candidate,
    radio::MutableByteView output) {
    if (output.data == nullptr) {
        return {CompanionSemanticCodecError::invalid_argument, 0};
    }
    if (output.size < candidate.size()) {
        return {CompanionSemanticCodecError::output_too_small,
                candidate.size()};
    }
    for (std::size_t index = 0; index < candidate.size(); ++index) {
        output.data[index] = candidate[index];
    }
    return {CompanionSemanticCodecError::none, candidate.size()};
}

}  // namespace

CompanionSemanticEncodeResult encode_companion_snapshot_request(
    const CompanionSnapshotRequest&,
    radio::MutableByteView output) {
    std::array<std::uint8_t, kCompanionSnapshotRequestBytes> candidate{};
    for (std::size_t index = 0; index < kSnapshotRequestMagic.size(); ++index) {
        candidate[index] = kSnapshotRequestMagic[index];
    }
    candidate[4] = kCompanionSemanticMajor;
    candidate[5] = kCompanionSemanticMinor;
    return write_candidate(candidate, output);
}

CompanionSemanticDecodeResult<CompanionSnapshotRequest>
decode_companion_snapshot_request(radio::ByteView encoded) {
    const auto error = validate_prefix(
        encoded, kSnapshotRequestMagic, kCompanionSnapshotRequestBytes);
    if (error != CompanionSemanticCodecError::none) {
        return {error, {}};
    }
    if (encoded.data[6] != 0 || encoded.data[7] != 0) {
        return {CompanionSemanticCodecError::reserved_bits_set, {}};
    }
    return {CompanionSemanticCodecError::none, {}};
}

CompanionSemanticEncodeResult encode_companion_status_snapshot(
    const CompanionStatusSnapshot& snapshot,
    radio::MutableByteView output) {
    if (output.data == nullptr) {
        return {CompanionSemanticCodecError::invalid_argument, 0};
    }
    const auto error = validate_snapshot(snapshot);
    if (error != CompanionSemanticCodecError::none) {
        return {error, 0};
    }
    std::array<std::uint8_t, kCompanionStatusSnapshotBytes> candidate{};
    for (std::size_t index = 0; index < kStatusSnapshotMagic.size(); ++index) {
        candidate[index] = kStatusSnapshotMagic[index];
    }
    candidate[4] = kCompanionSemanticMajor;
    candidate[5] = kCompanionSemanticMinor;
    candidate[6] = static_cast<std::uint8_t>(snapshot.radio);
    candidate[7] = static_cast<std::uint8_t>(snapshot.gnss);
    candidate[8] = static_cast<std::uint8_t>(snapshot.power);
    candidate[9] = static_cast<std::uint8_t>(snapshot.position_sharing);
    write_u16_le(candidate.data() + 10, snapshot.queued_action_count);
    write_u32_le(candidate.data() + 12, snapshot.revision);
    write_u64_le(candidate.data() + 16, snapshot.pending_critical_alert_id);
    return write_candidate(candidate, output);
}

CompanionSemanticDecodeResult<CompanionStatusSnapshot>
decode_companion_status_snapshot(radio::ByteView encoded) {
    const auto prefix_error = validate_prefix(
        encoded, kStatusSnapshotMagic, kCompanionStatusSnapshotBytes);
    if (prefix_error != CompanionSemanticCodecError::none) {
        return {prefix_error, {}};
    }
    for (std::size_t index = 24; index < kCompanionStatusSnapshotBytes;
         ++index) {
        if (encoded.data[index] != 0) {
            return {CompanionSemanticCodecError::reserved_bits_set, {}};
        }
    }
    CompanionStatusSnapshot snapshot{};
    snapshot.radio = static_cast<CompanionRadioState>(encoded.data[6]);
    snapshot.gnss = static_cast<CompanionGnssState>(encoded.data[7]);
    snapshot.power = static_cast<CompanionPowerState>(encoded.data[8]);
    snapshot.position_sharing =
        static_cast<CompanionPositionSharingState>(encoded.data[9]);
    snapshot.queued_action_count = read_u16_le(encoded.data + 10);
    snapshot.revision = read_u32_le(encoded.data + 12);
    snapshot.pending_critical_alert_id = read_u64_le(encoded.data + 16);
    const auto error = validate_snapshot(snapshot);
    return error == CompanionSemanticCodecError::none
               ? CompanionSemanticDecodeResult<CompanionStatusSnapshot>{
                     CompanionSemanticCodecError::none, snapshot}
               : CompanionSemanticDecodeResult<CompanionStatusSnapshot>{
                     error, {}};
}

CompanionSemanticEncodeResult encode_companion_action_request(
    const CompanionActionRequest& request,
    radio::MutableByteView output) {
    if (output.data == nullptr) {
        return {CompanionSemanticCodecError::invalid_argument, 0};
    }
    const auto error = validate_action(
        request.kind, request.quick_status, request.critical_alert_id);
    if (error != CompanionSemanticCodecError::none) {
        return {error, 0};
    }
    std::array<std::uint8_t, kCompanionActionRequestBytes> candidate{};
    for (std::size_t index = 0; index < kActionRequestMagic.size(); ++index) {
        candidate[index] = kActionRequestMagic[index];
    }
    candidate[4] = kCompanionSemanticMajor;
    candidate[5] = kCompanionSemanticMinor;
    candidate[6] = static_cast<std::uint8_t>(request.kind);
    candidate[7] = encoded_detail(request);
    write_u64_le(candidate.data() + 8, request.critical_alert_id);
    return write_candidate(candidate, output);
}

CompanionSemanticDecodeResult<CompanionActionRequest>
decode_companion_action_request(radio::ByteView encoded) {
    const auto prefix_error = validate_prefix(
        encoded, kActionRequestMagic, kCompanionActionRequestBytes);
    if (prefix_error != CompanionSemanticCodecError::none) {
        return {prefix_error, {}};
    }
    for (std::size_t index = 16; index < kCompanionActionRequestBytes;
         ++index) {
        if (encoded.data[index] != 0) {
            return {CompanionSemanticCodecError::reserved_bits_set, {}};
        }
    }
    CompanionActionRequest request{};
    request.kind = static_cast<CompanionActionKind>(encoded.data[6]);
    request.quick_status = static_cast<protocol::QuickStatusKind>(
        encoded.data[7]);
    request.critical_alert_id = read_u64_le(encoded.data + 8);
    if (known_action(request.kind) &&
        request.kind != CompanionActionKind::quick_status &&
        encoded.data[7] != 0) {
        return {CompanionSemanticCodecError::reserved_bits_set, {}};
    }
    const auto error = validate_action(
        request.kind, request.quick_status, request.critical_alert_id);
    return error == CompanionSemanticCodecError::none
               ? CompanionSemanticDecodeResult<CompanionActionRequest>{
                     CompanionSemanticCodecError::none, request}
               : CompanionSemanticDecodeResult<CompanionActionRequest>{
                     error, {}};
}

CompanionSemanticEncodeResult encode_companion_action_result(
    const CompanionActionResult& result,
    radio::MutableByteView output) {
    if (output.data == nullptr) {
        return {CompanionSemanticCodecError::invalid_argument, 0};
    }
    const auto error = validate_result(result);
    if (error != CompanionSemanticCodecError::none) {
        return {error, 0};
    }
    std::array<std::uint8_t, kCompanionActionResultBytes> candidate{};
    for (std::size_t index = 0; index < kActionResultMagic.size(); ++index) {
        candidate[index] = kActionResultMagic[index];
    }
    candidate[4] = kCompanionSemanticMajor;
    candidate[5] = kCompanionSemanticMinor;
    candidate[6] = static_cast<std::uint8_t>(result.disposition);
    candidate[7] = static_cast<std::uint8_t>(result.reject_reason);
    candidate[8] = static_cast<std::uint8_t>(result.kind);
    candidate[9] = encoded_detail(result);
    write_u64_le(candidate.data() + 12, result.critical_alert_id);
    return write_candidate(candidate, output);
}

CompanionSemanticDecodeResult<CompanionActionResult>
decode_companion_action_result(radio::ByteView encoded) {
    const auto prefix_error = validate_prefix(
        encoded, kActionResultMagic, kCompanionActionResultBytes);
    if (prefix_error != CompanionSemanticCodecError::none) {
        return {prefix_error, {}};
    }
    if (encoded.data[10] != 0 || encoded.data[11] != 0) {
        return {CompanionSemanticCodecError::reserved_bits_set, {}};
    }
    CompanionActionResult result{};
    result.disposition =
        static_cast<CompanionActionDisposition>(encoded.data[6]);
    result.reject_reason =
        static_cast<CompanionActionRejectReason>(encoded.data[7]);
    result.kind = static_cast<CompanionActionKind>(encoded.data[8]);
    result.quick_status = static_cast<protocol::QuickStatusKind>(encoded.data[9]);
    result.critical_alert_id = read_u64_le(encoded.data + 12);
    if (known_action(result.kind) &&
        result.kind != CompanionActionKind::quick_status &&
        encoded.data[9] != 0) {
        return {CompanionSemanticCodecError::reserved_bits_set, {}};
    }
    const auto error = validate_result(result);
    return error == CompanionSemanticCodecError::none
               ? CompanionSemanticDecodeResult<CompanionActionResult>{
                     CompanionSemanticCodecError::none, result}
               : CompanionSemanticDecodeResult<CompanionActionResult>{
                     error, {}};
}

CompanionSemanticCodecError validate_companion_semantic_fragment(
    const CompanionFragment& fragment) {
    if (fragment.fragment_index != 0 || fragment.fragment_count != 1 ||
        fragment.payload_bytes > fragment.payload.size()) {
        return CompanionSemanticCodecError::malformed;
    }
    const radio::ByteView payload{
        fragment.payload.data(), fragment.payload_bytes};
    switch (fragment.kind) {
        case CompanionFrameKind::snapshot_request:
            return decode_companion_snapshot_request(payload).error;
        case CompanionFrameKind::action_request:
            return decode_companion_action_request(payload).error;
        case CompanionFrameKind::snapshot:
            return decode_companion_status_snapshot(payload).error;
        case CompanionFrameKind::action_result:
            return decode_companion_action_result(payload).error;
        case CompanionFrameKind::event:
            return CompanionSemanticCodecError::unsupported_frame_kind;
    }
    return CompanionSemanticCodecError::unsupported_frame_kind;
}

}  // namespace opentrail::companion
