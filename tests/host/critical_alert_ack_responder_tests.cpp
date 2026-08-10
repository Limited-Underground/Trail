#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "opentrail/critical_alert_ack_responder.hpp"

namespace {

using namespace opentrail::integration;

constexpr std::uint64_t kConsumerId = 0x0102030405060708ULL;
constexpr std::uint32_t kBootSession = 0x11223344U;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

CriticalAlert alert(std::uint64_t event_id = 1) {
    CriticalAlert value{};
    value.type = CriticalAlertType::engine_over_temperature;
    value.severity = AlertSeverity::critical;
    value.state = AlertState::asserted;
    value.quality = AlertQuality::valid;
    value.unit = AlertUnit::celsius;
    value.producer_id = 10;
    value.vehicle_id = 20;
    value.event_id = event_id;
    value.condition_id = 1000 + event_id;
    value.event_time_utc_s = 1786243200U;
    value.age_ms = 1000;
    value.value_milli = 110000;
    value.diagnostic_code = 1;
    value.utc_present = true;
    value.value_present = true;
    return value;
}

std::array<std::uint8_t, kCriticalAlertFrameBytes> frame(
    const CriticalAlert& value) {
    std::array<std::uint8_t, kCriticalAlertFrameBytes> output{};
    EXPECT(encode_critical_alert(value, output).encoded());
    return output;
}

AlertIngressContext context(
    const CriticalAlert& value,
    std::uint64_t monotonic_ms = 5000) {
    return {
        true,
        true,
        value.producer_id,
        monotonic_ms,
        true,
        value.event_time_utc_s + 1};
}

CriticalAlertAckResponder responder(std::uint32_t sequence = 0) {
    CriticalAlertAckResponder value{};
    EXPECT(value.start({kConsumerId, kBootSession, sequence}) ==
           CriticalAlertAckResponseError::none);
    return value;
}

void test_lifecycle_and_configuration() {
    CriticalAlertAckResponder value{};
    const AlertIngressResult decision{};
    const AlertIngressContext ingress_context{};
    EXPECT(value.respond(decision, ingress_context, 0).error ==
           CriticalAlertAckResponseError::invalid_state);
    EXPECT(value.start({0, kBootSession, 0}) ==
           CriticalAlertAckResponseError::invalid_configuration);
    EXPECT(value.start({kConsumerId, 0, 0}) ==
           CriticalAlertAckResponseError::invalid_configuration);
    EXPECT(value.start({kConsumerId, kBootSession, 7}) ==
           CriticalAlertAckResponseError::none);
    EXPECT(value.start({kConsumerId, kBootSession, 7}) ==
           CriticalAlertAckResponseError::invalid_state);
    EXPECT(value.status().next_ack_sequence == 7);
    value.stop();
    EXPECT(value.respond(decision, ingress_context, 0).error ==
           CriticalAlertAckResponseError::invalid_state);
}

void test_accepted_decision_encodes_correlated_ack_and_age() {
    const auto input = alert();
    const auto encoded = frame(input);
    const auto ingress_context = context(input);
    CriticalAlertIngress ingress{};
    const auto decision = ingress.process(
        encoded.data(), encoded.size(), ingress_context);
    EXPECT(decision.accepted());

    auto value = responder(7);
    const auto response = value.respond(decision, ingress_context, 5005);
    EXPECT(response.produced());
    EXPECT(response.acknowledgement.disposition ==
           AlertAckDisposition::accepted);
    EXPECT(response.acknowledgement.reason == AlertAckReason::none);
    EXPECT(response.acknowledgement.consumer_id == kConsumerId);
    EXPECT(response.acknowledgement.producer_id == input.producer_id);
    EXPECT(response.acknowledgement.event_id == input.event_id);
    EXPECT(response.acknowledgement.condition_id == input.condition_id);
    EXPECT(response.acknowledgement.consumer_boot_session_id == kBootSession);
    EXPECT(response.acknowledgement.ack_sequence == 7);
    EXPECT(response.acknowledgement.observed_alert_age_ms == 1005);
    const auto decoded = decode_critical_alert_ack(
        response.frame.data(), response.frame.size());
    EXPECT(decoded.decoded());
    EXPECT(decoded.acknowledgement.event_id == input.event_id);
    EXPECT(value.status().next_ack_sequence == 8);
    EXPECT(value.status().accepted == 1);
}

void test_duplicate_same_content_maps_to_accepted_ack() {
    const auto input = alert();
    const auto encoded = frame(input);
    CriticalAlertIngress ingress{};
    auto ingress_context = context(input);
    EXPECT(ingress.process(encoded.data(), encoded.size(), ingress_context)
               .accepted());
    ++ingress_context.receive_monotonic_ms;
    const auto duplicate = ingress.process(
        encoded.data(), encoded.size(), ingress_context);
    EXPECT(duplicate.disposition == AlertIngressDisposition::duplicate);
    EXPECT(duplicate.error == AlertIngressError::none);

    auto value = responder();
    const auto response = value.respond(
        duplicate, ingress_context, ingress_context.receive_monotonic_ms);
    EXPECT(response.produced());
    EXPECT(response.acknowledgement.disposition ==
           AlertAckDisposition::accepted);
    EXPECT(response.acknowledgement.reason == AlertAckReason::none);
}

void test_authenticated_unauthorized_and_time_rejections_are_explicit() {
    const auto input = alert();
    const auto encoded = frame(input);
    CriticalAlertIngress ingress{};
    auto ingress_context = context(input);
    ingress_context.authorized_to_publish = false;
    auto decision = ingress.process(
        encoded.data(), encoded.size(), ingress_context);
    EXPECT(decision.error == AlertIngressError::unauthorized);
    auto value = responder();
    auto response = value.respond(decision, ingress_context, 5000);
    EXPECT(response.produced());
    EXPECT(response.acknowledgement.disposition ==
           AlertAckDisposition::rejected);
    EXPECT(response.acknowledgement.reason == AlertAckReason::unauthorized);

    auto stale = input;
    stale.event_id = 2;
    stale.condition_id = 1002;
    stale.age_ms = kMaximumAlertAgeMs + 1;
    const auto stale_frame = frame(stale);
    ingress_context = context(stale, 6000);
    CriticalAlertIngress stale_ingress{};
    decision = stale_ingress.process(
        stale_frame.data(), stale_frame.size(), ingress_context);
    EXPECT(decision.error == AlertIngressError::stale);
    response = value.respond(decision, ingress_context, 6000);
    EXPECT(response.produced());
    EXPECT(response.acknowledgement.reason == AlertAckReason::stale);
    EXPECT(value.status().rejected == 2);
}

void test_conflict_and_capacity_decisions_map_to_canonical_reasons() {
    auto input = alert();
    auto ingress_context = context(input);
    AlertIngressResult conflict{
        AlertIngressDisposition::rejected,
        AlertIngressError::duplicate_conflict,
        AlertCodecError::none,
        input};
    auto value = responder();
    auto response = value.respond(conflict, ingress_context, 5000);
    EXPECT(response.produced());
    EXPECT(response.acknowledgement.reason == AlertAckReason::conflict);

    input.event_id = 2;
    input.condition_id = 1002;
    ingress_context = context(input, 5001);
    AlertIngressResult limited{
        AlertIngressDisposition::rejected,
        AlertIngressError::rate_limited,
        AlertCodecError::none,
        input};
    response = value.respond(limited, ingress_context, 5001);
    EXPECT(response.produced());
    EXPECT(response.acknowledgement.reason == AlertAckReason::rate_limited);
    limited.error = AlertIngressError::producer_capacity_exhausted;
    response = value.respond(limited, ingress_context, 5001);
    EXPECT(response.produced());
    EXPECT(response.acknowledgement.reason == AlertAckReason::rate_limited);
}

void test_untrusted_unparseable_and_identity_mismatch_are_suppressed() {
    const auto input = alert();
    const auto encoded = frame(input);
    auto ingress_context = context(input);
    CriticalAlertIngress ingress{};
    ingress_context.authenticated = false;
    auto decision = ingress.process(
        encoded.data(), encoded.size(), ingress_context);
    auto value = responder();
    EXPECT(value.respond(decision, ingress_context, 5000).error ==
           CriticalAlertAckResponseError::response_suppressed);

    ingress_context = context(input);
    ++ingress_context.authenticated_producer_id;
    decision = ingress.process(
        encoded.data(), encoded.size(), ingress_context);
    EXPECT(decision.error == AlertIngressError::producer_mismatch);
    EXPECT(value.respond(decision, ingress_context, 5000).error ==
           CriticalAlertAckResponseError::producer_mismatch);

    std::array<std::uint8_t, 1> malformed{{0}};
    ingress_context = context(input);
    decision = ingress.process(
        malformed.data(), malformed.size(), ingress_context);
    EXPECT(decision.error == AlertIngressError::codec_rejected);
    EXPECT(value.respond(decision, ingress_context, 5000).error ==
           CriticalAlertAckResponseError::producer_mismatch);
    EXPECT(value.status().produced == 0);
    EXPECT(value.status().suppressed == 3);
}

void test_inconsistent_decisions_fail_without_sequence_consumption() {
    const auto input = alert();
    auto ingress_context = context(input);
    AlertIngressResult inconsistent{
        AlertIngressDisposition::accepted,
        AlertIngressError::stale,
        AlertCodecError::none,
        input};
    auto value = responder(9);
    EXPECT(value.respond(inconsistent, ingress_context, 5000).error ==
           CriticalAlertAckResponseError::inconsistent_decision);
    EXPECT(value.status().next_ack_sequence == 9);
    inconsistent.error = AlertIngressError::none;
    ingress_context.authorized_to_publish = false;
    EXPECT(value.respond(inconsistent, ingress_context, 5000).error ==
           CriticalAlertAckResponseError::inconsistent_decision);
    ingress_context.authorized_to_publish = true;
    inconsistent.codec_error = AlertCodecError::integrity_failure;
    EXPECT(value.respond(inconsistent, ingress_context, 5000).error ==
           CriticalAlertAckResponseError::inconsistent_decision);
    EXPECT(value.status().failures == 3);
    EXPECT(value.status().produced == 0);
}

void test_clock_age_and_sequence_wrap_are_fail_closed_and_atomic() {
    const auto input = alert();
    const auto encoded = frame(input);
    auto ingress_context = context(input);
    CriticalAlertIngress ingress{};
    auto decision = ingress.process(
        encoded.data(), encoded.size(), ingress_context);
    auto value = responder(std::numeric_limits<std::uint32_t>::max());
    EXPECT(value.respond(decision, ingress_context, 4999).error ==
           CriticalAlertAckResponseError::clock_regression);
    EXPECT(value.status().next_ack_sequence ==
           std::numeric_limits<std::uint32_t>::max());
    EXPECT(value.respond(decision, ingress_context, 86404001ULL).error ==
           CriticalAlertAckResponseError::observed_age_out_of_range);
    auto response = value.respond(decision, ingress_context, 5000);
    EXPECT(response.produced());
    EXPECT(response.acknowledgement.ack_sequence ==
           std::numeric_limits<std::uint32_t>::max());
    EXPECT(value.status().next_ack_sequence == 0);

    ++ingress_context.receive_monotonic_ms;
    decision = ingress.process(
        encoded.data(), encoded.size(), ingress_context);
    response = value.respond(
        decision, ingress_context, ingress_context.receive_monotonic_ms);
    EXPECT(response.produced());
    EXPECT(response.acknowledgement.ack_sequence == 0);
    EXPECT(value.status().next_ack_sequence == 1);
}

}  // namespace

int main() {
    test_lifecycle_and_configuration();
    test_accepted_decision_encodes_correlated_ack_and_age();
    test_duplicate_same_content_maps_to_accepted_ack();
    test_authenticated_unauthorized_and_time_rejections_are_explicit();
    test_conflict_and_capacity_decisions_map_to_canonical_reasons();
    test_untrusted_unparseable_and_identity_mismatch_are_suppressed();
    test_inconsistent_decisions_fail_without_sequence_consumption();
    test_clock_age_and_sequence_wrap_are_fail_closed_and_atomic();

    if (failures != 0) {
        std::cerr << failures << " critical alert ACK responder assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 critical alert ACK responder scenario groups\n";
    return EXIT_SUCCESS;
}
