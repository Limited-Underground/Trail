#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

#include "opentrail/companion_authorization_wire.hpp"

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

constexpr std::uint32_t kSession = 0x11223344U;
constexpr std::uint32_t kExchange = 0x55667788U;
constexpr std::uint64_t kGeneration = 7;

CompanionAuthorizationCorrelation correlation(std::uint8_t first = 0xA0) {
    CompanionAuthorizationCorrelation value{};
    for (std::size_t index = 0; index < value.bytes.size(); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(first + index);
    }
    return value;
}

template <std::size_t N>
CompanionFragment fragment(CompanionFrameKind kind,
                           std::uint32_t session,
                           std::uint32_t exchange,
                           const std::array<std::uint8_t, N>& payload) {
    CompanionFragment value{};
    value.kind = kind;
    value.session_nonce = session;
    value.exchange_id = exchange;
    value.fragment_count = 1;
    value.payload_bytes = static_cast<std::uint16_t>(payload.size());
    for (std::size_t index = 0; index < payload.size(); ++index) {
        value.payload[index] = payload[index];
    }
    return value;
}

std::array<std::uint8_t, kCompanionAuthorizationClaimStartBytes> start_bytes(
    CompanionAuthorizationPurpose purpose) {
    std::array<std::uint8_t, kCompanionAuthorizationClaimStartBytes> bytes{};
    EXPECT(encode_companion_authorization_claim_start(
               {purpose}, {bytes.data(), bytes.size()}).encoded());
    return bytes;
}

std::array<std::uint8_t, kCompanionAuthorizationClaimStatusBytes> status_bytes(
    CompanionAuthorizationPurpose purpose,
    CompanionAuthorizationCorrelation token = correlation()) {
    std::array<std::uint8_t, kCompanionAuthorizationClaimStatusBytes> bytes{};
    EXPECT(encode_companion_authorization_claim_status(
               {purpose, CompanionAuthorizationClaimState::pending, token},
               {bytes.data(), bytes.size()}).encoded());
    return bytes;
}

std::array<std::uint8_t, kCompanionAuthorizationClaimResultBytes> result_bytes(
    CompanionAuthorizationPurpose purpose,
    CompanionAuthorizationClaimOutcome outcome,
    CompanionAuthorizationDenyReason reason,
    CompanionAuthorizationCorrelation token = correlation()) {
    std::array<std::uint8_t, kCompanionAuthorizationClaimResultBytes> bytes{};
    EXPECT(encode_companion_authorization_claim_result(
               {purpose, outcome, reason, token},
               {bytes.data(), bytes.size()}).encoded());
    return bytes;
}

CompanionFragment start_fragment(
    CompanionAuthorizationPurpose purpose =
        CompanionAuthorizationPurpose::authorize_controller,
    std::uint32_t session = kSession,
    std::uint32_t exchange = kExchange) {
    return fragment(CompanionFrameKind::authorization_claim_start, session,
                    exchange, start_bytes(purpose));
}

CompanionFragment pending_fragment(
    CompanionAuthorizationPurpose purpose =
        CompanionAuthorizationPurpose::authorize_controller,
    CompanionAuthorizationCorrelation token = correlation(),
    std::uint32_t session = kSession,
    std::uint32_t exchange = kExchange) {
    return fragment(CompanionFrameKind::authorization_claim_status, session,
                    exchange, status_bytes(purpose, token));
}

CompanionFragment terminal_fragment(
    CompanionAuthorizationPurpose purpose,
    CompanionAuthorizationClaimOutcome outcome,
    CompanionAuthorizationDenyReason reason,
    CompanionAuthorizationCorrelation token = correlation(),
    std::uint32_t session = kSession,
    std::uint32_t exchange = kExchange) {
    return fragment(CompanionFrameKind::authorization_claim_result, session,
                    exchange, result_bytes(purpose, outcome, reason, token));
}

