#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "opentrail/map_selector_reset_policy.hpp"

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

void expect_no_mutation_authority(
    const MapSelectorResetPolicyResult& result) {
    EXPECT(!(result.selector_reseed_permit_required &&
             result.independent_authority_required));
    if (result.action ==
        MapSelectorResetPolicyAction::use_authorized_selector_reseed) {
        EXPECT(result.selector_reseed_permit_required);
        EXPECT(result.map_must_remain_unavailable);
    }
    if (result.action == MapSelectorResetPolicyAction::
                             use_external_same_device_recovery ||
        result.action == MapSelectorResetPolicyAction::
                             commission_fresh_device_domain) {
        EXPECT(result.independent_authority_required);
        EXPECT(result.map_must_remain_unavailable);
    }
}

void test_result_is_fixed_shape_and_carries_no_generation_or_handle() {
    static_assert(std::is_trivially_copyable_v<
                  MapSelectorResetPolicyResult>);
    static_assert(sizeof(MapSelectorResetPolicyResult) <= 8);
    MapSelectorResetPolicyResult result{};
    EXPECT(result.state == MapSelectorResetPolicyState::blocked);
    EXPECT(result.action == MapSelectorResetPolicyAction::none);
    EXPECT(result.map_must_remain_unavailable);
}

void test_factory_reset_always_preserves_both_map_domains() {
    const std::array states{
        MapSelectorLifecycleState::same_device_state_intact,
        MapSelectorLifecycleState::same_device_source_unavailable,
        MapSelectorLifecycleState::
            same_device_source_missing_or_replaced,
        MapSelectorLifecycleState::new_device_unprovisioned,
        MapSelectorLifecycleState::new_device_with_retained_selector};
    for (const auto state : states) {
        const auto result = classify_map_selector_reset(
            MapSelectorResetRequest::ordinary_factory_reset, state);
        EXPECT(result.routed());
        EXPECT(result.state ==
               MapSelectorResetPolicyState::preserve_map_state);
        EXPECT(result.action == MapSelectorResetPolicyAction::
                                    preserve_selector_and_trusted_history);
        EXPECT(result.ordinary_factory_reset_allowed);
        EXPECT(!result.selector_reseed_permit_required);
        EXPECT(!result.independent_authority_required);
        EXPECT(result.map_must_remain_unavailable ==
               (state !=
                MapSelectorLifecycleState::same_device_state_intact));
    }
}

void test_service_reseed_routes_only_with_intact_protected_history() {
    const auto result = classify_map_selector_reset(
        MapSelectorResetRequest::selector_service_reseed,
        MapSelectorLifecycleState::same_device_state_intact);
    EXPECT(result.state == MapSelectorResetPolicyState::
                               authorized_selector_service_required);
    EXPECT(result.action ==
           MapSelectorResetPolicyAction::use_authorized_selector_reseed);
    EXPECT(result.selector_reseed_permit_required);
    EXPECT(!result.independent_authority_required);
    EXPECT(result.map_must_remain_unavailable);
}

void test_temporarily_unavailable_source_blocks_selector_access() {
    const auto result = classify_map_selector_reset(
        MapSelectorResetRequest::selector_service_reseed,
        MapSelectorLifecycleState::same_device_source_unavailable);
    EXPECT(!result.routed());
    EXPECT(result.reason == MapSelectorResetPolicyReason::
                                protected_source_unavailable);
    EXPECT(result.action == MapSelectorResetPolicyAction::none);
    EXPECT(!result.selector_reseed_permit_required);
}

void test_missing_same_device_history_cannot_become_selector_reseed() {
    const auto result = classify_map_selector_reset(
        MapSelectorResetRequest::selector_service_reseed,
        MapSelectorLifecycleState::
            same_device_source_missing_or_replaced);
    EXPECT(result.state ==
           MapSelectorResetPolicyState::external_recovery_required);
    EXPECT(result.reason ==
           MapSelectorResetPolicyReason::same_device_history_missing);
    EXPECT(result.action == MapSelectorResetPolicyAction::
                                use_external_same_device_recovery);
    EXPECT(!result.selector_reseed_permit_required);
    EXPECT(result.independent_authority_required);
}

void test_protected_source_replacement_never_self_authorizes() {
    const auto healthy = classify_map_selector_reset(
        MapSelectorResetRequest::protected_source_replacement,
        MapSelectorLifecycleState::same_device_state_intact);
    EXPECT(!healthy.routed());
    EXPECT(healthy.reason == MapSelectorResetPolicyReason::
                                 protected_source_replacement_not_needed);

    const auto unavailable = classify_map_selector_reset(
        MapSelectorResetRequest::protected_source_replacement,
        MapSelectorLifecycleState::same_device_source_unavailable);
    EXPECT(unavailable.state ==
           MapSelectorResetPolicyState::external_recovery_required);
    EXPECT(unavailable.independent_authority_required);
    EXPECT(!unavailable.selector_reseed_permit_required);
}

