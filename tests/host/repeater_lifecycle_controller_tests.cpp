#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <type_traits>

#include "opentrail/repeater_lifecycle_controller.hpp"

namespace allocation_probe {
bool enabled = false;
std::size_t allocations = 0;
}

void* operator new(std::size_t size) {
    if (allocation_probe::enabled) {
        ++allocation_probe::allocations;
    }
    if (void* result = std::malloc(size == 0 ? 1 : size)) {
        return result;
    }
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

namespace {
using namespace opentrail::delivery;
using Fault = RepeaterLifecycleFault;
using State = RepeaterLifecycleState;
using Event = RepeaterLifecycleEvent;
constexpr std::uint64_t kGroup = 0x1001;
constexpr std::uint32_t kEpoch = 7;
constexpr std::uint64_t kSource = 0x2001;
constexpr std::uint64_t kLocal = 0x3001;
constexpr std::uint64_t kDestination = 0x4001;
constexpr std::array<std::uint8_t, 6> kFrame{{0x4f, 0x54, 0x01, 0xaa, 0x00, 0xff}};
int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

bool equal(const RepeaterLifecycleScope& a, const RepeaterLifecycleScope& b) {
    return a.instance_id == b.instance_id && a.generation == b.generation &&
           a.group_context_id == b.group_context_id && a.group_epoch == b.group_epoch;
}
bool equal(const RepeaterLifecycleRole& a, const RepeaterLifecycleRole& b) {
    return a.group_context_id == b.group_context_id && a.group_epoch == b.group_epoch &&
           a.generation == b.generation && a.enabled == b.enabled;
}
bool equal(const RepeaterLifecycleSnapshot& a, const RepeaterLifecycleSnapshot& b) {
    return equal(a.scope, b.scope) && equal(a.role, b.role) &&
           a.deadline_ms == b.deadline_ms &&
           a.readiness_generation == b.readiness_generation &&
           a.membership_current == b.membership_current &&
           a.protected_namespace_verified == b.protected_namespace_verified &&
           a.airtime_ready == b.airtime_ready;
}

enum class WriteFault { none, before_write, possible_commit, readback_error, readback_mismatch };

class FakeRoles final : public RepeaterLifecycleRoleStorage {
public:
    RepeaterRoleStorageError read(RepeaterLifecycleRole& output) noexcept override {
        ++reads;
        if (transaction_valid != nullptr && reads == invalidate_on_read) {
            *transaction_valid = false;
        }
        if (!available || fail_read) {
            fail_read = false;
            return RepeaterRoleStorageError::io_failure;
        }
        if (!present) {
            return RepeaterRoleStorageError::not_found;
        }
        output = role;
        if (mismatch_read) {
            mismatch_read = false;
            ++output.generation;
        } else {
            verified_generation = role.generation;
        }
        return RepeaterRoleStorageError::none;
    }
    RepeaterRoleStorageError write(const RepeaterLifecycleRole& input) noexcept override {
        ++writes;
        const auto fault = next_fault;
        next_fault = WriteFault::none;
        if (!available || fault == WriteFault::before_write) {
            return RepeaterRoleStorageError::io_failure;
        }
        role = input;
        present = true;
        verified_generation = 0;
        ++commits;
        if (transaction_valid != nullptr && invalidate_after_commit) {
            invalidate_after_commit = false;
            *transaction_valid = false;
        }
        if (fault == WriteFault::possible_commit) {
            return RepeaterRoleStorageError::commit_uncertain;
        }
        fail_read = fault == WriteFault::readback_error;
        mismatch_read = fault == WriteFault::readback_mismatch;
        return RepeaterRoleStorageError::none;
    }
    bool present{false};
    bool available{true};
    bool fail_read{false};
    bool mismatch_read{false};
    RepeaterLifecycleRole role{};
    WriteFault next_fault{WriteFault::none};
    std::uint64_t verified_generation{0};
    std::uint32_t reads{0};
    std::uint32_t writes{0};
    std::uint32_t commits{0};
    bool* transaction_valid{nullptr};
    std::uint32_t invalidate_on_read{0};
    bool invalidate_after_commit{false};
};

class FakeReplay final : public DuplicateCheckpointStorage {
public:
    DuplicateCheckpointStorageError read_slot(
        std::uint8_t slot, std::uint8_t* output, std::size_t size) override {
        ++reads;
        if (slot >= 2 || output == nullptr || size != kStoredDuplicateCheckpointBytes) {
            return DuplicateCheckpointStorageError::invalid_argument;
        }
        if (fail_read || fail_read_slot == slot) {
            fail_read = false;
            fail_read_slot = 255;
            return DuplicateCheckpointStorageError::io_failure;
        }
        if (!present[slot]) {
            return DuplicateCheckpointStorageError::not_found;
        }
        std::copy(slots[slot].begin(), slots[slot].end(), output);
        if (mismatch_read) {
            mismatch_read = false;
            output[0] ^= 1;
        }
        return DuplicateCheckpointStorageError::none;
    }
    DuplicateCheckpointStorageError write_slot(
        std::uint8_t slot, const std::uint8_t* input, std::size_t size) override {
        ++writes;
        if (slot >= 2 || input == nullptr || size != kStoredDuplicateCheckpointBytes) {
            return DuplicateCheckpointStorageError::invalid_argument;
        }
        if (role_owner != nullptr &&
            (!role_owner->present || !role_owner->role.enabled ||
             role_owner->verified_generation != role_owner->role.generation)) {
            write_before_verified_role = true;
        }
        const auto fault = next_fault;
        next_fault = WriteFault::none;
        if (fault == WriteFault::before_write) {
            return DuplicateCheckpointStorageError::io_failure;
        }
        std::copy_n(input, size, slots[slot].begin());
        present[slot] = true;
        ++commits;
        if (change_readiness_after_commit != nullptr) {
            ++*change_readiness_after_commit;
            change_readiness_after_commit = nullptr;
        }
        if (fault == WriteFault::possible_commit) {
            return DuplicateCheckpointStorageError::io_failure;
        }
        fail_read = fault == WriteFault::readback_error;
        mismatch_read = fault == WriteFault::readback_mismatch;
        return DuplicateCheckpointStorageError::none;
    }
    DuplicateCheckpointStorageError erase_slot(std::uint8_t slot) override {
        ++erases;
        if (slot >= 2) {
            return DuplicateCheckpointStorageError::invalid_argument;
        }
        present[slot] = false;
        slots[slot] = {};
        return DuplicateCheckpointStorageError::none;
    }
    std::array<std::uint8_t, 2 * (kStoredDuplicateCheckpointBytes + 1)> image() const {
        std::array<std::uint8_t, 2 * (kStoredDuplicateCheckpointBytes + 1)> result{};
        for (std::size_t slot = 0; slot < 2; ++slot) {
            const auto offset = slot * (kStoredDuplicateCheckpointBytes + 1);
            result[offset] = static_cast<std::uint8_t>(present[slot]);
            std::copy(slots[slot].begin(), slots[slot].end(), result.begin() + offset + 1);
        }
        return result;
    }
    std::array<std::array<std::uint8_t, kStoredDuplicateCheckpointBytes>, 2> slots{};
    std::array<bool, 2> present{};
    const FakeRoles* role_owner{nullptr};
    WriteFault next_fault{WriteFault::none};
    bool fail_read{false};
    std::uint8_t fail_read_slot{255};
    bool mismatch_read{false};
    bool write_before_verified_role{false};
    std::uint64_t* change_readiness_after_commit{nullptr};
    std::uint32_t reads{0};
    std::uint32_t writes{0};
    std::uint32_t commits{0};
    std::uint32_t erases{0};
};

class FakeProvider final : public RepeaterLifecycleProvider {
public:
    explicit FakeProvider(FakeRoles& role_store) : roles(role_store) {}

    bool consume_request(RepeaterLifecycleOperation operation, std::uint64_t handle,
                         const RepeaterLifecycleScope& scope, std::uint64_t,
                         RepeaterLifecycleRequest& output) noexcept override {
        ++request_calls;
        if (!available || !request_unused || handle != request_handle ||
            operation != request_operation || !equal(scope, issued_scope)) {
            return false;
        }
        request_unused = false;
        output = request;
        consumed_request = request;
        consumed_handle = handle;
        consumed_operation = operation;
        return true;
    }
    bool request_current(RepeaterLifecycleOperation operation, std::uint64_t handle,
                         const RepeaterLifecycleRequest& value,
                         std::uint64_t now) noexcept override {
        ++request_current_calls;
        return available && transaction_valid && handle == consumed_handle &&
               operation == consumed_operation &&
               equal(value.authority, consumed_request.authority) &&
               value.first_use == consumed_request.first_use &&
               value.enable_role == consumed_request.enable_role &&
               now < value.authority.deadline_ms;
    }
    bool current(const RepeaterLifecycleSnapshot& authority,
                 std::uint64_t now) noexcept override {
        ++current_calls;
        if (mutate_claim_ticket != nullptr) {
            ++mutate_claim_ticket->candidate_id;
            mutate_claim_ticket = nullptr;
        }
        if (reenter_current && controller != nullptr) {
            reenter_current = false;
            reentrant_result = controller->take_candidate(now).error;
        }
        if (shutdown_during_current && controller != nullptr) {
            shutdown_during_current = false;
            observed_transient_state = controller->status().state;
            reentrant_result = controller->shutdown();
        }
        if (change_during_current) {
            change_during_current = false;
            ++expected.readiness_generation;
        }
        return available && equal(authority, expected) &&
               roles.present && equal(roles.role, authority.role) &&
               now < authority.deadline_ms;
    }
    bool consume_consent(std::uint64_t handle,
                         const RepeaterLifecycleSnapshot&,
                         std::uint64_t,
                         RepeaterLifecycleConsent& output) noexcept override {
        ++consent_calls;
        if (!available || !consent_unused || handle != consent_handle) {
            return false;
        }
        consent_unused = false;
        output = consent_value;
        return true;
    }
    bool verify_frame(std::uint64_t handle,
                      const RepeaterLifecycleSnapshot& authority,
                      opentrail::radio::ByteView copy, std::uint64_t,
                      VerifiedForwardingMetadata& output) noexcept override {
        ++frame_calls;
        if (mutate_caller != nullptr) {
            mutate_caller[0] ^= 0xff;
            mutate_caller = nullptr;
        }
        const bool matches = available && frame_authorized && handle == frame_handle &&
                             equal(authority, expected) && copy.size == frame_size &&
                             copy.data != nullptr &&
                             std::equal(expected_frame.begin(), expected_frame.begin() + frame_size,
                                        copy.data);
        if (change_during_frame) {
            change_during_frame = false;
            ++expected.readiness_generation;
        }
        if (!matches) {
            return false;
        }
        output = frame_metadata;
        return true;
    }

    FakeRoles& roles;
    bool available{true};
    bool request_unused{false};
    bool consent_unused{false};
    bool frame_authorized{true};
    bool reenter_current{false};
    bool change_during_current{false};
    bool change_during_frame{false};
    bool transaction_valid{true};
    bool shutdown_during_current{false};
    State observed_transient_state{State::disabled};
    RepeaterLifecycleController* controller{nullptr};
    Fault reentrant_result{Fault::none};
    std::uint8_t* mutate_caller{nullptr};
    std::uint64_t request_handle{100};
    std::uint64_t consent_handle{200};
    std::uint64_t frame_handle{300};
    RepeaterLifecycleOperation request_operation{RepeaterLifecycleOperation::recover};
    RepeaterLifecycleScope issued_scope{};
    RepeaterLifecycleRequest request{};
    RepeaterLifecycleRequest consumed_request{};
    std::uint64_t consumed_handle{0};
    RepeaterLifecycleOperation consumed_operation{RepeaterLifecycleOperation::recover};
    RepeaterLifecycleSnapshot expected{};
    RepeaterLifecycleConsent consent_value{};
    VerifiedForwardingMetadata frame_metadata{};
    std::array<std::uint8_t, opentrail::radio::kMaximumFrameBytes> expected_frame{};
    std::size_t frame_size{0};
    std::uint32_t request_calls{0};
    std::uint32_t current_calls{0};
    std::uint32_t consent_calls{0};
    std::uint32_t frame_calls{0};
    std::uint32_t request_current_calls{0};
    RepeaterLifecycleDispatchTicket* mutate_claim_ticket{nullptr};
};

RepeaterLifecycleConfiguration configuration() {
    return {0x5001, 1, kLocal, kGroup, kEpoch, 10000,
            {1, true, 4, 8, 1000, 100}, 255};
}

struct Harness {
    FakeRoles roles{};
    FakeReplay replay{};
    FakeProvider provider{roles};
    RepeaterLifecycleConfiguration config{configuration()};
    std::optional<RepeaterLifecycleController> controller{};

    Harness() {
        replay.role_owner = &roles;
        roles.transaction_valid = &provider.transaction_valid;
        recreate();
    }
    void recreate() {
        controller.reset();
        controller.emplace(config, provider, roles, replay);
        provider.controller = &*controller;
    }
    std::uint64_t prepare_recover(bool first_use = false, bool enable = false,
                                  std::uint64_t deadline = 10000) {
        const RepeaterLifecycleScope scope{
            config.instance_id, controller->status().generation,
            config.group_context_id, config.group_epoch};
        RepeaterLifecycleRole desired = roles.present
            ? roles.role : RepeaterLifecycleRole{kGroup, kEpoch, 1, true};
        if (enable && roles.present && !roles.role.enabled) {
            desired.enabled = true;
            ++desired.generation;
        }
        provider.expected = {scope, desired, deadline, 11, true, true, true};
        provider.issued_scope = scope;
        provider.request = {provider.expected, first_use, enable};
        provider.request_operation = RepeaterLifecycleOperation::recover;
        provider.request_unused = true;
        provider.transaction_valid = true;
        return ++provider.request_handle;
    }
    std::uint64_t prepare_disable() {
        const RepeaterLifecycleScope scope{
            config.instance_id, controller->status().generation,
            config.group_context_id, config.group_epoch};
        auto desired = roles.role;
        if (desired.enabled) {
            desired.enabled = false;
            ++desired.generation;
        }
        provider.expected = {scope, desired, 10000, 11, false, false, false};
        provider.issued_scope = scope;
        provider.request = {provider.expected, false, false};
        provider.request_operation = RepeaterLifecycleOperation::disable;
        provider.request_unused = true;
        provider.transaction_valid = true;
        return ++provider.request_handle;
    }
    std::uint64_t prepare_consent(std::uint64_t deadline = 0) {
        provider.consent_value = {
            provider.expected.scope, provider.expected.role.generation,
            provider.expected.readiness_generation,
            deadline == 0 ? provider.expected.deadline_ms : deadline};
        provider.consent_unused = true;
        return ++provider.consent_handle;
    }
    void activate(std::uint64_t deadline = 10000) {
        const bool first = !roles.present;
        const auto handle = prepare_recover(first, first, deadline);
        EXPECT(controller->recover(handle, 1) == Fault::none);
        EXPECT(controller->status().state == State::awaiting_consent);
        EXPECT(controller->consent(prepare_consent(), 2) == Fault::none);
        EXPECT(controller->status().state == State::active);
    }
    std::uint64_t prepare_frame(std::uint32_t id = 1,
                                opentrail::radio::ByteView bytes = {kFrame.data(), kFrame.size()}) {
        EXPECT(bytes.size <= provider.expected_frame.size());
        provider.frame_size = bytes.size;
        if (bytes.data != nullptr && bytes.size <= provider.expected_frame.size()) {
            std::copy_n(bytes.data, bytes.size, provider.expected_frame.begin());
        }
        provider.frame_metadata = {kGroup, kSource, kDestination, kEpoch, id, true, true, true};
        return ++provider.frame_handle;
    }
    RepeaterLifecycleAdmission admit(std::uint32_t id = 1, std::uint64_t now = 10) {
        return controller->admit(prepare_frame(id), {kFrame.data(), kFrame.size()}, now);
    }
};

void expect_exact(const RepeaterLifecycleCandidate& result,
                  opentrail::radio::ByteView expected = {kFrame.data(), kFrame.size()}) {
    EXPECT(result.error == Fault::none);
    EXPECT(result.forwarding.has_frame);
    EXPECT(result.forwarding.frame.size == expected.size);
    EXPECT(std::equal(expected.data, expected.data + expected.size,
                      result.forwarding.frame.bytes.begin()));
}

void seed_replay(FakeReplay& storage, std::uint64_t context = kGroup,
                 std::uint32_t epoch = kEpoch, bool two_records = false) {
    const auto* saved_owner = storage.role_owner;
    storage.role_owner = nullptr;
    DuplicateCheckpointStore store{storage, context, epoch};
    DuplicateWindow window{10000};
    EXPECT(store.save(window, 0).saved());
    if (two_records) {
        EXPECT(window.observe({kSource, epoch, 55}, 1).valid());
        EXPECT(store.save(window, 1).saved());
    }
    storage.role_owner = saved_owner;
}

void put_u64(std::uint8_t* output, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        output[index] = static_cast<std::uint8_t>((value >> (index * 8U)) & 0xffU);
    }
}
void seal_crc(std::array<std::uint8_t, kStoredDuplicateCheckpointBytes>& bytes) {
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t index = 0; index < bytes.size() - 4; ++index) {
        crc ^= bytes[index];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
        }
    }
    crc = ~crc;
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[700 + index] = static_cast<std::uint8_t>((crc >> (index * 8U)) & 0xffU);
    }
}

