#pragma once

#include <cstdint>

#include "opentrail/companion_request_coordinator.hpp"
#include "opentrail/device_factory_reset_executor.hpp"

namespace opentrail::companion {

enum class CompanionFactoryResetAuthorityPhase : std::uint8_t {
    idle = 0,
    prepared,
    intent_committed,
    outcome_unknown,
};

struct CompanionFactoryResetAuthorityStatus {
    CompanionFactoryResetAuthorityPhase phase{
        CompanionFactoryResetAuthorityPhase::idle};
    bool reset_intent_committed{false};
    bool protected_operations_blocked{false};
};

// Protected-command authority for the V1 app-initiated factory reset. Link,
// bond, and application-owner authorization are enforced by the upstream GATT
// lifecycle. An admitted result means only that reset intent was durably
// committed; cleanup/reboot/readback remain separate completion gates.
class CompanionFactoryResetActionAuthority final
    : public CompanionActionAuthority {
public:
    explicit CompanionFactoryResetActionAuthority(
        DeviceFactoryResetExecutor& executor);

    [[nodiscard]] CompanionActionAuthorityResult prepare_action(
        const CompanionActionRequest& request) override;
    [[nodiscard]] CompanionAuthorityError commit_action(
        const CompanionActionRequest& request,
        const CompanionActionAuthorityResult& prepared) override;

    [[nodiscard]] CompanionFactoryResetAuthorityStatus status() const;

private:
    void clear_prepared();
    [[nodiscard]] std::uint32_t issue_token();

    DeviceFactoryResetExecutor& executor_;
    CompanionFactoryResetAuthorityPhase phase_{
        CompanionFactoryResetAuthorityPhase::idle};
    CompanionActionKind prepared_kind_{CompanionActionKind::quick_status};
    CompanionActionDisposition prepared_disposition_{
        CompanionActionDisposition::rejected};
    CompanionActionRejectReason prepared_reason_{
        CompanionActionRejectReason::internal_failure};
    std::uint64_t prepared_reset_receipt_{0};
    std::uint32_t prepared_token_{0};
    std::uint32_t next_token_{1};
};

}  // namespace opentrail::companion
