#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "opentrail/map_activation_guard.hpp"
#include "opentrail/map_selector_checkpoint.hpp"
#include "opentrail/map_selector_domain_authorization.hpp"
#include "opentrail/map_selector_domain_provisioner.hpp"
#include "opentrail/map_selector_domain_record.hpp"
#include "opentrail/map_selector_domain_store.hpp"
#include "opentrail/map_selector_store.hpp"

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

struct MutationOrder {
    std::uint32_t next{0};
    std::uint32_t domain_commit{0};
    std::uint32_t selector_erase{0};
    std::uint32_t protected_establish{0};

    std::uint32_t mark() {
        return ++next;
    }
};

MapSelectorDomainId domain(std::uint8_t seed = 1) {
    MapSelectorDomainId value{};
    for (std::size_t index = 0; index < value.size(); ++index) {
        value[index] = static_cast<std::uint8_t>(seed + index);
    }
    return value;
}

class FakeDomainStorage final : public MapSelectorDomainStorage {
public:
    MapSelectorDomainStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) override {
        ++read_calls;
        if (slot >= slots.size() || output == nullptr ||
            size != kMapSelectorDomainRecordBytes) {
            return MapSelectorDomainStorageError::invalid_argument;
        }
        if (fail_read_slot == static_cast<int>(slot)) {
            return MapSelectorDomainStorageError::io_failure;
        }
        if (!present[slot]) {
            return MapSelectorDomainStorageError::not_found;
        }
        std::copy(slots[slot].begin(), slots[slot].end(), output);
        return MapSelectorDomainStorageError::none;
    }

    MapSelectorDomainStorageError write_slot(
        std::uint8_t slot,
        const std::uint8_t* data,
        std::size_t size) override {
        if (slot >= slots.size() || data == nullptr ||
            size != kMapSelectorDomainRecordBytes) {
            return MapSelectorDomainStorageError::invalid_argument;
        }
        ++write_calls;
        present[slot] = true;
        if (partial_write_bytes < size) {
            slots[slot].fill(0);
            std::copy(
                data, data + partial_write_bytes, slots[slot].begin());
            partial_write_bytes = size;
            return MapSelectorDomainStorageError::io_failure;
        }
        std::copy(data, data + size, slots[slot].begin());
        return MapSelectorDomainStorageError::none;
    }

    MapSelectorDomainStorageError commit_slot(
        std::uint8_t slot,
        std::size_t offset,
        std::uint8_t value) override {
        if (slot >= slots.size() || !present[slot] ||
            offset != kMapSelectorDomainRecordCommitOffset ||
            value != kMapSelectorDomainRecordCommitMarker) {
            return MapSelectorDomainStorageError::invalid_argument;
        }
        ++commit_calls;
        if (order != nullptr && order->domain_commit == 0) {
            order->domain_commit = order->mark();
        }
        if (fail_commit_slot == static_cast<int>(slot)) {
            if (commit_then_fail) {
                slots[slot][offset] = value;
            }
            return MapSelectorDomainStorageError::io_failure;
        }
        slots[slot][offset] = value;
        return MapSelectorDomainStorageError::none;
    }

    void seed(
        std::uint8_t slot,
        const std::array<
            std::uint8_t,
            kMapSelectorDomainRecordBytes>& bytes) {
        slots[slot] = bytes;
        present[slot] = true;
    }

    std::array<
        std::array<std::uint8_t, kMapSelectorDomainRecordBytes>,
        kMapSelectorDomainSlotCount>
        slots{};
    std::array<bool, kMapSelectorDomainSlotCount> present{};
    MutationOrder* order{nullptr};
    int fail_read_slot{-1};
    int fail_commit_slot{-1};
    std::size_t partial_write_bytes{kMapSelectorDomainRecordBytes};
    bool commit_then_fail{false};
    std::uint32_t read_calls{0};
    std::uint32_t write_calls{0};
    std::uint32_t commit_calls{0};
};