// L1: neither construction nor an invalid configuration grants first-use IO.
void test_disabled_startup_and_configuration() {
    Harness h;
    EXPECT(h.controller->status().state == State::disabled);
    EXPECT(h.controller->take_candidate(0).error == Fault::wrong_state);
    EXPECT(h.admit(1, 0).error == Fault::wrong_state);
    EXPECT(h.replay.reads == 0 && h.replay.writes == 0 && h.roles.writes == 0);
    EXPECT(h.provider.current_calls == 0 && h.provider.frame_calls == 0);
    for (unsigned variant = 0; variant < 16; ++variant) {
        Harness bad;
        switch (variant) {
        case 0: bad.config.instance_id = 0; break;
        case 1: bad.config.initial_generation = 0; break;
        case 2: bad.config.local_alias = 0; break;
        case 3: bad.config.group_context_id = 0; break;
        case 4: bad.config.group_epoch = 0; break;
        case 5: bad.config.duplicate_retention_ms = 0; break;
        case 6: bad.config.policy.maximum_queue_depth = 0; break;
        case 7: bad.config.policy.maximum_queue_depth = 9; break;
        case 8: bad.config.policy.maximum_forwards_per_window = 0; break;
        case 9: bad.config.policy.rate_window_ms = 0; break;
        case 10: bad.config.policy.maximum_queue_age_ms = 0; break;
        case 11: bad.config.policy.configured_authorized_repeaters = 0; break;
        case 12: bad.config.policy.configured_authorized_repeaters = 2; break;
        case 13: bad.config.policy.local_repeater_authorized = false; break;
        case 14: bad.config.maximum_frame_bytes = 0; break;
        default: bad.config.maximum_frame_bytes = 256; break;
        }
        bad.recreate();
        EXPECT(bad.controller->recover(bad.prepare_recover(true, true), 1) != Fault::none);
        EXPECT(bad.controller->status().state != State::active);
        EXPECT(!bad.controller->take_candidate(2).forwarding.has_frame);
        EXPECT(bad.replay.writes == 0 && bad.roles.writes == 0);
    }
}

