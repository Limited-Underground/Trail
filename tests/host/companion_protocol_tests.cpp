#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "opentrail/companion_protocol.hpp"

namespace {

using namespace opentrail::companion;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

CompanionProtocolInfo full_info() {
    return {
        CompanionDeviceRole::screenless_client,
        kCompanionKnownCapabilityMask,
        static_cast<std::uint16_t>(kCompanionMaxFragmentPayloadBytes),
        kCompanionMinimumAttMtu,
        static_cast<std::uint8_t>(kCompanionMaxFragmentCount),
        1,
    };
}

CompanionFragment request(std::uint32_t session,
                          std::uint32_t request_id,
                          CompanionFrameKind kind =
                              CompanionFrameKind::action_request) {
    CompanionFragment fragment{};
    fragment.kind = kind;
    fragment.session_nonce = session;
    fragment.exchange_id = request_id;
    fragment.payload_bytes = 1;
    fragment.payload[0] = 0x2A;
    return fragment;
}

CompanionSessionEvidence evidence(std::uint64_t binding = 7) {
    return {binding, true, true, true};
}

void test_protocol_info_has_one_canonical_vector() {
    const std::array<std::uint8_t, kCompanionProtocolInfoBytes> expected{
        0x4F, 0x54, 0x42, 0x30, 0x00, 0x00, 0x01, 0x0F,
        0x80, 0x00, 0x97, 0x00, 0x10, 0x01, 0x00, 0x00,
    };
    std::array<std::uint8_t, kCompanionProtocolInfoBytes> encoded{};
    const auto result = encode_companion_protocol_info(
        full_info(), {encoded.data(), encoded.size()});
    EXPECT(result.encoded());
    EXPECT(result.encoded_bytes == encoded.size());
    EXPECT(encoded == expected);

    const auto decoded = decode_companion_protocol_info(
        {encoded.data(), encoded.size()});
    EXPECT(decoded.decoded());
    EXPECT(decoded.info.role == CompanionDeviceRole::screenless_client);
    EXPECT(decoded.info.capabilities == kCompanionKnownCapabilityMask);
    EXPECT(decoded.info.max_fragment_payload_bytes == 128);
    EXPECT(decoded.info.minimum_att_mtu == 151);
    EXPECT(decoded.info.max_fragment_count == 16);
    EXPECT(decoded.info.max_active_controllers == 1);
}

void test_protocol_info_encode_is_bounded_and_atomic() {
    std::array<std::uint8_t, kCompanionProtocolInfoBytes> encoded{};
    encoded.fill(0xA5);
    const auto before = encoded;
    EXPECT(encode_companion_protocol_info(
               full_info(), {nullptr, encoded.size()}).error ==
           CompanionCodecError::invalid_argument);
    const auto small = encode_companion_protocol_info(
        full_info(), {encoded.data(), encoded.size() - 1});
    EXPECT(small.error == CompanionCodecError::output_too_small);
    EXPECT(small.encoded_bytes == kCompanionProtocolInfoBytes);
    EXPECT(encoded == before);

    auto invalid = full_info();
    invalid.max_active_controllers = 2;
    EXPECT(encode_companion_protocol_info(
               invalid, {encoded.data(), encoded.size()}).error ==
           CompanionCodecError::invalid_limit);
    EXPECT(encoded == before);
    invalid = full_info();
    invalid.capabilities = 0x80;
    EXPECT(encode_companion_protocol_info(
               invalid, {encoded.data(), encoded.size()}).error ==
           CompanionCodecError::unknown_capability);
    EXPECT(encoded == before);
}

void test_protocol_info_decode_fails_closed() {
    std::array<std::uint8_t, kCompanionProtocolInfoBytes> encoded{};
    EXPECT(encode_companion_protocol_info(
               full_info(), {encoded.data(), encoded.size()}).encoded());
    EXPECT(decode_companion_protocol_info(
               {nullptr, encoded.size()}).error ==
           CompanionCodecError::invalid_argument);
    EXPECT(decode_companion_protocol_info(
               {encoded.data(), encoded.size() - 1}).error ==
           CompanionCodecError::malformed);

    auto changed = encoded;
    changed[0] = 'X';
    EXPECT(decode_companion_protocol_info(
               {changed.data(), changed.size()}).error ==
           CompanionCodecError::malformed);
    changed = encoded;
    changed[5] = 1;
    EXPECT(decode_companion_protocol_info(
               {changed.data(), changed.size()}).error ==
           CompanionCodecError::unsupported_version);
    changed = encoded;
    changed[6] = 0xFF;
    EXPECT(decode_companion_protocol_info(
               {changed.data(), changed.size()}).error ==
           CompanionCodecError::unknown_role);
    changed = encoded;
    changed[14] = 1;
    EXPECT(decode_companion_protocol_info(
               {changed.data(), changed.size()}).error ==
           CompanionCodecError::reserved_bits_set);
}

void test_fragment_has_one_canonical_vector() {
    auto fragment = request(0x11223344U, 0xA1B2C3D4U);
    fragment.payload_bytes = 3;
    fragment.payload[0] = 0x10;
    fragment.payload[1] = 0x20;
    fragment.payload[2] = 0x30;
    const std::array<std::uint8_t, 23> expected{
        0x4F, 0x54, 0x43, 0x30, 0x00, 0x00, 0x02, 0x00,
        0x44, 0x33, 0x22, 0x11, 0xD4, 0xC3, 0xB2, 0xA1,
        0x00, 0x01, 0x03, 0x00, 0x10, 0x20, 0x30,
    };
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> encoded{};
    const auto result = encode_companion_fragment(
        fragment, {encoded.data(), encoded.size()});
    EXPECT(result.encoded());
    EXPECT(result.encoded_bytes == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        EXPECT(encoded[index] == expected[index]);
    }
    const auto decoded = decode_companion_fragment(
        {encoded.data(), result.encoded_bytes});
    EXPECT(decoded.decoded());
    EXPECT(decoded.fragment.kind == CompanionFrameKind::action_request);
    EXPECT(decoded.fragment.session_nonce == 0x11223344U);
    EXPECT(decoded.fragment.exchange_id == 0xA1B2C3D4U);
    EXPECT(decoded.fragment.payload_bytes == 3);
    EXPECT(decoded.fragment.payload[2] == 0x30);
}

void test_all_frame_kinds_and_fragment_boundaries_round_trip() {
    for (const auto kind : {
             CompanionFrameKind::snapshot_request,
             CompanionFrameKind::action_request,
             CompanionFrameKind::snapshot,
             CompanionFrameKind::action_result,
             CompanionFrameKind::event,
         }) {
        CompanionFragment fragment{};
        fragment.kind = kind;
        fragment.session_nonce = 9;
        fragment.exchange_id = 17;
        fragment.fragment_index = 15;
        fragment.fragment_count = 16;
        fragment.payload_bytes =
            static_cast<std::uint16_t>(kCompanionMaxFragmentPayloadBytes);
        fragment.payload.fill(static_cast<std::uint8_t>(kind));
        std::array<std::uint8_t, kCompanionMaxFragmentBytes> encoded{};
        const auto result = encode_companion_fragment(
            fragment, {encoded.data(), encoded.size()});
        EXPECT(result.encoded());
        EXPECT(result.encoded_bytes == encoded.size());
        const auto decoded = decode_companion_fragment(
            {encoded.data(), result.encoded_bytes});
        EXPECT(decoded.decoded());
        EXPECT(decoded.fragment.kind == kind);
        EXPECT(decoded.fragment.fragment_index == 15);
        EXPECT(decoded.fragment.fragment_count == 16);
        EXPECT(decoded.fragment.payload == fragment.payload);
    }
}

void test_fragment_encode_rejects_invalid_input_without_mutation() {
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> encoded{};
    encoded.fill(0xA5);
    const auto before = encoded;
    auto fragment = request(3, 4);
    EXPECT(encode_companion_fragment(
               fragment, {nullptr, encoded.size()}).error ==
           CompanionCodecError::invalid_argument);
    const auto small = encode_companion_fragment(
        fragment,
        {encoded.data(), kCompanionFragmentHeaderBytes});
    EXPECT(small.error == CompanionCodecError::output_too_small);
    EXPECT(small.encoded_bytes == kCompanionFragmentHeaderBytes + 1);
    EXPECT(encoded == before);

    fragment.session_nonce = 0;
    EXPECT(encode_companion_fragment(
               fragment, {encoded.data(), encoded.size()}).error ==
           CompanionCodecError::invalid_session_nonce);
    EXPECT(encoded == before);
    fragment = request(3, 4);
    fragment.fragment_count = 0;
    EXPECT(encode_companion_fragment(
               fragment, {encoded.data(), encoded.size()}).error ==
           CompanionCodecError::invalid_fragment);
    EXPECT(encoded == before);
}

void test_fragment_decode_rejects_noncanonical_input() {
    const auto fragment = request(3, 4);
    std::array<std::uint8_t, kCompanionMaxFragmentBytes> encoded{};
    const auto encoded_result = encode_companion_fragment(
        fragment, {encoded.data(), encoded.size()});
    EXPECT(encoded_result.encoded());
    const auto size = encoded_result.encoded_bytes;
    EXPECT(decode_companion_fragment({nullptr, size}).error ==
           CompanionCodecError::invalid_argument);
    EXPECT(decode_companion_fragment({encoded.data(), 19}).error ==
           CompanionCodecError::malformed);

    auto changed = encoded;
    changed[4] = 1;
    EXPECT(decode_companion_fragment({changed.data(), size}).error ==
           CompanionCodecError::unsupported_version);
    changed = encoded;
    changed[6] = 0x7F;
    EXPECT(decode_companion_fragment({changed.data(), size}).error ==
           CompanionCodecError::unknown_frame_kind);
    changed = encoded;
    changed[7] = 1;
    EXPECT(decode_companion_fragment({changed.data(), size}).error ==
           CompanionCodecError::reserved_bits_set);
    changed = encoded;
    changed[18] = 2;
    EXPECT(decode_companion_fragment({changed.data(), size}).error ==
           CompanionCodecError::malformed);
}

void test_session_open_requires_all_adapter_security_evidence() {
    CompanionSessionGuard guard;
    auto denied = evidence();
    denied.controller_binding = 0;
    EXPECT(guard.open_session(denied, 10).error ==
           CompanionSessionError::invalid_controller_binding);
    denied = evidence();
    denied.link_encrypted = false;
    EXPECT(guard.open_session(denied, 10).error ==
           CompanionSessionError::link_not_encrypted);
    denied = evidence();
    denied.authenticated_bond = false;
    EXPECT(guard.open_session(denied, 10).error ==
           CompanionSessionError::bond_not_authenticated);
    denied = evidence();
    denied.application_authorized = false;
    EXPECT(guard.open_session(denied, 10).error ==
           CompanionSessionError::controller_not_authorized);
    EXPECT(!guard.status().active);
    EXPECT(guard.open_session(evidence(), 0).error ==
           CompanionSessionError::session_nonce_invalid);
    EXPECT(guard.open_session(evidence(), 10).opened());
}

void test_session_allows_exactly_one_controller() {
    CompanionSessionGuard guard;
    EXPECT(guard.open_session(evidence(7), 10).opened());
    EXPECT(guard.open_session(evidence(7), 11).error ==
           CompanionSessionError::session_in_use);
    EXPECT(guard.open_session(evidence(8), 11).error ==
           CompanionSessionError::session_in_use);
    EXPECT(guard.close_session(8) == CompanionSessionError::wrong_controller);
    EXPECT(guard.status().active);
    EXPECT(guard.close_session(7) == CompanionSessionError::none);
    EXPECT(!guard.status().active);
}

void test_request_admission_binds_security_controller_and_session() {
    CompanionSessionGuard guard;
    EXPECT(guard.admit_request(evidence(), request(10, 1)).error ==
           CompanionSessionError::no_active_session);
    EXPECT(guard.open_session(evidence(7), 10).opened());

    auto denied = evidence(7);
    denied.application_authorized = false;
    EXPECT(guard.admit_request(denied, request(10, 1)).error ==
           CompanionSessionError::controller_not_authorized);
    EXPECT(guard.admit_request(evidence(8), request(10, 1)).error ==
           CompanionSessionError::wrong_controller);
    EXPECT(guard.admit_request(evidence(7), request(11, 1)).error ==
           CompanionSessionError::wrong_session);
    EXPECT(guard.status().last_request_id == 0);
}

void test_request_admission_rejects_server_and_fragmented_frames() {
    CompanionSessionGuard guard;
    EXPECT(guard.open_session(evidence(), 10).opened());
    EXPECT(guard.admit_request(
               evidence(),
               request(10, 1, CompanionFrameKind::snapshot)).error ==
           CompanionSessionError::wrong_direction);
    auto fragmented = request(10, 1);
    fragmented.fragment_count = 2;
    EXPECT(guard.admit_request(evidence(), fragmented).error ==
           CompanionSessionError::fragmented_request);
    auto zero = request(10, 0);
    EXPECT(guard.admit_request(evidence(), zero).error ==
           CompanionSessionError::request_id_invalid);
    EXPECT(guard.status().last_request_id == 0);
}

void test_request_ids_are_monotonic_and_duplicates_do_not_reapply() {
    CompanionSessionGuard guard;
    EXPECT(guard.open_session(evidence(), 10).opened());
    const auto first = guard.admit_request(evidence(), request(10, 40));
    EXPECT(first.accepted());
    const auto duplicate = guard.admit_request(evidence(), request(10, 40));
    EXPECT(duplicate.duplicate());
    EXPECT(duplicate.error == CompanionSessionError::none);
    EXPECT(guard.admit_request(evidence(), request(10, 39)).error ==
           CompanionSessionError::stale_request);
    EXPECT(guard.admit_request(evidence(), request(10, 41)).accepted());
    const auto status = guard.status();
    EXPECT(status.last_request_id == 41);
    EXPECT(status.accepted_requests == 2);
    EXPECT(status.duplicate_requests == 1);
    EXPECT(status.rejected_requests == 1);
}

void test_reopen_resets_requests_but_rejects_nonce_reuse() {
    CompanionSessionGuard guard;
    EXPECT(guard.open_session(evidence(), 10).opened());
    EXPECT(guard.admit_request(evidence(), request(10, 99)).accepted());
    EXPECT(guard.close_session(7) == CompanionSessionError::none);
    EXPECT(guard.open_session(evidence(), 10).error ==
           CompanionSessionError::session_nonce_reused);
    EXPECT(guard.open_session(evidence(), 11).opened());
    EXPECT(guard.status().last_request_id == 0);
    EXPECT(guard.admit_request(evidence(), request(11, 1)).accepted());
    EXPECT(guard.close_session(7) == CompanionSessionError::none);
    EXPECT(guard.open_session(evidence(), 10).error ==
           CompanionSessionError::session_nonce_reused);
}

void test_session_nonce_and_request_id_exhaustion_fail_closed() {
    CompanionSessionGuard guard;
    EXPECT(guard.open_session(evidence(), UINT32_MAX).opened());
    EXPECT(guard.admit_request(
               evidence(), request(UINT32_MAX, UINT32_MAX)).accepted());
    EXPECT(guard.admit_request(
               evidence(), request(UINT32_MAX, UINT32_MAX)).duplicate());
    EXPECT(guard.admit_request(
               evidence(), request(UINT32_MAX, 1)).error ==
           CompanionSessionError::request_id_exhausted);
    EXPECT(guard.status().last_request_id == UINT32_MAX);
    EXPECT(guard.close_session(7) == CompanionSessionError::none);
    EXPECT(guard.open_session(evidence(), 1).error ==
           CompanionSessionError::session_nonce_exhausted);
    EXPECT(!guard.status().active);
}

void test_public_status_contains_no_controller_identity() {
    static_assert(std::is_trivially_copyable_v<CompanionProtocolInfo>);
    static_assert(std::is_trivially_copyable_v<CompanionFragment>);
    static_assert(sizeof(CompanionSessionStatus) <= 24);
    CompanionSessionGuard guard;
    EXPECT(guard.open_session(evidence(0xAABBCCDDEEFF0011ULL), 5).opened());
    const auto status = guard.status();
    EXPECT(status.active);
    EXPECT(status.session_nonce == 5);
    EXPECT(status.last_request_id == 0);
}

}  // namespace

int main() {
    test_protocol_info_has_one_canonical_vector();
    test_protocol_info_encode_is_bounded_and_atomic();
    test_protocol_info_decode_fails_closed();
    test_fragment_has_one_canonical_vector();
    test_all_frame_kinds_and_fragment_boundaries_round_trip();
    test_fragment_encode_rejects_invalid_input_without_mutation();
    test_fragment_decode_rejects_noncanonical_input();
    test_session_open_requires_all_adapter_security_evidence();
    test_session_allows_exactly_one_controller();
    test_request_admission_binds_security_controller_and_session();
    test_request_admission_rejects_server_and_fragmented_frames();
    test_request_ids_are_monotonic_and_duplicates_do_not_reapply();
    test_reopen_resets_requests_but_rejects_nonce_reuse();
    test_session_nonce_and_request_id_exhaustion_fail_closed();
    test_public_status_contains_no_controller_identity();

    if (failures != 0) {
        std::cerr << failures
                  << " companion protocol assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 15 companion protocol and session scenario groups\n";
    return EXIT_SUCCESS;
}
