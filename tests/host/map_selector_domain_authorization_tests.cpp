#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>
#include <utility>

#include "opentrail/map_selector_domain_authorization.hpp"

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

std::array<std::uint8_t, 16> domain(std::uint8_t seed = 1) {
    std::array<std::uint8_t, 16> value{};
    for (std::size_t i = 0; i < value.size(); ++i) {
        value[i] = static_cast<std::uint8_t>(seed + i);
    }
    return value;
}

MapSelectorDomainAuthorizationBinding same_device_binding() {
    return {
        MapSelectorResetRequest::protected_source_replacement,
        MapSelectorLifecycleState::same_device_source_missing_or_replaced,
        MapSelectorDomainMediaState::retained_quarantined,
        8,
        domain(101),
        domain()};
}

MapSelectorDomainAuthorizationBinding new_device_binding() {
    return {
        MapSelectorResetRequest::whole_device_replacement,
        MapSelectorLifecycleState::new_device_unprovisioned,
        MapSelectorDomainMediaState::verified_empty,
        0,
        {},
        domain(21)};
}

MapSelectorDomainAuthorizationGrant authorized_grant(
    const MapSelectorDomainAuthorizationBinding& binding =
        same_device_binding()) {
    const auto scope =
        binding.request ==
                MapSelectorResetRequest::protected_source_replacement
            ? MapSelectorDomainAuthorizationScope::replace_same_device_domain
            : MapSelectorDomainAuthorizationScope::commission_new_device_domain;
    return {
        MapSelectorDomainAuthorizationBackendState::authorized,
        scope,
        MapSelectorDomainServiceTransport::local_usb,
        41,
        73,
        100,
        200,
        9,
        binding,
        {true, true, true, true, true, true}};
}

class FakeBackend final : public MapSelectorDomainAuthorizationBackend {
public:
    MapSelectorDomainAuthorizationGrant verify_and_consume(
        std::uint64_t authorization_handle,
        const MapSelectorDomainAuthorizationBinding& expected_binding)
        override {
        ++calls;
        observed_handle = authorization_handle;
        observed_binding = expected_binding;
        if (single_use && consumed) {
            auto denied = grant;
            denied.state =
                MapSelectorDomainAuthorizationBackendState::denied;
            return denied;
        }
        if (grant.state ==
            MapSelectorDomainAuthorizationBackendState::authorized) {
            consumed = true;
        }
        return grant;
    }

    MapSelectorDomainAuthorizationGrant grant{authorized_grant()};
    MapSelectorDomainAuthorizationBinding observed_binding{};
    std::uint64_t observed_handle{0};
    std::uint32_t calls{0};
    bool single_use{false};
    bool consumed{false};
};

MapSelectorDomainAuthorizationResult authorize(
    FakeBackend& backend,
    MapSelectorDomainAuthorizationPermit& permit,
    const MapSelectorDomainAuthorizationBinding& binding =
        same_device_binding(),
    const MapSelectorDomainAuthorizationPolicy& policy = {100},
    const MapSelectorDomainAuthorizationRequest& request = {41, 73, 150}) {
    MapSelectorDomainAuthorizer authorizer{backend};
    return authorizer.authorize(policy, request, binding, permit);
}

void test_same_device_replacement_mints_exact_local_permit() {
    static_assert(!std::is_copy_constructible_v<
                  MapSelectorDomainAuthorizationPermit>);
    static_assert(!std::is_copy_assignable_v<
                  MapSelectorDomainAuthorizationPermit>);
    static_assert(std::is_move_constructible_v<
                  MapSelectorDomainAuthorizationPermit>);

    FakeBackend backend{};
    MapSelectorDomainAuthorizationPermit permit{};
    const auto result = authorize(backend, permit);
    EXPECT(result.authorized());
    EXPECT(result.backend_called);
    EXPECT(result.required_scope == MapSelectorDomainAuthorizationScope::
                                        replace_same_device_domain);
    EXPECT(permit.available());
    EXPECT(backend.calls == 1);
    EXPECT(backend.observed_handle == 41);
    EXPECT(backend.observed_binding.reviewed_selector_generation == 8);
}