// L2: real checkpoint refusal is exercised through the supervisor, not mocked.
void test_coherent_recovery_and_retained_media_refusals() {
    Harness good;
    EXPECT(good.controller->recover(good.prepare_recover(true, true), 1) == Fault::none);
    EXPECT(good.controller->status().state == State::awaiting_consent);
    EXPECT(good.roles.commits == 1 && good.replay.commits == 1);
    EXPECT(!good.replay.write_before_verified_role);
    EXPECT(good.replay.reads == 5 && good.replay.writes == 1);
    EXPECT(good.provider.request_current_calls == 3);
    EXPECT(good.provider.current_calls == 2);
    EXPECT(!good.controller->take_candidate(2).forwarding.has_frame);

    for (unsigned variant = 0; variant < 9; ++variant) {
        Harness h;
        h.roles.present = true;
        h.roles.role = {kGroup, kEpoch, 1, true};
        if (variant >= 3) {
            seed_replay(h.replay, variant == 3 ? kGroup + 1 : kGroup,
                        variant == 4 ? kEpoch + 1 : kEpoch, variant == 7);
        }
        if (variant == 5) {
            auto& record = h.replay.slots[0];
            record[4] = 0;
            std::fill(record.begin() + 16, record.begin() + 24, 0);
            std::fill(record.begin() + 696, record.begin() + 700, 0);
            seal_crc(record);
        }
        if (variant == 6) {
            h.replay.fail_read_slot = 1;
        }
        if (variant == 7) {
            put_u64(h.replay.slots[0].data() + 8, 2);
            seal_crc(h.replay.slots[0]);
        }
        if (variant == 8) {
            h.replay.slots[0][0] ^= 1;
        }
        const auto request = h.prepare_recover();
        if (variant == 1) {
            h.provider.request.authority.protected_namespace_verified = false;
        }
        if (variant == 2) {
            h.provider.change_during_current = true;
        }
        const auto image = h.replay.image();
        const auto writes = h.replay.writes;
        EXPECT(h.controller->recover(request, 1) != Fault::none);
        EXPECT(h.controller->status().state != State::awaiting_consent);
        EXPECT(h.controller->status().state != State::active);
        EXPECT(h.replay.writes == writes && h.replay.image() == image);
        EXPECT(!h.controller->take_candidate(2).forwarding.has_frame);
    }
    for (unsigned operation = 0; operation < 2; ++operation) {
        for (unsigned invalid = 0; invalid < 3; ++invalid) {
            Harness h;
            const auto maximum = std::numeric_limits<std::uint64_t>::max();
            const auto retained_generation = invalid == 0 ? 0 :
                (invalid == 1 ? maximum : maximum - 1);
            const auto desired_generation = invalid == 0 ? 1 :
                (invalid == 1 ? maximum - 1 : maximum);
            h.roles.present = true;
            h.roles.role = {kGroup, kEpoch, retained_generation, operation == 1};
            seed_replay(h.replay, kGroup, kEpoch, true);
            const auto image = h.replay.image();
            const auto writes = h.replay.writes;
            const auto role = h.roles.role;
            const auto handle = operation == 0 ? h.prepare_recover(false, true)
                                                : h.prepare_disable();
            h.provider.request.authority.role.generation = desired_generation;
            h.provider.expected = h.provider.request.authority;
            const auto result = operation == 0 ? h.controller->recover(handle, 2)
                                               : h.controller->disable(handle, 2);
            EXPECT(result != Fault::none);
            EXPECT(h.controller->status().state == State::contained);
            EXPECT(h.roles.writes == 0 && equal(h.roles.role, role));
            EXPECT(h.replay.writes == writes && h.replay.image() == image);
        }
    }
}

// L3: consuming a token yields a distinct bounded lease, never a renewable token.
void test_consent_scope_and_no_implicit_restart_resume() {
    for (unsigned variant = 0; variant < 8; ++variant) {
        Harness h;
        EXPECT(h.controller->recover(h.prepare_recover(true, true), 1) == Fault::none);
        const auto handle = h.prepare_consent();
        switch (variant) {
        case 0: ++h.provider.consent_value.scope.instance_id; break;
        case 1: ++h.provider.consent_value.scope.generation; break;
        case 2: ++h.provider.consent_value.scope.group_context_id; break;
        case 3: ++h.provider.consent_value.scope.group_epoch; break;
        case 4: ++h.provider.consent_value.role_generation; break;
        case 5: ++h.provider.consent_value.readiness_generation; break;
        case 6: h.provider.consent_unused = false; break;
        default: h.provider.consent_value.deadline_ms = 2; break;
        }
        EXPECT(h.controller->consent(handle, 2) != Fault::none);
        EXPECT(h.controller->status().state != State::active);
        EXPECT(!h.controller->take_candidate(3).forwarding.has_frame);
    }
    Harness h;
    EXPECT(h.controller->recover(h.prepare_recover(true, true, 100), 1) == Fault::none);
    const auto consent = h.prepare_consent(90);
    EXPECT(h.controller->consent(consent, 2) == Fault::none);
    EXPECT(h.controller->consent(consent, 3) != Fault::none);
    EXPECT(h.controller->status().state == State::active);
    EXPECT(h.admit(1, 10).forwarding.queued);
    EXPECT(!h.controller->take_candidate(90).forwarding.has_frame);
    EXPECT(h.controller->status().state != State::active);
    const auto old_consent = consent;
    ++h.config.instance_id;
    h.recreate();
    EXPECT(h.controller->recover(h.prepare_recover(), 100) == Fault::none);
    EXPECT(h.controller->status().state == State::awaiting_consent);
    EXPECT(h.controller->consent(old_consent, 101) != Fault::none);
    EXPECT(!h.controller->take_candidate(102).forwarding.has_frame);
}

// L4: fixture verification binds the private copied bytes, not a caller flag.
void test_exact_copy_binding_and_admission_order() {
    Harness h;
    h.activate();
    const auto original = h.replay.image();
    const auto writes = h.replay.writes;
    auto caller = kFrame;
    auto handle = h.prepare_frame(1);
    h.provider.expected_frame[0] ^= 1;
    EXPECT(h.controller->admit(handle, {caller.data(), caller.size()}, 10).error != Fault::none);
    EXPECT(h.replay.image() == original && h.replay.writes == writes);
    handle = h.prepare_frame(1);
    EXPECT(h.controller->admit(handle, {caller.data(), caller.size() - 1}, 11).error != Fault::none);
    EXPECT(h.replay.image() == original && h.replay.writes == writes);
    handle = h.prepare_frame(1);
    h.provider.frame_metadata.source_authenticated = false;
    EXPECT(!h.controller->admit(handle, {caller.data(), caller.size()}, 12).forwarding.queued);
    EXPECT(h.replay.image() == original && h.replay.writes == writes);
    handle = h.prepare_frame(1);
    h.provider.mutate_caller = caller.data();
    const auto currents = h.provider.current_calls;
    const auto reads = h.replay.reads;
    const auto accepted = h.controller->admit(handle, {caller.data(), caller.size()}, 13);
    EXPECT(accepted.error == Fault::none && accepted.forwarding.queued);
    EXPECT(accepted.replay_state_persisted);
    EXPECT(caller[0] != kFrame[0]);
    EXPECT(h.provider.current_calls - currents <= 3);
    EXPECT(h.replay.reads - reads == 3 && h.replay.writes == writes + 1);
    expect_exact(h.controller->take_candidate(14));

    Harness stale;
    stale.activate();
    const auto stale_writes = stale.replay.writes;
    stale.provider.change_during_frame = true;
    EXPECT(stale.admit().error != Fault::none);
    EXPECT(stale.replay.writes == stale_writes);
    EXPECT(!stale.controller->take_candidate(11).forwarding.has_frame);

    Harness changed_after_save;
    changed_after_save.activate();
    const auto writes_before = changed_after_save.replay.writes;
    const auto old_generation = changed_after_save.controller->status().generation;
    changed_after_save.replay.change_readiness_after_commit =
        &changed_after_save.provider.expected.readiness_generation;
    const auto persisted_then_stale = changed_after_save.admit(7, 10);
    EXPECT(persisted_then_stale.error == Fault::authority_changed);
    EXPECT(changed_after_save.replay.writes == writes_before + 1);
    EXPECT(changed_after_save.controller->status().state == State::contained);
    EXPECT(changed_after_save.controller->status().generation != old_generation);
    EXPECT(changed_after_save.controller->status().queue_depth == 0);
    EXPECT(!changed_after_save.controller->take_candidate(11).forwarding.has_frame);
    const auto stored_observation = changed_after_save.replay.image();
    const auto readiness = changed_after_save.provider.expected.readiness_generation;
    const auto recovery = changed_after_save.prepare_recover();
    changed_after_save.provider.request.authority.readiness_generation = readiness;
    changed_after_save.provider.expected = changed_after_save.provider.request.authority;
    EXPECT(changed_after_save.controller->recover(recovery, 30) == Fault::none);
    EXPECT(changed_after_save.controller->status().state == State::awaiting_consent);
    EXPECT(!changed_after_save.controller->take_candidate(31).forwarding.has_frame);
    EXPECT(changed_after_save.controller->consent(changed_after_save.prepare_consent(), 32)
           == Fault::none);
    EXPECT(changed_after_save.admit(7, 33).forwarding.disposition ==
           SingleRepeaterDisposition::duplicate);
    EXPECT(changed_after_save.replay.writes == writes_before + 1);
    EXPECT(changed_after_save.replay.image() == stored_observation);
    const auto retired = changed_after_save.controller->take_candidate(34);
    EXPECT(retired.error == Fault::none && !retired.forwarding.has_frame);
    EXPECT(retired.forwarding.expired_frames_dropped == 0);
}

