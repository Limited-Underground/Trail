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

#include "opentrail/repeater_dispatch_controller.hpp"

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
        if (clock != nullptr) { last_write_tick = ++*clock; }
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
    std::uint64_t* clock{nullptr};
    std::uint64_t last_write_tick{0};
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
        if (current_hook != nullptr && current_calls == hook_current_call) { current_hook(hook_context); }
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

    void (*current_hook)(void*) noexcept{nullptr};
    void* hook_context{nullptr};
    std::uint32_t hook_current_call{0};
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


using DF = RepeaterDispatchFault;
using DS = RepeaterDispatchState;
constexpr std::uint64_t kSecond = 0x2002;
constexpr std::uint64_t kCost = 399616;

bool same(const RepeaterDispatchSource& a, const RepeaterDispatchSource& b) {
    return a.group_context_id == b.group_context_id && a.group_epoch == b.group_epoch &&
           a.source_alias == b.source_alias;
}
bool same(const RepeaterDispatchLedger& a, const RepeaterDispatchLedger& b) {
    if (a.version != b.version || a.domain != b.domain || a.budget_epoch != b.budget_epoch ||
        a.sequence != b.sequence || a.global_remaining != b.global_remaining ||
        a.airtime_remaining_us != b.airtime_remaining_us || a.source_count != b.source_count) { return false; }
    for (std::size_t i = 0; i < kRepeaterDispatchSources; ++i) {
        if (!same(a.sources[i].source, b.sources[i].source) || a.sources[i].remaining != b.sources[i].remaining) { return false; }
    }
    return true;
}
bool same(const RepeaterDispatchRadioProof& a, const RepeaterDispatchRadioProof& b) {
    return a.lineage_id == b.lineage_id && a.driver_generation == b.driver_generation &&
        a.readiness_generation == b.readiness_generation && a.quiescence_receipt == b.quiescence_receipt &&
        a.verified_quiescent == b.verified_quiescent && a.receive_ready == b.receive_ready;
}
class FakeLedger final : public RepeaterDispatchLedgerPort {
public:
    RepeaterDispatchLedgerError read(RepeaterDispatchLedger& output) noexcept override {
        ++reads;
        if (clock != nullptr) { last_read_tick = ++*clock; }
        if (read_hook != nullptr && reads == hook_read_call) { read_hook(hook_context); }
        if (!present) { return RepeaterDispatchLedgerError::missing; }
        if (fail_next_read) { fail_next_read = false; return RepeaterDispatchLedgerError::unavailable; }
        output = record;
        if (mismatch_next_read) { mismatch_next_read = false; ++output.sequence; }
        return RepeaterDispatchLedgerError::none;
    }
    RepeaterDispatchLedgerError compare_exchange(const RepeaterDispatchLedger& expected,
                                                  const RepeaterDispatchLedger& desired) noexcept override {
        ++writes;
        if (clock != nullptr) { last_commit_tick = ++*clock; }
        last_expected = expected;
        last_desired = desired;
        if (!same(expected, record)) { return RepeaterDispatchLedgerError::conflict; }
        const auto fault = next_fault; next_fault = WriteFault::none;
        if (fault == WriteFault::before_write) { return RepeaterDispatchLedgerError::unavailable; }
        record = desired; ++commits;
        if (commit_hook != nullptr) { auto hook = commit_hook; commit_hook = nullptr; hook(hook_context); }
        if (fault == WriteFault::possible_commit) { return RepeaterDispatchLedgerError::commit_uncertain; }
        fail_next_read = fault == WriteFault::readback_error;
        mismatch_next_read = fault == WriteFault::readback_mismatch;
        return RepeaterDispatchLedgerError::none;
    }
    RepeaterDispatchLedger record{}, last_expected{}, last_desired{};
    WriteFault next_fault{WriteFault::none};
    bool present{true}, fail_next_read{false}, mismatch_next_read{false};
    std::uint32_t reads{0}, writes{0}, commits{0};
    std::uint64_t* clock{nullptr};
    std::uint64_t last_read_tick{0}, last_commit_tick{0};
    void (*commit_hook)(void*) noexcept{nullptr};
    void (*read_hook)(void*) noexcept{nullptr};
    void* hook_context{nullptr};
    std::uint32_t hook_read_call{0};
};
class FakeRadio final : public RepeaterDispatchRadioPort {
public:
    void invalidate_quiescence() noexcept override {
        proof.verified_quiescent = false; proof.receive_ready = false; ++invalidations;
    }
    bool observe(RepeaterDispatchRadioProof& output) noexcept override {
        ++observations;
        if (observe_hook != nullptr && observations == hook_observe_call) { observe_hook(hook_context); }
        output = proof; return available;
    }
    RepeaterDispatchSubmitResult submit(const RepeaterDispatchAttempt& attempt,
        const RepeaterDispatchRadioProof& expected, opentrail::radio::ByteView bytes) noexcept override {
        ++requests;
        last_attempt = attempt;
        if (submit_hook != nullptr) { submit_hook(hook_context); }
        if (!available || !same(expected, proof) || !proof.receive_ready || !proof.verified_quiescent) {
            ++tuple_refusals; return {RepeaterDispatchSubmitOutcome::tuple_refused, 0};
        }
        if (clock != nullptr) { last_submit_tick = ++*clock; }
        EXPECT(bytes.data != nullptr && bytes.size > 0 && bytes.size <= copy.size());
        if (outcome == RepeaterDispatchSubmitOutcome::accepted_pending || outcome == RepeaterDispatchSubmitOutcome::partial) {
            size = bytes.size;
            std::copy_n(bytes.data, bytes.size, copy.begin());
            ++copies;
        }
        proof.receive_ready = false; proof.verified_quiescent = false;
        if (outcome == RepeaterDispatchSubmitOutcome::accepted_pending) {
            return {outcome, wrong_accepted_size ? bytes.size - 1 : bytes.size};
        }
        if (outcome == RepeaterDispatchSubmitOutcome::partial) { return {outcome, bytes.size > 1 ? bytes.size - 1 : 0}; }
        return {outcome, 0};
    }
    bool consume_terminal(std::uint64_t handle, const RepeaterDispatchAttempt& expected,
                           RepeaterDispatchTerminal& output) noexcept override {
        (void)expected; ++terminal_reads;
        if (!terminal_unused || handle != terminal_handle) { return false; }
        terminal_unused = false; output = terminal_value; return true;
    }
    bool consume_rearm(std::uint64_t handle, const RepeaterDispatchAttempt& expected,
                        RepeaterDispatchRearm& output) noexcept override {
        (void)expected; ++rearm_reads;
        if (!rearm_unused || handle != rearm_handle) { return false; }
        rearm_unused = false; output = rearm_value; return true;
    }
    std::uint64_t issue_terminal(RepeaterDispatchTerminalOutcome value = RepeaterDispatchTerminalOutcome::completed) {
        terminal_value = {last_attempt, value}; terminal_unused = true; return ++terminal_handle;
    }
    std::uint64_t issue_rearm() {
        auto next = proof; next.receive_ready = true; next.verified_quiescent = true;
        ++next.readiness_generation; ++next.quiescence_receipt;
        proof = next; rearm_value = {last_attempt, next}; rearm_unused = true; return ++rearm_handle;
    }
    RepeaterDispatchRadioProof proof{0x9001, 3, 5, 7, true, true};
    RepeaterDispatchAttempt last_attempt{};
    RepeaterDispatchTerminal terminal_value{};
    RepeaterDispatchRearm rearm_value{};
    RepeaterDispatchSubmitOutcome outcome{RepeaterDispatchSubmitOutcome::accepted_pending};
    bool available{true}, terminal_unused{false}, rearm_unused{false}, wrong_accepted_size{false};
    std::uint64_t terminal_handle{500}, rearm_handle{600};
    std::array<std::uint8_t, 255> copy{};
    std::size_t size{0};
    std::uint32_t requests{0}, copies{0}, tuple_refusals{0}, observations{0}, terminal_reads{0}, rearm_reads{0}, invalidations{0};
    std::uint64_t* clock{nullptr}; std::uint64_t last_submit_tick{0};
    void (*observe_hook)(void*) noexcept{nullptr};
    void (*submit_hook)(void*) noexcept{nullptr};
    void* hook_context{nullptr}; std::uint32_t hook_observe_call{0};
};
RepeaterDispatchConfiguration configuration() {
    RepeaterDispatchConfiguration c{};
    c.lifecycle = {0x5001, 1, kLocal, kGroup, kEpoch, 10000, {1, true, 2, 8, 1000, 100}, 255};
    c.budget_domain = 0x7001; c.budget_epoch = 1; c.radio_lineage_id = 0x9001;
    c.source_count = 2; c.sources = {{{kGroup, kEpoch, kSource}, {kGroup, kEpoch, kSecond}}};
    c.airtime_ceiling_us = 2 * kCost; c.logical_byte_cap = 510;
    return c;
}
struct Harness {
    // Ports are deliberately declared BEFORE their reference-bearing controller.
    std::uint64_t clock{0};
    FakeRoles roles{}; FakeReplay replay{}; FakeProvider provider{roles};
    FakeLedger ledger{}; FakeRadio radio{};
    RepeaterDispatchConfiguration config{configuration()};
    std::optional<RepeaterDispatchController> controller{};
    Harness() {
        replay.role_owner = &roles; replay.clock = &clock; ledger.clock = &clock; radio.clock = &clock;
        seed(); recreate();
    }
    void seed() {
        ledger.record = {}; ledger.record.domain = config.budget_domain; ledger.record.budget_epoch = config.budget_epoch;
        ledger.record.sequence = 1; ledger.record.global_remaining = 2;
        ledger.record.airtime_remaining_us = config.airtime_ceiling_us; ledger.record.source_count = config.source_count;
        for (std::size_t i = 0; i < config.source_count && i < 2; ++i) {
            ledger.record.sources[i] = {config.sources[i], 1};
        }
    }
    void recreate() { controller.reset(); controller.emplace(config, provider, roles, replay, ledger, radio); }
    std::uint64_t prepare_recover(bool first = false, bool enable = false, std::uint64_t deadline = 10000) {
        const auto generation = controller->status().lifecycle.generation;
        const RepeaterLifecycleScope scope{config.lifecycle.instance_id, generation, config.lifecycle.group_context_id, config.lifecycle.group_epoch};
        auto role = roles.present ? roles.role : RepeaterLifecycleRole{config.lifecycle.group_context_id, config.lifecycle.group_epoch, 1, true};
        if (enable && roles.present && !role.enabled) { role.enabled = true; ++role.generation; }
        provider.expected = {scope, role, deadline, 11, true, true, true};
        provider.issued_scope = scope; provider.request = {provider.expected, first, enable};
        provider.request_operation = RepeaterLifecycleOperation::recover; provider.request_unused = true;
        provider.transaction_valid = true; return ++provider.request_handle;
    }
    std::uint64_t prepare_consent(std::uint64_t deadline = 0) {
        provider.consent_value = {provider.expected.scope, provider.expected.role.generation,
            provider.expected.readiness_generation, deadline == 0 ? provider.expected.deadline_ms : deadline};
        provider.consent_unused = true; return ++provider.consent_handle;
    }
    std::uint64_t prepare_disable() {
        auto role = roles.role; if (role.enabled) { role.enabled = false; ++role.generation; }
        const RepeaterLifecycleScope scope{config.lifecycle.instance_id, controller->status().lifecycle.generation,
            config.lifecycle.group_context_id, config.lifecycle.group_epoch};
        provider.expected = {scope, role, 10000, 11, false, false, false};
        provider.issued_scope = scope; provider.request = {provider.expected, false, false};
        provider.request_operation = RepeaterLifecycleOperation::disable;
        provider.request_unused = true; return ++provider.request_handle;
    }
    void activate(std::uint64_t now = 1, std::uint64_t deadline = 10000) {
        const bool first = !roles.present;
        EXPECT(controller->recover(prepare_recover(first, first, deadline), now) == DF::none);
        EXPECT(controller->status().state == DS::awaiting_consent);
        EXPECT(controller->consent(prepare_consent(), now + 1) == DF::none);
        EXPECT(controller->status().state == DS::ready);
    }
    std::uint64_t prepare_frame(std::uint32_t id, std::uint64_t source = kSource,
        opentrail::radio::ByteView bytes = {kFrame.data(), kFrame.size()}) {
        provider.frame_size = bytes.size;
        if (bytes.data != nullptr && bytes.size <= provider.expected_frame.size()) {
            std::copy_n(bytes.data, bytes.size, provider.expected_frame.begin());
        }
        provider.frame_metadata = {config.lifecycle.group_context_id, source, kDestination,
            config.lifecycle.group_epoch, id, true, true, true};
        return ++provider.frame_handle;
    }
    RepeaterLifecycleAdmission admit(std::uint32_t id = 1, std::uint64_t now = 10,
                                    std::uint64_t source = kSource) {
        return controller->admit(prepare_frame(id, source), {kFrame.data(), kFrame.size()}, now);
    }
    void complete(std::uint64_t terminal_now = 411) {
        EXPECT(controller->terminal(radio.issue_terminal(), terminal_now) == DF::none);
        EXPECT(controller->status().state == DS::awaiting_rearm);
        EXPECT(controller->rearm(radio.issue_rearm(), terminal_now + 1) == DF::none);
        EXPECT(controller->status().state == DS::ready);
    }
};
void start(Harness& h, std::uint64_t now = 11) {
    h.activate(); EXPECT(h.admit().forwarding.queued); EXPECT(h.controller->service(now) == DF::none);
    EXPECT(h.controller->status().state == DS::awaiting_terminal);
}
void revoke(void* p) noexcept { ++static_cast<Harness*>(p)->provider.expected.readiness_generation; }
void change_driver(void* p) noexcept { ++static_cast<Harness*>(p)->radio.proof.driver_generation; }
struct ReentryResult {
    Harness* h{nullptr}; std::array<bool, 10> refused{}; std::size_t occupancy{0}; bool possible{false};
};
void reenter(void* opaque) noexcept {
    auto& result = *static_cast<ReentryResult*>(opaque); auto& b = *result.h->controller;
    result.occupancy = b.status().occupied_frames; result.possible = b.status().possible_emission;
    result.refused = {{b.recover(0, 11) == DF::reentrant, b.consent(0, 11) == DF::reentrant,
        b.admit(0, {nullptr, 0}, 11).error == Fault::reentrant, b.service(11) == DF::reentrant,
        b.terminal(0, 11) == DF::reentrant, b.rearm(0, 11) == DF::reentrant,
        b.notify(b.status().lifecycle.generation, Event::suspend, 11) == DF::reentrant,
        b.disable(0, 11) == DF::reentrant, b.shutdown() == DF::reentrant,
        b.clear_diagnostics() == DF::reentrant}};
}
void mutate_attempt(RepeaterDispatchAttempt& a, unsigned field) {
    switch (field) {
    case 0: ++a.attempt_id; break;
    case 1: ++a.ticket.scope.instance_id; break;
    case 2: ++a.ticket.scope.generation; break;
    case 3: ++a.ticket.scope.group_context_id; break;
    case 4: ++a.ticket.scope.group_epoch; break;
    case 5: ++a.ticket.role_generation; break;
    case 6: ++a.ticket.readiness_generation; break;
    case 7: ++a.ticket.candidate_id; break;
    case 8: ++a.ticket.provenance.metadata.group_context_id; break;
    case 9: ++a.ticket.provenance.metadata.group_epoch; break;
    case 10: ++a.ticket.provenance.metadata.source_alias; break;
    case 11: ++a.ticket.provenance.metadata.destination_alias; break;
    case 12: ++a.ticket.provenance.metadata.message_id; break;
    case 13: a.ticket.provenance.metadata.source_authenticated = false; break;
    case 14: a.ticket.provenance.metadata.source_authorized = false; break;
    case 15: a.ticket.provenance.metadata.forwarding_permitted = false; break;
    case 16: ++a.ticket.provenance.enqueued_at_ms; break;
    case 17: ++a.ticket.provenance.maximum_queue_age_ms; break;
    case 18: ++a.ticket.authority_deadline_ms; break;
    case 19: ++a.ticket.consent_deadline_ms; break;
    case 20: ++a.ticket.frame_bytes; break;
    case 21: ++a.budget_domain; break;
    case 22: ++a.budget_epoch; break;
    case 23: ++a.budget_receipt_sequence; break;
    case 24: ++a.radio_lineage_id; break;
    case 25: ++a.driver_generation; break;
    case 26: ++a.readiness_generation; break;
    default: ++a.quiescence_receipt; break;
    }
}