void test_blank_new_device_routes_only_to_fresh_domain_provisioning() {
    const auto result = classify_map_selector_reset(
        MapSelectorResetRequest::whole_device_replacement,
        MapSelectorLifecycleState::new_device_unprovisioned);
    EXPECT(result.state == MapSelectorResetPolicyState::
                               new_device_provisioning_required);
    EXPECT(result.action == MapSelectorResetPolicyAction::
                                commission_fresh_device_domain);
    EXPECT(result.independent_authority_required);
    EXPECT(!result.selector_reseed_permit_required);
    EXPECT(result.map_must_remain_unavailable);
}

void test_retained_selector_cannot_be_imported_as_new_device_state() {
    const auto result = classify_map_selector_reset(
        MapSelectorResetRequest::whole_device_replacement,
        MapSelectorLifecycleState::new_device_with_retained_selector);
    EXPECT(!result.routed());
    EXPECT(result.reason == MapSelectorResetPolicyReason::
                                retained_selector_import_forbidden);
    EXPECT(result.action == MapSelectorResetPolicyAction::none);
}

void test_device_continuity_mismatch_fails_closed() {
    const auto same_as_new = classify_map_selector_reset(
        MapSelectorResetRequest::whole_device_replacement,
        MapSelectorLifecycleState::same_device_state_intact);
    const auto new_as_same = classify_map_selector_reset(
        MapSelectorResetRequest::protected_source_replacement,
        MapSelectorLifecycleState::new_device_unprovisioned);
    EXPECT(!same_as_new.routed());
    EXPECT(!new_as_same.routed());
    EXPECT(same_as_new.reason == MapSelectorResetPolicyReason::
                                      device_continuity_mismatch);
    EXPECT(new_as_same.reason == MapSelectorResetPolicyReason::
                                     device_continuity_mismatch);
}

void test_unknown_and_future_values_fail_closed_and_all_routes_cohere() {
    const std::array requests{
        MapSelectorResetRequest::ordinary_factory_reset,
        MapSelectorResetRequest::selector_service_reseed,
        MapSelectorResetRequest::protected_source_replacement,
        MapSelectorResetRequest::whole_device_replacement};
    const std::array states{
        MapSelectorLifecycleState::same_device_state_intact,
        MapSelectorLifecycleState::same_device_source_unavailable,
        MapSelectorLifecycleState::
            same_device_source_missing_or_replaced,
        MapSelectorLifecycleState::new_device_unprovisioned,
        MapSelectorLifecycleState::new_device_with_retained_selector};
    for (const auto request : requests) {
        for (const auto state : states) {
            expect_no_mutation_authority(
                classify_map_selector_reset(request, state));
        }
    }

    const auto unknown_request = classify_map_selector_reset(
        MapSelectorResetRequest::unknown,
        MapSelectorLifecycleState::same_device_state_intact);
    const auto future_request = classify_map_selector_reset(
        static_cast<MapSelectorResetRequest>(0xFF),
        MapSelectorLifecycleState::same_device_state_intact);
    const auto unknown_state = classify_map_selector_reset(
        MapSelectorResetRequest::selector_service_reseed,
        MapSelectorLifecycleState::unknown);
    const auto future_state = classify_map_selector_reset(
        MapSelectorResetRequest::selector_service_reseed,
        static_cast<MapSelectorLifecycleState>(0xFF));
    EXPECT(!unknown_request.routed());
    EXPECT(!future_request.routed());
    EXPECT(!unknown_state.routed());
    EXPECT(!future_state.routed());
    EXPECT(unknown_request.reason ==
           MapSelectorResetPolicyReason::invalid_request);
    EXPECT(future_state.reason ==
           MapSelectorResetPolicyReason::invalid_lifecycle_state);
}

}  // namespace

int main() {
    test_result_is_fixed_shape_and_carries_no_generation_or_handle();
    test_factory_reset_always_preserves_both_map_domains();
    test_service_reseed_routes_only_with_intact_protected_history();
    test_temporarily_unavailable_source_blocks_selector_access();
    test_missing_same_device_history_cannot_become_selector_reseed();
    test_protected_source_replacement_never_self_authorizes();
    test_blank_new_device_routes_only_to_fresh_domain_provisioning();
    test_retained_selector_cannot_be_imported_as_new_device_state();
    test_device_continuity_mismatch_fails_closed();
    test_unknown_and_future_values_fail_closed_and_all_routes_cohere();

    if (failures != 0) {
        std::cerr << failures
                  << " map selector reset-policy assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 map selector reset-policy scenario groups\n";
    return EXIT_SUCCESS;
}
