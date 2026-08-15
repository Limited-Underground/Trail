#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "opentrail/companion_protocol.hpp"
#include "opentrail/companion_semantics.hpp"

namespace {

using namespace opentrail::companion;
using opentrail::protocol::QuickStatusKind;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

CompanionStatusSnapshot snapshot() {
    return {
        0x11223344U,
        CompanionRadioState::ready,
        CompanionGnssState::searching,
        CompanionPowerState::low,
        CompanionPositionSharingState::waiting_for_fix,
        0x5566U,
        0x0102030405060708ULL,
    };
}

CompanionActionRequest quick(QuickStatusKind kind) {
    return {CompanionActionKind::quick_status, kind, 0};
}

CompanionActionRequest alert(std::uint64_t alert_id) {
    return {CompanionActionKind::acknowledge_critical_alert,
            QuickStatusKind::ok,
            alert_id};
}

CompanionActionRequest position(CompanionActionKind kind) {
    return {kind, QuickStatusKind::ok, 0};
}

void test_semantic_records_fit_one_v0_command_fragment() {
    static_assert(kCompanionSnapshotRequestBytes <=
                  kCompanionMaxFragmentPayloadBytes);
    static_assert(kCompanionStatusSnapshotBytes <=
                  kCompanionMaxFragmentPayloadBytes);
    static_assert(kCompanionActionRequestBytes <=
                  kCompanionMaxFragmentPayloadBytes);
    static_assert(kCompanionActionResultBytes <=
                  kCompanionMaxFragmentPayloadBytes);
    static_assert(std::is_trivially_copyable_v<CompanionStatusSnapshot>);
    static_assert(std::is_trivially_copyable_v<CompanionActionRequest>);
    static_assert(std::is_trivially_copyable_v<CompanionActionResult>);
    EXPECT(kCompanionSemanticMajor == 0);
    EXPECT(kCompanionSemanticMinor == 0);
}

void test_snapshot_request_has_one_exact_negotiated_vector() {
    const std::array<std::uint8_t, kCompanionSnapshotRequestBytes> expected{
        0x4F, 0x54, 0x58, 0x30, 0x00, 0x00, 0x00, 0x00,
    };
    std::array<std::uint8_t, kCompanionSnapshotRequestBytes> encoded{};
    const auto result = encode_companion_snapshot_request(
        {}, {encoded.data(), encoded.size()});
    EXPECT(result.encoded());
    EXPECT(result.encoded_bytes == encoded.size());
    EXPECT(encoded == expected);
    EXPECT(decode_companion_snapshot_request(
               {encoded.data(), encoded.size()}).decoded());

    auto changed = encoded;
    changed[5] = 1;
    EXPECT(decode_companion_snapshot_request(
               {changed.data(), changed.size()}).error ==
           CompanionSemanticCodecError::unsupported_version);
    changed = encoded;
    changed[7] = 1;
    EXPECT(decode_companion_snapshot_request(
               {changed.data(), changed.size()}).error ==
           CompanionSemanticCodecError::reserved_bits_set);
}

void test_status_snapshot_has_one_exact_vector() {
    const std::array<std::uint8_t, kCompanionStatusSnapshotBytes> expected{
        0x4F, 0x54, 0x4E, 0x30, 0x00, 0x00, 0x02, 0x02,
        0x03, 0x01, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    std::array<std::uint8_t, kCompanionStatusSnapshotBytes> encoded{};
    const auto result = encode_companion_status_snapshot(
        snapshot(), {encoded.data(), encoded.size()});
    EXPECT(result.encoded());
    EXPECT(result.encoded_bytes == encoded.size());
    EXPECT(encoded == expected);
    const auto decoded = decode_companion_status_snapshot(
        {encoded.data(), encoded.size()});
    EXPECT(decoded.decoded());
    EXPECT(decoded.value.revision == 0x11223344U);
    EXPECT(decoded.value.radio == CompanionRadioState::ready);
    EXPECT(decoded.value.gnss == CompanionGnssState::searching);
    EXPECT(decoded.value.power == CompanionPowerState::low);
    EXPECT(decoded.value.position_sharing ==
           CompanionPositionSharingState::waiting_for_fix);
    EXPECT(decoded.value.queued_action_count == 0x5566U);
    EXPECT(decoded.value.pending_critical_alert_id ==
           0x0102030405060708ULL);
}

void test_all_status_enums_round_trip_without_presentation_data() {
    std::array<std::uint8_t, kCompanionStatusSnapshotBytes> encoded{};
    auto value = snapshot();
    value.pending_critical_alert_id = 0;
    value.queued_action_count = 0;
    for (const auto radio : {
             CompanionRadioState::unknown,
             CompanionRadioState::unavailable,
             CompanionRadioState::ready,
             CompanionRadioState::degraded,
             CompanionRadioState::fault,
         }) {
        value.radio = radio;
        EXPECT(encode_companion_status_snapshot(
                   value, {encoded.data(), encoded.size()}).encoded());
        EXPECT(decode_companion_status_snapshot(
                   {encoded.data(), encoded.size()}).value.radio == radio);
    }
    for (const auto gnss : {
             CompanionGnssState::unknown,
             CompanionGnssState::unavailable,
             CompanionGnssState::searching,
             CompanionGnssState::current,
             CompanionGnssState::stale,
             CompanionGnssState::fault,
         }) {
        value.gnss = gnss;
        EXPECT(encode_companion_status_snapshot(
                   value, {encoded.data(), encoded.size()}).encoded());
        EXPECT(decode_companion_status_snapshot(
                   {encoded.data(), encoded.size()}).value.gnss == gnss);
    }
    for (const auto power : {
             CompanionPowerState::unknown,
             CompanionPowerState::external,
             CompanionPowerState::normal,
             CompanionPowerState::low,
             CompanionPowerState::critical,
             CompanionPowerState::fault,
         }) {
        value.power = power;
        EXPECT(encode_companion_status_snapshot(
                   value, {encoded.data(), encoded.size()}).encoded());
        EXPECT(decode_companion_status_snapshot(
                   {encoded.data(), encoded.size()}).value.power == power);
    }
    for (const auto sharing : {
             CompanionPositionSharingState::stopped,
             CompanionPositionSharingState::waiting_for_fix,
             CompanionPositionSharingState::active,
             CompanionPositionSharingState::deferred,
             CompanionPositionSharingState::fault,
         }) {
        value.position_sharing = sharing;
        EXPECT(encode_companion_status_snapshot(
                   value, {encoded.data(), encoded.size()}).encoded());
        EXPECT(decode_companion_status_snapshot(
                   {encoded.data(), encoded.size()}).value.position_sharing ==
               sharing);
    }
}

void test_status_snapshot_rejects_invalid_and_noncanonical_input_atomically() {
    std::array<std::uint8_t, kCompanionStatusSnapshotBytes> encoded{};
    encoded.fill(0xA5);
    const auto before = encoded;
    auto value = snapshot();
    value.revision = 0;
    EXPECT(encode_companion_status_snapshot(
               value, {encoded.data(), encoded.size()}).error ==
           CompanionSemanticCodecError::invalid_revision);
    EXPECT(encoded == before);
    value = snapshot();
    value.radio = static_cast<CompanionRadioState>(0xFF);
    EXPECT(encode_companion_status_snapshot(
               value, {encoded.data(), encoded.size()}).error ==
           CompanionSemanticCodecError::unknown_enum);
    EXPECT(encoded == before);
    EXPECT(encode_companion_status_snapshot(
               snapshot(), {encoded.data(), encoded.size() - 1}).error ==
           CompanionSemanticCodecError::output_too_small);
    EXPECT(encoded == before);

    EXPECT(encode_companion_status_snapshot(
               snapshot(), {encoded.data(), encoded.size()}).encoded());
    auto changed = encoded;
    changed[12] = 0;
    changed[13] = 0;
    changed[14] = 0;
    changed[15] = 0;
    EXPECT(decode_companion_status_snapshot(
               {changed.data(), changed.size()}).error ==
           CompanionSemanticCodecError::invalid_revision);
    changed = encoded;
    changed[24] = 1;
    EXPECT(decode_companion_status_snapshot(
               {changed.data(), changed.size()}).error ==
           CompanionSemanticCodecError::reserved_bits_set);
}

void test_four_canonical_quick_status_ids_round_trip() {
    std::array<std::uint8_t, kCompanionActionRequestBytes> encoded{};
    std::uint8_t expected_id = 1;
    for (const auto kind : {
             QuickStatusKind::ok,
             QuickStatusKind::need_assistance,
             QuickStatusKind::anyone_online,
             QuickStatusKind::available_to_help,
         }) {
        const auto result = encode_companion_action_request(
            quick(kind), {encoded.data(), encoded.size()});
        EXPECT(result.encoded());
        EXPECT(encoded[6] == 1);
        EXPECT(encoded[7] == expected_id);
        const auto decoded = decode_companion_action_request(
            {encoded.data(), encoded.size()});
        EXPECT(decoded.decoded());
        EXPECT(decoded.value.kind == CompanionActionKind::quick_status);
        EXPECT(decoded.value.quick_status == kind);
        EXPECT(decoded.value.critical_alert_id == 0);
        ++expected_id;
    }
}

void test_critical_acknowledgement_preserves_exact_device_alert_id() {
    const std::array<std::uint8_t, kCompanionActionRequestBytes> expected{
        0x4F, 0x54, 0x41, 0x30, 0x00, 0x00, 0x02, 0x00,
        0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
        0x00, 0x00, 0x00, 0x00,
    };
    std::array<std::uint8_t, kCompanionActionRequestBytes> encoded{};
    EXPECT(encode_companion_action_request(
               alert(0x1122334455667788ULL),
               {encoded.data(), encoded.size()}).encoded());
    EXPECT(encoded == expected);
    const auto decoded = decode_companion_action_request(
        {encoded.data(), encoded.size()});
    EXPECT(decoded.decoded());
    EXPECT(decoded.value.kind ==
           CompanionActionKind::acknowledge_critical_alert);
    EXPECT(decoded.value.critical_alert_id == 0x1122334455667788ULL);
    EXPECT(encode_companion_action_request(
               alert(0), {encoded.data(), encoded.size()}).error ==
           CompanionSemanticCodecError::invalid_alert_id);
}

void test_position_start_and_stop_are_explicit_distinct_intents() {
    std::array<std::uint8_t, kCompanionActionRequestBytes> encoded{};
    for (const auto kind : {
             CompanionActionKind::start_position_sharing,
             CompanionActionKind::stop_position_sharing,
         }) {
        EXPECT(encode_companion_action_request(
                   position(kind), {encoded.data(), encoded.size()}).encoded());
        EXPECT(encoded[6] == static_cast<std::uint8_t>(kind));
        EXPECT(encoded[7] == 0);
        const auto decoded = decode_companion_action_request(
            {encoded.data(), encoded.size()});
        EXPECT(decoded.decoded());
        EXPECT(decoded.value.kind == kind);
        EXPECT(decoded.value.critical_alert_id == 0);
    }
}

void test_action_requests_reject_ambiguous_or_unknown_values() {
    std::array<std::uint8_t, kCompanionActionRequestBytes> encoded{};
    encoded.fill(0xA5);
    const auto before = encoded;
    auto invalid = quick(static_cast<QuickStatusKind>(5));
    EXPECT(encode_companion_action_request(
               invalid, {encoded.data(), encoded.size()}).error ==
           CompanionSemanticCodecError::invalid_quick_status);
    EXPECT(encoded == before);
    invalid = position(CompanionActionKind::start_position_sharing);
    invalid.critical_alert_id = 1;
    EXPECT(encode_companion_action_request(
               invalid, {encoded.data(), encoded.size()}).error ==
           CompanionSemanticCodecError::incoherent_action);
    EXPECT(encoded == before);

    EXPECT(encode_companion_action_request(
               quick(QuickStatusKind::ok),
               {encoded.data(), encoded.size()}).encoded());
    auto changed = encoded;
    changed[6] = 0xFF;
    EXPECT(decode_companion_action_request(
               {changed.data(), changed.size()}).error ==
           CompanionSemanticCodecError::unknown_enum);
    changed = encoded;
    changed[16] = 1;
    EXPECT(decode_companion_action_request(
               {changed.data(), changed.size()}).error ==
           CompanionSemanticCodecError::reserved_bits_set);
}

void test_outbound_action_result_means_queued_not_delivered() {
    const CompanionActionResult queued{
        CompanionActionKind::quick_status,
        QuickStatusKind::need_assistance,
        0,
        CompanionActionDisposition::queued,
        CompanionActionRejectReason::none,
    };
    const std::array<std::uint8_t, kCompanionActionResultBytes> expected{
        0x4F, 0x54, 0x52, 0x30, 0x00, 0x00, 0x02, 0x00,
        0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };
    std::array<std::uint8_t, kCompanionActionResultBytes> encoded{};
    EXPECT(encode_companion_action_result(
               queued, {encoded.data(), encoded.size()}).encoded());
    EXPECT(encoded == expected);
    const auto decoded = decode_companion_action_result(
        {encoded.data(), encoded.size()});
    EXPECT(decoded.decoded());
    EXPECT(decoded.value.disposition == CompanionActionDisposition::queued);
    EXPECT(decoded.value.reject_reason == CompanionActionRejectReason::none);
}

void test_local_and_rejected_results_are_typed_and_coherent() {
    std::array<std::uint8_t, kCompanionActionResultBytes> encoded{};
    for (const auto kind : {
             CompanionActionKind::start_position_sharing,
             CompanionActionKind::stop_position_sharing,
         }) {
        const CompanionActionResult admitted{
            kind,
            QuickStatusKind::ok,
            0,
            CompanionActionDisposition::admitted,
            CompanionActionRejectReason::none,
        };
        EXPECT(encode_companion_action_result(
                   admitted, {encoded.data(), encoded.size()}).encoded());
        EXPECT(decode_companion_action_result(
                   {encoded.data(), encoded.size()}).value.kind == kind);
    }
    const CompanionActionResult rejected{
        CompanionActionKind::acknowledge_critical_alert,
        QuickStatusKind::ok,
        0x8877665544332211ULL,
        CompanionActionDisposition::rejected,
        CompanionActionRejectReason::stale_alert,
    };
    EXPECT(encode_companion_action_result(
               rejected, {encoded.data(), encoded.size()}).encoded());
    const auto decoded = decode_companion_action_result(
        {encoded.data(), encoded.size()});
    EXPECT(decoded.decoded());
    EXPECT(decoded.value.disposition == CompanionActionDisposition::rejected);
    EXPECT(decoded.value.reject_reason ==
           CompanionActionRejectReason::stale_alert);
    EXPECT(decoded.value.critical_alert_id == 0x8877665544332211ULL);
}

void test_result_codec_rejects_delivery_like_or_incoherent_states() {
    std::array<std::uint8_t, kCompanionActionResultBytes> encoded{};
    encoded.fill(0xA5);
    const auto before = encoded;
    auto result = CompanionActionResult{
        CompanionActionKind::quick_status,
        QuickStatusKind::ok,
        0,
        CompanionActionDisposition::admitted,
        CompanionActionRejectReason::none,
    };
    EXPECT(encode_companion_action_result(
               result, {encoded.data(), encoded.size()}).error ==
           CompanionSemanticCodecError::incoherent_result);
    EXPECT(encoded == before);
    result = CompanionActionResult{
        CompanionActionKind::start_position_sharing,
        QuickStatusKind::ok,
        0,
        CompanionActionDisposition::rejected,
        CompanionActionRejectReason::queue_full,
    };
    EXPECT(encode_companion_action_result(
               result, {encoded.data(), encoded.size()}).error ==
           CompanionSemanticCodecError::incoherent_result);
    EXPECT(encoded == before);
    result = CompanionActionResult{
        CompanionActionKind::quick_status,
        QuickStatusKind::ok,
        0,
        CompanionActionDisposition::rejected,
        CompanionActionRejectReason::stale_alert,
    };
    EXPECT(encode_companion_action_result(
               result, {encoded.data(), encoded.size()}).error ==
           CompanionSemanticCodecError::incoherent_result);
    EXPECT(encoded == before);
    result = CompanionActionResult{
        CompanionActionKind::quick_status,
        QuickStatusKind::ok,
        0,
        CompanionActionDisposition::rejected,
        CompanionActionRejectReason::queue_full,
    };
    EXPECT(encode_companion_action_result(
               result, {encoded.data(), encoded.size()}).encoded());
    auto changed = encoded;
    changed[10] = 1;
    EXPECT(decode_companion_action_result(
               {changed.data(), changed.size()}).error ==
           CompanionSemanticCodecError::reserved_bits_set);
}

void test_payloads_bind_to_expected_otc0_frame_kinds() {
    CompanionFragment fragment{};
    fragment.kind = CompanionFrameKind::action_request;
    fragment.session_nonce = 7;
    fragment.exchange_id = 9;
    const auto semantic = encode_companion_action_request(
        quick(QuickStatusKind::available_to_help),
        {fragment.payload.data(), fragment.payload.size()});
    EXPECT(semantic.encoded());
    fragment.payload_bytes = static_cast<std::uint16_t>(semantic.encoded_bytes);
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> encoded{};
    const auto envelope = encode_companion_fragment(
        fragment, {encoded.data(), encoded.size()});
    EXPECT(envelope.encoded());
    const auto decoded_envelope = decode_companion_fragment(
        {encoded.data(), envelope.encoded_bytes});
    EXPECT(decoded_envelope.decoded());
    EXPECT(decoded_envelope.fragment.kind == CompanionFrameKind::action_request);
    const auto decoded_action = decode_companion_action_request(
        {decoded_envelope.fragment.payload.data(),
         decoded_envelope.fragment.payload_bytes});
    EXPECT(decoded_action.decoded());
    EXPECT(decoded_action.value.quick_status ==
           QuickStatusKind::available_to_help);
    EXPECT(validate_companion_semantic_fragment(decoded_envelope.fragment) ==
           CompanionSemanticCodecError::none);

    auto wrong_kind = decoded_envelope.fragment;
    wrong_kind.kind = CompanionFrameKind::snapshot_request;
    EXPECT(validate_companion_semantic_fragment(wrong_kind) ==
           CompanionSemanticCodecError::malformed);
    wrong_kind.kind = CompanionFrameKind::snapshot;
    EXPECT(validate_companion_semantic_fragment(wrong_kind) ==
           CompanionSemanticCodecError::malformed);
    wrong_kind.kind = CompanionFrameKind::action_result;
    EXPECT(validate_companion_semantic_fragment(wrong_kind) ==
           CompanionSemanticCodecError::malformed);
    wrong_kind.kind = CompanionFrameKind::event;
    EXPECT(validate_companion_semantic_fragment(wrong_kind) ==
           CompanionSemanticCodecError::unsupported_frame_kind);

    CompanionFragment snapshot_fragment{};
    snapshot_fragment.kind = CompanionFrameKind::snapshot;
    snapshot_fragment.session_nonce = 7;
    snapshot_fragment.exchange_id = 10;
    const auto snapshot_payload = encode_companion_status_snapshot(
        snapshot(),
        {snapshot_fragment.payload.data(), snapshot_fragment.payload.size()});
    snapshot_fragment.payload_bytes =
        static_cast<std::uint16_t>(snapshot_payload.encoded_bytes);
    EXPECT(validate_companion_semantic_fragment(snapshot_fragment) ==
           CompanionSemanticCodecError::none);

    CompanionFragment request_fragment{};
    request_fragment.kind = CompanionFrameKind::snapshot_request;
    request_fragment.session_nonce = 7;
    request_fragment.exchange_id = 11;
    const auto request_payload = encode_companion_snapshot_request(
        {}, {request_fragment.payload.data(), request_fragment.payload.size()});
    request_fragment.payload_bytes =
        static_cast<std::uint16_t>(request_payload.encoded_bytes);
    EXPECT(validate_companion_semantic_fragment(request_fragment) ==
           CompanionSemanticCodecError::none);

    CompanionFragment result_fragment{};
    result_fragment.kind = CompanionFrameKind::action_result;
    result_fragment.session_nonce = 7;
    result_fragment.exchange_id = 9;
    const CompanionActionResult queued{
        CompanionActionKind::quick_status,
        QuickStatusKind::available_to_help,
        0,
        CompanionActionDisposition::queued,
        CompanionActionRejectReason::none,
    };
    const auto result_payload = encode_companion_action_result(
        queued,
        {result_fragment.payload.data(), result_fragment.payload.size()});
    result_fragment.payload_bytes =
        static_cast<std::uint16_t>(result_payload.encoded_bytes);
    EXPECT(validate_companion_semantic_fragment(result_fragment) ==
           CompanionSemanticCodecError::none);

    result_fragment.fragment_count = 2;
    EXPECT(validate_companion_semantic_fragment(result_fragment) ==
           CompanionSemanticCodecError::malformed);
}

}  // namespace

int main() {
    test_semantic_records_fit_one_v0_command_fragment();
    test_snapshot_request_has_one_exact_negotiated_vector();
    test_status_snapshot_has_one_exact_vector();
    test_all_status_enums_round_trip_without_presentation_data();
    test_status_snapshot_rejects_invalid_and_noncanonical_input_atomically();
    test_four_canonical_quick_status_ids_round_trip();
    test_critical_acknowledgement_preserves_exact_device_alert_id();
    test_position_start_and_stop_are_explicit_distinct_intents();
    test_action_requests_reject_ambiguous_or_unknown_values();
    test_outbound_action_result_means_queued_not_delivered();
    test_local_and_rejected_results_are_typed_and_coherent();
    test_result_codec_rejects_delivery_like_or_incoherent_states();
    test_payloads_bind_to_expected_otc0_frame_kinds();

    if (failures != 0) {
        std::cerr << failures
                  << " companion semantic codec assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 13 companion semantic payload scenario groups\n";
    return EXIT_SUCCESS;
}
