#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "opentrail/single_repeater_forwarder.hpp"

namespace {

using namespace opentrail::delivery;

constexpr std::uint64_t kGroupContext = 0x1001;
constexpr std::uint64_t kSource = 0x2001;
constexpr std::uint64_t kRepeater = 0x3001;
constexpr std::uint64_t kDestination = 0x4001;
constexpr std::uint32_t kEpoch = 7;
const std::array<std::uint8_t, 6> frame{0x4F, 0x54, 0x01, 0xAA, 0x00, 0xFF};

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

VerifiedForwardingMetadata metadata(std::uint32_t message_id = 1) {
    return {
        kGroupContext,
        kSource,
        kDestination,
        kEpoch,
        message_id,
        true,
        true,
        true,
    };
}

SingleRepeaterPolicy policy(
    std::size_t depth = 4,
    std::uint16_t rate = 8,
    std::uint32_t window_ms = 1000,
    std::uint32_t age_ms = 100) {
    return {1, true, depth, rate, window_ms, age_ms};
}

void test_valid_frame_is_forwarded_byte_for_byte() {
    DuplicateWindow duplicates{10000};
    SingleRepeaterForwarder forwarder{
        kRepeater, kGroupContext, kEpoch, policy(), duplicates};
    const auto decision = forwarder.process(
        metadata(), {frame.data(), frame.size()}, 10);
    EXPECT(decision.queued);
    EXPECT(decision.disposition == SingleRepeaterDisposition::queued);
    EXPECT(decision.replay_state_changed);
    const auto output = forwarder.next_forward(11);
    EXPECT(output.has_frame && output.frame.size == frame.size());
    for (std::size_t index = 0; index < frame.size(); ++index) {
        EXPECT(output.frame.bytes[index] == frame[index]);
    }
}

void test_authentication_failure_does_not_poison_duplicate_state() {
    DuplicateWindow duplicates{10000};
    SingleRepeaterForwarder forwarder{
        kRepeater, kGroupContext, kEpoch, policy(), duplicates};
    auto input = metadata(2);
    input.source_authenticated = false;
    EXPECT(forwarder.process(input, {frame.data(), frame.size()}, 0).disposition ==
           SingleRepeaterDisposition::source_authentication_required);
    input.source_authenticated = true;
    EXPECT(forwarder.process(input, {frame.data(), frame.size()}, 1).queued);
    EXPECT(forwarder.status().authentication_dropped == 1);
}

void test_authorization_context_and_permission_precede_duplicate_state() {
    DuplicateWindow duplicates{10000};
    SingleRepeaterForwarder forwarder{
        kRepeater, kGroupContext, kEpoch, policy(), duplicates};
    auto input = metadata(3);
    input.source_authorized = false;
    EXPECT(forwarder.process(input, {frame.data(), frame.size()}, 0).disposition ==
           SingleRepeaterDisposition::source_unauthorized);
    input.source_authorized = true;
    input.group_context_id += 1;
    EXPECT(forwarder.process(input, {frame.data(), frame.size()}, 1).disposition ==
           SingleRepeaterDisposition::wrong_group_or_epoch);
    input.group_context_id = kGroupContext;
    input.forwarding_permitted = false;
    EXPECT(forwarder.process(input, {frame.data(), frame.size()}, 2).disposition ==
           SingleRepeaterDisposition::forwarding_not_permitted);
    input.forwarding_permitted = true;
    EXPECT(forwarder.process(input, {frame.data(), frame.size()}, 3).queued);
}

void test_exactly_one_authorized_repeater_is_required() {
    for (const auto count : {std::uint8_t{0}, std::uint8_t{2}}) {
        DuplicateWindow duplicates{10000};
        auto invalid = policy();
        invalid.configured_authorized_repeaters = count;
        SingleRepeaterForwarder forwarder{
            kRepeater, kGroupContext, kEpoch, invalid, duplicates};
        EXPECT(forwarder.process(
                   metadata(4), {frame.data(), frame.size()}, 0)
                   .disposition == SingleRepeaterDisposition::invalid_policy);
    }
    DuplicateWindow duplicates{10000};
    auto unauthorized = policy();
    unauthorized.local_repeater_authorized = false;
    SingleRepeaterForwarder forwarder{
        kRepeater, kGroupContext, kEpoch, unauthorized, duplicates};
    EXPECT(forwarder.process(
               metadata(5), {frame.data(), frame.size()}, 0)
               .disposition == SingleRepeaterDisposition::invalid_policy);
}

void test_duplicate_and_reflection_are_forwarded_only_once() {
    DuplicateWindow duplicates{10000};
    SingleRepeaterForwarder forwarder{
        kRepeater, kGroupContext, kEpoch, policy(), duplicates};
    const auto input = metadata(6);
    EXPECT(forwarder.process(input, {frame.data(), frame.size()}, 0).queued);
    const auto duplicate = forwarder.process(
        input, {frame.data(), frame.size()}, 1);
    EXPECT(duplicate.disposition == SingleRepeaterDisposition::duplicate);
    EXPECT(!duplicate.replay_state_changed);
    EXPECT(forwarder.status().duplicates_dropped == 1);
}

void test_self_source_and_local_destination_do_not_forward() {
    DuplicateWindow duplicates{10000};
    SingleRepeaterForwarder forwarder{
        kRepeater, kGroupContext, kEpoch, policy(), duplicates};
    auto input = metadata(7);
    input.source_alias = kRepeater;
    EXPECT(forwarder.process(input, {frame.data(), frame.size()}, 0).disposition ==
           SingleRepeaterDisposition::self_source);
    input = metadata(8);
    input.destination_alias = kRepeater;
    EXPECT(forwarder.process(input, {frame.data(), frame.size()}, 1).disposition ==
           SingleRepeaterDisposition::destination_reached);
}

void test_queue_and_rate_limits_fail_closed() {
    DuplicateWindow queue_duplicates{10000};
    SingleRepeaterForwarder queue_limited{
        kRepeater, kGroupContext, kEpoch, policy(1, 8), queue_duplicates};
    EXPECT(queue_limited.process(
               metadata(10), {frame.data(), frame.size()}, 0).queued);
    const auto queue_full = queue_limited.process(
        metadata(11), {frame.data(), frame.size()}, 1);
    EXPECT(queue_full.disposition == SingleRepeaterDisposition::queue_full);
    EXPECT(queue_full.replay_state_changed);
    EXPECT(queue_limited.next_forward(2).has_frame);
    EXPECT(queue_limited.process(
               metadata(11), {frame.data(), frame.size()}, 3).disposition ==
           SingleRepeaterDisposition::duplicate);

    DuplicateWindow rate_duplicates{10000};
    SingleRepeaterForwarder rate_limited{
        kRepeater, kGroupContext, kEpoch, policy(4, 1), rate_duplicates};
    EXPECT(rate_limited.process(
               metadata(20), {frame.data(), frame.size()}, 0).queued);
    EXPECT(rate_limited.next_forward(1).has_frame);
    const auto rate_limited_result = rate_limited.process(
        metadata(21), {frame.data(), frame.size()}, 2);
    EXPECT(rate_limited_result.disposition ==
           SingleRepeaterDisposition::rate_limited);
    EXPECT(rate_limited_result.replay_state_changed);
    EXPECT(rate_limited.process(
               metadata(21), {frame.data(), frame.size()}, 1000).disposition ==
           SingleRepeaterDisposition::duplicate);
    EXPECT(rate_limited.process(
               metadata(22), {frame.data(), frame.size()}, 1001).queued);
}

void test_expiry_and_clock_regression_drop_without_transmit() {
    DuplicateWindow duplicates{10000};
    SingleRepeaterForwarder forwarder{
        kRepeater, kGroupContext, kEpoch, policy(4, 8, 1000, 100), duplicates};
    EXPECT(forwarder.process(
               metadata(30), {frame.data(), frame.size()}, 1000).queued);
    const auto exact_expiry = forwarder.next_forward(1100);
    EXPECT(!exact_expiry.has_frame && exact_expiry.expired_frames_dropped == 1);
    EXPECT(forwarder.process(
               metadata(31), {frame.data(), frame.size()}, 2000).queued);
    const auto regressed = forwarder.next_forward(1999);
    EXPECT(!regressed.has_frame && regressed.expired_frames_dropped == 1);
    EXPECT(forwarder.status().expired_dropped == 2);

    DuplicateWindow process_duplicates{10000};
    SingleRepeaterForwarder process_forwarder{
        kRepeater,
        kGroupContext,
        kEpoch,
        policy(4, 8, 1000, 100),
        process_duplicates};
    EXPECT(process_forwarder.process(
               metadata(32), {frame.data(), frame.size()}, 3000).queued);
    EXPECT(process_forwarder.process(
               metadata(33), {frame.data(), frame.size()}, 2999).disposition ==
           SingleRepeaterDisposition::clock_regression);
    EXPECT(process_forwarder.process(
               metadata(33), {frame.data(), frame.size()}, 3001).queued);
    EXPECT(process_forwarder.status().clock_regression_dropped == 1);
}

void test_invalid_frame_and_configuration_are_rejected() {
    DuplicateWindow duplicates{10000};
    SingleRepeaterForwarder forwarder{
        kRepeater, kGroupContext, kEpoch, policy(), duplicates};
    EXPECT(forwarder.process(metadata(40), {nullptr, 1}, 0).disposition ==
           SingleRepeaterDisposition::invalid_argument);
    auto invalid = metadata(41);
    invalid.source_alias = 0;
    EXPECT(forwarder.process(invalid, {frame.data(), frame.size()}, 0).disposition ==
           SingleRepeaterDisposition::invalid_argument);
}

class FakeAdmissionGate final : public SingleRepeaterAdmissionGate {
public:
    SingleRepeaterAdmission evaluate(const VerifiedForwardingMetadata& input,
                                     std::size_t size, std::uint64_t now) noexcept override {
        ++calls;
        seen = input;
        seen_size = size;
        seen_time = now;
        if (mutate_metadata != nullptr) {
            mutate_metadata->source_alias = 0;
            mutate_metadata->message_id = 999;
        }
        if (mutate_bytes != nullptr) { mutate_bytes[0] ^= 0xff; }
        if (reenter != nullptr) {
            auto* owner = reenter;
            reenter = nullptr;
            nested_process = owner->process(metadata(999), {frame.data(), frame.size()}, now).disposition;
            nested_release = owner->next_forward(now).has_frame;
            nested_pruned = owner->prune_expired(std::numeric_limits<std::uint64_t>::max());
        }
        return result;
    }
    SingleRepeaterAdmission result{SingleRepeaterAdmission::allow};
    std::uint32_t calls{0};
    VerifiedForwardingMetadata seen{};
    std::size_t seen_size{0};
    std::uint64_t seen_time{0};
    VerifiedForwardingMetadata* mutate_metadata{nullptr};
    std::uint8_t* mutate_bytes{nullptr};
    SingleRepeaterForwarder* reenter{nullptr};
    SingleRepeaterDisposition nested_process{SingleRepeaterDisposition::queued};
    bool nested_release{true};
    std::uint8_t nested_pruned{255};
};

// A1: original provenance survives FIFO movement, including near UINT64_MAX.
void test_original_provenance_and_expiry_only_service() {
    DuplicateWindow duplicates{10000};
    SingleRepeaterForwarder owner{kRepeater, kGroupContext, kEpoch, policy(), duplicates};
    EXPECT(owner.process(metadata(101), {frame.data(), frame.size()}, 20).queued);
    auto second_source = metadata(102);
    ++second_source.source_alias;
    EXPECT(owner.process(second_source, {frame.data(), frame.size()}, 40).queued);
    EXPECT(owner.prune_expired(119) == 0);
    EXPECT(owner.status().queue_depth == 2);
    EXPECT(owner.prune_expired(120) == 1);
    EXPECT(owner.status().queue_depth == 1);
    const auto live = owner.next_forward(121);
    EXPECT(live.has_frame && live.provenance.metadata.message_id == 102);
    EXPECT(live.provenance.metadata.source_alias == kSource + 1);
    EXPECT(live.provenance.metadata.group_context_id == kGroupContext);
    EXPECT(live.provenance.metadata.group_epoch == kEpoch);
    EXPECT(live.provenance.metadata.destination_alias == kDestination);
    EXPECT(live.provenance.enqueued_at_ms == 40);
    EXPECT(live.provenance.maximum_queue_age_ms == 100);
    EXPECT(live.frame.size == frame.size());
    EXPECT(live.frame.bytes[0] == frame[0]);
    for (const auto age : {std::uint64_t{99}, std::uint64_t{100}, std::uint64_t{101}}) {
        DuplicateWindow window{10000};
        SingleRepeaterForwarder boundary{kRepeater, kGroupContext, kEpoch, policy(), window};
        EXPECT(boundary.process(metadata(103), {frame.data(), frame.size()}, 10).queued);
        EXPECT(boundary.prune_expired(10 + age) == (age < 100 ? 0 : 1));
        EXPECT(boundary.status().queue_depth == (age < 100 ? 1U : 0U));
    }
    DuplicateWindow maximum_window{10000};
    SingleRepeaterForwarder maximum{kRepeater, kGroupContext, kEpoch, policy(), maximum_window};
    const auto limit = std::numeric_limits<std::uint64_t>::max();
    EXPECT(maximum.process(metadata(104), {frame.data(), frame.size()}, limit - 50).queued);
    EXPECT(maximum.prune_expired(limit) == 0);
    const auto at_maximum = maximum.next_forward(limit);
    EXPECT(at_maximum.has_frame && at_maximum.provenance.enqueued_at_ms == limit - 50);
    DuplicateWindow bounded_window{10000};
    SingleRepeaterForwarder bounded{kRepeater, kGroupContext, kEpoch, policy(8), bounded_window};
    for (std::uint32_t i = 0; i < 8; ++i) {
        EXPECT(bounded.process(metadata(110 + i), {frame.data(), frame.size()}, i).queued);
    }
    EXPECT(bounded.prune_expired(107) == 8);
    EXPECT(bounded.status().queue_depth == 0);
    EXPECT(!bounded.next_forward(108).has_frame);
}

// A2: only authenticated new keys reach accounting; later denial never refunds.
void test_gate_order_and_conservative_denied_observations() {
    DuplicateWindow window{10000};
    FakeAdmissionGate gate;
    SingleRepeaterForwarder owner{kRepeater, kGroupContext, kEpoch, policy(), window, &gate};
    EXPECT(!owner.process(metadata(120), {nullptr, 1}, 0).queued);
    auto unauthenticated = metadata(120);
    unauthenticated.source_authenticated = false;
    EXPECT(!owner.process(unauthenticated, {frame.data(), frame.size()}, 0).queued);
    auto unauthorized = metadata(120);
    unauthorized.source_authorized = false;
    EXPECT(!owner.process(unauthorized, {frame.data(), frame.size()}, 1).queued);
    auto context = metadata(120);
    ++context.group_epoch;
    EXPECT(!owner.process(context, {frame.data(), frame.size()}, 2).queued);
    auto permission = metadata(120);
    permission.forwarding_permitted = false;
    EXPECT(!owner.process(permission, {frame.data(), frame.size()}, 3).queued);
    EXPECT(gate.calls == 0 && window.status(3).entries == 0);
    EXPECT(owner.process(metadata(120), {frame.data(), frame.size()}, 10).queued);
    EXPECT(owner.process(metadata(119), {frame.data(), frame.size()}, 9).disposition ==
           SingleRepeaterDisposition::clock_regression);
    EXPECT(owner.process(metadata(120), {frame.data(), frame.size()}, 11).disposition ==
           SingleRepeaterDisposition::duplicate);
    EXPECT(gate.calls == 1);
    gate.result = SingleRepeaterAdmission::denied;
    const auto denied = owner.process(metadata(121), {frame.data(), frame.size()}, 12);
    EXPECT(!denied.queued && denied.replay_state_changed);
    EXPECT(denied.disposition == SingleRepeaterDisposition::admission_denied);
    EXPECT(gate.calls == 2 && gate.seen.source_alias == kSource && gate.seen.message_id == 121);
    EXPECT(gate.seen_size == frame.size() && gate.seen_time == 12);
    EXPECT(owner.process(metadata(121), {frame.data(), frame.size()}, 13).disposition ==
           SingleRepeaterDisposition::duplicate);
    EXPECT(gate.calls == 2);
    for (const bool queue_pressure : {false, true}) {
        DuplicateWindow pressure_window{10000};
        FakeAdmissionGate pressure_gate;
        SingleRepeaterForwarder pressure{kRepeater, kGroupContext, kEpoch,
            policy(queue_pressure ? 1 : 4, queue_pressure ? 8 : 1), pressure_window, &pressure_gate};
        EXPECT(pressure.process(metadata(122), {frame.data(), frame.size()}, 0).queued);
        const auto later = pressure.process(metadata(123), {frame.data(), frame.size()}, 1);
        EXPECT(!later.queued && later.replay_state_changed);
        EXPECT(later.disposition == (queue_pressure ? SingleRepeaterDisposition::queue_full
                                                   : SingleRepeaterDisposition::rate_limited));
        EXPECT(pressure_gate.calls == 2);
        EXPECT(pressure.process(metadata(123), {frame.data(), frame.size()}, 2).disposition ==
               SingleRepeaterDisposition::duplicate);
        EXPECT(pressure_gate.calls == 2);
    }
}

// A3: uncertainty contains older frames; raw callback reentry cannot pop them.
void test_gate_uncertainty_reentry_and_input_snapshots() {
    DuplicateWindow window{10000};
    FakeAdmissionGate gate;
    SingleRepeaterForwarder owner{kRepeater, kGroupContext, kEpoch, policy(), window, &gate};
    EXPECT(owner.process(metadata(130), {frame.data(), frame.size()}, 10).queued);
    gate.result = SingleRepeaterAdmission::uncertain;
    const auto uncertain = owner.process(metadata(131), {frame.data(), frame.size()}, 11);
    EXPECT(!uncertain.queued && uncertain.replay_state_changed);
    EXPECT(uncertain.disposition == SingleRepeaterDisposition::admission_uncertain);
    EXPECT(owner.status().admission_uncertain && owner.status().queue_depth == 1);
    EXPECT(!owner.next_forward(12).has_frame);
    EXPECT(owner.prune_expired(12) == 0 && owner.status().queue_depth == 1);
    EXPECT(!owner.process(metadata(132), {frame.data(), frame.size()}, 13).replay_state_changed);
    EXPECT(gate.calls == 2 && window.status(13).entries == 2);

    DuplicateWindow guarded_window{10000};
    FakeAdmissionGate guarded_gate;
    SingleRepeaterForwarder guarded{kRepeater, kGroupContext, kEpoch, policy(), guarded_window,
                                    &guarded_gate};
    EXPECT(guarded.process(metadata(133), {frame.data(), frame.size()}, 10).queued);
    auto original = metadata(134);
    auto bytes = frame;
    guarded_gate.reenter = &guarded;
    guarded_gate.mutate_metadata = &original;
    guarded_gate.mutate_bytes = bytes.data();
    EXPECT(guarded.process(original, {bytes.data(), bytes.size()}, 11).queued);
    EXPECT(original.source_alias == 0 && bytes[0] != frame[0]);
    EXPECT(guarded_gate.nested_process == SingleRepeaterDisposition::reentrant);
    EXPECT(!guarded_gate.nested_release && guarded_gate.nested_pruned == 0);
    EXPECT(guarded.status().queue_depth == 2 && guarded_window.status(11).entries == 2);
    EXPECT(guarded.next_forward(12).provenance.metadata.message_id == 133);
    const auto snapshot = guarded.next_forward(13);
    EXPECT(snapshot.has_frame && snapshot.provenance.metadata.message_id == 134);
    EXPECT(snapshot.provenance.metadata.source_alias == kSource);
    EXPECT(snapshot.provenance.enqueued_at_ms == 11);
    for (std::size_t index = 0; index < frame.size(); ++index) {
        EXPECT(snapshot.frame.bytes[index] == frame[index]);
    }
}

// OP v1 fixture: max-size invitation part and its separate signature.
// Framing matches Trail's pairing_frame.hpp at c486a42a; payloads are synthetic.
// The repeated public code supplies no authority or duplicate identity.
void test_pairing_shaped_bytes_require_authority_and_keep_original_fifo_age() {
    std::array<std::uint8_t, 194> invitation{};
    std::array<std::uint8_t, 70> signature{};
    const std::array<std::uint8_t, 6> invite_header{'O', 'P', 0x15, 0x40, 0xE2, 0x01};
    auto signature_header = invite_header;
    signature_header[2] = 0x16;
    for (std::size_t i = 0; i < invitation.size(); ++i) {
        invitation[i] = i < invite_header.size() ? invite_header[i] :
            static_cast<std::uint8_t>(i * 37U);
    }
    for (std::size_t i = 0; i < signature.size(); ++i) {
        signature[i] = i < signature_header.size() ? signature_header[i] :
            static_cast<std::uint8_t>(i * 13U);
    }
    const auto original_signature = signature;
    DuplicateWindow window{10000};
    SingleRepeaterForwarder owner{kRepeater, kGroupContext, kEpoch, policy(2), window};

    auto untrusted = metadata(201);
    untrusted.source_authenticated = false;
    const auto refused = owner.process(untrusted, {invitation.data(), invitation.size()}, 10);
    EXPECT(refused.disposition == SingleRepeaterDisposition::source_authentication_required);
    EXPECT(!refused.replay_state_changed && owner.status().queue_depth == 0);

    // Synthetic authority is supplied by the harness, never derived from either payload.
    EXPECT(owner.process(metadata(201), {invitation.data(), invitation.size()}, 11).queued);
    EXPECT(owner.process(metadata(202), {signature.data(), signature.size()}, 50).queued);
    EXPECT(owner.process(metadata(201), {invitation.data(), invitation.size()}, 100).disposition ==
           SingleRepeaterDisposition::duplicate);
    const auto full = owner.process(metadata(203), {frame.data(), frame.size()}, 101);
    EXPECT(full.disposition == SingleRepeaterDisposition::queue_full);
    EXPECT(!full.queued && full.replay_state_changed && owner.status().queue_depth == 2);

    signature.fill(0);
    const auto output = owner.next_forward(111);  // invitation expires at its original age
    EXPECT(output.has_frame && output.frame.size == original_signature.size());
    EXPECT(output.provenance.metadata.message_id == 202);
    EXPECT(output.provenance.enqueued_at_ms == 50);
    for (std::size_t i = 0; i < original_signature.size(); ++i) {
        EXPECT(output.frame.bytes[i] == original_signature[i]);
    }
    EXPECT(!owner.next_forward(112).has_frame);
    EXPECT(owner.process(metadata(203), {frame.data(), frame.size()}, 113).disposition ==
           SingleRepeaterDisposition::duplicate);
}

}  // namespace

int main() {
    test_valid_frame_is_forwarded_byte_for_byte();
    test_authentication_failure_does_not_poison_duplicate_state();
    test_authorization_context_and_permission_precede_duplicate_state();
    test_exactly_one_authorized_repeater_is_required();
    test_duplicate_and_reflection_are_forwarded_only_once();
    test_self_source_and_local_destination_do_not_forward();
    test_queue_and_rate_limits_fail_closed();
    test_expiry_and_clock_regression_drop_without_transmit();
    test_invalid_frame_and_configuration_are_rejected();
    test_original_provenance_and_expiry_only_service();
    test_gate_order_and_conservative_denied_observations();
    test_gate_uncertainty_reentry_and_input_snapshots();
    test_pairing_shaped_bytes_require_authority_and_keep_original_fifo_age();
    if (failures != 0) {
        std::cerr << failures
                  << " single-repeater forwarder assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 13 immutable single-repeater scenario groups (9 legacy + A1-A3 + F5 framing custody)\n";
    return EXIT_SUCCESS;
}