class FakeSelectorStorage final : public MapSelectorStorage {
public:
    MapSelectorStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) override {
        ++read_calls;
        if (inject_on_read_call != 0 &&
            read_calls == inject_on_read_call) {
            slots[0] = injected_bytes;
            present[0] = true;
        }
        if (slot >= slots.size() || output == nullptr ||
            size != kMapSelectorCheckpointBytes) {
            return MapSelectorStorageError::invalid_argument;
        }
        if (fail_read_slot == static_cast<int>(slot)) {
            return MapSelectorStorageError::io_failure;
        }
        if (!present[slot]) {
            return MapSelectorStorageError::not_found;
        }
        std::copy(slots[slot].begin(), slots[slot].end(), output);
        return MapSelectorStorageError::none;
    }

    MapSelectorStorageError write_slot(
        std::uint8_t slot,
        const std::uint8_t* data,
        std::size_t size) override {
        if (slot >= slots.size() || data == nullptr ||
            size != kMapSelectorCheckpointBytes) {
            return MapSelectorStorageError::invalid_argument;
        }
        ++write_calls;
        std::copy(data, data + size, slots[slot].begin());
        present[slot] = true;
        return MapSelectorStorageError::none;
    }

    MapSelectorStorageError commit_slot(
        std::uint8_t slot,
        std::size_t offset,
        std::uint8_t value) override {
        if (slot >= slots.size() || !present[slot] ||
            offset != kMapSelectorCommitOffset ||
            value != kMapSelectorCommitMarker) {
            return MapSelectorStorageError::invalid_argument;
        }
        slots[slot][offset] = value;
        return MapSelectorStorageError::none;
    }

    MapSelectorStorageError erase_slot(std::uint8_t slot) override {
        if (slot >= slots.size()) {
            return MapSelectorStorageError::invalid_argument;
        }
        ++erase_calls;
        if (order != nullptr && order->selector_erase == 0) {
            order->selector_erase = order->mark();
        }
        if (fail_erase_slot == static_cast<int>(slot)) {
            return MapSelectorStorageError::io_failure;
        }
        if (pretend_erase_success_slot == static_cast<int>(slot)) {
            return MapSelectorStorageError::none;
        }
        present[slot] = false;
        slots[slot].fill(0);
        return MapSelectorStorageError::none;
    }

    void seed(
        std::uint8_t slot,
        const std::array<std::uint8_t, kMapSelectorCheckpointBytes>& bytes) {
        slots[slot] = bytes;
        present[slot] = true;
    }

    std::array<std::array<std::uint8_t, kMapSelectorCheckpointBytes>, 2>
        slots{};
    std::array<bool, 2> present{};
    MutationOrder* order{nullptr};
    int fail_read_slot{-1};
    int fail_erase_slot{-1};
    int pretend_erase_success_slot{-1};
    std::uint32_t inject_on_read_call{0};
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> injected_bytes{};
    std::uint32_t read_calls{0};
    std::uint32_t write_calls{0};
    std::uint32_t erase_calls{0};
};

class FakeProtectedSource final
    : public MapSelectorDomainProtectedSource {
public:
    MapSelectorDomainProtectedSourceRead read() override {
        ++read_calls;
        if (fail_read_call == static_cast<int>(read_calls)) {
            return {
                read_error,
                MapSelectorDomainProtectedSourceState::unknown,
                {},
                0};
        }
        auto visible_domain = current_domain;
        auto visible_generation = selector_generation;
        if (wrong_readback && establish_calls != 0) {
            visible_domain = domain(201);
            visible_generation = 9;
        }
        return {
            MapSelectorDomainProtectedSourceError::none,
            state,
            visible_domain,
            visible_generation};
    }

    MapSelectorDomainProtectedSourceError establish_fresh_domain(
        const MapSelectorDomainProtectedEstablishRequest& request) override {
        ++establish_calls;
        last_request = request;
        if (order != nullptr && order->protected_establish == 0) {
            order->protected_establish = order->mark();
        }
        if (establish_error ==
                MapSelectorDomainProtectedSourceError::none ||
            apply_then_fail) {
            state = MapSelectorDomainProtectedSourceState::ready;
            current_domain = request.proposed_domain;
            selector_generation = 0;
        }
        return establish_error;
    }

    void set_ready(
        const MapSelectorDomainId& value,
        std::uint64_t generation = 0) {
        state = MapSelectorDomainProtectedSourceState::ready;
        current_domain = value;
        selector_generation = generation;
    }

    MutationOrder* order{nullptr};
    MapSelectorDomainProtectedSourceState state{
        MapSelectorDomainProtectedSourceState::uninitialized};
    MapSelectorDomainId current_domain{};
    std::uint64_t selector_generation{0};
    MapSelectorDomainProtectedSourceError establish_error{
        MapSelectorDomainProtectedSourceError::none};
    MapSelectorDomainProtectedSourceError read_error{
        MapSelectorDomainProtectedSourceError::io_failure};
    int fail_read_call{-1};
    bool apply_then_fail{false};
    bool wrong_readback{false};
    std::uint32_t read_calls{0};
    std::uint32_t establish_calls{0};
    MapSelectorDomainProtectedEstablishRequest last_request{};
};