// L5: existing queues have no per-source erase; the whole bundle is retired.
void test_whole_queue_retirement_and_old_notifications() {
    for (unsigned event = 0; event < 4; ++event) {
        Harness h;
        h.activate();
        EXPECT(h.admit().forwarding.queued);
        EXPECT(h.controller->status().queue_depth == 1);
        const auto image = h.replay.image();
        const auto generation = h.controller->status().generation;
        if (event == 0) {
            EXPECT(h.controller->notify(generation, Event::suspend, 20) == Fault::suspended);
        } else if (event == 1) {
            ++h.provider.expected.role.generation;  // membership authority superseded
            EXPECT(h.controller->take_candidate(20).error != Fault::none);
        } else if (event == 2) {
            EXPECT(h.controller->disable(h.prepare_disable(), 20) == Fault::none);
            EXPECT(!h.roles.role.enabled);
        } else {
            ++h.provider.expected.scope.group_context_id;
            EXPECT(h.controller->take_candidate(20).error != Fault::none);
        }
        EXPECT(h.controller->status().generation != generation);
        EXPECT(h.controller->status().queue_depth == 0);
        EXPECT(h.replay.image() == image && h.replay.erases == 0);
        const auto state = h.controller->status().state;
        const auto current_generation = h.controller->status().generation;
        EXPECT(h.controller->notify(generation, Event::suspend, 0) != Fault::none);
        EXPECT(h.controller->status().state == state);
        EXPECT(h.controller->status().generation == current_generation);
        EXPECT(!h.controller->take_candidate(21).forwarding.has_frame);
    }
}

// L6: clear readiness does not resume; explicit recovery still loses RAM work.
void test_explicit_resume_preserves_saved_and_denied_replay() {
    Harness h;
    h.config.policy.maximum_queue_depth = 2;
    h.config.policy.maximum_forwards_per_window = 1;
    h.recreate();
    h.activate();
    EXPECT(h.admit(1, 10).forwarding.queued);
    const auto denied = h.admit(2, 11);
    EXPECT(denied.forwarding.disposition == SingleRepeaterDisposition::rate_limited);
    EXPECT(denied.replay_state_persisted);
    const auto image = h.replay.image();
    const auto writes = h.replay.writes;
    const auto generation = h.controller->status().generation;
    EXPECT(h.controller->notify(generation, Event::radio_unavailable, 20) != Fault::none);
    h.provider.expected.airtime_ready = true;
    EXPECT(!h.controller->take_candidate(21).forwarding.has_frame);
    EXPECT(h.controller->status().state == State::suspended);
    EXPECT(h.replay.image() == image && h.replay.writes == writes);
    EXPECT(h.controller->recover(h.prepare_recover(), 30) == Fault::none);
    EXPECT(h.controller->status().state == State::awaiting_consent);
    EXPECT(!h.controller->take_candidate(31).forwarding.has_frame);
    EXPECT(h.controller->consent(h.prepare_consent(), 32) == Fault::none);
    EXPECT(h.controller->status().queue_depth == 0);
    EXPECT(!h.controller->take_candidate(33).forwarding.has_frame);
    EXPECT(h.admit(1, 34).forwarding.disposition == SingleRepeaterDisposition::duplicate);
    EXPECT(h.admit(2, 35).forwarding.disposition == SingleRepeaterDisposition::duplicate);
    EXPECT(h.replay.writes == writes && h.replay.image() == image);
    EXPECT(!h.controller->take_candidate(36).forwarding.has_frame);
}

// L7: stored bytes, not requested outcomes, decide explicit reconciliation.
void test_role_and_replay_interruption_seams() {
    const std::array<WriteFault, 4> faults{{WriteFault::before_write,
        WriteFault::possible_commit, WriteFault::readback_error, WriteFault::readback_mismatch}};
    for (const auto fault : faults) {
        Harness first;
        first.roles.next_fault = fault;
        EXPECT(first.controller->recover(first.prepare_recover(true, true), 1) != Fault::none);
        EXPECT(first.controller->status().state == State::contained);
        EXPECT(first.roles.writes == 1);
        EXPECT(first.replay.writes == 0 && !first.replay.write_before_verified_role);
        EXPECT(!first.controller->take_candidate(2).forwarding.has_frame);
        EXPECT(first.controller->recover(first.prepare_recover(true, true), 3) != Fault::none);
        EXPECT(first.replay.writes == 0);

        Harness h;
        h.activate();
        EXPECT(h.admit().forwarding.queued);
        const auto image = h.replay.image();
        h.roles.next_fault = fault;
        EXPECT(h.controller->disable(h.prepare_disable(), 20) != Fault::none);
        EXPECT(h.controller->status().state == State::contained);
        EXPECT(h.controller->status().queue_depth == 0);
        EXPECT(h.replay.image() == image);
        EXPECT(!h.controller->take_candidate(21).forwarding.has_frame);
        // Before-write failure leaves enabled role; possible commit/readback
        // failures retain disabled role and need an explicit next-generation enable.
        const bool needs_enable = !h.roles.role.enabled;
        EXPECT(h.controller->recover(h.prepare_recover(false, needs_enable), 30) == Fault::none);
        EXPECT(h.controller->consent(h.prepare_consent(), 31) == Fault::none);
        const auto writes = h.replay.writes;
        EXPECT(h.admit(1, 32).forwarding.disposition == SingleRepeaterDisposition::duplicate);
        EXPECT(h.replay.writes == writes && h.replay.image() == image);
    }
    for (const auto fault : faults) {
        Harness h;
        h.activate();
        h.replay.next_fault = fault;
        EXPECT(h.admit().error == Fault::replay_storage);
        EXPECT(h.controller->status().state == State::contained);
        EXPECT(h.controller->status().queue_depth == 0);
        EXPECT(!h.controller->take_candidate(11).forwarding.has_frame);
        EXPECT(h.replay.erases == 0);
    }
    for (unsigned operation = 0; operation < 2; ++operation) {
        for (unsigned seam = 0; seam < 2; ++seam) {
            Harness h;
            if (operation == 1) {
                h.activate();
                EXPECT(h.admit().forwarding.queued);
            }
            const auto image = h.replay.image();
            const auto role_writes = h.roles.writes;
            const auto replay_writes = h.replay.writes;
            const auto handle = operation == 0 ? h.prepare_recover(true, true)
                                               : h.prepare_disable();
            if (seam == 0) {
                h.roles.invalidate_on_read = h.roles.reads + 1;
            } else {
                h.roles.invalidate_after_commit = true;
            }
            const auto result = operation == 0 ? h.controller->recover(handle, 20)
                                               : h.controller->disable(handle, 20);
            EXPECT(result == Fault::authority_changed);
            EXPECT(h.controller->status().state == State::contained);
            EXPECT(h.roles.writes == role_writes + (seam == 0 ? 0 : 1));
            EXPECT(h.replay.writes == replay_writes && h.replay.image() == image);
            EXPECT(!h.controller->take_candidate(21).forwarding.has_frame);
        }
    }
}

// L8: independent fixtures prevent grant expiry from hiding frame expiry.
void test_original_deadline_queue_expiry_and_clock_rollback() {
    for (std::uint64_t age = 99; age <= 101; ++age) {
        Harness h;
        h.activate(10000);
        EXPECT(h.admit(1, 10).forwarding.queued);
        const auto result = h.controller->take_candidate(10 + age);
        EXPECT(result.error == Fault::none);
        EXPECT(h.controller->status().state == State::active);
        if (age == 99) {
            expect_exact(result);
            EXPECT(result.forwarding.expired_frames_dropped == 0);
        } else {
            EXPECT(!result.forwarding.has_frame);
            EXPECT(result.forwarding.expired_frames_dropped == 1);
        }
        EXPECT(h.controller->status().queue_depth == 0);
    }
    for (std::uint64_t now = 99; now <= 101; ++now) {
        Harness h;
        h.activate(100);
        EXPECT(h.admit(1, 98).forwarding.queued);
        const auto writes = h.replay.writes;
        const auto result = h.controller->take_candidate(now);
        if (now == 99) {
            expect_exact(result);
        } else {
            EXPECT(!result.forwarding.has_frame);
            EXPECT(result.error != Fault::none);
            EXPECT(h.controller->status().state != State::active);
        }
        EXPECT(h.replay.writes == writes);
    }
    Harness rollback;
    rollback.activate();
    EXPECT(rollback.admit(1, 10).forwarding.queued);
    EXPECT(rollback.controller->take_candidate(9).error == Fault::clock_regression);
    EXPECT(rollback.controller->status().state == State::contained);
    EXPECT(!rollback.controller->take_candidate(11).forwarding.has_frame);
}