void test_blank_new_device_uses_distinct_commissioning_scope() {
    const auto binding = new_device_binding();
    FakeBackend backend{};
    backend.grant = authorized_grant(binding);
    MapSelectorDomainAuthorizationPermit permit{};
    const auto result = authorize(backend, permit, binding);
    EXPECT(result.authorized());
    EXPECT(result.required_scope == MapSelectorDomainAuthorizationScope::
                                        commission_new_device_domain);
    EXPECT(permit.available());
}

void test_invalid_routes_never_reach_backend() {
    std::array<MapSelectorDomainAuthorizationBinding, 5> bindings{
        same_device_binding(),
        same_device_binding(),
        same_device_binding(),
        new_device_binding(),
        same_device_binding()};
    bindings[0].request = MapSelectorResetRequest::ordinary_factory_reset;
    bindings[1].request = MapSelectorResetRequest::selector_service_reseed;
    bindings[2].lifecycle_state =
        MapSelectorLifecycleState::same_device_source_unavailable;
    bindings[3].lifecycle_state =
        MapSelectorLifecycleState::new_device_with_retained_selector;
    bindings[4].request = static_cast<MapSelectorResetRequest>(0xFF);

    FakeBackend backend{};
    for (const auto& binding : bindings) {
        MapSelectorDomainAuthorizationPermit permit{};
        EXPECT(authorize(backend, permit, binding).error ==
               MapSelectorDomainAuthorizationError::invalid_route);
        EXPECT(!permit.available());
    }
    EXPECT(backend.calls == 0);
}

void test_invalid_policy_request_domain_and_media_never_reach_backend() {
    FakeBackend backend{};
    MapSelectorDomainAuthorizationPermit permit{};
    EXPECT(authorize(backend, permit, same_device_binding(), {0}).error ==
           MapSelectorDomainAuthorizationError::invalid_policy);
    EXPECT(authorize(
               backend,
               permit,
               same_device_binding(),
               {kMapSelectorDomainMaximumAuthorizationLifetimeMs + 1})
               .error ==
           MapSelectorDomainAuthorizationError::invalid_policy);
    EXPECT(authorize(
               backend,
               permit,
               same_device_binding(),
               {100},
               {0, 73, 150})
               .error ==
           MapSelectorDomainAuthorizationError::invalid_request);

    auto zero_domain = same_device_binding();
    zero_domain.proposed_domain = {};
    EXPECT(authorize(backend, permit, zero_domain).error ==
           MapSelectorDomainAuthorizationError::invalid_binding);
    auto missing_retired = same_device_binding();
    missing_retired.retired_domain = {};
    EXPECT(authorize(backend, permit, missing_retired).error ==
           MapSelectorDomainAuthorizationError::invalid_binding);
    auto reused_domain = same_device_binding();
    reused_domain.proposed_domain = reused_domain.retired_domain;
    EXPECT(authorize(backend, permit, reused_domain).error ==
           MapSelectorDomainAuthorizationError::invalid_binding);
    auto new_with_retired = new_device_binding();
    new_with_retired.retired_domain = domain(91);
    EXPECT(authorize(backend, permit, new_with_retired).error ==
           MapSelectorDomainAuthorizationError::invalid_binding);
    auto bad_empty = same_device_binding();
    bad_empty.media_state = MapSelectorDomainMediaState::verified_empty;
    EXPECT(authorize(backend, permit, bad_empty).error ==
           MapSelectorDomainAuthorizationError::invalid_binding);
    auto bad_retained = same_device_binding();
    bad_retained.reviewed_selector_generation = 0;
    EXPECT(authorize(backend, permit, bad_retained).error ==
           MapSelectorDomainAuthorizationError::invalid_binding);
    auto new_retained = new_device_binding();
    new_retained.media_state =
        MapSelectorDomainMediaState::retained_quarantined;
    new_retained.reviewed_selector_generation = 4;
    EXPECT(authorize(backend, permit, new_retained).error ==
           MapSelectorDomainAuthorizationError::invalid_binding);
    EXPECT(backend.calls == 0);
}

