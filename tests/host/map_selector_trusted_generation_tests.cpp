#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "opentrail/map_selector_trusted_generation.hpp"

namespace {

using namespace opentrail::maps;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

class FakeSource final : public MapSelectorTrustedGenerationSource {
public:
    MapSelectorTrustedGenerationRead read() override {
        ++read_count;
        if (advanced && readback_error !=
                            MapSelectorTrustedGenerationSourceError::none) {
            return {readback_error, generation};
        }
        return {read_error, generation};
    }

    MapSelectorTrustedGenerationSourceError compare_and_advance(
        std::uint64_t expected_current_generation,
        std::uint64_t requested_generation) override {
        ++advance_count;
        observed_expected = expected_current_generation;
        observed_requested = requested_generation;
        advanced = true;
        if (advance_error !=
            MapSelectorTrustedGenerationSourceError::none) {
            if (apply_on_error) {
                generation = requested_generation;
            }
            return advance_error;
        }
        if (forced_readback != 0) {
            generation = forced_readback;
        } else if (!freeze_after_advance) {
            generation = requested_generation;
        }
        return MapSelectorTrustedGenerationSourceError::none;
    }

    MapSelectorTrustedGenerationSourceError read_error{
        MapSelectorTrustedGenerationSourceError::none};
    MapSelectorTrustedGenerationSourceError readback_error{
        MapSelectorTrustedGenerationSourceError::none};
    MapSelectorTrustedGenerationSourceError advance_error{
        MapSelectorTrustedGenerationSourceError::none};
    std::uint64_t generation{0};
    std::uint64_t observed_expected{0};
    std::uint64_t observed_requested{0};
    std::uint64_t forced_readback{0};
    std::uint32_t read_count{0};
    std::uint32_t advance_count{0};
    bool advanced{false};
    bool freeze_after_advance{false};
    bool apply_on_error{false};
};

void test_ready_inspection_accepts_zero_and_nonzero_floors() {
    static_assert(
        !std::is_copy_constructible_v<MapSelectorTrustedGeneration>);
    static_assert(!std::is_move_constructible_v<MapSelectorTrustedGeneration>);

    FakeSource source{};
    MapSelectorTrustedGeneration trusted{source};
    const auto empty_history = trusted.inspect();
    EXPECT(empty_history.usable());
    EXPECT(empty_history.state == MapSelectorTrustedGenerationState::ready);
    EXPECT(empty_history.observed_generation == 0);

    source.generation = 17;
    const auto established = trusted.inspect();
    EXPECT(established.usable());
    EXPECT(established.observed_generation == 17);
    EXPECT(source.read_count == 2);
}

void test_read_failures_are_typed_and_retryable_before_any_write() {
    FakeSource source{};
    source.read_error = MapSelectorTrustedGenerationSourceError::not_ready;
    MapSelectorTrustedGeneration trusted{source};
    const auto unavailable = trusted.inspect();
    EXPECT(!unavailable.usable());
    EXPECT(unavailable.reason ==
           MapSelectorTrustedGenerationReason::read_failed);
    EXPECT(unavailable.source_error ==
           MapSelectorTrustedGenerationSourceError::not_ready);
    EXPECT(!trusted.reconciliation_required());

    source.read_error = MapSelectorTrustedGenerationSourceError::none;
    source.generation = 3;
    EXPECT(trusted.inspect().usable());
}

void test_unknown_source_errors_are_contained_as_invalid_state() {
    FakeSource source{};
    source.read_error =
        static_cast<MapSelectorTrustedGenerationSourceError>(0xFF);
    MapSelectorTrustedGeneration trusted{source};
    const auto result = trusted.inspect();
    EXPECT(result.reason == MapSelectorTrustedGenerationReason::read_failed);
    EXPECT(result.source_error ==
           MapSelectorTrustedGenerationSourceError::invalid_state);
    EXPECT(!result.reconciliation_required);
}

void test_mismatched_or_nonincreasing_requests_never_write() {
    FakeSource source{};
    source.generation = 4;
    MapSelectorTrustedGeneration trusted{source};

    EXPECT(trusted.advance_exact(5, 6).reason ==
           MapSelectorTrustedGenerationReason::source_rollback);
    EXPECT(trusted.advance_exact(3, 5).reason ==
           MapSelectorTrustedGenerationReason::generation_conflict);
    EXPECT(trusted.advance_exact(4, 4).reason ==
           MapSelectorTrustedGenerationReason::invalid_argument);
    EXPECT(trusted.advance_exact(4, 3).reason ==
           MapSelectorTrustedGenerationReason::invalid_argument);
    EXPECT(source.advance_count == 0);
    EXPECT(!trusted.reconciliation_required());
}

void test_exact_compare_advance_requires_exact_readback() {
    FakeSource source{};
    source.generation = 8;
    MapSelectorTrustedGeneration trusted{source};
    const auto result = trusted.advance_exact(8, 9);
    EXPECT(result.usable());
    EXPECT(result.state == MapSelectorTrustedGenerationState::advanced);
    EXPECT(result.observed_generation == 9);
    EXPECT(source.observed_expected == 8);
    EXPECT(source.observed_requested == 9);
    EXPECT(source.read_count == 2);
    EXPECT(source.advance_count == 1);
    EXPECT(!trusted.reconciliation_required());
}

void test_any_advance_error_is_commit_uncertain_and_latches() {
    const std::array errors{
        MapSelectorTrustedGenerationSourceError::not_initialized,
        MapSelectorTrustedGenerationSourceError::not_ready,
        MapSelectorTrustedGenerationSourceError::io_failure,
        MapSelectorTrustedGenerationSourceError::invalid_state,
        MapSelectorTrustedGenerationSourceError::rejected,
        MapSelectorTrustedGenerationSourceError::conflict};
    for (const auto error : errors) {
        FakeSource source{};
        source.generation = 2;
        source.advance_error = error;
        source.apply_on_error = true;
        MapSelectorTrustedGeneration trusted{source};
        const auto result = trusted.advance_exact(2, 3);
        EXPECT(result.reason ==
               MapSelectorTrustedGenerationReason::advance_failed);
        EXPECT(result.source_error == error);
        EXPECT(result.reconciliation_required);
        EXPECT(trusted.reconciliation_required());
    }
}

void test_post_advance_read_failure_latches_reconciliation() {
    FakeSource source{};
    source.generation = 11;
    source.readback_error =
        MapSelectorTrustedGenerationSourceError::io_failure;
    MapSelectorTrustedGeneration trusted{source};
    const auto result = trusted.advance_exact(11, 12);
    EXPECT(result.reason ==
           MapSelectorTrustedGenerationReason::readback_failed);
    EXPECT(result.observed_generation == 12);
    EXPECT(result.reconciliation_required);
    EXPECT(trusted.reconciliation_required());
}

void test_frozen_or_advanced_past_readback_both_latch() {
    FakeSource frozen{};
    frozen.generation = 20;
    frozen.freeze_after_advance = true;
    MapSelectorTrustedGeneration frozen_trust{frozen};
    const auto below = frozen_trust.advance_exact(20, 21);
    EXPECT(below.reason ==
           MapSelectorTrustedGenerationReason::readback_mismatch);
    EXPECT(below.observed_generation == 20);
    EXPECT(below.reconciliation_required);

    FakeSource jumped{};
    jumped.generation = 20;
    jumped.forced_readback = 22;
    MapSelectorTrustedGeneration jumped_trust{jumped};
    const auto above = jumped_trust.advance_exact(20, 21);
    EXPECT(above.reason ==
           MapSelectorTrustedGenerationReason::readback_mismatch);
    EXPECT(above.observed_generation == 22);
    EXPECT(above.reconciliation_required);
}

void test_latch_blocks_all_later_backend_access() {
    FakeSource source{};
    source.generation = 1;
    source.advance_error = MapSelectorTrustedGenerationSourceError::rejected;
    MapSelectorTrustedGeneration trusted{source};
    EXPECT(trusted.advance_exact(1, 2).reconciliation_required);
    const auto reads_before = source.read_count;
    const auto advances_before = source.advance_count;

    const auto inspect = trusted.inspect();
    const auto advance = trusted.advance_exact(2, 3);
    EXPECT(inspect.reason ==
           MapSelectorTrustedGenerationReason::reconciliation_required);
    EXPECT(advance.reason ==
           MapSelectorTrustedGenerationReason::reconciliation_required);
    EXPECT(source.read_count == reads_before);
    EXPECT(source.advance_count == advances_before);
}

void test_fresh_boot_instance_can_reconcile_applied_then_failed_state() {
    FakeSource source{};
    source.generation = 30;
    source.advance_error = MapSelectorTrustedGenerationSourceError::io_failure;
    source.apply_on_error = true;
    MapSelectorTrustedGeneration first_boot{source};
    EXPECT(first_boot.advance_exact(30, 31).reconciliation_required);
    EXPECT(source.generation == 31);

    source.advance_error = MapSelectorTrustedGenerationSourceError::none;
    source.apply_on_error = false;
    MapSelectorTrustedGeneration reconciled_boot{source};
    const auto reconciled = reconciled_boot.inspect();
    EXPECT(reconciled.usable());
    EXPECT(reconciled.observed_generation == 31);
    EXPECT(!reconciled.reconciliation_required);
}

}  // namespace

int main() {
    test_ready_inspection_accepts_zero_and_nonzero_floors();
    test_read_failures_are_typed_and_retryable_before_any_write();
    test_unknown_source_errors_are_contained_as_invalid_state();
    test_mismatched_or_nonincreasing_requests_never_write();
    test_exact_compare_advance_requires_exact_readback();
    test_any_advance_error_is_commit_uncertain_and_latches();
    test_post_advance_read_failure_latches_reconciliation();
    test_frozen_or_advanced_past_readback_both_latch();
    test_latch_blocks_all_later_backend_access();
    test_fresh_boot_instance_can_reconcile_applied_then_failed_state();

    if (failures != 0) {
        std::cerr << failures
                  << " map selector trusted-generation assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout
        << "PASS: 10 map selector trusted-generation scenario groups\n";
    return EXIT_SUCCESS;
}
