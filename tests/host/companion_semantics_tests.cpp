#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "opentrail/companion_protocol.hpp"
#include "opentrail/companion_reset_receipt.hpp"
#include "opentrail/companion_semantics.hpp"
#include "opentrail/pairing_result_owner.hpp"

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

CompanionActionRequest factory_reset() {
    return {CompanionActionKind::factory_reset, QuickStatusKind::ok,
            UINT64_C(0x1122334455667788)};
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

void test_factory_reset_has_exact_receipt_vectors() {
    const std::array<std::uint8_t, kCompanionActionRequestBytes>
        expected_request{
            0x4F, 0x54, 0x41, 0x30, 0x00, 0x00, 0x05, 0x00,
            0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
            0x00, 0x00, 0x00, 0x00,
        };
    std::array<std::uint8_t, kCompanionActionRequestBytes> request_bytes{};
    EXPECT(encode_companion_action_request(
               factory_reset(),
               {request_bytes.data(), request_bytes.size()}).encoded());
    EXPECT(request_bytes == expected_request);
    const auto decoded_request = decode_companion_action_request(
        {request_bytes.data(), request_bytes.size()});
    EXPECT(decoded_request.decoded());
    EXPECT(decoded_request.value.kind == CompanionActionKind::factory_reset);
    EXPECT(decoded_request.value.critical_alert_id ==
           UINT64_C(0x1122334455667788));

    const CompanionActionResult admitted{
        CompanionActionKind::factory_reset,
        QuickStatusKind::ok,
        UINT64_C(0x1122334455667788),
        CompanionActionDisposition::admitted,
        CompanionActionRejectReason::none,
    };
    const std::array<std::uint8_t, kCompanionActionResultBytes>
        expected_result{
            0x4F, 0x54, 0x52, 0x30, 0x00, 0x00, 0x01, 0x00,
            0x05, 0x00, 0x00, 0x00, 0x88, 0x77, 0x66, 0x55,
            0x44, 0x33, 0x22, 0x11,
        };
    std::array<std::uint8_t, kCompanionActionResultBytes> result_bytes{};
    EXPECT(encode_companion_action_result(
               admitted,
               {result_bytes.data(), result_bytes.size()}).encoded());
    EXPECT(result_bytes == expected_result);
    const auto decoded_result = decode_companion_action_result(
        {result_bytes.data(), result_bytes.size()});
    EXPECT(decoded_result.decoded());
    EXPECT(decoded_result.value.kind == CompanionActionKind::factory_reset);
    EXPECT(decoded_result.value.disposition ==
           CompanionActionDisposition::admitted);
    EXPECT(decoded_result.value.critical_alert_id ==
           UINT64_C(0x1122334455667788));

    const auto service_data = encode_companion_reset_receipt_service_data(
        UINT64_C(0x1122334455667788));
    const std::array<std::uint8_t,
                     kCompanionResetReceiptServiceDataBytes>
        expected_service_data{
            0xD1, 0xB7, 0x43, 0x1F, 0x4F, 0x0C, 0x10, 0xA2,
            0xA3, 0x4E, 0x6B, 0x7C, 0x00, 0x2A, 0x0F, 0x5E,
            'O', 'T', 'R', 'R', 0x01,
            0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
        };
    EXPECT(service_data.valid);
    EXPECT(service_data.bytes == expected_service_data);
    EXPECT(!encode_companion_reset_receipt_service_data(0).valid);

    auto changed = request_bytes;
    changed[7] = 1;
    EXPECT(decode_companion_action_request(
               {changed.data(), changed.size()}).error ==
           CompanionSemanticCodecError::reserved_bits_set);
    changed = request_bytes;
    for (std::size_t index = 8; index < 16; ++index) changed[index] = 0;
    EXPECT(decode_companion_action_request(
               {changed.data(), changed.size()}).error ==
           CompanionSemanticCodecError::incoherent_action);
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

void test_enrollment_start_is_versioned_pending_request_only() {
    const CompanionActionRequest request{
        CompanionActionKind::start_enrollment, QuickStatusKind::ok, 0,
        CompanionEnrollmentRole::initiator, 123456};
    std::array<std::uint8_t, kCompanionActionRequestBytes> bytes{};
    EXPECT(encode_companion_action_request(
        request, {bytes.data(), bytes.size()}).encoded());
    EXPECT(bytes[4] == kCompanionSemanticMajor);
    EXPECT(bytes[5] == kCompanionSemanticMinor);
    EXPECT(bytes[6] == 6);
    // D-085 layout: role at byte 16, code little-endian at bytes 17-19.
    EXPECT(bytes[16] == 1);
    EXPECT(bytes[17] == 0x40 && bytes[18] == 0xE2 && bytes[19] == 0x01);
    const auto decoded = decode_companion_action_request(
        {bytes.data(), bytes.size()});
    EXPECT(decoded.decoded());
    EXPECT(decoded.value.kind == CompanionActionKind::start_enrollment);
    EXPECT(decoded.value.enrollment_role == CompanionEnrollmentRole::initiator);
    EXPECT(decoded.value.pairing_code == 123456);
    auto altered = bytes;
    altered[7] = 1;
    EXPECT(decode_companion_action_request(
        {altered.data(), altered.size()}).error ==
           CompanionSemanticCodecError::reserved_bits_set);
    altered = bytes;
    altered[8] = 1;
    EXPECT(decode_companion_action_request(
        {altered.data(), altered.size()}).error ==
           CompanionSemanticCodecError::incoherent_action);
    const CompanionActionResult pending{
        CompanionActionKind::start_enrollment, QuickStatusKind::ok, 0,
        CompanionActionDisposition::admitted, CompanionActionRejectReason::none};
    std::array<std::uint8_t, kCompanionActionResultBytes> response{};
    EXPECT(encode_companion_action_result(
        pending, {response.data(), response.size()}).encoded());
    EXPECT(decode_companion_action_result(
        {response.data(), response.size()}).decoded());
}

void test_enrollment_start_busy_and_rejection_results() {
    // R2a review: the board answers a refused pairing request with a correlated
    // rejected result. busy (7) is valid only for start_enrollment.
    const CompanionActionResult busy{
        CompanionActionKind::start_enrollment, QuickStatusKind::ok, 0,
        CompanionActionDisposition::rejected, CompanionActionRejectReason::busy};
    std::array<std::uint8_t, kCompanionActionResultBytes> bytes{};
    EXPECT(encode_companion_action_result(busy, {bytes.data(), bytes.size()}).encoded());
    // Expected bytes for the app: OTR0, 0, 0, rejected(3), busy(7), kind 6.
    const std::array<std::uint8_t, kCompanionActionResultBytes> expected{
        0x4F, 0x54, 0x52, 0x30, 0x00, 0x00, 0x03, 0x07,
        0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };
    EXPECT(bytes == expected);
    const auto decoded = decode_companion_action_result({bytes.data(), bytes.size()});
    EXPECT(decoded.decoded());
    EXPECT(decoded.value.reject_reason == CompanionActionRejectReason::busy);
    EXPECT(decoded.value.disposition == CompanionActionDisposition::rejected);

    for (const auto reason : {CompanionActionRejectReason::policy_denied,
                              CompanionActionRejectReason::unavailable}) {
        const CompanionActionResult rejected{
            CompanionActionKind::start_enrollment, QuickStatusKind::ok, 0,
            CompanionActionDisposition::rejected, reason};
        EXPECT(encode_companion_action_result(rejected, {bytes.data(), bytes.size()}).encoded());
    }
    // busy is incoherent for other kinds.
    const CompanionActionResult wrong{
        CompanionActionKind::start_position_sharing, QuickStatusKind::ok, 0,
        CompanionActionDisposition::rejected, CompanionActionRejectReason::busy};
    EXPECT(encode_companion_action_result(wrong, {bytes.data(), bytes.size()}).error ==
           CompanionSemanticCodecError::incoherent_result);
    // An unknown reason above busy is refused on decode.
    auto altered = expected;
    altered[7] = 8;
    EXPECT(decode_companion_action_result({altered.data(), altered.size()}).error ==
           CompanionSemanticCodecError::unknown_enum);
}

void test_enrollment_start_requires_owner_role_and_code() {
    // D-085: role and code come only from the owner's phone and are bounded.
    auto encode = [](CompanionEnrollmentRole role, std::uint32_t code) {
        const CompanionActionRequest request{
            CompanionActionKind::start_enrollment, QuickStatusKind::ok, 0,
            role, code};
        std::array<std::uint8_t, kCompanionActionRequestBytes> bytes{};
        return encode_companion_action_request(
            request, {bytes.data(), bytes.size()}).error;
    };
    EXPECT(encode(CompanionEnrollmentRole::initiator, 1) ==
           CompanionSemanticCodecError::none);
    EXPECT(encode(CompanionEnrollmentRole::responder, 999999) ==
           CompanionSemanticCodecError::none);
    EXPECT(encode(CompanionEnrollmentRole::none, 123456) ==
           CompanionSemanticCodecError::incoherent_action);
    EXPECT(encode(static_cast<CompanionEnrollmentRole>(3), 123456) ==
           CompanionSemanticCodecError::incoherent_action);
    EXPECT(encode(CompanionEnrollmentRole::responder, 0) ==
           CompanionSemanticCodecError::incoherent_action);
    EXPECT(encode(CompanionEnrollmentRole::responder, 1000000) ==
           CompanionSemanticCodecError::incoherent_action);

    const CompanionActionRequest good{
        CompanionActionKind::start_enrollment, QuickStatusKind::ok, 0,
        CompanionEnrollmentRole::responder, 654321};
    std::array<std::uint8_t, kCompanionActionRequestBytes> bytes{};
    EXPECT(encode_companion_action_request(
        good, {bytes.data(), bytes.size()}).encoded());
    auto altered = bytes;
    altered[16] = 0;  // role missing
    EXPECT(decode_companion_action_request(
        {altered.data(), altered.size()}).error ==
           CompanionSemanticCodecError::incoherent_action);
    altered = bytes;
    altered[16] = 7;  // unknown role
    EXPECT(decode_companion_action_request(
        {altered.data(), altered.size()}).error ==
           CompanionSemanticCodecError::incoherent_action);
    altered = bytes;
    altered[17] = altered[18] = altered[19] = 0;  // code missing
    EXPECT(decode_companion_action_request(
        {altered.data(), altered.size()}).error ==
           CompanionSemanticCodecError::incoherent_action);
    altered = bytes;
    altered[17] = altered[18] = altered[19] = 0xFF;  // above 999999
    EXPECT(decode_companion_action_request(
        {altered.data(), altered.size()}).error ==
           CompanionSemanticCodecError::incoherent_action);

    // Other kinds may not carry pairing fields.
    const CompanionActionRequest stray{
        CompanionActionKind::start_position_sharing, QuickStatusKind::ok, 0,
        CompanionEnrollmentRole::initiator, 5};
    std::array<std::uint8_t, kCompanionActionRequestBytes> unused{};
    EXPECT(encode_companion_action_request(
        stray, {unused.data(), unused.size()}).error ==
           CompanionSemanticCodecError::incoherent_action);
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


void test_pairing_snapshot_is_explicit_and_separate_from_v0() {
    std::array<std::uint8_t, kCompanionPairingSnapshotRequestBytes> request{};
    EXPECT(encode_companion_pairing_snapshot_request(
        {}, {request.data(), request.size()}).encoded());
    const std::array<std::uint8_t, 8> expected{'O','T','X','1',0,0,0,0};
    EXPECT(request == expected);
    EXPECT(decode_companion_pairing_snapshot_request(
        {request.data(), request.size()}).decoded());
    EXPECT(!decode_companion_snapshot_request({request.data(), request.size()}).decoded());
    request[7] = 1;
    EXPECT(!decode_companion_pairing_snapshot_request({request.data(), request.size()}).decoded());

    CompanionPairingStatusSnapshot value{snapshot(),
        {CompanionPairingState::paired_both, CompanionPairingReason::none,
         0x0A3BU, 0x11223344U, 0x55667788U, 0x99AABBCCU}};
    std::array<std::uint8_t, kCompanionPairingStatusSnapshotBytes> bytes{};
    EXPECT(encode_companion_pairing_status_snapshot(
        value, {bytes.data(), bytes.size()}).encoded());
    EXPECT(bytes.size() == 40 && bytes[3] == '1');
    const std::array<std::uint8_t, 16> tail{
        3,0,0x3B,0x0A,0x44,0x33,0x22,0x11,
        0x88,0x77,0x66,0x55,0xCC,0xBB,0xAA,0x99};
    for (std::size_t i = 0; i < tail.size(); ++i) EXPECT(bytes[24+i] == tail[i]);
    const auto read = decode_companion_pairing_status_snapshot({bytes.data(), bytes.size()});
    EXPECT(read.decoded());
    EXPECT(read.value.snapshot.revision == value.snapshot.revision);
    EXPECT(read.value.snapshot.pending_critical_alert_id == value.snapshot.pending_critical_alert_id);
    EXPECT(read.value.pairing.state == value.pairing.state);
    EXPECT(read.value.pairing.peer_short_id == 0x0A3BU);
    EXPECT(read.value.pairing.action_exchange_id == 0x11223344U);
    EXPECT(read.value.pairing.action_session_nonce == 0x55667788U);
    EXPECT(read.value.pairing.revision == 0x99AABBCCU);
    EXPECT(!decode_companion_status_snapshot({bytes.data(), bytes.size()}).decoded());
    CompanionFragment legacy{};
    legacy.kind = CompanionFrameKind::snapshot;
    legacy.payload_bytes = static_cast<std::uint16_t>(bytes.size());
    std::copy(bytes.begin(), bytes.end(), legacy.payload.begin());
    EXPECT(validate_companion_semantic_fragment(legacy) != CompanionSemanticCodecError::none);
    for (std::size_t n : {std::size_t{0}, std::size_t{31}, std::size_t{39}}) {
        EXPECT(!decode_companion_pairing_status_snapshot({bytes.data(), n}).decoded());
    }
    auto malformed = bytes;
    malformed[5] = 1;
    EXPECT(!decode_companion_pairing_status_snapshot({malformed.data(), malformed.size()}).decoded());
    malformed = bytes;
    malformed[6] = 255;
    EXPECT(!decode_companion_pairing_status_snapshot({malformed.data(), malformed.size()}).decoded());
}

void test_pairing_result_has_only_canonical_state_reason_combinations() {
    using S = CompanionPairingState;
    using R = CompanionPairingReason;
    CompanionPairingStatusSnapshot value{snapshot(), {}};
    std::array<std::uint8_t, kCompanionPairingStatusSnapshotBytes> bytes{};
    EXPECT(companion_pairing_result_valid(value.pairing));
    EXPECT(encode_companion_pairing_status_snapshot(value,{bytes.data(),bytes.size()}).encoded());
    EXPECT(decode_companion_pairing_status_snapshot({bytes.data(),bytes.size()}).decoded());
    value.pairing.revision=42; // An expired result clears with a new projection revision.
    EXPECT(companion_pairing_result_valid(value.pairing));
    EXPECT(encode_companion_pairing_status_snapshot(value,{bytes.data(),bytes.size()}).encoded());
    EXPECT(decode_companion_pairing_status_snapshot({bytes.data(),bytes.size()}).value.pairing.revision==42);
    auto check = [&](S state, R reason, std::uint16_t peer, bool valid) {
        value.pairing = {state, reason, peer, 17, 29, 1};
        EXPECT(companion_pairing_result_valid(value.pairing) == valid);
        bytes.fill(0xA5);
        const auto prior = bytes;
        const auto result = encode_companion_pairing_status_snapshot(value,{bytes.data(),bytes.size()});
        EXPECT(result.encoded() == valid);
        if (valid) {
            EXPECT(decode_companion_pairing_status_snapshot({bytes.data(),bytes.size()}).decoded());
        } else {
            EXPECT(bytes == prior);
        }
    };
    check(S::running, R::none, 0, true);
    check(S::running, R::timed_out, 0, false);
    check(S::paired_local, R::none, 0, true);
    check(S::paired_both, R::none, 65535, true);
    check(S::paired_both, R::storage, 42, false);
    for (unsigned reason=1; reason<=7; ++reason) {
        check(S::failed, static_cast<R>(reason), 0, true);
    }
    check(S::failed, R::none, 0, false);
    check(S::failed, R::no_reply, 1, false);
    check(S::cancelled, R::request_ended, 0, true);
    check(S::cancelled, R::none, 0, false);
    for (auto reason : {R::none, R::storage, R::request_ended}) {
        check(S::incomplete, reason, 0, true);
    }
    check(S::incomplete, R::timed_out, 0, false);
    check(S::none, R::none, 0, false); // none cannot carry an old action.
    check(static_cast<S>(255), R::none, 0, false);
    check(S::failed, static_cast<R>(255), 0, false);
    value.pairing = {S::running, R::none, 0, 17, 29, 1};
    for (unsigned missing=0; missing<3; ++missing) {
        auto bad = value;
        if (missing==0) bad.pairing.action_exchange_id=0;
        if (missing==1) bad.pairing.action_session_nonce=0;
        if (missing==2) bad.pairing.revision=0;
        EXPECT(!companion_pairing_result_valid(bad.pairing));
    }
    value.pairing = {S::paired_local, R::none, 0, 17, 29, 1};
    EXPECT(encode_companion_pairing_status_snapshot(value,{bytes.data(),bytes.size()}).encoded());
    const auto before = bytes;
    EXPECT(!encode_companion_pairing_status_snapshot(value,{bytes.data(),bytes.size()-1}).encoded());
    EXPECT(bytes == before);
    for (unsigned field : {24,25,28,32,36}) {
        auto bad = before;
        if (field < 26) bad[field]=255;
        else std::fill_n(bad.begin()+field,4,0);
        EXPECT(!decode_companion_pairing_status_snapshot({bad.data(),bad.size()}).decoded());
    }
}


void test_pairing_result_survives_request_consumption_and_same_owner_reconnect() {
    using S=CompanionPairingState;
    using R=CompanionPairingReason;
    DeviceNameAuthority current{DeviceNamePhase::connected,{1,2,3,4,5,6,7},100};
    SelectedEnrollmentRequestOwner requests(300000);
    EXPECT(requests.admit(current,8,9,10,CompanionEnrollmentRole::initiator,123456)==
        SelectedEnrollmentRequestResult::admitted);
    const auto ticket=requests.request();
    PairingResultOwner result;
    result.begin(ticket);
    const auto running=result.read(current);
    EXPECT(running.state==S::running && running.action_exchange_id==10 &&
        running.action_session_nonce==7 && running.revision!=0);
    EXPECT(companion_pairing_result_valid(running));
    auto new_link=current;
    ++new_link.context.transport_generation;++new_link.context.controller;
    ++new_link.context.session_nonce;
    EXPECT(result.read(new_link).state==S::none);
    EXPECT(result.claim(ticket));
    EXPECT(requests.cancel_exact(ticket));
    EXPECT(!requests.pending());
    EXPECT(result.publish(ticket,S::paired_local,R::none,0x003B,200));
    current.now_ms=200;
    const auto local=result.read(current);
    EXPECT(local.state==S::paired_local && local.peer_short_id==0x003B &&
        local.revision>running.revision);
    EXPECT(!result.publish(ticket,S::failed,R::storage,0,201));
    EXPECT(!result.publish(ticket,S::cancelled,R::request_ended,0,201));
    auto disconnected=current;disconnected.phase=DeviceNamePhase::disconnected;
    result.observe(disconnected);
    EXPECT(result.read(disconnected).state==S::none);
    EXPECT(result.read(current).state==S::paired_local);
    auto returning=current;
    ++returning.context.transport_generation;++returning.context.controller;
    ++returning.context.session_nonce;returning.now_ms=300;
    EXPECT(result.read(returning).state==S::paired_local);
    EXPECT(result.read(returning).action_session_nonce==ticket.authority.session_nonce);
    for (unsigned field=0;field<4;++field) {
        auto other=returning;
        if(field==0)++other.context.device;
        if(field==1)++other.context.runtime;
        if(field==2)++other.context.owner;
        if(field==3)++other.context.owner_generation;
        EXPECT(result.read(other).state==S::none);
    }
    EXPECT(result.publish(ticket,S::paired_both,R::none,0x003B,400));
    returning.now_ms=400;
    const auto both=result.read(returning);
    EXPECT(both.state==S::paired_both && both.revision>local.revision);
    EXPECT(!result.publish(ticket,S::failed,R::no_reply,0,401));
    returning.now_ms=400+PairingResultOwner::kRetentionMs-1;
    EXPECT(result.read(returning).state==S::paired_both);
    returning.now_ms++;
    const auto expired=result.read(returning);
    EXPECT(expired.state==S::none && expired.revision>both.revision);
    EXPECT(companion_pairing_result_valid(expired));
    EXPECT(result.read(returning).revision==expired.revision);
}

void test_pairing_result_exact_ticket_replacement_and_reset() {
    using S=CompanionPairingState;
    using R=CompanionPairingReason;
    DeviceNameAuthority current{DeviceNamePhase::connected,{1,2,3,4,5,6,7},100};
    SelectedEnrollmentRequestOwner requests(300000);
    EXPECT(requests.admit(current,8,9,10,CompanionEnrollmentRole::responder,123456)==
        SelectedEnrollmentRequestResult::admitted);
    const auto first=requests.request();
    PairingResultOwner result;result.begin(first);
    for(unsigned field=0;field<7;++field) {
        auto stale=first;
        if(field==0)++stale.connection_handle;
        if(field==1)++stale.delivery_token;
        if(field==2)++stale.exchange_id;
        if(field==3)++stale.authority.session_nonce;
        if(field==4)++stale.authority.transport_generation;
        if(field==5)++stale.admitted_ms;
        if(field==6)++stale.deadline_ms;
        EXPECT(!result.claim(stale));
        EXPECT(!result.publish(stale,S::failed,R::storage,0,200));
    }
    EXPECT(result.claim(first));
    EXPECT(result.publish(first,S::failed,R::storage,0,200));
    EXPECT(requests.cancel_exact(first));
    current.now_ms=250;
    EXPECT(requests.admit(current,8,10,11,CompanionEnrollmentRole::responder,654321)==
        SelectedEnrollmentRequestResult::admitted);
    const auto second=requests.request();
    result.begin(second);
    EXPECT(result.read(current).state==S::running &&
        result.read(current).action_exchange_id==second.exchange_id);
    EXPECT(!result.publish(first,S::paired_both,R::none,0x1234,300));
    EXPECT(!result.claim(first));
    result.retire();
    EXPECT(!result.claim(second));
    EXPECT(!result.publish(second,S::paired_local,R::none,0x003B,300));
    result.begin(second);
    EXPECT(result.read(current).state==S::none);
}

void test_unclaimed_pairing_request_gets_exact_expiry_or_cancellation() {
    using S=CompanionPairingState;
    using R=CompanionPairingReason;
    DeviceNameAuthority current{DeviceNamePhase::connected,{1,2,3,4,5,6,7},100};
    SelectedEnrollmentRequestOwner requests(300000);
    EXPECT(requests.admit(current,8,9,10,CompanionEnrollmentRole::initiator,123456)==
        SelectedEnrollmentRequestResult::admitted);
    const auto ticket=requests.request();
    PairingResultOwner expired;expired.begin(ticket);
    current.now_ms=ticket.deadline_ms-1;
    expired.observe(current);
    EXPECT(expired.read(current).state==S::running);
    current.now_ms=ticket.deadline_ms;
    expired.observe(current);
    const auto failed=expired.read(current);
    EXPECT(failed.state==S::failed && failed.reason==R::timed_out);
    EXPECT(companion_pairing_result_valid(failed));

    PairingResultOwner cancelled;cancelled.begin(ticket);
    current.now_ms=200;current.phase=DeviceNamePhase::disconnected;
    cancelled.observe(current);
    current.phase=DeviceNamePhase::connected;
    ++current.context.transport_generation;++current.context.session_nonce;
    const auto stopped=cancelled.read(current);
    EXPECT(stopped.state==S::cancelled && stopped.reason==R::request_ended);
    EXPECT(stopped.action_session_nonce==ticket.authority.session_nonce);
    EXPECT(companion_pairing_result_valid(stopped));
}

}  // namespace

int main() {
    test_pairing_result_survives_request_consumption_and_same_owner_reconnect();
    test_pairing_result_exact_ticket_replacement_and_reset();
    test_unclaimed_pairing_request_gets_exact_expiry_or_cancellation();
    test_pairing_snapshot_is_explicit_and_separate_from_v0();
    test_pairing_result_has_only_canonical_state_reason_combinations();
    test_semantic_records_fit_one_v0_command_fragment();
    test_snapshot_request_has_one_exact_negotiated_vector();
    test_status_snapshot_has_one_exact_vector();
    test_all_status_enums_round_trip_without_presentation_data();
    test_status_snapshot_rejects_invalid_and_noncanonical_input_atomically();
    test_four_canonical_quick_status_ids_round_trip();
    test_critical_acknowledgement_preserves_exact_device_alert_id();
    test_position_start_and_stop_are_explicit_distinct_intents();
    test_factory_reset_has_exact_receipt_vectors();
    test_action_requests_reject_ambiguous_or_unknown_values();
    test_enrollment_start_is_versioned_pending_request_only();
    test_enrollment_start_requires_owner_role_and_code();
    test_enrollment_start_busy_and_rejection_results();
    test_outbound_action_result_means_queued_not_delivered();
    test_local_and_rejected_results_are_typed_and_coherent();
    test_result_codec_rejects_delivery_like_or_incoherent_states();
    test_payloads_bind_to_expected_otc0_frame_kinds();

    if (failures != 0) {
        std::cerr << failures
                  << " companion semantic codec assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 22 companion semantic payload scenario groups\n";
    return EXIT_SUCCESS;
}