// L9: these events are injected lifecycle observations, never RF/rearm tests.
void test_radio_fault_and_readiness_are_fail_closed_inputs() {
    const std::array<Event, 3> events{{Event::radio_unavailable,
                                    Event::uncertain_submission, Event::rearm_failed}};
    for (const auto event : events) {
        Harness h;
        h.activate();
        EXPECT(h.admit().forwarding.queued);
        const auto image = h.replay.image();
        const auto generation = h.controller->status().generation;
        EXPECT(h.controller->notify(generation, event, 20) != Fault::none);
        EXPECT(h.controller->status().state ==
               (event == Event::radio_unavailable ? State::suspended : State::contained));
        EXPECT(h.controller->status().queue_depth == 0);
        EXPECT(!h.controller->take_candidate(21).forwarding.has_frame);
        EXPECT(h.controller->notify(generation, event, 22) != Fault::none);
        EXPECT(h.replay.image() == image);
    }
    for (unsigned variant = 0; variant < 3; ++variant) {
        Harness h;
        h.activate();
        EXPECT(h.admit().forwarding.queued);
        if (variant == 0) {
            h.provider.available = false;
        } else if (variant == 1) {
            h.provider.expected.airtime_ready = false;
        } else {
            ++h.provider.expected.readiness_generation;
        }
        EXPECT(!h.controller->take_candidate(20).forwarding.has_frame);
        EXPECT(h.controller->status().state != State::active);
        h.provider.available = true;
        EXPECT(!h.controller->take_candidate(21).forwarding.has_frame);
    }
}

// L10: include reentrant shutdown from the transient Recovering state.
void test_shutdown_is_terminal_idempotent_and_preserves_media() {
    Harness transient;
    transient.provider.shutdown_during_current = true;
    EXPECT(transient.controller->recover(transient.prepare_recover(true, true), 1) == Fault::none);
    EXPECT(transient.provider.observed_transient_state == State::recovering);
    EXPECT(transient.provider.reentrant_result == Fault::reentrant);
    EXPECT(transient.controller->status().state == State::awaiting_consent);
    EXPECT(transient.controller->shutdown() == Fault::none);
    for (unsigned variant = 0; variant < 6; ++variant) {
        Harness h;
        if (variant == 1) {
            EXPECT(h.controller->recover(h.prepare_recover(true, true), 1) == Fault::none);
        } else if (variant >= 2) {
            h.activate();
            EXPECT(h.admit().forwarding.queued);
            if (variant == 3) {
                EXPECT(h.controller->notify(h.controller->status().generation, Event::suspend, 20)
                       != Fault::none);
            } else if (variant == 4) {
                EXPECT(h.controller->notify(h.controller->status().generation, Event::rearm_failed, 20)
                       != Fault::none);
            } else if (variant == 5) {
                EXPECT(h.controller->shutdown() == Fault::none);
            }
        }
        const auto replay = h.replay.image();
        const auto role = h.roles.role;
        const auto present = h.roles.present;
        const auto writes = h.replay.writes;
        const auto role_writes = h.roles.writes;
        EXPECT(h.controller->shutdown() == Fault::none);
        EXPECT(h.controller->status().state == State::shutdown);
        EXPECT(h.controller->status().queue_depth == 0);
        EXPECT(h.controller->shutdown() == Fault::none);
        EXPECT(h.controller->recover(h.prepare_recover(), 30) != Fault::none);
        EXPECT(h.controller->consent(h.prepare_consent(), 31) != Fault::none);
        EXPECT(h.admit(2, 32).error != Fault::none);
        EXPECT(!h.controller->take_candidate(33).forwarding.has_frame);
        EXPECT(h.controller->disable(h.prepare_disable(), 34) != Fault::none);
        EXPECT(h.controller->notify(h.controller->status().generation, Event::suspend, 35) != Fault::none);
        EXPECT(h.controller->status().state == State::shutdown);
        EXPECT(h.replay.image() == replay && h.replay.writes == writes);
        EXPECT(h.roles.present == present && equal(h.roles.role, role) && h.roles.writes == role_writes);
    }
}

// L11: object lifetime is stable; unavailability is an explicit provider result.
void test_serialization_generation_and_resource_bounds() {
    static_assert(!std::is_copy_constructible_v<RepeaterLifecycleController>);
    static_assert(!std::is_move_constructible_v<RepeaterLifecycleController>);
    Harness h;
    h.activate();
    h.provider.reenter_current = true;
    EXPECT(h.admit().forwarding.queued);
    EXPECT(h.provider.reentrant_result == Fault::reentrant);
    EXPECT(h.controller->status().state == State::active);
    expect_exact(h.controller->take_candidate(11));
    allocation_probe::allocations = 0;
    allocation_probe::enabled = true;
    bool empty = true;
    for (std::uint64_t now = 20; now < 120; ++now) {
        const auto result = h.controller->take_candidate(now);
        empty = empty && result.error == Fault::none && !result.forwarding.has_frame;
    }
    allocation_probe::enabled = false;
    EXPECT(empty && allocation_probe::allocations == 0);

    for (const std::size_t depth : {std::size_t{1}, std::size_t{8}}) {
        Harness queue;
        queue.config.policy.maximum_queue_depth = depth;
        queue.config.policy.maximum_forwards_per_window = 32;
        queue.recreate();
        queue.activate();
        for (std::size_t index = 0; index < depth; ++index) {
            EXPECT(queue.admit(static_cast<std::uint32_t>(index + 1), 10 + index).forwarding.queued);
            EXPECT(queue.controller->status().queue_depth == index + 1);
        }
        const auto full = queue.admit(100, 20);
        EXPECT(full.forwarding.disposition == SingleRepeaterDisposition::queue_full);
        EXPECT(full.replay_state_persisted);
        EXPECT(queue.controller->status().queue_depth == depth);
        for (std::size_t index = 0; index < depth; ++index) {
            expect_exact(queue.controller->take_candidate(30 + index));
        }
        EXPECT(!queue.controller->take_candidate(40).forwarding.has_frame);
        EXPECT(queue.replay.erases == 0);
    }
    Harness exhausted;
    exhausted.config.initial_generation = std::numeric_limits<std::uint64_t>::max() - 1;
    exhausted.recreate();
    exhausted.activate();
    EXPECT(exhausted.controller->notify(exhausted.controller->status().generation,
                                      Event::suspend, 10) == Fault::generation_exhausted);
    EXPECT(exhausted.controller->status().state == State::contained);
    EXPECT(exhausted.controller->recover(exhausted.prepare_recover(), 11) != Fault::none);
    EXPECT(!exhausted.controller->take_candidate(12).forwarding.has_frame);
    Harness maximum;
    maximum.config.initial_generation = std::numeric_limits<std::uint64_t>::max();
    maximum.recreate();
    EXPECT(maximum.controller->status().state == State::contained);
    EXPECT(maximum.controller->status().fault == Fault::generation_exhausted);
    EXPECT(maximum.controller->recover(maximum.prepare_recover(true, true), 1) ==
           Fault::generation_exhausted);
    EXPECT(maximum.roles.writes == 0 && maximum.replay.writes == 0);
}

// L12: the public status surface consists only of explicit category/count fields.
void test_status_privacy_surface_and_maximum_opaque_frame() {
    static_assert(std::is_standard_layout_v<RepeaterLifecycleStatus>);
    static_assert(std::is_trivially_copyable_v<RepeaterLifecycleStatus>);
    static_assert(std::is_same_v<decltype(RepeaterLifecycleStatus::fault), Fault>);
    static_assert(std::is_same_v<decltype(RepeaterLifecycleStatus::state), State>);
    Harness h;
    h.activate();
    std::array<std::uint8_t, 255> payload{};
    constexpr char sentinel[] = "SECRET_KEY IDENTITY PRIVATE_LOCATION OPAQUE_PAYLOAD";
    std::copy(std::begin(sentinel), std::end(sentinel), payload.begin());
    const auto handle = h.prepare_frame(1, {payload.data(), payload.size()});
    EXPECT(h.controller->admit(handle, {payload.data(), payload.size()}, 10).forwarding.queued);
    const auto status = h.controller->status();
    EXPECT(status.queue_depth == 1 && status.state == State::active && status.fault == Fault::none);
    char public_projection[160]{};
    const int size = std::snprintf(public_projection, sizeof(public_projection),
        "state=%u fault=%u generation=%llu checkpoint=%llu depth=%zu",
        static_cast<unsigned>(status.state), static_cast<unsigned>(status.fault),
        static_cast<unsigned long long>(status.generation),
        static_cast<unsigned long long>(status.persisted_generation), status.queue_depth);
    EXPECT(size > 0 && static_cast<std::size_t>(size) < sizeof(public_projection));
    for (const auto* secret : {"SECRET_KEY", "IDENTITY", "PRIVATE_LOCATION", "OPAQUE_PAYLOAD"}) {
        EXPECT(std::strstr(public_projection, secret) == nullptr);
    }
    expect_exact(h.controller->take_candidate(11), {payload.data(), payload.size()});
    const auto writes = h.replay.writes;
    EXPECT(h.controller->admit(0, {nullptr, 1}, 12).error != Fault::none);
    EXPECT(h.controller->admit(0, {payload.data(), 0}, 13).error != Fault::none);
    std::array<std::uint8_t, 256> oversized{};
    EXPECT(h.controller->admit(0, {oversized.data(), oversized.size()}, 14).error != Fault::none);
    EXPECT(h.replay.writes == writes);
    EXPECT(h.replay.erases == 0);
}

