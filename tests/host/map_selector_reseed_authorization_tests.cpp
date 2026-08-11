#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>
#include <utility>

#include "opentrail/map_selector_reseed_authorization.hpp"

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

MapActivationPolicy activation_policy() {
    return {8U * 1024U * 1024U, 500, 3, 3};
}

MapPackageEvidence package() {
    return {MapSlot::slot_a,
            10,
            1024U * 1024U,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true};
}

MapSelectorReseedAuthorizationBinding binding(
    std::uint64_t trusted_minimum_generation = 4) {
    return {activation_policy(), trusted_minimum_generation, package()};
}

MapSelectorReseedAuthorizationGrant authorized_grant(
    MapSelectorReseedServiceTransport transport =
        MapSelectorReseedServiceTransport::local_usb) {
    return {MapSelectorReseedAuthorizationBackendState::authorized,
            MapSelectorReseedAuthorizationScope::selector_reseed,
            transport,
            41,
            73,
            100,
            200,
            9,
            binding(),
            {true, true, true, true, true}};
}

class FakeBackend final : public MapSelectorReseedAuthorizationBackend {
public:
    MapSelectorReseedAuthorizationGrant verify_and_consume(
        std::uint64_t authorization_handle,
        const MapSelectorReseedAuthorizationBinding& expected_binding)
        override {
        ++calls;
        observed_handle = authorization_handle;
        observed_binding = expected_binding;
        if (single_use && consumed) {
            auto denied = grant;
            denied.state = MapSelectorReseedAuthorizationBackendState::denied;
            return denied;
        }
        if (grant.state ==
            MapSelectorReseedAuthorizationBackendState::authorized) {
            consumed = true;
        }
        return grant;
    }

    MapSelectorReseedAuthorizationGrant grant{authorized_grant()};
    MapSelectorReseedAuthorizationBinding observed_binding{};
    std::uint64_t observed_handle{0};
    std::uint32_t calls{0};
    bool single_use{false};
    bool consumed{false};
};

MapSelectorReseedAuthorizationResult authorize(
    FakeBackend& backend,
    MapSelectorReseedPermit& permit,
    const MapSelectorReseedAuthorizationPolicy& policy = {100},
    const MapSelectorReseedAuthorizationRequest& request = {41, 73, 150},
    const MapSelectorReseedAuthorizationBinding& expected = binding()) {
    MapSelectorReseedAuthorizer authorizer{backend};
    return authorizer.authorize(policy, request, expected, permit);
}

void test_exact_local_usb_grant_mints_bounded_permit() {
    static_assert(!std::is_copy_constructible_v<MapSelectorReseedPermit>);
    static_assert(!std::is_copy_assignable_v<MapSelectorReseedPermit>);
    static_assert(std::is_move_constructible_v<MapSelectorReseedPermit>);

    FakeBackend backend{};
    MapSelectorReseedPermit permit{};
    const auto result = authorize(backend, permit);
    EXPECT(result.authorized());
    EXPECT(result.backend_called);
    EXPECT(result.backend_state ==
           MapSelectorReseedAuthorizationBackendState::authorized);
    EXPECT(permit.available());
    EXPECT(backend.calls == 1);
    EXPECT(backend.observed_handle == 41);
    EXPECT(backend.observed_binding.trusted_minimum_generation == 4);
}

void test_authenticated_local_wireless_is_the_only_other_transport() {
    FakeBackend wireless{};
    wireless.grant = authorized_grant(
        MapSelectorReseedServiceTransport::authenticated_local_wireless);
    MapSelectorReseedPermit permit{};
    EXPECT(authorize(wireless, permit).authorized());
    EXPECT(permit.available());

    for (const auto transport :
         {MapSelectorReseedServiceTransport::unknown,
          MapSelectorReseedServiceTransport::remote_radio}) {
        FakeBackend rejected{};
        rejected.grant = authorized_grant(transport);
        MapSelectorReseedPermit unavailable{};
        const auto result = authorize(rejected, unavailable);
        EXPECT(result.error ==
               MapSelectorReseedAuthorizationError::transport_not_local);
        EXPECT(!unavailable.available());
    }
}