CompanionAuthorizationProvisionalEvidence evidence(
    std::uint64_t generation = kGeneration,
    bool encrypted = true,
    bool authenticated = true,
    bool claim_wire_supported = true) {
    return {generation, encrypted, authenticated, claim_wire_supported};
}

void open(CompanionAuthorizationResponseTracker& tracker,
          std::uint64_t generation = kGeneration,
          std::uint32_t session = kSession) {
    EXPECT(tracker.open_provisional_session(
               evidence(generation), session) ==
           CompanionAuthorizationWireError::none);
}

void begin(CompanionAuthorizationResponseTracker& tracker,
           CompanionAuthorizationPurpose purpose =
               CompanionAuthorizationPurpose::authorize_controller,
           std::uint64_t generation = kGeneration,
           std::uint32_t session = kSession,
           std::uint32_t exchange = kExchange) {
    EXPECT(tracker.begin(
               generation, start_fragment(purpose, session, exchange)) ==
           CompanionAuthorizationWireError::none);
}

void test_fixed_sizes_and_exact_start_vector() {
    static_assert(kCompanionAuthorizationClaimStartBytes == 8);
    static_assert(kCompanionAuthorizationClaimStatusBytes == 24);
    static_assert(kCompanionAuthorizationClaimResultBytes == 28);
    static_assert(kCompanionAuthorizationCorrelationBytes == 16);
    static_assert(kCompanionAuthorizationClaimResultBytes <=
                  kCompanionMaxFragmentPayloadBytes);
    static_assert(std::is_trivially_copyable_v<
                  CompanionAuthorizationCorrelation>);
    static_assert(std::is_trivially_copyable_v<
                  CompanionAuthorizationClaimStart>);
    static_assert(std::is_trivially_copyable_v<
                  CompanionAuthorizationClaimStatus>);
    static_assert(std::is_trivially_copyable_v<
                  CompanionAuthorizationClaimResult>);

    const std::array<std::uint8_t, 8> expected{
        0x4F, 0x54, 0x4C, 0x30, 0x00, 0x00, 0x01, 0x00};
    const auto encoded = start_bytes(
        CompanionAuthorizationPurpose::authorize_controller);
    EXPECT(encoded == expected);
    const auto decoded = decode_companion_authorization_claim_start(
        {encoded.data(), encoded.size()});
    EXPECT(decoded.decoded());
    EXPECT(decoded.value.purpose ==
           CompanionAuthorizationPurpose::authorize_controller);
}

void test_exact_pending_vector() {
    const std::array<std::uint8_t, 24> expected{
        0x4F, 0x54, 0x50, 0x30, 0x00, 0x00, 0x02, 0x01,
        0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
        0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF};
    const auto encoded = status_bytes(
        CompanionAuthorizationPurpose::replace_controller);
    EXPECT(encoded == expected);
    const auto decoded = decode_companion_authorization_claim_status(
        {encoded.data(), encoded.size()});
    EXPECT(decoded.decoded());
    EXPECT(decoded.value.purpose ==
           CompanionAuthorizationPurpose::replace_controller);
    EXPECT(decoded.value.state == CompanionAuthorizationClaimState::pending);
    EXPECT(decoded.value.correlation == correlation());
}