class BridgeGate final : public SingleRepeaterAdmissionGate {
public:
    SingleRepeaterAdmission evaluate(const VerifiedForwardingMetadata& input,
                                     std::size_t bytes, std::uint64_t now) noexcept override {
        ++calls;
        seen = input;
        size = bytes;
        if (controller != nullptr) {
            nested = controller->admit(0, {nullptr, 0}, now).error;
        }
        if (change_readiness != nullptr) {
            ++*change_readiness;
            change_readiness = nullptr;
        }
        return result;
    }
    SingleRepeaterAdmission result{SingleRepeaterAdmission::allow};
    std::uint32_t calls{0};
    std::size_t size{0};
    VerifiedForwardingMetadata seen{};
    RepeaterLifecycleController* controller{nullptr};
    Fault nested{Fault::none};
    std::uint64_t* change_readiness{nullptr};
};

class CapturingClaim final : public RepeaterLifecycleClaimCallback {
public:
    void invoke(const RepeaterLifecycleDispatchTicket& ticket,
                opentrail::radio::ByteView borrowed) noexcept override {
        ++calls;
        seen = ticket;
        size = borrowed.size;
        std::copy_n(borrowed.data, borrowed.size, copy.begin());
        if (controller != nullptr) {
            const auto status = controller->status();
            consumed_before_callback = status.claim_in_progress && status.pending;
            nested_claim = controller->claim_dispatch(ticket, now, *this).error;
            nested_prepare = controller->prepare_dispatch(now).error;
            nested_prune = controller->prune_expired(now).error;
            nested_admit = controller->admit(0, {nullptr, 0}, now).error;
            nested_disable = controller->disable(0, now);
            nested_consent = controller->consent(0, now);
            nested_recover = controller->recover(0, now);
            nested_shutdown = controller->shutdown();
            nested_notify = controller->notify(ticket.scope.generation,
                                               Event::uncertain_submission, now);
            bytes_survived_reentry = std::equal(copy.begin(), copy.begin() + size, borrowed.data);
        }
        if (change_readiness != nullptr) { ++*change_readiness; }
    }
    std::uint32_t calls{0};
    std::size_t size{0};
    std::array<std::uint8_t, 255> copy{};
    RepeaterLifecycleDispatchTicket seen{};
    RepeaterLifecycleController* controller{nullptr};
    std::uint64_t now{21};
    std::uint64_t* change_readiness{nullptr};
    bool consumed_before_callback{false};
    bool bytes_survived_reentry{false};
    Fault nested_claim{Fault::none}, nested_prepare{Fault::none}, nested_prune{Fault::none};
    Fault nested_admit{Fault::none}, nested_disable{Fault::none}, nested_shutdown{Fault::none};
    Fault nested_notify{Fault::none}, nested_consent{Fault::none}, nested_recover{Fault::none};
};

void bridge(Harness& h, BridgeGate* gate = nullptr) {
    h.config.bridge_mode = true;
    h.config.admission_gate = gate;
    h.recreate();
}

// A4: fixed bridge mode exposes metadata, never the legacy payload escape path.
void test_bridge_mode_and_single_pending_ticket() {
    Harness legacy;
    legacy.activate();
    EXPECT(legacy.controller->prepare_dispatch(5).error == Fault::bridge_required);
    EXPECT(legacy.admit().forwarding.queued);
    expect_exact(legacy.controller->take_candidate(11));
    Harness h;
    bridge(h);
    EXPECT(h.controller->take_candidate(0).error == Fault::bridge_required);
    EXPECT(h.controller->prepare_dispatch(0).error == Fault::wrong_state);
    h.activate();
    EXPECT(h.admit(1, 10).forwarding.queued);
    EXPECT(h.admit(2, 11).forwarding.queued);
    EXPECT(h.controller->take_candidate(12).error == Fault::bridge_required);
    EXPECT(h.controller->status().queue_depth == 2);
    const auto prepared = h.controller->prepare_dispatch(20);
    EXPECT(prepared.error == Fault::none && prepared.has_ticket);
    EXPECT(prepared.ticket.provenance.metadata.message_id == 1);
    EXPECT(prepared.ticket.provenance.metadata.source_alias == kSource);
    EXPECT(prepared.ticket.provenance.enqueued_at_ms == 10);
    EXPECT(prepared.ticket.provenance.maximum_queue_age_ms == 100);
    EXPECT(prepared.ticket.frame_bytes == kFrame.size());
    EXPECT(prepared.ticket.authority_deadline_ms == 10000);
    EXPECT(prepared.ticket.consent_deadline_ms == 10000);
    EXPECT(h.controller->status().pending && h.controller->status().queue_depth == 1);
    EXPECT(h.controller->status().occupied_frames == 2);
    EXPECT(h.controller->prepare_dispatch(21).error == Fault::pending_busy);
    EXPECT(h.controller->status().queue_depth == 1);
    CapturingClaim callback;
    const auto claimed = h.controller->claim_dispatch(prepared.ticket, 22, callback);
    EXPECT(claimed.error == Fault::none && claimed.callback_invoked && callback.calls == 1);
    EXPECT(callback.size == kFrame.size());
    EXPECT(std::equal(kFrame.begin(), kFrame.end(), callback.copy.begin()));
    EXPECT(!h.controller->status().pending && h.controller->status().occupied_frames == 1);
    EXPECT(!h.controller->claim_dispatch(prepared.ticket, 23, callback).callback_invoked);
    EXPECT(callback.calls == 1);
    const auto second = h.controller->prepare_dispatch(24);
    EXPECT(second.has_ticket && second.ticket.provenance.metadata.message_id == 2);
    EXPECT(second.ticket.provenance.enqueued_at_ms == 11);
    EXPECT(second.ticket.candidate_id != prepared.ticket.candidate_id);
}

// A5: pending pressure is an eligible durable denial, not an early busy bypass.
void test_pending_pressure_persists_and_stays_duplicate_on_reconstruction() {
    BridgeGate gate;
    Harness h;
    h.config.policy.maximum_queue_depth = 2;
    bridge(h, &gate);
    h.activate();
    EXPECT(h.admit(1, 10).forwarding.queued);
    EXPECT(h.admit(2, 11).forwarding.queued);
    const auto pending = h.controller->prepare_dispatch(20);
    EXPECT(pending.has_ticket && gate.calls == 2);
    const auto writes = h.replay.writes;
    const auto denied = h.admit(3, 21);
    EXPECT(denied.error == Fault::none && !denied.forwarding.queued);
    EXPECT(denied.forwarding.disposition == SingleRepeaterDisposition::admission_denied);
    EXPECT(denied.forwarding.replay_state_changed && denied.replay_state_persisted);
    EXPECT(h.replay.writes == writes + 1 && gate.calls == 2);
    EXPECT(h.controller->status().occupied_frames == 2);
    EXPECT(h.admit(3, 22).forwarding.disposition == SingleRepeaterDisposition::duplicate);
    EXPECT(h.replay.writes == writes + 1 && gate.calls == 2);
    EXPECT(h.controller->notify(h.controller->status().generation, Event::suspend, 23) != Fault::none);
    EXPECT(!h.controller->status().pending && h.controller->status().occupied_frames == 0);
    const auto stored = h.replay.image();
    // Reconstruct every volatile owner and the supervisor, not merely its bundle.
    ++h.config.instance_id;
    h.recreate();
    EXPECT(h.controller->recover(h.prepare_recover(), 30) == Fault::none);
    EXPECT(h.controller->consent(h.prepare_consent(), 31) == Fault::none);
    EXPECT(h.admit(3, 32).forwarding.disposition == SingleRepeaterDisposition::duplicate);
    EXPECT(h.admit(1, 33).forwarding.disposition == SingleRepeaterDisposition::duplicate);
    EXPECT(h.replay.writes == writes + 1 && h.replay.image() == stored);
    EXPECT(!h.controller->prepare_dispatch(34).has_ticket);
    CapturingClaim callback;
    EXPECT(!h.controller->claim_dispatch(pending.ticket, 0, callback).callback_invoked);
    EXPECT(callback.calls == 0 && h.controller->status().state == State::active);
}