void test_backend_denial_readiness_failure_and_unknown_are_typed() {
    const std::array states{
        MapSelectorDomainAuthorizationBackendState::denied,
        MapSelectorDomainAuthorizationBackendState::not_ready,
        MapSelectorDomainAuthorizationBackendState::failed,
        static_cast<MapSelectorDomainAuthorizationBackendState>(0xFF)};
    const std::array errors{
        MapSelectorDomainAuthorizationError::denied,
        MapSelectorDomainAuthorizationError::backend_not_ready,
        MapSelectorDomainAuthorizationError::backend_failed,
        MapSelectorDomainAuthorizationError::backend_failed};

    for (std::size_t i = 0; i < states.size(); ++i) {
        FakeBackend backend{};
        backend.grant.state = states[i];
        MapSelectorDomainAuthorizationPermit permit{};
        const auto result = authorize(backend, permit);
        EXPECT(result.error == errors[i]);
        EXPECT(result.backend_state == states[i]);
        EXPECT(!permit.available());
    }
}

void test_handle_scope_usb_and_boot_must_match_exactly() {
    std::array<MapSelectorDomainAuthorizationGrant, 6> grants{
        authorized_grant(),
        authorized_grant(),
        authorized_grant(),
        authorized_grant(),
        authorized_grant(),
        authorized_grant()};
    grants[0].authorization_handle = 42;
    grants[1].scope =
        MapSelectorDomainAuthorizationScope::commission_new_device_domain;
    grants[2].transport = MapSelectorDomainServiceTransport::unknown;
    grants[3].transport =
        MapSelectorDomainServiceTransport::authenticated_local_wireless;
    grants[4].transport = MapSelectorDomainServiceTransport::remote_radio;
    grants[5].boot_session_id = 74;
    const std::array errors{
        MapSelectorDomainAuthorizationError::handle_mismatch,
        MapSelectorDomainAuthorizationError::scope_mismatch,
        MapSelectorDomainAuthorizationError::transport_not_local_usb,
        MapSelectorDomainAuthorizationError::transport_not_local_usb,
        MapSelectorDomainAuthorizationError::transport_not_local_usb,
        MapSelectorDomainAuthorizationError::boot_session_mismatch};

    for (std::size_t i = 0; i < grants.size(); ++i) {
        FakeBackend backend{};
        backend.grant = grants[i];
        MapSelectorDomainAuthorizationPermit permit{};
        EXPECT(authorize(backend, permit).error == errors[i]);
        EXPECT(!permit.available());
    }
}

void test_every_binding_field_is_echo_checked() {
    std::array<MapSelectorDomainAuthorizationGrant, 6> grants{
        authorized_grant(),
        authorized_grant(),
        authorized_grant(),
        authorized_grant(),
        authorized_grant(),
        authorized_grant()};
    grants[0].binding.request =
        MapSelectorResetRequest::whole_device_replacement;
    grants[1].binding.lifecycle_state =
        MapSelectorLifecycleState::new_device_unprovisioned;
    grants[2].binding.media_state =
        MapSelectorDomainMediaState::verified_empty;
    ++grants[3].binding.reviewed_selector_generation;
    ++grants[4].binding.retired_domain[0];
    ++grants[5].binding.proposed_domain[0];

    for (const auto& grant : grants) {
        FakeBackend backend{};
        backend.grant = grant;
        MapSelectorDomainAuthorizationPermit permit{};
        EXPECT(authorize(backend, permit).error ==
               MapSelectorDomainAuthorizationError::binding_mismatch);
        EXPECT(!permit.available());
    }
}