MapSelectorDomainAuthorizationScope scope_for(
    const MapSelectorDomainAuthorizationBinding& binding) {
    return binding.request ==
                   MapSelectorResetRequest::protected_source_replacement
               ? MapSelectorDomainAuthorizationScope::
                     replace_same_device_domain
               : MapSelectorDomainAuthorizationScope::
                     commission_new_device_domain;
}

class FakeAuthorizationBackend final
    : public MapSelectorDomainAuthorizationBackend {
public:
    explicit FakeAuthorizationBackend(
        const MapSelectorDomainAuthorizationBinding& operation_binding)
        : binding(operation_binding) {}

    MapSelectorDomainAuthorizationGrant verify_and_consume(
        std::uint64_t authorization_handle,
        const MapSelectorDomainAuthorizationBinding&) override {
        if (consumed) {
            return {};
        }
        consumed = true;
        return {
            MapSelectorDomainAuthorizationBackendState::authorized,
            scope_for(binding),
            MapSelectorDomainServiceTransport::local_usb,
            authorization_handle,
            7,
            50,
            150,
            1,
            binding,
            {true, true, true, true, true, true}};
    }

    MapSelectorDomainAuthorizationBinding binding{};
    bool consumed{false};
};

MapSelectorDomainAuthorizationPermit authorized_permit(
    const MapSelectorDomainAuthorizationBinding& binding) {
    FakeAuthorizationBackend backend{binding};
    MapSelectorDomainAuthorizer authorizer{backend};
    MapSelectorDomainAuthorizationPermit permit{};
    const auto result = authorizer.authorize(
        {100}, {91, 7, 100}, binding, permit);
    EXPECT(result.authorized());
    EXPECT(permit.available());
    return permit;
}

MapSelectorDomainAuthorizationBinding commission_binding(
    std::uint8_t proposed_seed = 41) {
    return {
        MapSelectorResetRequest::whole_device_replacement,
        MapSelectorLifecycleState::new_device_unprovisioned,
        MapSelectorDomainMediaState::verified_empty,
        0,
        {},
        domain(proposed_seed)};
}

MapSelectorDomainAuthorizationBinding replacement_binding(
    MapSelectorDomainMediaState media_state =
        MapSelectorDomainMediaState::retained_quarantined,
    std::uint64_t reviewed_generation = 5,
    std::uint8_t retired_seed = 1,
    std::uint8_t proposed_seed = 41) {
    return {
        MapSelectorResetRequest::protected_source_replacement,
        MapSelectorLifecycleState::same_device_source_missing_or_replaced,
        media_state,
        reviewed_generation,
        domain(retired_seed),
        domain(proposed_seed)};
}

MapActivationPolicy activation_policy() {
    return {8U * 1024U * 1024U, 500, 3, 3};
}