// A6: every exposed field binds the private pending slot; age is not refreshed.
void test_ticket_binding_original_age_and_consent_expiry() {
    for (unsigned field = 0; field < 20; ++field) {
        Harness h;
        bridge(h);
        h.activate();
        EXPECT(h.admit().forwarding.queued);
        const auto prepared = h.controller->prepare_dispatch(20);
        auto wrong = prepared.ticket;
        switch (field) {
        case 0: ++wrong.scope.instance_id; break;
        case 1: ++wrong.scope.generation; break;
        case 2: ++wrong.scope.group_context_id; break;
        case 3: ++wrong.scope.group_epoch; break;
        case 4: ++wrong.role_generation; break;
        case 5: ++wrong.readiness_generation; break;
        case 6: wrong.candidate_id = 0; break;
        case 7: ++wrong.provenance.metadata.group_context_id; break;
        case 8: ++wrong.provenance.metadata.source_alias; break;
        case 9: ++wrong.provenance.metadata.destination_alias; break;
        case 10: ++wrong.provenance.metadata.group_epoch; break;
        case 11: ++wrong.provenance.metadata.message_id; break;
        case 12: wrong.provenance.metadata.source_authenticated = false; break;
        case 13: wrong.provenance.metadata.source_authorized = false; break;
        case 14: wrong.provenance.metadata.forwarding_permitted = false; break;
        case 15: ++wrong.provenance.enqueued_at_ms; break;
        case 16: ++wrong.provenance.maximum_queue_age_ms; break;
        case 17: ++wrong.authority_deadline_ms; break;
        case 18: ++wrong.consent_deadline_ms; break;
        default: ++wrong.frame_bytes; break;
        }
        CapturingClaim callback;
        EXPECT(!h.controller->claim_dispatch(wrong, 0, callback).callback_invoked);
        EXPECT(h.controller->status().state == State::active && h.controller->status().pending);
        EXPECT(callback.calls == 0);
        EXPECT(h.controller->claim_dispatch(prepared.ticket, 21, callback).callback_invoked);
        EXPECT(callback.calls == 1);
    }
    for (std::uint64_t age = 99; age <= 101; ++age) {
        Harness h;
        bridge(h);
        h.activate(10000);
        EXPECT(h.admit(1, 10).forwarding.queued);
        const auto prepared = h.controller->prepare_dispatch(20);
        EXPECT(h.controller->prepare_dispatch(21).error == Fault::pending_busy);
        CapturingClaim callback;
        const auto claimed = h.controller->claim_dispatch(prepared.ticket, 10 + age, callback);
        EXPECT(claimed.callback_invoked == (age == 99));
        EXPECT(callback.calls == (age == 99 ? 1U : 0U));
        EXPECT(claimed.error == (age == 99 ? Fault::none : Fault::expired));
        EXPECT(!h.controller->status().pending);
        EXPECT(!h.controller->claim_dispatch(prepared.ticket, 10 + age, callback).callback_invoked);
    }
    for (std::uint64_t now = 99; now <= 101; ++now) {
        Harness h;
        h.config.policy.maximum_queue_age_ms = 1000;
        bridge(h);
        h.activate(100);
        EXPECT(h.admit(1, 98).forwarding.queued);
        const auto prepared = h.controller->prepare_dispatch(98);
        CapturingClaim callback;
        const auto claimed = h.controller->claim_dispatch(prepared.ticket, now, callback);
        EXPECT(claimed.callback_invoked == (now == 99));
        EXPECT(callback.calls == (now == 99 ? 1U : 0U));
        if (now >= 100) { EXPECT(h.controller->status().state == State::suspended); }
    }
}

// A7: consumed-before-callback and fixed borrowed lifetime survive reentry.
void test_serialized_claim_callback_and_changed_original_ticket() {
    Harness h;
    bridge(h);
    h.activate();
    auto caller = kFrame;
    EXPECT(h.controller->admit(h.prepare_frame(1), {caller.data(), caller.size()}, 10).forwarding.queued);
    caller[0] ^= 0xff;
    auto ticket = h.controller->prepare_dispatch(20).ticket;
    const auto original_id = ticket.candidate_id;
    h.provider.mutate_claim_ticket = &ticket;
    CapturingClaim callback;
    callback.controller = &*h.controller;
    const auto result = h.controller->claim_dispatch(ticket, 21, callback);
    EXPECT(result.error == Fault::none && result.callback_invoked && callback.calls == 1);
    EXPECT(ticket.candidate_id != original_id && callback.seen.candidate_id == original_id);
    EXPECT(callback.consumed_before_callback && callback.bytes_survived_reentry);
    EXPECT(std::equal(kFrame.begin(), kFrame.end(), callback.copy.begin()));
    EXPECT(callback.nested_claim == Fault::reentrant && callback.nested_prepare == Fault::reentrant);
    EXPECT(callback.nested_prune == Fault::reentrant && callback.nested_admit == Fault::reentrant);
    EXPECT(callback.nested_disable == Fault::reentrant && callback.nested_shutdown == Fault::reentrant);
    EXPECT(callback.nested_notify == Fault::reentrant && callback.nested_consent == Fault::reentrant);
    EXPECT(callback.nested_recover == Fault::reentrant);
    EXPECT(!h.controller->status().pending && !h.controller->status().claim_in_progress);
    // A later observed uncertain outcome is explicitly reported after the callback;
    // the callback itself cannot reenter and manufacture a terminal/rearm result.
    EXPECT(h.controller->notify(h.controller->status().generation, Event::uncertain_submission, 22)
           == Fault::uncertain_submission);
    EXPECT(h.controller->status().state == State::contained);

    Harness revoked;
    bridge(revoked);
    revoked.activate();
    EXPECT(revoked.admit().forwarding.queued);
    const auto revoke_ticket = revoked.controller->prepare_dispatch(20).ticket;
    CapturingClaim revoking;
    revoking.change_readiness = &revoked.provider.expected.readiness_generation;
    const auto after_claim = revoked.controller->claim_dispatch(revoke_ticket, 21, revoking);
    EXPECT(after_claim.callback_invoked && after_claim.error == Fault::authority_changed);
    EXPECT(revoking.calls == 1 && revoked.controller->status().state == State::contained);
    EXPECT(!revoked.controller->claim_dispatch(revoke_ticket, 22, revoking).callback_invoked);
    EXPECT(revoking.calls == 1);
}

// A8: revocation/expiry/shutdown retire both the pending slot and remaining FIFO.
void test_bridge_retirement_reconstruction_and_expiry_only_pruning() {
    for (unsigned event = 0; event < 8; ++event) {
        Harness h;
        bridge(h);
        h.activate();
        EXPECT(h.admit(1, 10).forwarding.queued && h.admit(2, 11).forwarding.queued);
        const auto ticket = h.controller->prepare_dispatch(20).ticket;
        const auto image = h.replay.image();
        if (event == 0) {
            EXPECT(h.controller->notify(h.controller->status().generation, Event::suspend, 21) != Fault::none);
        } else if (event == 1) {
            EXPECT(h.controller->notify(h.controller->status().generation, Event::authority_changed, 21)
                   != Fault::none);
        } else if (event == 2) {
            EXPECT(h.controller->disable(h.prepare_disable(), 21) == Fault::none);
        } else if (event == 3) {
            ++h.provider.expected.scope.group_epoch;
            CapturingClaim refused;
            EXPECT(!h.controller->claim_dispatch(ticket, 21, refused).callback_invoked);
            EXPECT(refused.calls == 0);
        } else if (event == 4) {
            EXPECT(h.controller->shutdown() == Fault::none);
        } else {
            const std::array<Event, 3> faults{{Event::uncertain_submission,
                                              Event::rearm_failed, Event::radio_unavailable}};
            EXPECT(h.controller->notify(h.controller->status().generation, faults[event - 5], 21)
                   != Fault::none);
        }
        EXPECT(!h.controller->status().pending && h.controller->status().occupied_frames == 0);
        EXPECT(h.replay.image() == image);
        CapturingClaim callback;
        EXPECT(!h.controller->claim_dispatch(ticket, 22, callback).callback_invoked);
        if (event == 4) { ++h.config.instance_id; h.recreate(); }
        EXPECT(h.controller->recover(h.prepare_recover(false, !h.roles.role.enabled), 30) == Fault::none);
        EXPECT(h.controller->consent(h.prepare_consent(), 31) == Fault::none);
        const auto writes = h.replay.writes;
        EXPECT(h.admit(1, 32).forwarding.disposition == SingleRepeaterDisposition::duplicate);
        EXPECT(h.admit(2, 33).forwarding.disposition == SingleRepeaterDisposition::duplicate);
        EXPECT(h.replay.writes == writes && !h.controller->prepare_dispatch(34).has_ticket);
    }
    Harness h;
    bridge(h);
    h.activate();
    EXPECT(h.admit(1, 10).forwarding.queued && h.admit(2, 40).forwarding.queued);
    const auto ticket = h.controller->prepare_dispatch(50).ticket;
    const auto polls = h.provider.current_calls;
    const auto pruned = h.controller->prune_expired(110);
    EXPECT(pruned.error == Fault::none && pruned.pending_dropped && pruned.queued_dropped == 0);
    EXPECT(h.provider.current_calls == polls);
    EXPECT(h.controller->status().queue_depth == 1 && !h.controller->status().pending);
    CapturingClaim callback;
    EXPECT(!h.controller->claim_dispatch(ticket, 111, callback).callback_invoked);
    const auto live = h.controller->prepare_dispatch(112);
    EXPECT(live.has_ticket && live.ticket.provenance.enqueued_at_ms == 40);
    EXPECT(h.controller->prune_expired(140).pending_dropped);

    Harness lease;
    lease.config.policy.maximum_queue_age_ms = 1000;
    bridge(lease);
    lease.activate(100);
    EXPECT(lease.admit(1, 98).forwarding.queued);
    EXPECT(lease.controller->prepare_dispatch(98).has_ticket);
    const auto lease_polls = lease.provider.current_calls;
    EXPECT(lease.controller->prune_expired(100).error == Fault::expired);
    EXPECT(lease.controller->status().state == State::suspended);
    EXPECT(lease.controller->status().occupied_frames == 0 && !lease.controller->status().pending);
    EXPECT(lease.provider.current_calls == lease_polls);
}