void test_exact_terminal_vectors_and_coherence() {
    const std::array<std::uint8_t, 28> expected{
        0x4F, 0x54, 0x46, 0x30, 0x00, 0x00, 0x02, 0x03,
        0x00, 0x00, 0x00, 0x00,
        0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
        0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF};
    const auto encoded = result_bytes(
        CompanionAuthorizationPurpose::replace_controller,
        CompanionAuthorizationClaimOutcome::replaced,
        CompanionAuthorizationDenyReason::none);
    EXPECT(encoded == expected);
    const auto decoded = decode_companion_authorization_claim_result(
        {encoded.data(), encoded.size()});
    EXPECT(decoded.decoded());
    EXPECT(decoded.value.outcome ==
           CompanionAuthorizationClaimOutcome::replaced);

    std::array<std::uint8_t, 28> output{};
    EXPECT(encode_companion_authorization_claim_result(
               {CompanionAuthorizationPurpose::replace_controller,
                CompanionAuthorizationClaimOutcome::accepted,
                CompanionAuthorizationDenyReason::none, correlation()},
               {output.data(), output.size()}).error ==
           CompanionAuthorizationWireError::incoherent_result);
    EXPECT(encode_companion_authorization_claim_result(
               {CompanionAuthorizationPurpose::authorize_controller,
                CompanionAuthorizationClaimOutcome::replaced,
                CompanionAuthorizationDenyReason::none, correlation()},
               {output.data(), output.size()}).error ==
           CompanionAuthorizationWireError::incoherent_result);
    EXPECT(encode_companion_authorization_claim_result(
               {CompanionAuthorizationPurpose::authorize_controller,
                CompanionAuthorizationClaimOutcome::accepted,
                CompanionAuthorizationDenyReason::policy_denied,
                correlation()},
               {output.data(), output.size()}).error ==
           CompanionAuthorizationWireError::incoherent_result);
    EXPECT(encode_companion_authorization_claim_result(
               {CompanionAuthorizationPurpose::authorize_controller,
                CompanionAuthorizationClaimOutcome::denied,
                CompanionAuthorizationDenyReason::none, correlation()},
               {output.data(), output.size()}).error ==
           CompanionAuthorizationWireError::incoherent_result);
}

void test_all_authoritative_denial_reasons_round_trip() {
    for (std::uint8_t raw = 1; raw <= 8; ++raw) {
        const auto reason =
            static_cast<CompanionAuthorizationDenyReason>(raw);
        const auto encoded = result_bytes(
            CompanionAuthorizationPurpose::authorize_controller,
            CompanionAuthorizationClaimOutcome::denied, reason);
        const auto decoded = decode_companion_authorization_claim_result(
            {encoded.data(), encoded.size()});
        EXPECT(decoded.decoded());
        EXPECT(decoded.value.reason == reason);
    }
}

void test_malformed_unknown_and_reserved_values_fail_closed() {
    auto start = start_bytes(
        CompanionAuthorizationPurpose::authorize_controller);
    EXPECT(decode_companion_authorization_claim_start({nullptr, start.size()})
               .error == CompanionAuthorizationWireError::invalid_argument);
    EXPECT(decode_companion_authorization_claim_start(
               {start.data(), start.size() - 1}).error ==
           CompanionAuthorizationWireError::malformed);
    start[4] = 1;
    EXPECT(decode_companion_authorization_claim_start(
               {start.data(), start.size()}).error ==
           CompanionAuthorizationWireError::unsupported_version);
    start = start_bytes(CompanionAuthorizationPurpose::authorize_controller);
    start[6] = 0xFF;
    EXPECT(decode_companion_authorization_claim_start(
               {start.data(), start.size()}).error ==
           CompanionAuthorizationWireError::unknown_purpose);
    start[6] = 1;
    start[7] = 1;
    EXPECT(decode_companion_authorization_claim_start(
               {start.data(), start.size()}).error ==
           CompanionAuthorizationWireError::reserved_bits_set);

    auto pending = status_bytes(
        CompanionAuthorizationPurpose::authorize_controller);
    pending[7] = 0xFF;
    EXPECT(decode_companion_authorization_claim_status(
               {pending.data(), pending.size()}).error ==
           CompanionAuthorizationWireError::unknown_state);
    pending = status_bytes(
        CompanionAuthorizationPurpose::authorize_controller);
    for (std::size_t index = 8; index < pending.size(); ++index) {
        pending[index] = 0;
    }
    EXPECT(decode_companion_authorization_claim_status(
               {pending.data(), pending.size()}).error ==
           CompanionAuthorizationWireError::invalid_correlation);

    auto terminal = result_bytes(
        CompanionAuthorizationPurpose::authorize_controller,
        CompanionAuthorizationClaimOutcome::accepted,
        CompanionAuthorizationDenyReason::none);
    terminal[7] = 0xFF;
    EXPECT(decode_companion_authorization_claim_result(
               {terminal.data(), terminal.size()}).error ==
           CompanionAuthorizationWireError::unknown_outcome);
    terminal = result_bytes(
        CompanionAuthorizationPurpose::authorize_controller,
        CompanionAuthorizationClaimOutcome::denied,
        CompanionAuthorizationDenyReason::unknown);
    terminal[8] = 0xFF;
    EXPECT(decode_companion_authorization_claim_result(
               {terminal.data(), terminal.size()}).error ==
           CompanionAuthorizationWireError::unknown_reason);
    terminal[8] = 1;
    terminal[10] = 1;
    EXPECT(decode_companion_authorization_claim_result(
               {terminal.data(), terminal.size()}).error ==
           CompanionAuthorizationWireError::reserved_bits_set);
}