void test_all_confirmations_and_local_revision_are_required() {
    std::array<MapSelectorDomainAuthorizationGrant, 6> grants{
        authorized_grant(),
        authorized_grant(),
        authorized_grant(),
        authorized_grant(),
        authorized_grant(),
        authorized_grant()};
    grants[0].acknowledgements.explicit_operator_confirmation = false;
    grants[1].acknowledgements.physical_access_confirmed = false;
    grants[2].acknowledgements.map_unavailability_acknowledged = false;
    grants[3].acknowledgements.protected_history_retirement_acknowledged =
        false;
    grants[4].acknowledgements.retained_selector_import_forbidden = false;
    grants[5].acknowledgements.fresh_domain_confirmed = false;

    for (const auto& grant : grants) {
        FakeBackend backend{};
        backend.grant = grant;
        MapSelectorDomainAuthorizationPermit permit{};
        EXPECT(authorize(backend, permit).error ==
               MapSelectorDomainAuthorizationError::intent_incomplete);
        EXPECT(!permit.available());
    }

    FakeBackend no_revision{};
    no_revision.grant.local_confirmation_revision = 0;
    MapSelectorDomainAuthorizationPermit permit{};
    EXPECT(authorize(no_revision, permit).error ==
           MapSelectorDomainAuthorizationError::
               local_confirmation_missing);
}

void test_time_window_is_short_lived_and_fail_closed() {
    std::array<MapSelectorDomainAuthorizationGrant, 4> grants{
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
        MapSelectorDomainAuthorizationError::invalid_time_window,
        MapSelectorDomainAuthorizationError::not_yet_valid,
        MapSelectorDomainAuthorizationError::expired,
        MapSelectorDomainAuthorizationError::lifetime_exceeded};

    for (std::size_t i = 0; i < grants.size(); ++i) {
        FakeBackend backend{};
        backend.grant = grants[i];
        MapSelectorDomainAuthorizationPermit permit{};
        EXPECT(authorize(backend, permit).error == errors[i]);
        EXPECT(!permit.available());
    }
}

void test_backend_replay_output_invalidation_and_move_preserve_one_owner() {
    FakeBackend backend{};
    backend.single_use = true;
    MapSelectorDomainAuthorizationPermit first{};
    EXPECT(authorize(backend, first).authorized());
    EXPECT(first.available());

    MapSelectorDomainAuthorizationPermit replay{};
    EXPECT(authorize(backend, replay).error ==
           MapSelectorDomainAuthorizationError::denied);
    EXPECT(!replay.available());

    MapSelectorDomainAuthorizationPermit moved{std::move(first)};
    EXPECT(!first.available());
    EXPECT(moved.available());
    MapSelectorDomainAuthorizationPermit reassigned{};
    reassigned = std::move(moved);
    EXPECT(!moved.available());
    EXPECT(reassigned.available());

    EXPECT(authorize(
               backend,
               reassigned,
               same_device_binding(),
               {100},
               {0, 73, 150})
               .error ==
           MapSelectorDomainAuthorizationError::invalid_request);
    EXPECT(!reassigned.available());
    EXPECT(backend.calls == 2);
}

}  // namespace

int main() {
    test_same_device_replacement_mints_exact_local_permit();
    test_blank_new_device_uses_distinct_commissioning_scope();
    test_invalid_routes_never_reach_backend();
    test_invalid_policy_request_domain_and_media_never_reach_backend();
    test_backend_denial_readiness_failure_and_unknown_are_typed();
    test_handle_scope_usb_and_boot_must_match_exactly();
    test_every_binding_field_is_echo_checked();
    test_all_confirmations_and_local_revision_are_required();
    test_time_window_is_short_lived_and_fail_closed();
    test_backend_replay_output_invalidation_and_move_preserve_one_owner();

    if (failures != 0) {
        std::cerr << failures
                  << " map selector domain-authorization assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout
        << "PASS: 10 map selector domain-authorization scenario groups\n";
    return EXIT_SUCCESS;
}