// A9: uncertain accounting is persisted before containment; no older work escapes.
void test_gate_uncertainty_and_pressure_save_failure_contain_bridge() {
    BridgeGate gate;
    Harness h;
    bridge(h, &gate);
    h.activate();
    EXPECT(h.admit(1, 10).forwarding.queued);
    const auto writes = h.replay.writes;
    gate.result = SingleRepeaterAdmission::uncertain;
    const auto uncertain = h.admit(2, 11);
    EXPECT(uncertain.error == Fault::admission_uncertain);
    EXPECT(uncertain.forwarding.replay_state_changed && uncertain.replay_state_persisted);
    EXPECT(h.replay.writes == writes + 1);
    EXPECT(h.controller->status().state == State::contained && h.controller->status().occupied_frames == 0);
    EXPECT(!h.controller->prepare_dispatch(12).has_ticket);
    gate.result = SingleRepeaterAdmission::allow;
    EXPECT(h.controller->recover(h.prepare_recover(), 30) == Fault::none);
    EXPECT(h.controller->consent(h.prepare_consent(), 31) == Fault::none);
    const auto gate_calls = gate.calls;
    EXPECT(h.admit(1, 32).forwarding.disposition == SingleRepeaterDisposition::duplicate);
    EXPECT(h.admit(2, 33).forwarding.disposition == SingleRepeaterDisposition::duplicate);
    EXPECT(gate.calls == gate_calls && h.replay.writes == writes + 1);

    Harness failed;
    bridge(failed);
    failed.activate();
    EXPECT(failed.admit().forwarding.queued);
    const auto pending = failed.controller->prepare_dispatch(20).ticket;
    failed.replay.next_fault = WriteFault::before_write;
    EXPECT(failed.admit(2, 21).error == Fault::replay_storage);
    EXPECT(failed.controller->status().state == State::contained && !failed.controller->status().pending);
    CapturingClaim callback;
    EXPECT(!failed.controller->claim_dispatch(pending, 22, callback).callback_invoked);
    EXPECT(callback.calls == 0);

    BridgeGate reentering;
    Harness gate_callback;
    bridge(gate_callback, &reentering);
    gate_callback.activate();
    reentering.controller = &*gate_callback.controller;
    EXPECT(gate_callback.admit().forwarding.queued);
    EXPECT(reentering.nested == Fault::reentrant);
    reentering.change_readiness = &gate_callback.provider.expected.readiness_generation;
    EXPECT(gate_callback.admit(2, 11).error == Fault::authority_changed);
    EXPECT(gate_callback.controller->status().occupied_frames == 0);
}

// A10: fixed logical capacity, ticket exhaustion, foreign instances and heap bounds.
void test_bridge_capacity_identity_and_fixed_resource_bounds() {
    for (const std::size_t depth : {std::size_t{1}, std::size_t{8}}) {
        for (const std::size_t bytes : {std::size_t{1}, std::size_t{255}}) {
            Harness h;
            h.config.policy.maximum_queue_depth = depth;
            h.config.policy.maximum_forwards_per_window = 32;
            h.config.maximum_frame_bytes = bytes;
            bridge(h);
            h.activate();
            std::array<std::uint8_t, 255> payload{};
            payload[0] = 0xab;
            for (std::size_t index = 0; index < depth; ++index) {
                const auto handle = h.prepare_frame(static_cast<std::uint32_t>(index + 1),
                                                     {payload.data(), bytes});
                EXPECT(h.controller->admit(handle, {payload.data(), bytes}, 10 + index).forwarding.queued);
                EXPECT(h.controller->status().occupied_frames <= depth);
            }
            const auto ticket = h.controller->prepare_dispatch(20).ticket;
            EXPECT(h.controller->status().occupied_frames == depth);
            const auto denied_handle = h.prepare_frame(100, {payload.data(), bytes});
            EXPECT(h.controller->admit(denied_handle, {payload.data(), bytes}, 21).forwarding.disposition ==
                   SingleRepeaterDisposition::admission_denied);
            EXPECT(h.controller->status().occupied_frames == depth);
            CapturingClaim callback;
            EXPECT(h.controller->claim_dispatch(ticket, 22, callback).callback_invoked);
            EXPECT(callback.size == bytes && callback.copy[0] == 0xab);
            EXPECT(h.controller->status().occupied_frames == depth - 1);
        }
    }
    for (const auto initial : {std::uint64_t{0}, std::numeric_limits<std::uint64_t>::max()}) {
        Harness h;
        h.config.initial_candidate_id = initial;
        bridge(h);
        if (initial == 0) {
            EXPECT(h.controller->recover(h.prepare_recover(true, true), 1) == Fault::invalid_configuration);
            EXPECT(h.replay.writes == 0 && h.roles.writes == 0);
        } else {
            h.activate();
            EXPECT(h.admit().forwarding.queued);
            EXPECT(h.controller->prepare_dispatch(20).error == Fault::generation_exhausted);
            EXPECT(h.controller->status().state == State::contained);
        }
    }
    Harness last;
    last.config.initial_candidate_id = std::numeric_limits<std::uint64_t>::max() - 1;
    bridge(last);
    last.activate();
    EXPECT(last.admit().forwarding.queued);
    const auto final_ticket = last.controller->prepare_dispatch(20).ticket;
    CapturingClaim final_callback;
    EXPECT(last.controller->claim_dispatch(final_ticket, 21, final_callback).callback_invoked);
    EXPECT(last.controller->prepare_dispatch(22).error == Fault::generation_exhausted);

    Harness instance;
    bridge(instance);
    instance.activate();
    EXPECT(instance.admit().forwarding.queued);
    const auto old = instance.controller->prepare_dispatch(20).ticket;
    EXPECT(instance.controller->shutdown() == Fault::none);
    ++instance.config.instance_id;
    instance.recreate();
    EXPECT(instance.controller->recover(instance.prepare_recover(), 30) == Fault::none);
    EXPECT(instance.controller->consent(instance.prepare_consent(), 31) == Fault::none);
    EXPECT(instance.admit(2, 32).forwarding.queued);
    const auto fresh = instance.controller->prepare_dispatch(33).ticket;
    CapturingClaim callback;
    EXPECT(!instance.controller->claim_dispatch(old, 0, callback).callback_invoked);
    EXPECT(instance.controller->claim_dispatch(fresh, 34, callback).callback_invoked && callback.calls == 1);

    Harness fixed;
    fixed.config.policy.maximum_forwards_per_window = 32;
    bridge(fixed);
    fixed.activate();
    CapturingClaim fixed_callback;
    allocation_probe::allocations = 0;
    allocation_probe::enabled = true;
    bool passed = true;
    for (std::uint32_t index = 0; index < 10; ++index) {
        const auto admitted = fixed.admit(100 + index, 10 + index * 3);
        const auto prepared = fixed.controller->prepare_dispatch(11 + index * 3);
        const auto claimed = fixed.controller->claim_dispatch(prepared.ticket, 12 + index * 3,
                                                              fixed_callback);
        passed = passed && admitted.forwarding.queued && prepared.has_ticket && claimed.callback_invoked;
    }
    allocation_probe::enabled = false;
    EXPECT(passed && fixed_callback.calls == 10 && allocation_probe::allocations == 0);
    std::cout << "INFO: sizeof lifecycle controller=" << sizeof(RepeaterLifecycleController)
              << ", status=" << sizeof(RepeaterLifecycleStatus)
              << ", metadata ticket=" << sizeof(RepeaterLifecycleDispatchTicket)
              << "; raw forwarder admission also uses fixed 255-byte stack copy plus metadata\n";
}
}  // namespace

int main() {
    test_disabled_startup_and_configuration();
    test_coherent_recovery_and_retained_media_refusals();
    test_consent_scope_and_no_implicit_restart_resume();
    test_exact_copy_binding_and_admission_order();
    test_whole_queue_retirement_and_old_notifications();
    test_explicit_resume_preserves_saved_and_denied_replay();
    test_role_and_replay_interruption_seams();
    test_original_deadline_queue_expiry_and_clock_rollback();
    test_radio_fault_and_readiness_are_fail_closed_inputs();
    test_shutdown_is_terminal_idempotent_and_preserves_media();
    test_serialization_generation_and_resource_bounds();
    test_status_privacy_surface_and_maximum_opaque_frame();
    test_bridge_mode_and_single_pending_ticket();
    test_pending_pressure_persists_and_stays_duplicate_on_reconstruction();
    test_ticket_binding_original_age_and_consent_expiry();
    test_serialized_claim_callback_and_changed_original_ticket();
    test_bridge_retirement_reconstruction_and_expiry_only_pruning();
    test_gate_uncertainty_and_pressure_save_failure_contain_bridge();
    test_bridge_capacity_identity_and_fixed_resource_bounds();
    if (failures != 0) {
        std::cerr << failures << " lifecycle assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 19 repeater lifecycle scenario groups (12 legacy + A4-A10)\n";
    return EXIT_SUCCESS;
}