void test_encode_failures_are_atomic() {
    std::array<std::uint8_t, 28> output{};
    output.fill(0xCC);
    CompanionAuthorizationCorrelation zero{};
    const auto invalid = encode_companion_authorization_claim_result(
        {CompanionAuthorizationPurpose::authorize_controller,
         CompanionAuthorizationClaimOutcome::accepted,
         CompanionAuthorizationDenyReason::none, zero},
        {output.data(), output.size()});
    EXPECT(invalid.error ==
           CompanionAuthorizationWireError::invalid_correlation);
    for (const auto byte : output) {
        EXPECT(byte == 0xCC);
    }
    const auto small = encode_companion_authorization_claim_result(
        {CompanionAuthorizationPurpose::authorize_controller,
         CompanionAuthorizationClaimOutcome::accepted,
         CompanionAuthorizationDenyReason::none, correlation()},
        {output.data(), output.size() - 1});
    EXPECT(small.error == CompanionAuthorizationWireError::output_too_small);
    EXPECT(small.encoded_bytes == output.size());
    for (const auto byte : output) {
        EXPECT(byte == 0xCC);
    }
}

void test_strict_otc0_kind_binding_and_exact_envelope_vector() {
    const auto start = start_fragment();
    EXPECT(validate_companion_authorization_fragment(start) ==
           CompanionAuthorizationWireError::none);
    EXPECT(validate_companion_authorization_fragment(pending_fragment()) ==
           CompanionAuthorizationWireError::none);
    EXPECT(validate_companion_authorization_fragment(terminal_fragment(
               CompanionAuthorizationPurpose::authorize_controller,
               CompanionAuthorizationClaimOutcome::accepted,
               CompanionAuthorizationDenyReason::none)) ==
           CompanionAuthorizationWireError::none);

    auto wrong = start;
    wrong.kind = CompanionFrameKind::authorization_claim_status;
    EXPECT(validate_companion_authorization_fragment(wrong) ==
           CompanionAuthorizationWireError::malformed);
    wrong = start;
    wrong.kind = CompanionFrameKind::action_request;
    EXPECT(validate_companion_authorization_fragment(wrong) ==
           CompanionAuthorizationWireError::unsupported_frame_kind);
    wrong = start;
    wrong.fragment_count = 2;
    EXPECT(validate_companion_authorization_fragment(wrong) ==
           CompanionAuthorizationWireError::malformed);

    std::array<std::uint8_t, kCompanionMaxFragmentBytes> encoded{};
    const auto envelope = encode_companion_fragment(
        start, {encoded.data(), encoded.size()});
    EXPECT(envelope.encoded());
    const std::array<std::uint8_t, 28> expected{
        0x4F, 0x54, 0x43, 0x30, 0x00, 0x00, 0x03, 0x00,
        0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55,
        0x00, 0x01, 0x08, 0x00,
        0x4F, 0x54, 0x4C, 0x30, 0x00, 0x00, 0x01, 0x00};
    EXPECT(envelope.encoded_bytes == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        EXPECT(encoded[index] == expected[index]);
    }
    const auto decoded = decode_companion_fragment(
        {encoded.data(), envelope.encoded_bytes});
    EXPECT(decoded.decoded());
    EXPECT(decoded.fragment.kind ==
           CompanionFrameKind::authorization_claim_start);
    EXPECT(validate_companion_authorization_fragment(decoded.fragment) ==
           CompanionAuthorizationWireError::none);

    const auto pending = pending_fragment();
    const auto pending_envelope = encode_companion_fragment(
        pending, {encoded.data(), encoded.size()});
    const std::array<std::uint8_t, 44> expected_pending{
        0x4F, 0x54, 0x43, 0x30, 0x00, 0x00, 0x84, 0x00,
        0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55,
        0x00, 0x01, 0x18, 0x00,
        0x4F, 0x54, 0x50, 0x30, 0x00, 0x00, 0x01, 0x01,
        0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
        0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF};
    EXPECT(pending_envelope.encoded());
    EXPECT(pending_envelope.encoded_bytes == expected_pending.size());
    for (std::size_t index = 0; index < expected_pending.size(); ++index) {
        EXPECT(encoded[index] == expected_pending[index]);
    }

    const auto accepted = terminal_fragment(
        CompanionAuthorizationPurpose::authorize_controller,
        CompanionAuthorizationClaimOutcome::accepted,
        CompanionAuthorizationDenyReason::none);
    const auto accepted_envelope = encode_companion_fragment(
        accepted, {encoded.data(), encoded.size()});
    const std::array<std::uint8_t, 48> expected_accepted{
        0x4F, 0x54, 0x43, 0x30, 0x00, 0x00, 0x85, 0x00,
        0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55,
        0x00, 0x01, 0x1C, 0x00,
        0x4F, 0x54, 0x46, 0x30, 0x00, 0x00, 0x01, 0x01,
        0x00, 0x00, 0x00, 0x00,
        0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
        0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF};
    EXPECT(accepted_envelope.encoded());
    EXPECT(accepted_envelope.encoded_bytes == expected_accepted.size());
    for (std::size_t index = 0; index < expected_accepted.size(); ++index) {
        EXPECT(encoded[index] == expected_accepted[index]);
    }
}

