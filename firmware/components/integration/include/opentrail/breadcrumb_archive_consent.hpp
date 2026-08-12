#pragma once

#include <cstdint>

#include "opentrail/breadcrumb_archive_runtime_owner.hpp"
#include "opentrail/local_interface.hpp"

namespace opentrail::integration {

enum class BreadcrumbArchiveConsentMode : std::uint8_t {
    start = 0,
    stop,
};

enum class BreadcrumbArchiveConsentPresentationError : std::uint8_t {
    none = 0,
    invalid_revision,
    invalid_mode,
};

struct BreadcrumbArchiveConsentPresentation {
    BreadcrumbArchiveConsentPresentationError error{
        BreadcrumbArchiveConsentPresentationError::invalid_mode};
    ui::UiFrame frame{};

    [[nodiscard]] constexpr bool presentable() const {
        return error == BreadcrumbArchiveConsentPresentationError::none;
    }
};

[[nodiscard]] BreadcrumbArchiveConsentPresentation
make_breadcrumb_archive_consent_presentation(
    BreadcrumbArchiveConsentMode mode,
    std::uint32_t revision);

enum class BreadcrumbArchiveConsentDisposition : std::uint8_t {
    started = 0,
    stopped,
    cancelled,
    deferred,
    rejected,
    failed,
};

enum class BreadcrumbArchiveConsentError : std::uint8_t {
    none = 0,
    invalid_initial_session_id,
    invalid_resolved_action,
    unsupported_action,
    clock_not_ready,
    clock_failed,
    session_id_exhausted,
    runtime_deferred,
    runtime_failed,
    start_rejected,
};

struct BreadcrumbArchiveConsentResult {
    BreadcrumbArchiveConsentDisposition disposition{
        BreadcrumbArchiveConsentDisposition::failed};
    BreadcrumbArchiveConsentError error{
        BreadcrumbArchiveConsentError::none};
    time::MonotonicClockError clock_error{
        time::MonotonicClockError::none};
    BreadcrumbArchiveRuntimeResult runtime{};
    std::uint64_t session_id{0};
    bool resolved_local_action{false};
    bool session_id_consumed{false};
};

struct BreadcrumbArchiveConsentStatus {
    bool configuration_valid{false};
    bool session_id_exhausted{false};
    std::uint64_t next_session_id{0};
    std::uint32_t action_calls{0};
    std::uint32_t started{0};
    std::uint32_t stopped{0};
    std::uint32_t cancelled{0};
    std::uint32_t deferred{0};
    std::uint32_t rejected{0};
    std::uint32_t failed{0};
};

// Accepts only actions already resolved against the exact active local frame.
// It has no radio, network, server, remote-command, or automatic-start input.
class BreadcrumbArchiveConsentController {
public:
    BreadcrumbArchiveConsentController(
        SerializedBreadcrumbArchiveRuntimeOwner& runtime,
        time::CheckedMonotonicClock& clock,
        std::uint64_t initial_session_id = 1);

    [[nodiscard]] BreadcrumbArchiveConsentResult apply(
        const ui::ResolvedAction& action);
    [[nodiscard]] BreadcrumbArchiveConsentStatus status() const;

private:
    [[nodiscard]] BreadcrumbArchiveConsentResult start();
    [[nodiscard]] BreadcrumbArchiveConsentResult stop();
    void consume_session_id(BreadcrumbArchiveConsentResult& result);

    SerializedBreadcrumbArchiveRuntimeOwner& runtime_;
    time::CheckedMonotonicClock& clock_;
    BreadcrumbArchiveConsentStatus status_{};
};

}  // namespace opentrail::integration