void test_invalid_policy_request_and_binding_never_reach_backend() {
    FakeBackend backend{};
    MapSelectorReseedPermit permit{};
    EXPECT(authorize(backend, permit, {0}).error ==
           MapSelectorReseedAuthorizationError::invalid_policy);
    EXPECT(authorize(
               backend,
               permit,
               {kMapSelectorReseedMaximumAuthorizationLifetimeMs + 1})
               .error ==
           MapSelectorReseedAuthorizationError::invalid_policy);
    EXPECT(authorize(backend, permit, {100}, {0, 73, 150}).error ==
           MapSelectorReseedAuthorizationError::invalid_request);
    EXPECT(authorize(backend, permit, {100}, {41, 0, 150}).error ==
           MapSelectorReseedAuthorizationError::invalid_request);
    auto invalid = binding();
    invalid.baseline.integrity_verified = false;
    EXPECT(authorize(backend, permit, {100}, {41, 73, 150}, invalid)
               .error ==
           MapSelectorReseedAuthorizationError::invalid_binding);
    EXPECT(backend.calls == 0);
    EXPECT(!permit.available());
}

void test_backend_denial_readiness_and_failure_are_distinct() {
    const std::array states{
        MapSelectorReseedAuthorizationBackendState::denied,
        MapSelectorReseedAuthorizationBackendState::not_ready,
        MapSelectorReseedAuthorizationBackendState::failed,
        static_cast<MapSelectorReseedAuthorizationBackendState>(0xFF)};
    const std::array errors{
        MapSelectorReseedAuthorizationError::denied,
        MapSelectorReseedAuthorizationError::backend_not_ready,
        MapSelectorReseedAuthorizationError::backend_failed,
        MapSelectorReseedAuthorizationError::backend_failed};

    for (std::size_t i = 0; i < states.size(); ++i) {
        FakeBackend backend{};
        backend.grant.state = states[i];
        MapSelectorReseedPermit permit{};
        const auto result = authorize(backend, permit);
        EXPECT(result.error == errors[i]);
        EXPECT(result.backend_state == states[i]);
        EXPECT(result.backend_called);
        EXPECT(!permit.available());
    }
}

void test_handle_scope_and_boot_session_must_match_exactly() {
    std::array<MapSelectorReseedAuthorizationGrant, 3> grants{
        authorized_grant(), authorized_grant(), authorized_grant()};
    grants[0].authorization_handle = 42;
    grants[1].scope = MapSelectorReseedAuthorizationScope::none;
    grants[2].boot_session_id = 74;
    const std::array errors{
        MapSelectorReseedAuthorizationError::handle_mismatch,
        MapSelectorReseedAuthorizationError::scope_mismatch,
        MapSelectorReseedAuthorizationError::boot_session_mismatch};

    for (std::size_t i = 0; i < grants.size(); ++i) {
        FakeBackend backend{};
        backend.grant = grants[i];
        MapSelectorReseedPermit permit{};
        EXPECT(authorize(backend, permit).error == errors[i]);
        EXPECT(!permit.available());
    }
}

void test_complete_operation_binding_is_echo_checked() {
    std::array<MapSelectorReseedAuthorizationGrant, 6> grants{
        authorized_grant(),
        authorized_grant(),
        authorized_grant(),
        authorized_grant(),
        authorized_grant(),
        authorized_grant()};
    ++grants[0].binding.policy.maximum_package_bytes;
    ++grants[1].binding.policy.trial_deadline_ms;
    ++grants[2].binding.trusted_minimum_generation;
    grants[3].binding.baseline.slot = MapSlot::slot_b;
    ++grants[4].binding.baseline.generation;
    --grants[5].binding.baseline.package_bytes;

    for (const auto& grant : grants) {
        FakeBackend backend{};
        backend.grant = grant;
        MapSelectorReseedPermit permit{};
        EXPECT(authorize(backend, permit).error ==
               MapSelectorReseedAuthorizationError::binding_mismatch);
        EXPECT(!permit.available());
    }
}