void test_provisional_security_and_zero_context_gate() {
    CompanionSessionGuard normal_guard;
    const CompanionSessionEvidence normal_evidence{7, true, true, true};
    EXPECT(normal_guard.open_session(normal_evidence, kSession).opened());
    EXPECT(normal_guard.admit_request(normal_evidence, start_fragment()).error ==
           CompanionSessionError::wrong_direction);

    CompanionAuthorizationResponseTracker tracker;
    EXPECT(tracker.begin(kGeneration, start_fragment()) ==
           CompanionAuthorizationWireError::no_provisional_session);
    EXPECT(tracker.open_provisional_session(
               evidence(0), kSession) ==
           CompanionAuthorizationWireError::invalid_transport_generation);
    EXPECT(tracker.open_provisional_session(
               evidence(kGeneration), 0) ==
           CompanionAuthorizationWireError::invalid_transport_generation);
    EXPECT(tracker.open_provisional_session(
               evidence(kGeneration, false, true), kSession) ==
           CompanionAuthorizationWireError::link_not_encrypted);
    EXPECT(tracker.open_provisional_session(
               evidence(kGeneration, true, false), kSession) ==
           CompanionAuthorizationWireError::bond_not_authenticated);
    EXPECT(tracker.open_provisional_session(
               evidence(kGeneration, true, true, false), kSession) ==
           CompanionAuthorizationWireError::
               claim_capability_not_negotiated);
    EXPECT(!tracker.status().provisional_session_open);
    open(tracker);
    EXPECT(!tracker.allows_normal_companion_traffic(kGeneration));
    EXPECT(tracker.begin(kGeneration, start_fragment(
               CompanionAuthorizationPurpose::authorize_controller, 0,
               kExchange)) == CompanionAuthorizationWireError::wrong_session);
    EXPECT(tracker.begin(kGeneration, start_fragment(
               CompanionAuthorizationPurpose::authorize_controller, kSession,
               0)) == CompanionAuthorizationWireError::wrong_exchange);
    EXPECT(tracker.status().phase ==
           CompanionAuthorizationResponsePhase::idle);
    EXPECT(tracker.begin(kGeneration, start_fragment()) ==
           CompanionAuthorizationWireError::none);
}

