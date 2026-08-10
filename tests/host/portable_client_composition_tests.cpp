#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/portable_client_composition.hpp"

namespace {

using opentrail::targets::PortableClientComposition;
using opentrail::targets::PortableClientCompositionIssue;
using opentrail::targets::PortableClientTargetBindings;
using opentrail::targets::PortableClientTargetPolicy;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

class CountingRadio final : public opentrail::radio::RadioTransport {
public:
    std::size_t advertised_mtu{64};
    mutable std::uint32_t mtu_reads{0};
    mutable std::uint32_t status_reads{0};
    std::uint32_t send_calls{0};
    std::uint32_t receive_calls{0};
    std::uint32_t service_calls{0};

    [[nodiscard]] std::size_t mtu() const override {
        ++mtu_reads;
        return advertised_mtu;
    }

    [[nodiscard]] opentrail::radio::TransportStatus status() const override {
        ++status_reads;
        return {};
    }

    opentrail::radio::SendResult send(opentrail::radio::ByteView,
                                      std::uint64_t) override {
        ++send_calls;
        return {};
    }

    opentrail::radio::ReceiveResult receive(
        opentrail::radio::MutableByteView) override {
        ++receive_calls;
        return {};
    }

    void service(std::uint64_t) override {
        ++service_calls;
    }
};

class CountingGps final : public opentrail::location::GpsProvider {
public:
    mutable std::uint32_t reads{0};

    [[nodiscard]] opentrail::location::GpsReadResult latest_fix() const override {
        ++reads;
        return {};
    }
};

class CountingLogSink final : public opentrail::diagnostics::LogSink {
public:
    std::uint32_t writes{0};

    bool write(const opentrail::diagnostics::LogRecord&) override {
        ++writes;
        return true;
    }
};

class CountingStorage final : public opentrail::persistence::PersistentStorage {
public:
    std::uint32_t operations{0};

    opentrail::persistence::StorageReadResult read_slot(
        opentrail::persistence::StorageDomain,
        std::size_t,
        opentrail::persistence::MutableStorageByteView) override {
        ++operations;
        return {};
    }

    opentrail::persistence::StorageError erase_slot(
        opentrail::persistence::StorageDomain,
        std::size_t) override {
        ++operations;
        return opentrail::persistence::StorageError::none;
    }

    opentrail::persistence::StorageError write_slot(
        opentrail::persistence::StorageDomain,
        std::size_t,
        std::size_t,
        opentrail::persistence::StorageByteView) override {
        ++operations;
        return opentrail::persistence::StorageError::none;
    }

    opentrail::persistence::StorageError sync_slot(
        opentrail::persistence::StorageDomain,
        std::size_t) override {
        ++operations;
        return opentrail::persistence::StorageError::none;
    }
};

class CountingDuplicateStorage final
    : public opentrail::delivery::DuplicateCheckpointStorage {
public:
    std::uint32_t operations{0};

    [[nodiscard]] opentrail::delivery::DuplicateCheckpointStorageError read_slot(
        std::uint8_t, std::uint8_t*, std::size_t) override {
        ++operations;
        return opentrail::delivery::DuplicateCheckpointStorageError::not_found;
    }

    [[nodiscard]] opentrail::delivery::DuplicateCheckpointStorageError write_slot(
        std::uint8_t, const std::uint8_t*, std::size_t) override {
        ++operations;
        return opentrail::delivery::DuplicateCheckpointStorageError::none;
    }

    [[nodiscard]] opentrail::delivery::DuplicateCheckpointStorageError erase_slot(
        std::uint8_t) override {
        ++operations;
        return opentrail::delivery::DuplicateCheckpointStorageError::none;
    }
};

class CountingRandom final : public opentrail::security::SecureRandomSource {
public:
    mutable std::uint32_t state_reads{0};
    std::uint32_t fills{0};

    [[nodiscard]] opentrail::security::EntropyState state() const override {
        ++state_reads;
        return opentrail::security::EntropyState::not_ready;
    }

    [[nodiscard]] opentrail::security::RandomFillResult fill(
        std::uint8_t*, std::size_t) override {
        ++fills;
        return {opentrail::security::RandomFillError::entropy_not_ready, 0};
    }
};

class CountingClock final : public opentrail::time::MonotonicCounterSource {
public:
    std::uint32_t reads{0};

    [[nodiscard]] opentrail::time::RawClockSample read() override {
        ++reads;
        return {};
    }
};

class CountingPower final : public opentrail::power::PowerStatusSource {
public:
    std::uint32_t reads{0};

    [[nodiscard]] opentrail::power::RawPowerObservation read() override {
        ++reads;
        return {};
    }
};

class CountingDisplay final : public opentrail::ui::DisplaySink {
public:
    std::uint32_t presents{0};

