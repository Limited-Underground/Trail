#pragma once

#include <cstdint>
#include <optional>

#include "opentrail/breadcrumb_archive_session_lease_store.hpp"
#include "opentrail/breadcrumb_archive_workflow_coordinator.hpp"

namespace opentrail::integration {

enum class BreadcrumbArchiveWorkflowBootstrapState : std::uint8_t {
    dormant = 0,
    ready,
    failed,
};

enum class BreadcrumbArchiveWorkflowBootstrapDisposition : std::uint8_t {
    initialized = 0,
    already_ready,
    forwarded,
    rejected,
    failed,
};

enum class BreadcrumbArchiveWorkflowBootstrapError : std::uint8_t {
    none = 0,
    invalid_configuration,
    not_initialized,
    lease_allocation_failed,
    workflow_configuration_failed,
};

struct BreadcrumbArchiveWorkflowBootstrapResult {
    BreadcrumbArchiveWorkflowBootstrapDisposition disposition{
        BreadcrumbArchiveWorkflowBootstrapDisposition::failed};
    BreadcrumbArchiveWorkflowBootstrapError error{
        BreadcrumbArchiveWorkflowBootstrapError::none};
    persistence::BreadcrumbArchiveSessionLeaseError lease_error{
        persistence::BreadcrumbArchiveSessionLeaseError::none};
    BreadcrumbArchiveWorkflowResult workflow{};
    bool lease_attempted{false};
    bool workflow_called{false};
};

struct BreadcrumbArchiveWorkflowBootstrapStatus {
    BreadcrumbArchiveWorkflowBootstrapState state{
        BreadcrumbArchiveWorkflowBootstrapState::dormant};
    BreadcrumbArchiveWorkflowBootstrapError latched_error{
        BreadcrumbArchiveWorkflowBootstrapError::none};
    persistence::BreadcrumbArchiveSessionLeaseError lease_error{
        persistence::BreadcrumbArchiveSessionLeaseError::none};
    std::uint32_t lease_generation{0};
    std::uint64_t first_session_id{0};
    std::uint64_t final_session_id{0};
    std::uint32_t initialize_calls{0};
    std::uint32_t lease_attempts{0};
    std::uint32_t workflow_calls{0};
};

// Fixed-memory one-boot composition. The workflow does not exist until one
// complete durable lease has been allocated and read back. A failed or
// uncertain allocation latches this optional path; there is no reset or retry
// in the same object lifetime and no base-radio authority.
class BreadcrumbArchiveWorkflowBootstrap {
public:
    BreadcrumbArchiveWorkflowBootstrap(
        persistence::PersistentStorage& storage,
        SerializedBreadcrumbArchiveRuntimeOwner& runtime,
        time::CheckedMonotonicClock& clock,
        ui::CheckedLocalInterface& local_interface,
        persistence::BreadcrumbArchiveSessionLeaseRequest lease_request,
        std::uint32_t initial_revision = 1);
    BreadcrumbArchiveWorkflowBootstrap(
        const BreadcrumbArchiveWorkflowBootstrap&) = delete;
    BreadcrumbArchiveWorkflowBootstrap& operator=(
        const BreadcrumbArchiveWorkflowBootstrap&) = delete;
    BreadcrumbArchiveWorkflowBootstrap(
        BreadcrumbArchiveWorkflowBootstrap&&) = delete;
    BreadcrumbArchiveWorkflowBootstrap& operator=(
        BreadcrumbArchiveWorkflowBootstrap&&) = delete;

    [[nodiscard]] BreadcrumbArchiveWorkflowBootstrapResult initialize();
    [[nodiscard]] BreadcrumbArchiveWorkflowBootstrapResult service();
    [[nodiscard]] BreadcrumbArchiveWorkflowBootstrapResult enter(
        const ui::ResolvedAction& action);
    [[nodiscard]] BreadcrumbArchiveWorkflowBootstrapStatus status() const;

private:
    [[nodiscard]] BreadcrumbArchiveWorkflowBootstrapResult unavailable() const;
    void latch(BreadcrumbArchiveWorkflowBootstrapError error);

    persistence::BreadcrumbArchiveSessionLeaseStore lease_store_;
    SerializedBreadcrumbArchiveRuntimeOwner& runtime_;
    time::CheckedMonotonicClock& clock_;
    ui::CheckedLocalInterface& local_interface_;
    persistence::BreadcrumbArchiveSessionLeaseRequest lease_request_{};
    std::uint32_t initial_revision_{0};
    std::optional<BreadcrumbArchiveWorkflowCoordinator> workflow_{};
    BreadcrumbArchiveWorkflowBootstrapStatus status_{};
};

}  // namespace opentrail::integration