// B1/F1-F3: actual P2 admission, exact bytes, full-C debit and explicit completion.
void test_startup_and_exact_full_cost_dispatch() {
    Harness h;
    EXPECT(h.controller->service(0) == DF::wrong_state);
    EXPECT(h.admit(1, 0).error != Fault::none);
    EXPECT(h.radio.requests == 0 && h.ledger.writes == 0);
    h.activate();
    std::array<std::uint8_t, 255> payload{};
    for (std::size_t i = 0; i < payload.size(); ++i) { payload[i] = static_cast<std::uint8_t>(i); }
    const auto handle = h.prepare_frame(1, kSource, {payload.data(), payload.size()});
    EXPECT(h.controller->admit(handle, {payload.data(), payload.size()}, 10).forwarding.queued);
    EXPECT(h.ledger.record.global_remaining == 1 && h.ledger.record.sources[0].remaining == 0);
    EXPECT(h.ledger.last_commit_tick < h.ledger.last_read_tick && h.ledger.last_read_tick < h.replay.last_write_tick);
    EXPECT(h.controller->service(11) == DF::none);
    EXPECT(h.radio.requests == 1 && h.radio.copies == 1 && h.radio.size == payload.size());
    EXPECT(std::equal(payload.begin(), payload.end(), h.radio.copy.begin()));
    EXPECT(h.ledger.record.airtime_remaining_us == kCost);
    EXPECT(h.ledger.last_commit_tick < h.ledger.last_read_tick && h.ledger.last_read_tick < h.radio.last_submit_tick);
    EXPECT(h.radio.last_attempt.ticket.provenance.enqueued_at_ms == 10);
    EXPECT(h.radio.last_attempt.ticket.provenance.metadata.source_alias == kSource);
    EXPECT(h.controller->status().diagnostics.claim_callbacks == 1);
    EXPECT(h.controller->status().diagnostics.submit_requests == 1);
    EXPECT(h.controller->status().diagnostics.accepted_copies == 1);
    EXPECT(h.controller->rearm(0, 12) == DF::wrong_state);
    EXPECT(h.controller->service(200) == DF::none && h.radio.requests == 1);
    h.complete();
    EXPECT(h.admit(2, 413, kSecond).forwarding.queued);
    EXPECT(h.controller->service(414) == DF::none);
    h.complete(814);
    EXPECT(h.radio.requests == 2 && h.radio.copies == 2);
    EXPECT(h.ledger.record.global_remaining == 0 && h.ledger.record.airtime_remaining_us == 0);
}
// B2/F4: gate sees only eligible new keys; source/global exhaustion never refills.
void test_source_global_and_unknown_roster_limits() {
    Harness h; h.activate();
    const auto writes = h.ledger.writes;
    auto handle = h.prepare_frame(1); h.provider.frame_metadata.source_authenticated = false;
    EXPECT(!h.controller->admit(handle, {kFrame.data(), kFrame.size()}, 5).forwarding.queued);
    EXPECT(h.ledger.writes == writes);
    EXPECT(h.admit(1, 10).forwarding.queued);
    EXPECT(h.admit(1, 11).forwarding.disposition == SingleRepeaterDisposition::duplicate);
    const auto denied = h.admit(2, 12);
    EXPECT(denied.forwarding.disposition == SingleRepeaterDisposition::admission_denied);
    EXPECT(denied.replay_state_persisted && h.ledger.writes == writes + 1);
    const auto unknown = h.admit(3, 13, 0x9999);
    EXPECT(unknown.forwarding.disposition == SingleRepeaterDisposition::admission_denied && unknown.replay_state_persisted);
    EXPECT(h.ledger.writes == writes + 1);
    EXPECT(h.admit(4, 14, kSecond).forwarding.queued);
    EXPECT(h.ledger.record.global_remaining == 0);
    EXPECT(!h.admit(5, 15, kSecond).forwarding.queued && h.ledger.writes == writes + 2);
    const auto ledger_image = h.ledger.record; const auto replay_image = h.replay.image();
    EXPECT(h.controller->notify(h.controller->status().lifecycle.generation, Event::suspend, 16) != DF::none);
    ++h.config.lifecycle.instance_id; h.recreate(); h.activate(30);
    EXPECT(h.admit(2, 32).forwarding.disposition == SingleRepeaterDisposition::duplicate);
    EXPECT(h.admit(3, 33, 0x9999).forwarding.disposition == SingleRepeaterDisposition::duplicate);
    EXPECT(!h.admit(6, 34).forwarding.queued);
    EXPECT(same(h.ledger.record, ledger_image));
    EXPECT(h.replay.image() != replay_image); // new eligible denied key 6 was saved.
    EXPECT(h.radio.requests == 0);
    // Isolate global exhaustion from per-source exhaustion: source2 still has credit.
    Harness global; global.ledger.record.global_remaining = 1; global.activate();
    EXPECT(global.admit(1, 10).forwarding.queued);
    const auto global_writes = global.ledger.writes;
    const auto global_denial = global.admit(2, 11, kSecond);
    EXPECT(global_denial.forwarding.disposition == SingleRepeaterDisposition::admission_denied && global_denial.replay_state_persisted);
    EXPECT(global.ledger.record.sources[1].remaining == 1 && global.ledger.record.global_remaining == 0);
    EXPECT(global.ledger.writes == global_writes);
}
// B3/F5: airtime boundary and whole volatile queue retirement on exhaustion.
void test_airtime_boundaries_and_exhaustion_retirement() {
    for (const auto amount : {kCost - 1, kCost, kCost + 1, 2 * kCost}) {
        Harness h; h.config.airtime_ceiling_us = amount; h.seed(); h.recreate(); h.activate();
        EXPECT(h.admit(1, 10).forwarding.queued);
        EXPECT(h.admit(2, 10, kSecond).forwarding.queued);
        const auto saved = h.replay.image();
        const auto result = h.controller->service(11);
        if (amount < kCost) {
            EXPECT(result == DF::budget_exhausted && h.controller->status().state == DS::suspended);
            EXPECT(h.controller->status().lifecycle.occupied_frames == 0 && h.radio.requests == 0);
            EXPECT(h.ledger.record.airtime_remaining_us == amount);
        } else {
            EXPECT(result == DF::none && h.radio.requests == 1);
            EXPECT(h.ledger.record.airtime_remaining_us == amount - kCost);
            EXPECT(h.radio.size == kFrame.size()); // short frame still pays C, not short-frame airtime.
            h.complete();
            EXPECT(h.controller->status().diagnostics.expired == 1); // older second frame was not renewed.
        }
        EXPECT(h.replay.image() == saved);
    }
}
// B4/F5: independently fault both irreversible transaction phases.
void test_ledger_phase_faults_and_replay_order() {
    for (const bool airtime : {false, true}) {
        for (const auto fault : {WriteFault::before_write, WriteFault::possible_commit,
                                 WriteFault::readback_error, WriteFault::readback_mismatch}) {
            Harness h; h.activate();
            if (airtime) { EXPECT(h.admit().forwarding.queued); }
            const auto before = h.ledger.record;
            const auto writes = h.replay.writes;
            h.ledger.next_fault = fault;
            if (airtime) { EXPECT(h.controller->service(11) != DF::none); }
            else {
                const auto result = h.admit();
                EXPECT(result.forwarding.replay_state_changed && result.replay_state_persisted);
                EXPECT(h.replay.writes == writes + 1);
            }
            EXPECT(h.controller->status().state == DS::contained);
            EXPECT(h.radio.requests == 0 && h.controller->status().lifecycle.occupied_frames == 0);
            if (fault == WriteFault::before_write) { EXPECT(same(h.ledger.record, before)); }
            else {
                EXPECT(h.ledger.record.sequence == before.sequence + 1);
                EXPECT(airtime ? h.ledger.record.airtime_remaining_us == before.airtime_remaining_us - kCost
                               : h.ledger.record.global_remaining == before.global_remaining - 1);
            }
            const auto attempts = h.ledger.writes;
            EXPECT(h.controller->service(12) != DF::none && h.ledger.writes == attempts);
            // Reconstruct after each transaction boundary without reseeding any retained owner.
            const auto retained_ledger = h.ledger.record;
            const auto retained_replay = h.replay.image();
            const auto retained_radio = h.radio.proof;
            ++h.config.lifecycle.instance_id; h.recreate(); h.activate(30);
            EXPECT(h.admit(1, 32).forwarding.disposition == SingleRepeaterDisposition::duplicate);
            EXPECT(same(h.ledger.record, retained_ledger) && h.replay.image() == retained_replay);
            EXPECT(same(h.radio.proof, retained_radio));
            EXPECT(h.ledger.writes == attempts && h.radio.requests == 0);
        }
    }
    // The built-in rate gate denies AFTER successful B admission accounting, with spare FIFO space.
    Harness rate; rate.config.lifecycle.policy.maximum_forwards_per_window = 1;
    rate.recreate(); rate.activate(); EXPECT(rate.admit(1, 10).forwarding.queued);
    const auto rate_limited = rate.admit(2, 11, kSecond);
    EXPECT(rate_limited.forwarding.disposition == SingleRepeaterDisposition::rate_limited);
    EXPECT(rate_limited.replay_state_persisted && rate.controller->status().lifecycle.queue_depth == 1);
    EXPECT(rate.ledger.record.global_remaining == 0 && rate.ledger.record.sources[0].remaining == 0 &&
           rate.ledger.record.sources[1].remaining == 0 && rate.ledger.writes == 2);
    const auto rate_record = rate.ledger.record; const auto rate_replay = rate.replay.image();
    ++rate.config.lifecycle.instance_id; rate.recreate(); rate.activate(30);
    EXPECT(rate.admit(2, 32, kSecond).forwarding.disposition == SingleRepeaterDisposition::duplicate);
    EXPECT(rate.ledger.writes == 2 && same(rate.ledger.record, rate_record) && rate.replay.image() == rate_replay);
    EXPECT(rate.controller->status().lifecycle.occupied_frames == 0 && rate.radio.requests == 0);
    Harness failed_replay; failed_replay.activate();
    failed_replay.replay.next_fault = WriteFault::before_write;
    EXPECT(failed_replay.admit().error == Fault::replay_storage);
    EXPECT(failed_replay.ledger.record.sources[0].remaining == 0 && failed_replay.ledger.record.global_remaining == 1);
    EXPECT(failed_replay.radio.requests == 0);
}
// B5/F3/F9: active in-flight/rearm pressure must save denied replay, not early-busy return.
void test_inflight_pressure_and_actual_reconstruction() {
    Harness h; start(h);
    const auto credits = h.ledger.record;
    const auto writes = h.replay.writes;
    auto denied = h.admit(2, 20, kSecond);
    EXPECT(denied.forwarding.disposition == SingleRepeaterDisposition::admission_denied);
    EXPECT(denied.replay_state_persisted && h.replay.writes == writes + 1);
    EXPECT(same(h.ledger.record, credits));
    EXPECT(h.controller->terminal(h.radio.issue_terminal(), 411) == DF::none);
    denied = h.admit(3, 412, kSecond);
    EXPECT(denied.replay_state_persisted && h.replay.writes == writes + 2);
    EXPECT(h.controller->rearm(h.radio.issue_rearm(), 413) == DF::none);
    const auto image = h.replay.image();
    EXPECT(h.controller->shutdown() == DF::none);
    ++h.config.lifecycle.instance_id; h.recreate(); h.activate(500);
    EXPECT(h.admit(2, 503, kSecond).forwarding.disposition == SingleRepeaterDisposition::duplicate);
    EXPECT(h.admit(3, 504, kSecond).forwarding.disposition == SingleRepeaterDisposition::duplicate);
    EXPECT(same(h.ledger.record, credits) && h.replay.image() == image);
    EXPECT(h.admit(4, 505, kSecond).forwarding.queued);
    EXPECT(h.controller->service(506) == DF::none && h.radio.requests == 2);
}
// B6/F6: authority and original radio tuple loss across ledger/claim boundaries.
void test_claim_races_and_atomic_expected_radio_proof() {
    for (unsigned kind = 0; kind < 3; ++kind) {
        Harness h; h.activate(); EXPECT(h.admit().forwarding.queued);
        h.ledger.hook_context = &h;
        if (kind == 0) { h.ledger.commit_hook = revoke; }
        if (kind == 1) { h.ledger.commit_hook = change_driver; }
        if (kind == 2) { h.radio.submit_hook = change_driver; h.radio.hook_context = &h; }
        EXPECT(h.controller->service(11) != DF::none);
        EXPECT(h.ledger.record.airtime_remaining_us == kCost);
        EXPECT(h.radio.copies == 0);
        EXPECT(h.controller->status().diagnostics.claim_callbacks == (kind == 2 ? 1U : 0U));
        EXPECT(h.radio.requests == (kind == 2 ? 1U : 0U));
        EXPECT(h.controller->service(12) != DF::none && h.radio.copies == 0);
    }
    Harness post; post.activate(); EXPECT(post.admit().forwarding.queued);
    post.radio.submit_hook = revoke; post.radio.hook_context = &post;
    EXPECT(post.controller->service(11) != DF::none);
    EXPECT(post.radio.copies == 1 && post.controller->status().possible_emission);
    EXPECT(post.controller->status().diagnostics.claim_callbacks == 1);
}
// B7/F7: no refund/retry on definitive rejection, partial or unknown submission.
void test_submission_outcomes_are_irreversible() {
    for (const auto outcome : {RepeaterDispatchSubmitOutcome::busy, RepeaterDispatchSubmitOutcome::rejected,
         RepeaterDispatchSubmitOutcome::partial, RepeaterDispatchSubmitOutcome::uncertain,
         static_cast<RepeaterDispatchSubmitOutcome>(255)}) {
        Harness h; h.radio.outcome = outcome; h.activate(); EXPECT(h.admit().forwarding.queued);
        (void)h.controller->service(11);
        const auto image = h.ledger.record;
        EXPECT(h.radio.requests == 1 && image.airtime_remaining_us == kCost);
        const bool definite = outcome == RepeaterDispatchSubmitOutcome::busy || outcome == RepeaterDispatchSubmitOutcome::rejected;
        if (definite) {
            EXPECT(h.controller->status().state == DS::awaiting_rearm);
            EXPECT(!h.controller->status().possible_emission);
            EXPECT(h.controller->service(12) == DF::none && h.radio.requests == 1);
            EXPECT(h.controller->rearm(h.radio.issue_rearm(), 13) == DF::none);
        } else {
            EXPECT(h.controller->status().state == DS::contained && h.controller->status().possible_emission);
            EXPECT(h.controller->service(12) != DF::none);
        }
        EXPECT(same(h.ledger.record, image) && h.radio.requests == 1);
    }
    Harness partial; partial.radio.wrong_accepted_size = true; partial.activate();
    EXPECT(partial.admit().forwarding.queued);
    EXPECT(partial.controller->service(11) == DF::uncertain_emission);
    EXPECT(partial.controller->status().possible_emission && partial.radio.requests == 1);
}
// B8/F7-F8: every bound attempt field must match both terminal and rearm evidence.
void test_all_terminal_rearm_bindings_and_one_use_events() {
    for (const bool rearm_phase : {false, true}) {
        for (unsigned field = 0; field < 28; ++field) {
            Harness h; start(h);
            if (rearm_phase) { EXPECT(h.controller->terminal(h.radio.issue_terminal(), 411) == DF::none); }
            const auto ledger = h.ledger.record;
            const auto handle = rearm_phase ? h.radio.issue_rearm() : h.radio.issue_terminal();
            auto& attempt = rearm_phase ? h.radio.rearm_value.attempt : h.radio.terminal_value.attempt;
            mutate_attempt(attempt, field);
            const auto result = rearm_phase ? h.controller->rearm(handle, 412) : h.controller->terminal(handle, 411);
            EXPECT(result == DF::invalid_event);
            EXPECT(h.controller->status().state == (rearm_phase ? DS::awaiting_rearm : DS::awaiting_terminal));
            EXPECT(h.radio.requests == 1 && same(h.ledger.record, ledger));
            EXPECT((rearm_phase ? h.controller->rearm(handle, 412) : h.controller->terminal(handle, 411)) == DF::invalid_event);
        }
    }
    Harness h; start(h);
    const auto terminal = h.radio.issue_terminal();
    EXPECT(h.controller->terminal(terminal, 411) == DF::none);
    EXPECT(h.controller->terminal(terminal, 412) == DF::wrong_state);
    const auto rearm = h.radio.issue_rearm(); EXPECT(h.controller->rearm(rearm, 413) == DF::none);
    EXPECT(h.controller->rearm(rearm, 414) == DF::wrong_state);
    EXPECT(h.radio.requests == 1);
}
// B9/F8: original phase deadlines, malformed outcomes and independent rearm proof.
void test_timeouts_rearm_faults_and_no_early_completion() {
    for (const bool rearm_phase : {false, true}) {
        for (const std::uint64_t elapsed : {999, 1000, 1001}) {
            Harness h; start(h);
            const std::uint64_t origin = rearm_phase ? 411 : 11;
            if (rearm_phase) { EXPECT(h.controller->terminal(h.radio.issue_terminal(), origin) == DF::none); }
            // Invalid evidence must not move the original timeout origin.
            EXPECT((rearm_phase ? h.controller->rearm(0, origin + 500) : h.controller->terminal(0, origin + 500)) == DF::invalid_event);
            const auto result = h.controller->service(origin + elapsed);
            EXPECT(result == (elapsed < 1000 ? DF::none : rearm_phase ? DF::rearm_timeout : DF::terminal_timeout));
            EXPECT(h.radio.requests == 1);
        }
    }
    Harness early; start(early);
    EXPECT(early.controller->terminal(early.radio.issue_terminal(), 410) == DF::uncertain_emission);
    EXPECT(early.controller->status().state == DS::contained);
    for (const auto outcome : {RepeaterDispatchTerminalOutcome::failed, RepeaterDispatchTerminalOutcome::uncertain}) {
        Harness h; start(h);
        EXPECT(h.controller->terminal(h.radio.issue_terminal(outcome), 411) == DF::uncertain_emission);
        EXPECT(h.controller->status().possible_emission && h.radio.requests == 1);
    }
    for (unsigned field = 0; field < 7; ++field) {
        Harness h; start(h); EXPECT(h.controller->terminal(h.radio.issue_terminal(), 411) == DF::none);
        const auto handle = h.radio.issue_rearm();
        switch (field) {
        case 0: ++h.radio.rearm_value.proof.lineage_id; break;
        case 1: ++h.radio.rearm_value.proof.driver_generation; break;
        case 2: h.radio.rearm_value.proof.readiness_generation = h.radio.last_attempt.readiness_generation; break;
        case 3: h.radio.rearm_value.proof.quiescence_receipt = h.radio.last_attempt.quiescence_receipt; break;
        case 4: h.radio.rearm_value.proof.receive_ready = false; break;
        case 5: h.radio.rearm_value.proof.verified_quiescent = false; break;
        default: ++h.radio.proof.quiescence_receipt; break;
        }
        EXPECT(h.controller->rearm(handle, 412) == DF::radio_changed);
        EXPECT(h.controller->status().state == DS::contained && h.radio.requests == 1);
    }
}
// B10/F9: preserve actual retained ledger/replay/radio uncertainty across reconstruction.
void test_restart_never_defaults_credit_or_radio_readiness() {
    Harness h; start(h);
    const auto ledger = h.ledger.record; const auto replay = h.replay.image();
    EXPECT(h.controller->shutdown() == DF::none);
    ++h.config.lifecycle.instance_id; h.recreate();
    EXPECT(h.controller->recover(h.prepare_recover(), 20) == DF::radio_not_ready);
    EXPECT(same(h.ledger.record, ledger) && h.replay.image() == replay && h.radio.requests == 1);
    h.radio.proof.receive_ready = true; h.radio.proof.verified_quiescent = true;
    ++h.radio.proof.readiness_generation; ++h.radio.proof.quiescence_receipt; // explicit retained-port reconciliation.
    h.activate(30);
    EXPECT(h.admit(1, 33).forwarding.disposition == SingleRepeaterDisposition::duplicate);
    EXPECT(h.admit(2, 34).forwarding.disposition == SingleRepeaterDisposition::admission_denied);
    EXPECT(same(h.ledger.record, ledger));
    Harness uncertain;
    uncertain.activate();
    EXPECT(uncertain.controller->notify(uncertain.controller->status().lifecycle.generation,
        Event::uncertain_submission, 10) != DF::none);
    EXPECT(uncertain.radio.requests == 0 && !uncertain.radio.proof.verified_quiescent);
    ++uncertain.config.lifecycle.instance_id; uncertain.recreate();
    EXPECT(uncertain.controller->recover(uncertain.prepare_recover(), 20) == DF::radio_not_ready);
    uncertain.radio.proof.verified_quiescent = true; uncertain.radio.proof.receive_ready = true;
    ++uncertain.radio.proof.readiness_generation; ++uncertain.radio.proof.quiescence_receipt;
    uncertain.activate(30);
    EXPECT(uncertain.radio.requests == 0);
    for (const auto event : {Event::radio_unavailable, Event::rearm_failed}) {
        Harness freshness; freshness.activate(); const auto old = freshness.radio.proof;
        EXPECT(freshness.controller->notify(freshness.controller->status().lifecycle.generation, event, 10) != DF::none);
        freshness.radio.proof = old; // Flags-only restoration of the exact stale tuple is not reconciliation.
        EXPECT(freshness.controller->recover(freshness.prepare_recover(), 20) == DF::radio_not_ready);
        ++freshness.radio.proof.readiness_generation; ++freshness.radio.proof.quiescence_receipt;
        freshness.activate(30);
        EXPECT(freshness.radio.requests == 0 && freshness.ledger.writes == 0);
    }
    Harness missing; missing.ledger.present = false;
    EXPECT(missing.controller->recover(missing.prepare_recover(true, true), 1) != DF::none);
    EXPECT(missing.ledger.writes == 0 && missing.radio.requests == 0);
    for (unsigned field = 0; field < 7; ++field) {
        Harness bad;
        switch (field) {
        case 0: bad.ledger.record.version = 0; break;
        case 1: ++bad.ledger.record.domain; break;
        case 2: ++bad.ledger.record.budget_epoch; break;
        case 3: bad.ledger.record.sequence = 0; break;
        case 4: bad.ledger.record.global_remaining = 3; break;
        case 5: ++bad.ledger.record.sources[0].source.group_context_id; break;
        default: bad.ledger.record.source_count = 3; break;
        }
        EXPECT(bad.controller->recover(bad.prepare_recover(true, true), 1) != DF::none);
        EXPECT(bad.ledger.writes == 0 && bad.radio.requests == 0);
    }
    Harness changed; changed.activate(); EXPECT(changed.admit().forwarding.queued);
    EXPECT(changed.controller->shutdown() == DF::none);
    ++changed.config.lifecycle.instance_id; ++changed.config.budget_epoch; changed.recreate();
    EXPECT(changed.controller->recover(changed.prepare_recover(), 20) != DF::none);
    EXPECT(changed.ledger.record.global_remaining == 1 && changed.ledger.writes == 1);
}
// B11/F10: lifecycle faults retire all volatile queued work, no automatic dispatch resume.
void test_expiry_suspension_shutdown_and_retirement() {
    Harness expired; expired.activate(); EXPECT(expired.admit().forwarding.queued);
    EXPECT(expired.controller->service(110) == DF::none);
    EXPECT(expired.radio.requests == 0 && expired.controller->status().diagnostics.expired == 1);
    EXPECT(expired.ledger.record.airtime_remaining_us == 2 * kCost);
    Harness unavailable; unavailable.activate(); EXPECT(unavailable.admit().forwarding.queued);
    unavailable.radio.available = false;
    EXPECT(unavailable.controller->service(110) == DF::radio_not_ready);
    EXPECT(unavailable.controller->status().lifecycle.occupied_frames == 0 && unavailable.controller->status().diagnostics.expired == 1);
    EXPECT(unavailable.ledger.record.airtime_remaining_us == 2 * kCost && unavailable.radio.requests == 0);
    for (const auto event : {Event::suspend, Event::authority_changed, Event::uncertain_submission,
                             Event::rearm_failed, Event::radio_unavailable}) {
        Harness h; h.activate(); EXPECT(h.admit(1, 10).forwarding.queued && h.admit(2, 10, kSecond).forwarding.queued);
        const auto ledger = h.ledger.record; const auto replay = h.replay.image();
        EXPECT(h.controller->notify(h.controller->status().lifecycle.generation, event, 11) != DF::none);
        EXPECT(h.controller->status().lifecycle.occupied_frames == 0);
        EXPECT(h.controller->service(12) != DF::none && h.radio.requests == 0);
        EXPECT(same(h.ledger.record, ledger) && h.replay.image() == replay);
    }
    Harness stale; stale.activate();
    const auto invalidations = stale.radio.invalidations;
    EXPECT(stale.controller->notify(stale.controller->status().lifecycle.generation + 1,
        Event::uncertain_submission, 0) == DF::invalid_event);
    EXPECT(stale.radio.invalidations == invalidations && stale.radio.proof.verified_quiescent);
    Harness revoke_at; revoke_at.activate(); EXPECT(revoke_at.admit().forwarding.queued);
    ++revoke_at.provider.expected.readiness_generation;
    EXPECT(revoke_at.controller->service(11) != DF::none && revoke_at.radio.requests == 0);
}
// B12/F3/F6/F11: serialized reentry and memory count do not duplicate candidate ownership.
void test_reentrant_ports_fixed_occupancy_and_no_heap() {
    ReentryResult entry{}; Harness h; entry.h = &h;
    h.activate(); EXPECT(h.admit().forwarding.queued);
    h.radio.submit_hook = reenter; h.radio.hook_context = &entry;
    allocation_probe::allocations = 0; allocation_probe::enabled = true;
    const auto result = h.controller->service(11);
    allocation_probe::enabled = false;
    EXPECT(result == DF::none && allocation_probe::allocations == 0);
    EXPECT(entry.occupancy == 1 && entry.possible);
    for (const auto refused : entry.refused) { EXPECT(refused); }
    EXPECT(h.radio.copies == 1 && h.controller->status().occupied_frames == 1);
    EXPECT(h.controller->status().logical_payload_bytes <= h.config.logical_byte_cap);
    Harness steady; steady.activate();
    allocation_probe::allocations = 0; allocation_probe::enabled = true;
    bool no_heap_path = steady.admit(1, 10).forwarding.queued && steady.controller->service(11) == DF::none;
    no_heap_path = no_heap_path && steady.controller->terminal(steady.radio.issue_terminal(), 411) == DF::none;
    no_heap_path = no_heap_path && steady.controller->rearm(steady.radio.issue_rearm(), 412) == DF::none;
    no_heap_path = no_heap_path && steady.admit(2, 413, kSecond).forwarding.queued;
    no_heap_path = no_heap_path && steady.controller->service(414) == DF::none;
    no_heap_path = no_heap_path && steady.controller->terminal(steady.radio.issue_terminal(), 814) == DF::none;
    no_heap_path = no_heap_path && steady.controller->rearm(steady.radio.issue_rearm(), 815) == DF::none;
    allocation_probe::enabled = false;
    EXPECT(no_heap_path && allocation_probe::allocations == 0);
    std::cout << "INFO: sizeof dispatch=" << sizeof(RepeaterDispatchController)
              << ", ledger=" << sizeof(RepeaterDispatchLedger) << ", attempt=" << sizeof(RepeaterDispatchAttempt)
              << ", status=" << sizeof(RepeaterDispatchStatus) << "; fake radio has fixed255byte copy, rawforwarder fixed255byte stack copy\n";
}
// B13/F3/F4/F10: finite configuration/roster, frame ceilings and nonwrapping identities.
void test_configuration_capacity_and_identity_boundaries() {
    for (unsigned invalid = 0; invalid < 12; ++invalid) {
        Harness h;
        switch (invalid) {
        case 0: h.config.source_count = 0; break;
        case 1: h.config.source_count = 3; break;
        case 2: h.config.sources[1] = h.config.sources[0]; break;
        case 3: h.config.per_source_credit_limit = 2; break;
        case 4: h.config.global_credit_limit = 3; break;
        case 5: h.config.logical_byte_cap = 1; break;
        case 6: h.config.phy.bandwidth_hz = 0; break;
        case 7: h.config.terminal_timeout_ms = 0; break;
        case 8: h.config.initial_attempt_id = 0; break;
        case 9: h.config.initial_attempt_id = std::numeric_limits<std::uint64_t>::max(); break;
        case 10: h.config.budget_epoch = std::numeric_limits<std::uint64_t>::max(); break;
        default: h.config.rearm_timeout_ms = 0; break;
        }
        h.recreate(); EXPECT(h.controller->recover(h.prepare_recover(true, true), 1) != DF::none);
        EXPECT(h.ledger.writes == 0 && h.radio.requests == 0);
    }
    for (const auto depth : {std::size_t{1}, std::size_t{8}}) {
        for (const auto length : {std::size_t{1}, std::size_t{255}}) {
            Harness h; h.config.lifecycle.policy.maximum_queue_depth = depth;
            h.config.lifecycle.maximum_frame_bytes = length; h.config.logical_byte_cap = depth * length;
            h.recreate(); h.activate(); std::array<std::uint8_t, 255> payload{}; payload[0] = 0xa5;
            const auto handle = h.prepare_frame(1, kSource, {payload.data(), length});
            EXPECT(h.controller->admit(handle, {payload.data(), length}, 10).forwarding.queued);
            EXPECT(h.controller->status().occupied_frames <= depth);
            if (depth == 1) {
                const auto second_handle = h.prepare_frame(2, kSecond, {payload.data(), length});
                const auto full = h.controller->admit(second_handle, {payload.data(), length}, 10);
                EXPECT(full.forwarding.disposition == SingleRepeaterDisposition::queue_full && full.replay_state_persisted);
                EXPECT(h.ledger.record.global_remaining == 0 && h.ledger.record.sources[1].remaining == 0);
            }
            EXPECT(h.controller->service(11) == DF::none);
            EXPECT(h.radio.size == length && h.ledger.record.airtime_remaining_us == kCost);
            EXPECT(h.controller->status().occupied_frames <= depth);
        }
    }
    Harness single; single.config.source_count = 1; single.seed(); single.recreate(); single.activate();
    EXPECT(single.admit(1, 10).forwarding.queued);
    EXPECT(single.admit(2, 11, kSecond).forwarding.disposition == SingleRepeaterDisposition::admission_denied);
    Harness final; final.config.initial_attempt_id = std::numeric_limits<std::uint64_t>::max() - 1;
    final.recreate(); start(final); final.complete(); EXPECT(final.admit(2, 413, kSecond).forwarding.queued);
    EXPECT(final.controller->service(414) == DF::identity_exhausted && final.radio.requests == 1);
    Harness sequence; sequence.ledger.record.sequence = std::numeric_limits<std::uint64_t>::max() - 1;
    sequence.activate(); EXPECT(!sequence.admit().forwarding.queued);
    EXPECT(sequence.radio.requests == 0 && sequence.ledger.writes == 0);
}
// B14/F10-F11: terminal shutdown is immutable, diagnostics cannot mint security state.
void test_diagnostic_privacy_clear_and_terminal_shutdown() {
    Harness preserved; start(preserved);
    const auto preserved_ledger = preserved.ledger.record;
    EXPECT(preserved.controller->clear_diagnostics() == DF::none);
    preserved.complete();
    EXPECT(same(preserved.ledger.record, preserved_ledger));
    Harness h; start(h);
    const auto ledger = h.ledger.record; const auto replay = h.replay.image();
    EXPECT(h.controller->clear_diagnostics() == DF::none);
    EXPECT(same(h.ledger.record, ledger) && h.replay.image() == replay && h.radio.requests == 1);
    std::uint32_t counter = std::numeric_limits<std::uint32_t>::max() - 1; bool overflow = false;
    increment_repeater_dispatch_counter(counter, overflow); EXPECT(counter == std::numeric_limits<std::uint32_t>::max() && !overflow);
    increment_repeater_dispatch_counter(counter, overflow); EXPECT(counter == std::numeric_limits<std::uint32_t>::max() && overflow);
    const auto terminal_handle = h.radio.issue_terminal(); const auto rearm_handle = h.radio.issue_rearm();
    EXPECT(h.controller->shutdown() == DF::none);
    const auto snapshot = h.controller->status(); const auto writes = h.ledger.writes;
    const auto invalidations = h.radio.invalidations;
    EXPECT(h.controller->service(1000) == DF::wrong_state);
    EXPECT(h.controller->terminal(terminal_handle, 1000) == DF::wrong_state);
    EXPECT(h.controller->rearm(rearm_handle, 1000) == DF::wrong_state);
    EXPECT(h.controller->recover(0, 1000) == DF::wrong_state);
    EXPECT(h.controller->clear_diagnostics() == DF::wrong_state);
    EXPECT(h.controller->notify(snapshot.lifecycle.generation, Event::uncertain_submission, 1000) == DF::wrong_state);
    EXPECT(h.radio.invalidations == invalidations);
    EXPECT(h.controller->shutdown() == DF::none);
    const auto after = h.controller->status();
    EXPECT(after.state == snapshot.state && after.fault == snapshot.fault && after.possible_emission == snapshot.possible_emission);
    const auto& a = after.diagnostics; const auto& b = snapshot.diagnostics;
    EXPECT(a.admitted == b.admitted && a.denied == b.denied && a.ledger_commits == b.ledger_commits &&
        a.claim_callbacks == b.claim_callbacks && a.submit_requests == b.submit_requests &&
        a.accepted_copies == b.accepted_copies && a.terminals == b.terminals && a.rearms == b.rearms &&
        a.expired == b.expired && a.uncertain == b.uncertain && a.overflow == b.overflow);
    EXPECT(after.lifecycle.generation == snapshot.lifecycle.generation && after.occupied_frames == snapshot.occupied_frames);
    EXPECT(h.ledger.writes == writes && same(h.ledger.record, ledger) && h.replay.image() == replay);
    char projected[256]{};
    const auto size = std::snprintf(projected, sizeof(projected), "%u/%u/%zu/%u",
        static_cast<unsigned>(after.state), static_cast<unsigned>(after.fault), after.occupied_frames, after.diagnostics.accepted_copies);
    EXPECT(size > 0);
    for (const auto* secret : {"SOURCE_ALIAS", "SECRET_KEY", "PRIVATE_LOCATION", "OPAQUE_PAYLOAD"}) {
        EXPECT(std::strstr(projected, secret) == nullptr);
    }
}
// B15/F8/F10: explicit clock rollback/overflow cannot create an attempt.
void test_clock_rollback_and_checked_timeout_arithmetic() {
    Harness rollback; start(rollback);
    EXPECT(rollback.controller->service(10) == DF::clock_regression);
    EXPECT(rollback.radio.requests == 1 && rollback.controller->status().state == DS::contained);
    Harness high; const auto maximum = std::numeric_limits<std::uint64_t>::max();
    high.activate(maximum - 3000, maximum - 1);
    EXPECT(high.admit(1, maximum - 500).forwarding.queued);
    EXPECT(high.controller->service(maximum - 499) == DF::time_overflow);
    EXPECT(high.radio.requests == 0);
}
}  // namespace
int main() {
    test_startup_and_exact_full_cost_dispatch();
    test_source_global_and_unknown_roster_limits();
    test_airtime_boundaries_and_exhaustion_retirement();
    test_ledger_phase_faults_and_replay_order();
    test_inflight_pressure_and_actual_reconstruction();
    test_claim_races_and_atomic_expected_radio_proof();
    test_submission_outcomes_are_irreversible();
    test_all_terminal_rearm_bindings_and_one_use_events();
    test_timeouts_rearm_faults_and_no_early_completion();
    test_restart_never_defaults_credit_or_radio_readiness();
    test_expiry_suspension_shutdown_and_retirement();
    test_reentrant_ports_fixed_occupancy_and_no_heap();
    test_configuration_capacity_and_identity_boundaries();
    test_diagnostic_privacy_clear_and_terminal_shutdown();
    test_clock_rollback_and_checked_timeout_arithmetic();
    if (failures != 0) { std::cerr << failures << " dispatch assertion(s) failed\n"; return EXIT_FAILURE; }
    std::cout << "PASS: 15 repeater dispatch/budget scenario groups (B1-B15; reviewed F1-F12)\n";
    return EXIT_SUCCESS;
}