void test_all_intent_confirmations_and_local_revision_are_required() {
    std::array<MapSelectorReseedAuthorizationGrant, 5> grants{
        authorized_grant(),
        authorized_grant(),
        authorized_grant(),
        authorized_grant(),
        authorized_grant()};
    grants[0].acknowledgements.explicit_operator_confirmation = false;
    grants[1].acknowledgements.map_unavailability_acknowledged = false;
    grants[2].acknowledgements.selector_only_scope_confirmed = false;
    grants[3].acknowledgements.package_retention_confirmed = false;
    grants[4].acknowledgements.trusted_generation_reviewed = false;

    for (const auto& grant : grants) {
        FakeBackend backend{};
        backend.grant = grant;
        MapSelectorReseedPermit permit{};
        EXPECT(authorize(backend, permit).error ==
               MapSelectorReseedAuthorizationError::intent_incomplete);
    }

    FakeBackend no_local_confirmation{};
    no_local_confirmation.grant.local_confirmation_revision = 0;
    MapSelectorReseedPermit permit{};
    EXPECT(authorize(no_local_confirmation, permit).error ==
           MapSelectorReseedAuthorizationError::
               local_confirmation_missing);
}

void test_grant_time_window_is_short_lived_and_fail_closed() {
    std::array<MapSelectorReseedAuthorizationGrant, 4> grants{
        authorized_grant(),
        authorized_grant(),
        authorized_grant(),
        authorized_grant()};
    grants[0].expires_at_ms = grants[0].issued_at_ms;
    grants[1].issued_at_ms = 151;
    grants[1].expires_at_ms = 200;
    grants[2].expires_at_ms = 150;
    grants[3].expires_at_ms = 201;
    const std::array errors{
        MapSelectorReseedAuthorizationError::invalid_time_window,
        MapSelectorReseedAuthorizationError::not_yet_valid,
        MapSelectorReseedAuthorizationError::expired,
        MapSelectorReseedAuthorizationError::lifetime_exceeded};

    for (std::size_t i = 0; i < grants.size(); ++i) {
        FakeBackend backend{};
        backend.grant = grants[i];
        MapSelectorReseedPermit permit{};
        EXPECT(authorize(backend, permit).error == errors[i]);
        EXPECT(!permit.available());
    }
}

void test_backend_single_use_blocks_replay_and_clears_output() {
    FakeBackend backend{};
    backend.single_use = true;
    MapSelectorReseedPermit first{};
    EXPECT(authorize(backend, first).authorized());
    EXPECT(first.available());

    MapSelectorReseedPermit replay{};
    const auto replay_result = authorize(backend, replay);
    EXPECT(replay_result.error ==
           MapSelectorReseedAuthorizationError::denied);
    EXPECT(!replay.available());
    EXPECT(backend.calls == 2);

    const auto invalid_request = authorize(
        backend, first, {100}, {0, 73, 150}, binding());
    EXPECT(invalid_request.error ==
           MapSelectorReseedAuthorizationError::invalid_request);
    EXPECT(!first.available());
    EXPECT(backend.calls == 2);
}

void test_move_transfers_permit_without_cloning_authority() {
    FakeBackend backend{};
    MapSelectorReseedPermit original{};
    EXPECT(authorize(backend, original).authorized());
    MapSelectorReseedPermit moved{std::move(original)};
    EXPECT(!original.available());
    EXPECT(moved.available());

    MapSelectorReseedPermit reassigned{};
    reassigned = std::move(moved);
    EXPECT(!moved.available());
    EXPECT(reassigned.available());
}

}  // namespace

int main() {
    test_exact_local_usb_grant_mints_bounded_permit();
    test_authenticated_local_wireless_is_the_only_other_transport();
    test_invalid_policy_request_and_binding_never_reach_backend();
    test_backend_denial_readiness_and_failure_are_distinct();
    test_handle_scope_and_boot_session_must_match_exactly();
    test_complete_operation_binding_is_echo_checked();
    test_all_intent_confirmations_and_local_revision_are_required();
    test_grant_time_window_is_short_lived_and_fail_closed();
    test_backend_single_use_blocks_replay_and_clears_output();
    test_move_transfers_permit_without_cloning_authority();

    if (failures != 0) {
        std::cerr << failures
                  << " map selector reseed authorization assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 map selector reseed authorization scenario groups\n";
    return EXIT_SUCCESS;
}
