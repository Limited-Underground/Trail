#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/companion_protocol.hpp"
#include "opentrail/companion_semantics.hpp"
#include "opentrail/radio_transport.hpp"

namespace opentrail::companion {

inline constexpr std::size_t kCompanionMaxRequestRecordBytes =
    kCompanionFragmentHeaderBytes + kCompanionActionRequestBytes;
inline constexpr std::size_t kCompanionMaxResponseRecordBytes =
    kCompanionFragmentHeaderBytes + kCompanionStatusSnapshotBytes;

enum class CompanionAuthorityError : std::uint8_t {
    none = 0,
    not_ready,
    failed,
    outcome_unknown,
};

struct CompanionSnapshotAuthorityResult {
    CompanionAuthorityError error{CompanionAuthorityError::not_ready};
    CompanionStatusSnapshot snapshot{};

    [[nodiscard]] constexpr bool ready() const {
        return error == CompanionAuthorityError::none;
    }
};

struct CompanionActionAuthorityResult {
    CompanionAuthorityError error{CompanionAuthorityError::not_ready};
    CompanionActionDisposition disposition{
        CompanionActionDisposition::rejected};
    CompanionActionRejectReason reject_reason{
        CompanionActionRejectReason::internal_failure};
    std::uint32_t operation_token{0};

    [[nodiscard]] constexpr bool ready() const {
        return error == CompanionAuthorityError::none;
    }
};

class CompanionSnapshotAuthority {
public:
    virtual ~CompanionSnapshotAuthority() = default;

    // Returns one device-authoritative atomic snapshot. A non-ready result
    // supplies no partial state and this read must not mutate device policy.
    [[nodiscard]] virtual CompanionSnapshotAuthorityResult read_snapshot() = 0;
};

class CompanionActionAuthority {
public:
    virtual ~CompanionActionAuthority() = default;

    // Preparation is pure: it must not mutate state, queue work, or reserve a
    // resource. A ready result carries an opaque nonzero validation token and
    // describes the result that commit_action will revalidate and atomically
    // apply.
    [[nodiscard]] virtual CompanionActionAuthorityResult prepare_action(
        const CompanionActionRequest& request) = 0;
    // The adapter may mutate state only when returning none. not_ready or
    // failed guarantees that no action was applied or queued. outcome_unknown
    // means a durable commit may have begun and the adapter has entered a
    // fail-closed recovery state; no response may be emitted. A rejected
    // prepared result must never apply or queue the user action. Commit owns
    // any reservation and must consume/release the token on every outcome.
    [[nodiscard]] virtual CompanionAuthorityError commit_action(
        const CompanionActionRequest& request,
        const CompanionActionAuthorityResult& prepared) = 0;
};

enum class CompanionCoordinatorError : std::uint8_t {
    none = 0,
    invalid_argument,
    output_too_small,
    envelope_rejected,
    wrong_frame_kind,
    semantic_rejected,
    session_rejected,
    duplicate_conflict,
    duplicate_without_result,
    snapshot_authority_failed,
    action_authority_failed,
    response_rejected,
};

enum class CompanionCoordinatorDisposition : std::uint8_t {
    rejected = 0,
    processed_new,
    replayed_cached_response,
};

struct CompanionCoordinatorResult {
    CompanionCoordinatorDisposition disposition{
        CompanionCoordinatorDisposition::rejected};
    CompanionCoordinatorError error{
        CompanionCoordinatorError::invalid_argument};
    CompanionCodecError envelope_error{CompanionCodecError::none};
    CompanionSemanticCodecError semantic_error{
        CompanionSemanticCodecError::none};
    CompanionSessionError session_error{CompanionSessionError::none};
    std::uint32_t exchange_id{0};
    std::size_t response_bytes{0};

    [[nodiscard]] constexpr bool responded() const {
        return error == CompanionCoordinatorError::none;
    }

    [[nodiscard]] constexpr bool duplicate() const {
        return disposition ==
               CompanionCoordinatorDisposition::replayed_cached_response;
    }
};

// One fixed-memory request owner for one boot-local companion controller
// session. Device authority remains outside this class. The coordinator caches
// only the exact most recently completed request/response so an equal request
// ID can be answered without applying device intent twice. A new request ID is
// consumed before authority work. Any authority/response failure therefore has
// no cache and makes an exact retry terminal duplicate_without_result; adapters
// must honor the prepare/commit no-mutation-on-error contract above.
class CompanionRequestCoordinator {
public:
    CompanionRequestCoordinator(
        CompanionSnapshotAuthority& snapshot_authority,
        CompanionActionAuthority& action_authority);

    [[nodiscard]] CompanionSessionResult open_session(
        const CompanionSessionEvidence& evidence,
        std::uint32_t device_generated_session_nonce);
    [[nodiscard]] CompanionSessionError close_session(
        std::uint64_t controller_binding);
    [[nodiscard]] CompanionCoordinatorResult service(
        const CompanionSessionEvidence& evidence,
        radio::ByteView encoded_request,
        radio::MutableByteView response_output);
    [[nodiscard]] CompanionSessionStatus session_status() const;

private:
    void clear_cache();
    [[nodiscard]] bool request_matches_cache(
        const CompanionSessionEvidence& evidence,
        radio::ByteView encoded_request,
        const CompanionFragment& fragment) const;
    [[nodiscard]] CompanionCoordinatorResult reject(
        CompanionCoordinatorError error,
        std::uint32_t exchange_id = 0,
        CompanionCodecError envelope_error = CompanionCodecError::none,
        CompanionSemanticCodecError semantic_error =
            CompanionSemanticCodecError::none,
        CompanionSessionError session_error = CompanionSessionError::none)
        const;

    CompanionSnapshotAuthority& snapshot_authority_;
    CompanionActionAuthority& action_authority_;
    CompanionSessionGuard session_guard_{};
    bool cache_valid_{false};
    std::uint64_t cached_controller_binding_{0};
    std::uint32_t cached_session_nonce_{0};
    std::uint32_t cached_exchange_id_{0};
    CompanionFrameKind cached_kind_{CompanionFrameKind::snapshot_request};
    std::size_t cached_request_bytes_{0};
    std::array<std::uint8_t, kCompanionMaxRequestRecordBytes> cached_request_{};
    std::size_t cached_response_bytes_{0};
    std::array<std::uint8_t, kCompanionMaxResponseRecordBytes> cached_response_{};
};

}  // namespace opentrail::companion