void test_authorize_pending_then_terminal_enables_normal_traffic() {
    CompanionAuthorizationResponseTracker tracker;
    open(tracker);
    begin(tracker);
    EXPECT(!tracker.allows_normal_companion_traffic(kGeneration));
    const auto pending = tracker.observe(kGeneration, pending_fragment());
    EXPECT(pending.accepted());
    EXPECT(pending.phase == CompanionAuthorizationResponsePhase::pending);
    EXPECT(!tracker.allows_normal_companion_traffic(kGeneration));
    const auto accepted = tracker.observe(
        kGeneration,
        terminal_fragment(
            CompanionAuthorizationPurpose::authorize_controller,
            CompanionAuthorizationClaimOutcome::accepted,
            CompanionAuthorizationDenyReason::none));
    EXPECT(accepted.terminal());
    EXPECT(accepted.outcome ==
           CompanionAuthorizationClaimOutcome::accepted);
    EXPECT(tracker.allows_normal_companion_traffic(kGeneration));
    const auto status = tracker.status();
    EXPECT(status.terminal_received);
    EXPECT(status.application_authorized);
    EXPECT(!status.provisional_session_open);
    EXPECT(tracker.cancel(kGeneration) ==
           CompanionAuthorizationWireError::duplicate_response);
    EXPECT(tracker.allows_normal_companion_traffic(kGeneration));
}

void test_replace_and_denial_are_authoritative_only_after_pending() {
    CompanionAuthorizationResponseTracker replacement;
    open(replacement);
    begin(replacement, CompanionAuthorizationPurpose::replace_controller);
    EXPECT(replacement.observe(
               kGeneration,
               terminal_fragment(
                   CompanionAuthorizationPurpose::replace_controller,
                   CompanionAuthorizationClaimOutcome::replaced,
                   CompanionAuthorizationDenyReason::none)).error ==
           CompanionAuthorizationWireError::response_out_of_order);
    EXPECT(replacement.observe(
               kGeneration,
               pending_fragment(
                   CompanionAuthorizationPurpose::replace_controller))
               .accepted());
    const auto replaced = replacement.observe(
        kGeneration,
        terminal_fragment(CompanionAuthorizationPurpose::replace_controller,
                          CompanionAuthorizationClaimOutcome::replaced,
                          CompanionAuthorizationDenyReason::none));
    EXPECT(replaced.terminal());
    EXPECT(replacement.allows_normal_companion_traffic(kGeneration));

    CompanionAuthorizationResponseTracker denied;
    open(denied, kGeneration + 1);
    begin(denied, CompanionAuthorizationPurpose::authorize_controller,
          kGeneration + 1);
    EXPECT(denied.observe(kGeneration + 1, pending_fragment()).accepted());
    const auto terminal = denied.observe(
        kGeneration + 1,
        terminal_fragment(
            CompanionAuthorizationPurpose::authorize_controller,
            CompanionAuthorizationClaimOutcome::denied,
            CompanionAuthorizationDenyReason::unsupported));
    EXPECT(terminal.terminal());
    EXPECT(terminal.reason ==
           CompanionAuthorizationDenyReason::unsupported);
    EXPECT(!denied.allows_normal_companion_traffic(kGeneration + 1));
    EXPECT(!denied.status().provisional_session_open);
    EXPECT(denied.open_provisional_session(
               evidence(kGeneration + 2), kSession + 1) ==
           CompanionAuthorizationWireError::claim_in_progress);
    EXPECT(denied.status().terminal_received);
    EXPECT(!denied.status().application_authorized);
}