MapPackageEvidence package() {
    return {
        MapSlot::slot_a,
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

MapActivationGuard active_guard() {
    MapActivationGuard guard{};
    EXPECT(guard.start(
               activation_policy(),
               {MapSelectorState::valid, package()}) ==
           MapActivationError::none);
    return guard;
}

MapActivationGuard mapless_guard() {
    MapActivationGuard guard{};
    EXPECT(guard.start(
               activation_policy(),
               {MapSelectorState::unreadable, {}}) ==
           MapActivationError::none);
    EXPECT(guard.status().state == MapActivationState::mapless);
    return guard;
}

MapSelectorDomainRecord active_domain(
    std::uint64_t accepted_selector_generation = 5,
    std::uint64_t record_generation = 2) {
    return {
        kMapSelectorDomainRecordVersion,
        MapSelectorDomainRecordState::active,
        MapSelectorDomainRecordOrigin::fresh_device_commissioning,
        domain(),
        {},
        0,
        accepted_selector_generation,
        1,
        record_generation};
}

std::array<std::uint8_t, kMapSelectorDomainRecordBytes> encoded_domain(
    const MapSelectorDomainRecord& record) {
    std::array<std::uint8_t, kMapSelectorDomainRecordBytes> bytes{};
    EXPECT(encode_map_selector_domain_record(
               record, bytes.data(), bytes.size())
               .succeeded());
    return bytes;
}

std::array<std::uint8_t, kMapSelectorCheckpointBytes> encoded_selector(
    std::uint64_t record_generation) {
    const auto guard = active_guard();
    MapSelectorCheckpoint checkpoint{};
    EXPECT(guard.export_checkpoint(record_generation, checkpoint) ==
           MapActivationError::none);
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> bytes{};
    EXPECT(encode_map_selector_checkpoint(
               checkpoint, bytes.data(), bytes.size())
               .succeeded());
    return bytes;
}

struct Fixture {
    Fixture()
        : domain_store(domain_storage),
          selector_store(selector_storage),
          provisioner(domain_store, selector_store, protected_source) {
        domain_storage.order = &order;
        selector_storage.order = &order;
        protected_source.order = &order;
    }

    MutationOrder order{};
    FakeDomainStorage domain_storage{};
    FakeSelectorStorage selector_storage{};
    FakeProtectedSource protected_source{};
    MapSelectorDomainStore domain_store;
    MapSelectorStore selector_store;
    MapSelectorDomainProvisioner provisioner;
};

MapSelectorDomainProvisionContext context(
    std::uint64_t boot = 7,
    std::uint64_t use_time = 100) {
    return {boot, use_time};
}

void seed_replacement_state(
    Fixture& fixture,
    std::uint64_t accepted_generation = 5,
    std::uint64_t selector_generation = 5) {
    fixture.domain_storage.seed(
        0, encoded_domain(active_domain(accepted_generation)));
    fixture.selector_storage.seed(
        0, encoded_selector(selector_generation));
}

void test_fresh_commissioning_persists_pending_before_source() {
    Fixture fixture{};
    auto binding = commission_binding();
    auto permit = authorized_permit(binding);
    MapActivationGuard guard{};
    const auto result = fixture.provisioner.provision(
        guard, context(), binding, permit);

    EXPECT(result.prepared());
    EXPECT(result.authorization_consumed);
    EXPECT(result.scope == MapSelectorDomainAuthorizationScope::
                               commission_new_device_domain);
    EXPECT(result.domain_record_generation == 1);
    EXPECT(result.domain_epoch == 1);
    EXPECT(result.retired_selector_floor == 0);
    EXPECT(!result.selector_clear_attempted);
    EXPECT(result.protected_source_called);
    EXPECT(fixture.order.domain_commit != 0);
    EXPECT(fixture.order.domain_commit <
           fixture.order.protected_establish);
    EXPECT(guard.status().state == MapActivationState::stopped);

    const auto stored = fixture.domain_store.inspect();
    EXPECT(stored.record_available);
    EXPECT(stored.record.state ==
           MapSelectorDomainRecordState::pending_first_baseline);
    EXPECT(stored.record.current_domain == binding.proposed_domain);
    EXPECT(stored.record.record_generation == 1);

    const auto reads_before = fixture.domain_storage.read_calls;
    const auto replay = fixture.provisioner.provision(
        guard, context(), binding, permit);
    EXPECT(replay.reason == MapSelectorDomainProvisionReason::
                                authorization_already_consumed);
    EXPECT(fixture.domain_storage.read_calls == reads_before);
}

void test_retained_replacement_orders_record_clear_and_source() {
    Fixture fixture{};
    seed_replacement_state(fixture);
    auto binding = replacement_binding();
    auto permit = authorized_permit(binding);
    auto guard = mapless_guard();
    const auto result = fixture.provisioner.provision(
        guard, context(), binding, permit);

    EXPECT(result.prepared());
    EXPECT(result.selector_clear_attempted);
    EXPECT(result.selector_reset.cleared());
    EXPECT(result.retired_selector_floor == 5);
    EXPECT(result.domain_record_generation == 3);
    EXPECT(result.domain_epoch == 2);
    EXPECT(fixture.order.domain_commit < fixture.order.selector_erase);
    EXPECT(fixture.order.selector_erase <
           fixture.order.protected_establish);
    EXPECT(!fixture.selector_storage.present[0]);
    EXPECT(!fixture.selector_storage.present[1]);
    EXPECT(fixture.protected_source.last_request.scope ==
           MapSelectorDomainAuthorizationScope::
               replace_same_device_domain);
    EXPECT(fixture.protected_source.last_request.retired_domain ==
           binding.retired_domain);

    const auto stored = fixture.domain_store.inspect();
    EXPECT(stored.record.state ==
           MapSelectorDomainRecordState::pending_selector_reseed);
    EXPECT(stored.record.current_domain == binding.proposed_domain);
    EXPECT(stored.record.retired_domain == binding.retired_domain);
}

void test_empty_replacement_retains_prior_accepted_floor() {
    Fixture fixture{};
    fixture.domain_storage.seed(0, encoded_domain(active_domain(7)));
    auto binding = replacement_binding(
        MapSelectorDomainMediaState::verified_empty, 0);
    auto permit = authorized_permit(binding);
    auto guard = mapless_guard();
    const auto result = fixture.provisioner.provision(
        guard, context(), binding, permit);

    EXPECT(result.prepared());
    EXPECT(result.retired_selector_floor == 7);
    EXPECT(!result.selector_clear_attempted);
    EXPECT(fixture.selector_storage.erase_calls == 0);
    EXPECT(fixture.domain_store.inspect()
               .record.retired_selector_generation == 7);
}

void test_invalid_permit_use_burns_before_any_io() {
    Fixture fixture{};
    auto binding = commission_binding();
    MapActivationGuard guard{};
    MapSelectorDomainAuthorizationPermit missing{};
    const auto unavailable = fixture.provisioner.provision(
        guard, context(), binding, missing);
    EXPECT(unavailable.reason ==
           MapSelectorDomainProvisionReason::authorization_required);

    auto wrong_binding_permit = authorized_permit(binding);
    auto changed = commission_binding(61);
    const auto mismatched = fixture.provisioner.provision(
        guard, context(), changed, wrong_binding_permit);
    EXPECT(mismatched.reason == MapSelectorDomainProvisionReason::
                                    authorization_binding_mismatch);
    EXPECT(mismatched.authorization_consumed);

    auto boot_permit = authorized_permit(binding);
    EXPECT(fixture.provisioner
               .provision(guard, context(8, 100), binding, boot_permit)
               .reason == MapSelectorDomainProvisionReason::
                              authorization_boot_session_mismatch);
    auto early_permit = authorized_permit(binding);
    EXPECT(fixture.provisioner
               .provision(guard, context(7, 49), binding, early_permit)
               .reason == MapSelectorDomainProvisionReason::
                              authorization_not_yet_valid);
    auto expired_permit = authorized_permit(binding);
    EXPECT(fixture.provisioner
               .provision(guard, context(7, 150), binding, expired_permit)
               .reason ==
           MapSelectorDomainProvisionReason::authorization_expired);
    EXPECT(fixture.domain_storage.read_calls == 0);
    EXPECT(fixture.selector_storage.read_calls == 0);
    EXPECT(fixture.protected_source.read_calls == 0);
}

void test_live_map_rejection_burns_permit_without_storage() {
    Fixture fixture{};
    auto binding = commission_binding();
    auto permit = authorized_permit(binding);
    auto guard = active_guard();
    const auto result = fixture.provisioner.provision(
        guard, context(), binding, permit);
    EXPECT(result.reason ==
           MapSelectorDomainProvisionReason::map_unavailable_required);
    EXPECT(result.authorization_consumed);
    EXPECT(result.map_exposure_allowed);
    EXPECT(guard.status().state == MapActivationState::active);
    EXPECT(fixture.domain_storage.read_calls == 0);
    EXPECT(fixture.selector_storage.read_calls == 0);
    EXPECT(fixture.protected_source.read_calls == 0);
}

void test_domain_or_selector_preflight_mismatch_stops_before_write() {
    Fixture wrong_domain{};
    wrong_domain.domain_storage.seed(
        0, encoded_domain(active_domain()));
    auto domain_binding = replacement_binding(
        MapSelectorDomainMediaState::verified_empty, 0, 81, 101);
    auto domain_permit = authorized_permit(domain_binding);
    auto domain_guard = mapless_guard();
    const auto domain_result = wrong_domain.provisioner.provision(
        domain_guard, context(), domain_binding, domain_permit);
    EXPECT(domain_result.reason ==
           MapSelectorDomainProvisionReason::domain_state_mismatch);
    EXPECT(wrong_domain.domain_storage.write_calls == 0);
    EXPECT(wrong_domain.selector_storage.read_calls == 0);
    EXPECT(wrong_domain.protected_source.read_calls == 0);

    Fixture wrong_selector{};
    seed_replacement_state(wrong_selector, 5, 6);
    auto selector_binding = replacement_binding();
    auto selector_permit = authorized_permit(selector_binding);
    auto selector_guard = mapless_guard();
    const auto selector_result = wrong_selector.provisioner.provision(
        selector_guard, context(), selector_binding, selector_permit);
    EXPECT(selector_result.reason ==
           MapSelectorDomainProvisionReason::selector_state_mismatch);
    EXPECT(wrong_selector.domain_storage.write_calls == 0);
    EXPECT(wrong_selector.protected_source.read_calls == 0);
}

void test_protected_source_conflict_or_failure_prevents_pending_write() {
    Fixture conflict{};
    auto binding = commission_binding();
    conflict.protected_source.set_ready(domain(81));
    auto permit = authorized_permit(binding);
    MapActivationGuard guard{};
    const auto result = conflict.provisioner.provision(
        guard, context(), binding, permit);
    EXPECT(result.reason ==
           MapSelectorDomainProvisionReason::protected_source_conflict);
    EXPECT(conflict.domain_storage.write_calls == 0);

    Fixture unavailable{};
    unavailable.protected_source.fail_read_call = 1;
    unavailable.protected_source.read_error =
        MapSelectorDomainProtectedSourceError::not_ready;
    auto unavailable_permit = authorized_permit(binding);
    MapActivationGuard unavailable_guard{};
    const auto unavailable_result = unavailable.provisioner.provision(
        unavailable_guard, context(), binding, unavailable_permit);
    EXPECT(unavailable_result.reason ==
           MapSelectorDomainProvisionReason::protected_source_unavailable);
    EXPECT(unavailable.domain_storage.write_calls == 0);

    Fixture reordered{};
    reordered.protected_source.set_ready(binding.proposed_domain);
    auto reordered_permit = authorized_permit(binding);
    MapActivationGuard reordered_guard{};
    const auto reordered_result = reordered.provisioner.provision(
        reordered_guard, context(), binding, reordered_permit);
    EXPECT(reordered_result.reason ==
           MapSelectorDomainProvisionReason::protected_source_conflict);
    EXPECT(reordered.domain_storage.write_calls == 0);
}

void test_domain_commit_failure_never_touches_selector_or_source() {
    Fixture fixture{};
    fixture.domain_storage.fail_commit_slot = 0;
    auto binding = commission_binding();
    auto permit = authorized_permit(binding);
    MapActivationGuard guard{};
    const auto result = fixture.provisioner.provision(
        guard, context(), binding, permit);
    EXPECT(result.state ==
           MapSelectorDomainProvisionState::reconciliation_required);
    EXPECT(result.reason ==
           MapSelectorDomainProvisionReason::domain_commit_uncertain);
    EXPECT(result.pending_commit_uncertain);
    EXPECT(!result.pending_record_persisted);
    EXPECT(fixture.selector_storage.erase_calls == 0);
    EXPECT(fixture.protected_source.establish_calls == 0);
}

void test_selector_clear_failure_leaves_pending_and_source_untouched() {
    Fixture fixture{};
    seed_replacement_state(fixture);
    fixture.selector_storage.fail_erase_slot = 1;
    auto binding = replacement_binding();
    auto permit = authorized_permit(binding);
    auto guard = mapless_guard();
    const auto result = fixture.provisioner.provision(
        guard, context(), binding, permit);
    EXPECT(result.state ==
           MapSelectorDomainProvisionState::reconciliation_required);
    EXPECT(result.reason ==
           MapSelectorDomainProvisionReason::selector_clear_failed);
    EXPECT(result.pending_record_persisted);
    EXPECT(result.selector_clear_attempted);
    EXPECT(fixture.protected_source.establish_calls == 0);
    EXPECT(fixture.domain_store.inspect().record.state ==
           MapSelectorDomainRecordState::pending_selector_reseed);
}

void test_source_failure_resumes_from_pending_with_new_permit() {
    Fixture fixture{};
    seed_replacement_state(fixture);
    fixture.protected_source.establish_error =
        MapSelectorDomainProtectedSourceError::io_failure;
    auto retained_binding = replacement_binding();
    auto first_permit = authorized_permit(retained_binding);
    auto guard = mapless_guard();
    const auto failed = fixture.provisioner.provision(
        guard, context(), retained_binding, first_permit);
    EXPECT(failed.state ==
           MapSelectorDomainProvisionState::reconciliation_required);
    EXPECT(failed.pending_record_persisted);
    EXPECT(failed.selector_empty_verified);
    EXPECT(fixture.protected_source.establish_calls == 1);
    const auto commits = fixture.domain_storage.commit_calls;

    fixture.protected_source.establish_error =
        MapSelectorDomainProtectedSourceError::none;
    auto empty_binding = replacement_binding(
        MapSelectorDomainMediaState::verified_empty, 0);
    auto retry_permit = authorized_permit(empty_binding);
    const auto retried = fixture.provisioner.provision(
        guard, context(), empty_binding, retry_permit);
    EXPECT(retried.prepared());
    EXPECT(retried.resumed_pending_record);
    EXPECT(fixture.domain_storage.commit_calls == commits);
    EXPECT(fixture.protected_source.establish_calls == 2);
}

void test_applied_then_failed_source_is_reconciled_without_reapply() {
    Fixture fixture{};
    fixture.protected_source.establish_error =
        MapSelectorDomainProtectedSourceError::io_failure;
    fixture.protected_source.apply_then_fail = true;
    auto binding = commission_binding();
    auto first_permit = authorized_permit(binding);
    MapActivationGuard guard{};
    const auto failed = fixture.provisioner.provision(
        guard, context(), binding, first_permit);
    EXPECT(failed.state ==
           MapSelectorDomainProvisionState::reconciliation_required);
    EXPECT(fixture.protected_source.state ==
           MapSelectorDomainProtectedSourceState::ready);
    EXPECT(fixture.protected_source.establish_calls == 1);

    fixture.protected_source.establish_error =
        MapSelectorDomainProtectedSourceError::none;
    fixture.protected_source.apply_then_fail = false;
    auto retry_permit = authorized_permit(binding);
    const auto retried = fixture.provisioner.provision(
        guard, context(), binding, retry_permit);
    EXPECT(retried.prepared());
    EXPECT(retried.resumed_pending_record);
    EXPECT(!retried.protected_source_called);
    EXPECT(fixture.protected_source.establish_calls == 1);
}

void test_selector_race_and_source_readback_mismatch_reconcile() {
    Fixture raced{};
    auto binding = commission_binding();
    raced.selector_storage.injected_bytes = encoded_selector(1);
    raced.selector_storage.inject_on_read_call = 3;
    auto raced_permit = authorized_permit(binding);
    MapActivationGuard raced_guard{};
    const auto raced_result = raced.provisioner.provision(
        raced_guard, context(), binding, raced_permit);
    EXPECT(raced_result.reason ==
           MapSelectorDomainProvisionReason::selector_changed);
    EXPECT(raced_result.state ==
           MapSelectorDomainProvisionState::reconciliation_required);
    EXPECT(raced_result.pending_record_persisted);
    EXPECT(raced.protected_source.establish_calls == 0);

    Fixture mismatch{};
    mismatch.protected_source.wrong_readback = true;
    auto mismatch_permit = authorized_permit(binding);
    MapActivationGuard mismatch_guard{};
    const auto mismatch_result = mismatch.provisioner.provision(
        mismatch_guard, context(), binding, mismatch_permit);
    EXPECT(mismatch_result.reason ==
           MapSelectorDomainProvisionReason::protected_readback_mismatch);
    EXPECT(mismatch_result.state ==
           MapSelectorDomainProvisionState::reconciliation_required);
    EXPECT(!mismatch_result.protected_source_verified);
}

void test_generation_exhaustion_and_retained_rollback_fail_closed() {
    Fixture exhausted{};
    exhausted.domain_storage.seed(
        0,
        encoded_domain(active_domain(
            5, std::numeric_limits<std::uint64_t>::max())));
    auto empty_binding = replacement_binding(
        MapSelectorDomainMediaState::verified_empty, 0);
    auto exhausted_permit = authorized_permit(empty_binding);
    auto exhausted_guard = mapless_guard();
    const auto exhausted_result = exhausted.provisioner.provision(
        exhausted_guard, context(), empty_binding, exhausted_permit);
    EXPECT(exhausted_result.reason ==
           MapSelectorDomainProvisionReason::domain_generation_exhausted);
    EXPECT(exhausted.domain_storage.write_calls == 0);

    Fixture rollback{};
    rollback.domain_storage.seed(0, encoded_domain(active_domain(7)));
    rollback.selector_storage.seed(0, encoded_selector(5));
    auto retained_binding = replacement_binding();
    auto rollback_permit = authorized_permit(retained_binding);
    auto rollback_guard = mapless_guard();
    const auto rollback_result = rollback.provisioner.provision(
        rollback_guard, context(), retained_binding, rollback_permit);
    EXPECT(rollback_result.reason ==
           MapSelectorDomainProvisionReason::domain_state_mismatch);
    EXPECT(rollback.domain_storage.write_calls == 0);
    EXPECT(rollback.selector_storage.erase_calls == 0);
}

}  // namespace

int main() {
    test_fresh_commissioning_persists_pending_before_source();
    test_retained_replacement_orders_record_clear_and_source();
    test_empty_replacement_retains_prior_accepted_floor();
    test_invalid_permit_use_burns_before_any_io();
    test_live_map_rejection_burns_permit_without_storage();
    test_domain_or_selector_preflight_mismatch_stops_before_write();
    test_protected_source_conflict_or_failure_prevents_pending_write();
    test_domain_commit_failure_never_touches_selector_or_source();
    test_selector_clear_failure_leaves_pending_and_source_untouched();
    test_source_failure_resumes_from_pending_with_new_permit();
    test_applied_then_failed_source_is_reconciled_without_reapply();
    test_selector_race_and_source_readback_mismatch_reconcile();
    test_generation_exhaustion_and_retained_rollback_fail_closed();

    if (failures != 0) {
        std::cerr << failures
                  << " map selector domain-provisioner assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 13 map selector domain-provisioner scenario groups\n";
    return EXIT_SUCCESS;
}