    [[nodiscard]] opentrail::ui::DisplayWriteError present(
        const opentrail::ui::UiFrame&) override {
        ++presents;
        return opentrail::ui::DisplayWriteError::none;
    }
};

class CountingInput final : public opentrail::ui::LocalInputSource {
public:
    std::uint32_t reads{0};

    [[nodiscard]] opentrail::ui::LocalInputEvent read() override {
        ++reads;
        return {};
    }
};

struct Fixture {
    CountingRadio radio{};
    CountingGps gps{};
    CountingLogSink log{};
    CountingStorage storage{};
    CountingDuplicateStorage duplicate_storage{};
    CountingRandom random{};
    CountingClock clock{};
    CountingPower power{};
    CountingDisplay display{};
    CountingInput input{};

    [[nodiscard]] PortableClientTargetBindings bindings() {
        return {&radio, &gps, &log, &storage, &duplicate_storage,
                &random, &clock, &power, &display, &input};
    }
};

PortableClientTargetPolicy valid_policy() {
    PortableClientTargetPolicy policy{};
    policy.minimum_radio_mtu_bytes = 38;
    policy.power = {30, 15, 30'000};
    policy.display = {128, 64, 1, 3, false, true, true};
    policy.minimum_action_slots = 2;
    policy.critical_confirmation_required = true;
    return policy;
}

void expect_no_mutating_adapter_calls(const Fixture& fixture) {
    EXPECT(fixture.radio.status_reads == 0);
    EXPECT(fixture.radio.send_calls == 0);
    EXPECT(fixture.radio.receive_calls == 0);
    EXPECT(fixture.radio.service_calls == 0);
    EXPECT(fixture.gps.reads == 0);
    EXPECT(fixture.log.writes == 0);
    EXPECT(fixture.storage.operations == 0);
    EXPECT(fixture.duplicate_storage.operations == 0);
    EXPECT(fixture.random.state_reads == 0);
    EXPECT(fixture.random.fills == 0);
    EXPECT(fixture.clock.reads == 0);
    EXPECT(fixture.power.reads == 0);
    EXPECT(fixture.display.presents == 0);
    EXPECT(fixture.input.reads == 0);
}

void test_complete_button_target_is_structurally_ready() {
    Fixture fixture{};
    PortableClientComposition composition{fixture.bindings(), valid_policy()};

    const auto result = composition.review();
    EXPECT(result.ready());
    EXPECT(result.issue_mask == 0);
    EXPECT(result.observed_radio_mtu_bytes == 64);
    EXPECT(fixture.radio.mtu_reads == 1);
    expect_no_mutating_adapter_calls(fixture);
}

void test_touch_target_and_not_ready_services_are_allowed() {
    Fixture fixture{};
    auto policy = valid_policy();
    policy.display = {466, 466, 16, 4, true, false, true};
    PortableClientComposition composition{fixture.bindings(), policy};

    EXPECT(composition.review().ready());
    EXPECT(fixture.random.state_reads == 0);
    EXPECT(fixture.gps.reads == 0);
    EXPECT(fixture.clock.reads == 0);
    expect_no_mutating_adapter_calls(fixture);
}

void test_all_missing_bindings_are_reported_together() {
    PortableClientComposition composition{{}, valid_policy()};
    const auto result = composition.review();

    EXPECT(!result.ready());
    EXPECT(result.observed_radio_mtu_bytes == 0);
    EXPECT(result.has(PortableClientCompositionIssue::missing_radio));
    EXPECT(result.has(PortableClientCompositionIssue::missing_gps));
    EXPECT(result.has(PortableClientCompositionIssue::missing_log_sink));
    EXPECT(result.has(PortableClientCompositionIssue::missing_storage));
    EXPECT(result.has(
        PortableClientCompositionIssue::missing_duplicate_checkpoint_storage));
    EXPECT(result.has(PortableClientCompositionIssue::missing_secure_random));
    EXPECT(result.has(PortableClientCompositionIssue::missing_monotonic_clock));
    EXPECT(result.has(PortableClientCompositionIssue::missing_power));
    EXPECT(result.has(PortableClientCompositionIssue::missing_display));
    EXPECT(result.has(PortableClientCompositionIssue::missing_input));
    EXPECT(!result.has(PortableClientCompositionIssue::none));
}

void test_radio_requirement_and_capability_fail_closed() {
    Fixture fixture{};
    auto policy = valid_policy();
    policy.minimum_radio_mtu_bytes = 0;
    EXPECT((PortableClientComposition{fixture.bindings(), policy}
               .review()
               .has(PortableClientCompositionIssue::invalid_radio_requirement)));

    policy = valid_policy();
    policy.minimum_radio_mtu_bytes =
        opentrail::radio::kMaximumFrameBytes + 1;
    EXPECT((PortableClientComposition{fixture.bindings(), policy}
               .review()
               .has(PortableClientCompositionIssue::invalid_radio_requirement)));

    policy = valid_policy();
    fixture.radio.advertised_mtu = 0;
    EXPECT((PortableClientComposition{fixture.bindings(), policy}
               .review()
               .has(PortableClientCompositionIssue::invalid_radio_capability)));

    fixture.radio.advertised_mtu =
        opentrail::radio::kMaximumFrameBytes + 1;
    EXPECT((PortableClientComposition{fixture.bindings(), policy}
               .review()
               .has(PortableClientCompositionIssue::invalid_radio_capability)));

    fixture.radio.advertised_mtu = 37;
    EXPECT((PortableClientComposition{fixture.bindings(), policy}
               .review()
               .has(PortableClientCompositionIssue::radio_mtu_too_small)));
    expect_no_mutating_adapter_calls(fixture);
}

void test_power_policy_uses_the_shared_pure_validator() {
    Fixture fixture{};
    auto policy = valid_policy();
    EXPECT(opentrail::power::valid_power_policy(policy.power));

    policy.power.stale_after_ms = 0;
    EXPECT(!opentrail::power::valid_power_policy(policy.power));
    EXPECT((PortableClientComposition{fixture.bindings(), policy}
               .review()
               .has(PortableClientCompositionIssue::invalid_power_policy)));

    policy = valid_policy();
    policy.power.critical_battery_percent =
        policy.power.low_battery_percent;
    EXPECT((!PortableClientComposition{fixture.bindings(), policy}
                .review()
                .ready()));
    EXPECT(fixture.power.reads == 0);
}

void test_display_metadata_uses_the_shared_pure_validator() {
    Fixture fixture{};
    auto policy = valid_policy();
    EXPECT(opentrail::ui::valid_display_capabilities(policy.display));

    policy.display.width_px = 0;
    EXPECT(!opentrail::ui::valid_display_capabilities(policy.display));
    const auto result =
        PortableClientComposition{fixture.bindings(), policy}.review();
    EXPECT(result.has(
        PortableClientCompositionIssue::invalid_display_capabilities));
    EXPECT(fixture.display.presents == 0);
    EXPECT(fixture.input.reads == 0);
}

void test_ui_policy_and_capability_are_separate_failures() {
    Fixture fixture{};
    auto policy = valid_policy();
    policy.minimum_action_slots = 0;
    EXPECT((PortableClientComposition{fixture.bindings(), policy}
               .review()
               .has(PortableClientCompositionIssue::invalid_ui_requirement)));

    policy = valid_policy();
    policy.minimum_action_slots = 1;
    EXPECT((PortableClientComposition{fixture.bindings(), policy}
               .review()
               .has(PortableClientCompositionIssue::invalid_ui_requirement)));

    policy = valid_policy();
    policy.display.max_action_slots = 1;
    EXPECT((PortableClientComposition{fixture.bindings(), policy}
               .review()
               .has(PortableClientCompositionIssue::insufficient_ui_capability)));

    policy = valid_policy();
    policy.display.supports_hold = false;
    EXPECT((PortableClientComposition{fixture.bindings(), policy}
               .review()
               .has(PortableClientCompositionIssue::critical_hold_unsupported)));

    policy.critical_confirmation_required = false;
    EXPECT((PortableClientComposition{fixture.bindings(), policy}.review().ready()));
}

void test_repeated_review_is_deterministic_and_noninvasive() {
    Fixture fixture{};
    PortableClientComposition composition{fixture.bindings(), valid_policy()};
    const auto first = composition.review();
    const auto second = composition.review();

    EXPECT(first.issue_mask == second.issue_mask);
    EXPECT(first.observed_radio_mtu_bytes == second.observed_radio_mtu_bytes);
    EXPECT(fixture.radio.mtu_reads == 2);
    expect_no_mutating_adapter_calls(fixture);
}

}  // namespace

int main() {
    test_complete_button_target_is_structurally_ready();
    test_touch_target_and_not_ready_services_are_allowed();
    test_all_missing_bindings_are_reported_together();
    test_radio_requirement_and_capability_fail_closed();
    test_power_policy_uses_the_shared_pure_validator();
    test_display_metadata_uses_the_shared_pure_validator();
    test_ui_policy_and_capability_are_separate_failures();
    test_repeated_review_is_deterministic_and_noninvasive();

    if (failures != 0) {
        std::cerr << failures
                  << " portable-client composition assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 portable-client composition scenario groups\n";
    return EXIT_SUCCESS;
}