void test_context_and_correlation_mismatch_never_satisfy_claim() {
    CompanionAuthorizationResponseTracker tracker;
    open(tracker);
    begin(tracker);
    EXPECT(tracker.observe(kGeneration, pending_fragment(
               CompanionAuthorizationPurpose::authorize_controller,
               correlation(), kSession + 1, kExchange)).error ==
           CompanionAuthorizationWireError::wrong_session);
    EXPECT(tracker.observe(kGeneration, pending_fragment(
               CompanionAuthorizationPurpose::authorize_controller,
               correlation(), kSession, kExchange + 1)).error ==
           CompanionAuthorizationWireError::wrong_exchange);
    EXPECT(tracker.observe(kGeneration, pending_fragment(
               CompanionAuthorizationPurpose::replace_controller)).error ==
           CompanionAuthorizationWireError::purpose_mismatch);
    EXPECT(tracker.observe(kGeneration, pending_fragment()).accepted());
    EXPECT(tracker.observe(kGeneration, terminal_fragment(
               CompanionAuthorizationPurpose::authorize_controller,
               CompanionAuthorizationClaimOutcome::accepted,
               CompanionAuthorizationDenyReason::none,
               correlation(0xB0))).error ==
           CompanionAuthorizationWireError::correlation_mismatch);
    EXPECT(!tracker.allows_normal_companion_traffic(kGeneration));
    EXPECT(tracker.observe(kGeneration, terminal_fragment(
               CompanionAuthorizationPurpose::authorize_controller,
               CompanionAuthorizationClaimOutcome::accepted,
               CompanionAuthorizationDenyReason::none)).terminal());
}

void test_duplicate_out_of_order_and_claim_in_progress_fail_closed() {
    CompanionAuthorizationResponseTracker tracker;
    open(tracker);
    begin(tracker);
    EXPECT(tracker.begin(kGeneration, start_fragment(
               CompanionAuthorizationPurpose::authorize_controller,
               kSession, kExchange + 1)) ==
           CompanionAuthorizationWireError::claim_in_progress);
    EXPECT(tracker.observe(kGeneration, terminal_fragment(
               CompanionAuthorizationPurpose::authorize_controller,
               CompanionAuthorizationClaimOutcome::accepted,
               CompanionAuthorizationDenyReason::none)).error ==
           CompanionAuthorizationWireError::response_out_of_order);
    EXPECT(tracker.observe(kGeneration, pending_fragment()).accepted());
    EXPECT(tracker.observe(kGeneration, pending_fragment()).error ==
           CompanionAuthorizationWireError::duplicate_response);
    EXPECT(tracker.observe(kGeneration, terminal_fragment(
               CompanionAuthorizationPurpose::authorize_controller,
               CompanionAuthorizationClaimOutcome::accepted,
               CompanionAuthorizationDenyReason::none)).terminal());
    EXPECT(tracker.observe(kGeneration, terminal_fragment(
               CompanionAuthorizationPurpose::authorize_controller,
               CompanionAuthorizationClaimOutcome::accepted,
               CompanionAuthorizationDenyReason::none)).error ==
           CompanionAuthorizationWireError::duplicate_response);
}

