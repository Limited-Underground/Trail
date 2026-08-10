#pragma once

#include <cstdint>

#include "opentrail/duplicate_checkpoint_store.hpp"
#include "opentrail/single_repeater_forwarder.hpp"

namespace opentrail::delivery {

struct ReplayStorageBindingEvidence {
    std::uint64_t group_context_id{0};
    std::uint32_t group_epoch{0};
    bool protected_namespace_verified{false};
    bool empty_store_provisioning_authorized{false};
};

enum class RepeaterReplayBootOutcome : std::uint8_t {
    service_required = 0,
    first_boot_initialized,
    restored,
    restored_and_repaired,
};

enum class RepeaterReplayError : std::uint8_t {
    none = 0,
    invalid_binding,
    already_booted,
    live_window_not_empty,
    missing_checkpoint,
    store_failure,
    legacy_checkpoint,
    checkpoint_binding_mismatch,
    checkpoint_epoch_mismatch,
    live_restore_failure,
    not_operational,
    persistence_failure,
};

struct RepeaterReplayBootResult {
    RepeaterReplayError error{RepeaterReplayError::invalid_binding};
    RepeaterReplayBootOutcome outcome{
        RepeaterReplayBootOutcome::service_required};
    DuplicateCheckpointStoreError store_error{
        DuplicateCheckpointStoreError::no_checkpoint};
    std::uint64_t generation{0};

    [[nodiscard]] constexpr bool operational() const {
        return error == RepeaterReplayError::none;
    }
};

struct RepeaterReplayProcessResult {
    RepeaterReplayError error{RepeaterReplayError::not_operational};
    SingleRepeaterDecision forwarding{};
    DuplicateCheckpointStoreError store_error{
        DuplicateCheckpointStoreError::none};
    std::uint64_t generation{0};
    bool replay_state_persisted{false};
};

struct RepeaterReplayTransmitResult {
    RepeaterReplayError error{RepeaterReplayError::not_operational};
    ExactForwardedFrameResult forwarding{};
};

struct RepeaterReplayStatus {
    bool boot_attempted{false};
    bool operational{false};
    std::uint64_t persisted_generation{0};
    std::uint32_t persistence_failures{0};
};

class SingleRepeaterReplayCoordinator {
public:
    SingleRepeaterReplayCoordinator(
        std::uint32_t duplicate_retention_ms,
        std::uint64_t expected_group_context_id,
        std::uint32_t expected_group_epoch,
        DuplicateWindow& live_window,
        DuplicateCheckpointStore& store,
        SingleRepeaterForwarder& forwarder);

    [[nodiscard]] RepeaterReplayBootResult boot(
        const ReplayStorageBindingEvidence& binding,
        std::uint64_t now_ms);
    [[nodiscard]] RepeaterReplayProcessResult process_and_persist(
        const VerifiedForwardingMetadata& metadata,
        radio::ByteView exact_protected_frame,
        std::uint64_t now_ms);
    [[nodiscard]] RepeaterReplayTransmitResult next_transmit(
        std::uint64_t now_ms);
    [[nodiscard]] RepeaterReplayStatus status() const;

private:
    void fail_persistence();

    std::uint32_t duplicate_retention_ms_{0};
    std::uint64_t expected_group_context_id_{0};
    std::uint32_t expected_group_epoch_{0};
    DuplicateWindow& live_window_;
    DuplicateCheckpointStore& store_;
    SingleRepeaterForwarder& forwarder_;
    bool boot_attempted_{false};
    bool operational_{false};
    std::uint64_t persisted_generation_{0};
    std::uint32_t persistence_failures_{0};
};

}  // namespace opentrail::delivery