void test_cancel_disconnect_and_generation_replay_are_closed() {
    CompanionAuthorizationResponseTracker tracker;
    open(tracker);
    begin(tracker);
    EXPECT(tracker.cancel(kGeneration + 1) ==
           CompanionAuthorizationWireError::wrong_transport_generation);
    EXPECT(tracker.cancel(kGeneration) ==
           CompanionAuthorizationWireError::none);
    EXPECT(!tracker.status().provisional_session_open);
    EXPECT(tracker.open_provisional_session(
               evidence(kGeneration + 1), kSession + 1) ==
           CompanionAuthorizationWireError::claim_in_progress);
    EXPECT(tracker.status().phase ==
           CompanionAuthorizationResponsePhase::idle);
    EXPECT(tracker.observe(kGeneration, pending_fragment()).error ==
           CompanionAuthorizationWireError::no_claim_in_progress);
    EXPECT(tracker.close_transport_generation(kGeneration) ==
           CompanionAuthorizationWireError::none);
    EXPECT(tracker.open_provisional_session(evidence(kGeneration), kSession) ==
           CompanionAuthorizationWireError::stale_start);

    open(tracker, kGeneration + 1, kSession + 1);
    begin(tracker, CompanionAuthorizationPurpose::authorize_controller,
          kGeneration + 1, kSession + 1, 1);
    EXPECT(tracker.observe(kGeneration, pending_fragment()).error ==
           CompanionAuthorizationWireError::wrong_transport_generation);
    EXPECT(tracker.observe(
               kGeneration,
               terminal_fragment(
                   CompanionAuthorizationPurpose::authorize_controller,
                   CompanionAuthorizationClaimOutcome::accepted,
                   CompanionAuthorizationDenyReason::none)).error ==
           CompanionAuthorizationWireError::wrong_transport_generation);
    EXPECT(!tracker.allows_normal_companion_traffic(kGeneration + 1));
}

void test_exchange_replay_and_redacted_status() {
    CompanionAuthorizationResponseTracker tracker;
    open(tracker);
    begin(tracker);
    EXPECT(tracker.observe(kGeneration, pending_fragment()).accepted());
    EXPECT(tracker.observe(kGeneration, terminal_fragment(
               CompanionAuthorizationPurpose::authorize_controller,
               CompanionAuthorizationClaimOutcome::denied,
               CompanionAuthorizationDenyReason::policy_denied)).terminal());
    EXPECT(tracker.begin(kGeneration, start_fragment(
               CompanionAuthorizationPurpose::authorize_controller,
               kSession, kExchange)) ==
           CompanionAuthorizationWireError::no_provisional_session);

    const auto status = tracker.status();
    static_assert(std::is_trivially_copyable_v<
                  CompanionAuthorizationResponseStatus>);
    static_assert(sizeof(CompanionAuthorizationResponseStatus) <= 8);
    static_assert(sizeof(CompanionAuthorizationResponseTracker) <= 96);
    EXPECT(status.terminal_received);
    EXPECT(status.correlation_present);
    EXPECT(!status.application_authorized);
}

}  // namespace

int main() {
    test_fixed_sizes_and_exact_start_vector();
    test_exact_pending_vector();
    test_exact_terminal_vectors_and_coherence();
    test_all_authoritative_denial_reasons_round_trip();
    test_malformed_unknown_and_reserved_values_fail_closed();
    test_encode_failures_are_atomic();
    test_strict_otc0_kind_binding_and_exact_envelope_vector();
    test_provisional_security_and_zero_context_gate();
    test_authorize_pending_then_terminal_enables_normal_traffic();
    test_replace_and_denial_are_authoritative_only_after_pending();
    test_context_and_correlation_mismatch_never_satisfy_claim();
    test_duplicate_out_of_order_and_claim_in_progress_fail_closed();
    test_cancel_disconnect_and_generation_replay_are_closed();
    test_exchange_replay_and_redacted_status();

    if (failures != 0) {
        std::cerr << failures
                  << " companion authorization wire assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 14 companion authorization wire scenario groups\n";
    return EXIT_SUCCESS;
}
